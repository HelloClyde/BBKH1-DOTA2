/* Isolated implementation; relies only on existing renderer cache storage. */
#ifndef H1_COMPRESSED_CACHE_EXPERIMENT_H
#define H1_COMPRESSED_CACHE_EXPERIMENT_H
#include "cache_rle.h"
#define CACHE_RLE_FLAG 0x80000000u
static uint32_t cache_payload_bytes(const SpriteCache *entry){return entry->size&~CACHE_RLE_FLAG;}
static uint32_t cache_payload_allocation(const SpriteCache *entry){return (cache_payload_bytes(entry)+3u)&~3u;}
static void cache_copy_bytes(unsigned char *dst,const unsigned char *src,unsigned n){while(n--)*dst++=*src++;}
static int cache_payload_valid(const RuntimeFrame *frame,const unsigned char *data){
    if(rf_flags(frame)&BS_HAS_SHADOW){
        const unsigned char *header=data+(unsigned)rf_width(frame)*rf_height(frame)*5u;
        unsigned w=read_u16(header),h=read_u16(header+2);int ax=read_i16(header+4),ay=read_i16(header+6);
        if(!w || !h || w>BS_MAX_FRAME_SIDE || h>BS_MAX_FRAME_SIDE ||
           ax<-1024 || ax>1024 || ay<-1024 || ay>1024 ||
           rf_size(frame)!=(uint32_t)rf_width(frame)*rf_height(frame)*5u+8u+w*h){
            ++stats.missing_frames;error_code=14;return 0;
        }
    }
    return 1;
}
static void cache_dense_invalidate(unsigned slot){
    SpriteCache *entry=&dense_entries[slot];
    if(dense_lookup && entry->frame>=0 && dense_lookup[entry->frame]==slot)dense_lookup[entry->frame]=0xffffu;
    entry->frame=-1;
}
static unsigned char *cache_dense_prepare(unsigned size,unsigned *chosen){
    unsigned i,slot=dense_slots,oldest=0;uint32_t age=0,start=dense_write,end;
    size=(size+3u)&~3u;if(size>dense_capacity)return 0;
    if(start>dense_capacity-size)start=0;end=start+size;
    for(i=0;i<dense_slots;++i){SpriteCache *entry=&dense_entries[i];uint32_t elapsed;
        if(entry->frame>=0 && (entry->size>dense_capacity || entry->start>dense_capacity-entry->size ||
           (start<entry->start+entry->size && entry->start<end)))cache_dense_invalidate(i);
        if(entry->frame<0)slot=i;
        else{elapsed=cache_clock-entry->stamp;if(elapsed>=age){oldest=i;age=elapsed;}}
    }
    if(slot==dense_slots)slot=oldest;
    cache_dense_invalidate(slot);dense_entries[slot].start=start;dense_entries[slot].size=size;*chosen=slot;
    dense_write=end;return dense_data+start;
}
static void cache_dense_publish(unsigned slot,unsigned index){dense_entries[slot].frame=(int)index;dense_entries[slot].stamp=++cache_clock;if(dense_lookup)dense_lookup[index]=(uint16_t)slot;}
static unsigned cache_heap_reserve(unsigned bytes,uint32_t *start){
    unsigned i,slot=sprite_slots;uint32_t oldest=0xffffffffu,end,size=(bytes+3u)&~3u;
    if(size>sprite_capacity)return slot;
    *start=sprite_write;if(*start>sprite_capacity-size)*start=0;end=*start+size;
    for(i=0;i<sprite_slots;++i){SpriteCache *entry=&sprite_entries[i];
        if(entry->frame>=0 && *start<entry->start+cache_payload_allocation(entry) && entry->start<end){
            if(sprite_lookup && sprite_lookup[entry->frame]==i)sprite_lookup[entry->frame]=0xffffu;
            entry->frame=-1;
        }
        if(entry->frame<0){slot=i;oldest=0;}
        else if(oldest && entry->stamp<oldest){oldest=entry->stamp;slot=i;}
    }
    if(slot==sprite_slots)return slot;
    if(sprite_lookup && sprite_entries[slot].frame>=0 && sprite_lookup[sprite_entries[slot].frame]==slot)sprite_lookup[sprite_entries[slot].frame]=0xffffu;
    sprite_entries[slot].frame=-1;return slot;
}
static const unsigned char *compressed_get_sprite(unsigned index){
    unsigned i,dense_slot,heap_slot,raw,encoded,stored;uint32_t start;unsigned char *dense;
    const RuntimeFrame *frame;SpriteCache *entry=0;
    if(index>=pack.frame_count)return 0;
    frame=frame_info(index);if(!frame){++stats.missing_frames;return 0;}raw=rf_size(frame);
    if(dense_lookup)i=dense_lookup[index];
    else{for(i=0;i<dense_slots;++i)if(dense_entries[i].frame==(int)index)break;}
    if(i<dense_slots && dense_entries[i].frame==(int)index){
        if(dense_entries[i].size>=raw && dense_entries[i].size<=dense_capacity && dense_entries[i].start<=dense_capacity-dense_entries[i].size){++cache_clock;++stats.sprite_hits;return dense_data+dense_entries[i].start;}
        cache_dense_invalidate(i);
    }
    i=sprite_lookup?sprite_lookup[index]:sprite_slots;
    if(i<sprite_slots && sprite_entries[i].frame==(int)index)entry=&sprite_entries[i];
    dense=cache_dense_prepare(raw,&dense_slot);if(!dense){++stats.missing_frames;return 0;}
    if(entry){
        stored=cache_payload_bytes(entry);
        if(!stored || stored>sprite_capacity || entry->start>sprite_capacity-cache_payload_allocation(entry) ||
           ((entry->size&CACHE_RLE_FLAG)?stored>=raw:stored!=raw)){
            ++stats.missing_frames;error_code=19;return 0;
        }
        if(entry->size&CACHE_RLE_FLAG){
            if(cache_rle_decode(sprite_data+entry->start,stored,dense,raw)!=raw){++stats.missing_frames;error_code=19;return 0;}
        }else cache_copy_bytes(dense,sprite_data+entry->start,raw);
        if(!cache_payload_valid(frame,dense))return 0;
        entry->stamp=++cache_clock;++stats.sprite_hits;cache_dense_publish(dense_slot,index);return dense;
    }
    ++stats.sprite_misses;
    if(!read_block(frame->offset,dense,raw) || !cache_payload_valid(frame,dense))return 0;
    encoded=cache_rle_encoded_size(dense,raw);stored=encoded<raw?encoded:raw;
    heap_slot=cache_heap_reserve(stored,&start);if(heap_slot==sprite_slots){++stats.missing_frames;return 0;}
    if(encoded<raw){if(cache_rle_encode(dense,raw,sprite_data+start,stored)!=stored){++stats.missing_frames;error_code=19;return 0;}}
    else cache_copy_bytes(sprite_data+start,dense,raw);
    entry=&sprite_entries[heap_slot];entry->start=start;entry->size=stored|(encoded<raw?CACHE_RLE_FLAG:0u);
    entry->frame=(int)index;entry->stamp=++cache_clock;if(sprite_lookup)sprite_lookup[index]=(uint16_t)heap_slot;
    sprite_write=start+((stored+3u)&~3u);cache_dense_publish(dense_slot,index);return dense;
}
static int compressed_warm_stage(const RuntimeFrame *frame,uint32_t source_offset,uint32_t *packed,SpriteCache *entry){
    unsigned raw=rf_size(frame),encoded,stored,allocation;unsigned char *scratch=(unsigned char *)sprite_storage;
    if(raw>SPRITE_CACHE_BYTES || source_offset>sprite_capacity-raw){error_code=19;return 0;}
    cache_copy_bytes(scratch,sprite_data+source_offset,raw);encoded=cache_rle_encoded_size(scratch,raw);stored=encoded<raw?encoded:raw;allocation=(stored+3u)&~3u;
    /* The whole source frame was copied before any overlapping write. */
    if(*packed>source_offset || allocation>sprite_capacity-*packed || *packed+allocation>source_offset+((raw+3u)&~3u)){error_code=19;return 0;}
    if(encoded<raw){if(cache_rle_encode(scratch,raw,sprite_data+*packed,stored)!=stored){error_code=19;return 0;}}
    else cache_copy_bytes(sprite_data+*packed,scratch,raw);
    entry->frame=-1;entry->start=*packed;entry->size=stored|(encoded<raw?CACHE_RLE_FLAG:0u);entry->stamp=0;*packed+=allocation;return 1;
}
#endif
