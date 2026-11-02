/* Immutable alpha extents, independent of payload-ring addresses. Skipping
 * zero alpha changes neither depth tests nor selection/statistic updates. */
#ifndef H1_SPRITE_ROWS_H
#define H1_SPRITE_ROWS_H
typedef struct { int frame;uint16_t first[BS_MAX_FRAME_SIDE],end[BS_MAX_FRAME_SIDE]; } SpriteRows;
static SpriteRows *sprite_rows;
static uint16_t *sprite_row_lookup;
static unsigned sprite_row_slots,sprite_row_cursor,sprite_row_heap_bytes;
static void release_sprite_rows(void){
    cache_memory_free(sprite_rows);sprite_rows=0;sprite_row_lookup=0;
    sprite_row_slots=sprite_row_cursor=sprite_row_heap_bytes=0;
}
unsigned bs_reserve_sprite_row_cache(unsigned slots){
    unsigned i,total;release_sprite_rows();if(!ready || !slots)return 0;
    if(slots>1024u)slots=1024u;
    total=slots*sizeof(SpriteRows)+pack.frame_count*sizeof(uint16_t);
    sprite_rows=cache_memory_alloc(total);if(!sprite_rows)return 0;
    sprite_row_heap_bytes=total;sprite_row_slots=slots;
    sprite_row_lookup=(uint16_t *)(void *)(sprite_rows+slots);
    for(i=0;i<slots;++i)sprite_rows[i].frame=-1;
    for(i=0;i<pack.frame_count;++i)sprite_row_lookup[i]=0xffffu;
    return slots;
}
static const SpriteRows *sprite_alpha_rows(unsigned frame,int width,int height,const unsigned char *alpha){
    unsigned slot;SpriteRows *entry;int y;
    if(!sprite_rows)return 0;
    slot=sprite_row_lookup[frame];
    if(slot<sprite_row_slots && sprite_rows[slot].frame==(int)frame)return &sprite_rows[slot];
    slot=sprite_row_cursor++;if(sprite_row_cursor==sprite_row_slots)sprite_row_cursor=0;
    entry=&sprite_rows[slot];if(entry->frame>=0)sprite_row_lookup[entry->frame]=0xffffu;
    for(y=0;y<height;++y){
        const unsigned char *row=alpha+(unsigned)y*width;int first=0,end=width;
        while(first<end && !row[first])++first;
        while(end>first && !row[end-1])--end;
        entry->first[y]=(uint16_t)first;entry->end[y]=(uint16_t)end;
    }
    entry->frame=(int)frame;sprite_row_lookup[frame]=(uint16_t)slot;return entry;
}
#endif
