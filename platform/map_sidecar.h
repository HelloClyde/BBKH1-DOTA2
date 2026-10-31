/* Optional same-source MAP12 loader. Failure always returns to SCENE. */
#ifndef H1_MAP_SIDECAR_LOADER_H
#define H1_MAP_SIDECAR_LOADER_H
#include "map_sidecar_format.h"
#include "map_sidecar_identity.h"
#include "map_codec.h"
#define MAP_SIDECAR_PATH_B "B:\\DOTA2\\MAP12.H1C"
#define MAP_SIDECAR_PATH_A "A:\\DOTA2\\MAP12.H1C"
#define MAP_SIDECAR_MEMORY (MAPC_TILE_BYTES+MAPC_EXPECTED_ENTRIES*MAPC_ENTRY_BYTES)
enum { MAP_SC_OK=0,MAP_SC_MISSING=1,MAP_SC_SOURCE=2,MAP_SC_HEADER=3,MAP_SC_MEMORY=4,MAP_SC_DIRECTORY=5,MAP_SC_IO=6,MAP_SC_PADDING=7,MAP_SC_DECODE=8,MAP_SC_CRC=9 };
static unsigned char *map_sidecar_memory;
static BSMapSidecarStats map_sidecar_stats;
static unsigned char*map_sidecar_directory(void){return map_sidecar_memory+MAPC_TILE_BYTES;}
static void map_sidecar_release(void){
    cache_memory_free(map_sidecar_memory);map_sidecar_memory=0;
    map_sidecar_stats.ready=0;map_sidecar_stats.heap_bytes=0;baked_io_aux_close();
}
void bs_map_sidecar_close(void){map_sidecar_release();}
static void map_sidecar_reset(void){map_sidecar_release();clear_bytes(&map_sidecar_stats,sizeof(map_sidecar_stats));}
static int map_sidecar_disable(unsigned reason){
    if(reason==MAP_SC_IO)++map_sidecar_stats.io_errors;
    else if(reason!=MAP_SC_MISSING && reason!=MAP_SC_MEMORY)++map_sidecar_stats.validation_errors;
    map_sidecar_stats.disabled=1;map_sidecar_stats.last_reason=reason;map_sidecar_release();return 0;
}
static int map_sidecar_source_matches(void){
    return ready && pack.version==12u && pack.tile_size==128u && pack.file_size==406028212u &&
        pack.tile_columns==30u && pack.tile_rows==22u &&
        mapc_crc32((const unsigned char*)&pack,sizeof(pack))==MAPC_SOURCE_HEADER_CRC &&
        mapc_crc32((const unsigned char*)tile_offsets,MAPC_EXPECTED_ENTRIES*4u)==MAPC_SOURCE_INDEX_CRC;
}
int bs_map_sidecar_open(const char*path){
    unsigned char header[MAPC_HEADER_BYTES];unsigned i,count,index_bytes,cursor,file_bytes;
    map_sidecar_release();++map_sidecar_stats.open_attempts;map_sidecar_stats.disabled=0;map_sidecar_stats.last_reason=MAP_SC_OK;
    if(!map_sidecar_source_matches())return map_sidecar_disable(MAP_SC_SOURCE);
    if(!baked_io_aux_open(path))return map_sidecar_disable(MAP_SC_MISSING);
    file_bytes=baked_io_aux_size();
    if(file_bytes<MAPC_HEADER_BYTES+MAPC_EXPECTED_ENTRIES*MAPC_ENTRY_BYTES)return map_sidecar_disable(MAP_SC_HEADER);
    if(!baked_io_aux_read_at(0,header,sizeof(header)))return map_sidecar_disable(MAP_SC_IO);
    for(i=0;i<8u;i++)if(header[i]!=(unsigned char)MAPC_MAGIC[i])return map_sidecar_disable(MAP_SC_HEADER);
    for(i=0;i<32u;i++)if(header[64u+i]!=mapc_expected_source_sha[i])return map_sidecar_disable(MAP_SC_SOURCE);
    if(mapc_get32(header+8)!=1u || mapc_get32(header+12)!=128u || mapc_get32(header+16)!=pack.file_size ||
       mapc_get32(header+20)!=pack.tile_columns || mapc_get32(header+24)!=pack.tile_rows ||
       mapc_get32(header+28)!=MAPC_EXPECTED_ENTRIES || mapc_get32(header+32)!=128u || mapc_get32(header+36)!=16u ||
       mapc_get32(header+40)!=128u+MAPC_EXPECTED_ENTRIES*16u || mapc_get32(header+48)!=file_bytes ||
       mapc_get32(header+56)!=12u || mapc_get32(header+60)!=65536u || mapc_get32(header+96)!=0u ||
       mapc_header_crc32(header)!=mapc_get32(header+52))return map_sidecar_disable(MAP_SC_HEADER);
    for(i=100u;i<128u;i++)if(header[i])return map_sidecar_disable(MAP_SC_HEADER);
    count=MAPC_EXPECTED_ENTRIES;index_bytes=count*MAPC_ENTRY_BYTES;
    map_sidecar_memory=cache_memory_alloc(MAP_SIDECAR_MEMORY);
    if(!map_sidecar_memory)return map_sidecar_disable(MAP_SC_MEMORY);
    map_sidecar_stats.heap_bytes=MAP_SIDECAR_MEMORY;
    if(!baked_io_aux_read_at(128u,map_sidecar_directory(),index_bytes))return map_sidecar_disable(MAP_SC_IO);
    if(mapc_crc32(map_sidecar_directory(),index_bytes)!=mapc_get32(header+44))return map_sidecar_disable(MAP_SC_DIRECTORY);
    cursor=128u+index_bytes;
    for(i=0;i<count;i++){
        const unsigned char*record=map_sidecar_directory()+i*16u;
        unsigned offset=mapc_get32(record),stored=mapc_get32(record+4),crc=mapc_get32(record+8),type=mapc_get32(record+12),allocation;
        if(!tile_offsets[i]){if(offset||stored||crc||type)return map_sidecar_disable(MAP_SC_DIRECTORY);continue;}
        if(offset!=cursor || (offset&3u) || !stored || stored>65536u || crc!=mapc_expected_raw_crc[i] ||
           (type!=MAPC_RAW && type!=MAPC_DELTA_ZRLE) || (type==MAPC_RAW && stored!=65536u) ||
           (type==MAPC_DELTA_ZRLE && stored>=65536u))return map_sidecar_disable(MAP_SC_DIRECTORY);
        allocation=(stored+3u)&~3u;
        if(cursor>file_bytes || allocation>file_bytes-cursor)return map_sidecar_disable(MAP_SC_DIRECTORY);
        cursor+=allocation;
    }
    if(cursor!=file_bytes)return map_sidecar_disable(MAP_SC_DIRECTORY);
    map_sidecar_stats.ready=1;map_sidecar_stats.disabled=0;return 1;
}
int bs_map_sidecar_open_paths(const char*primary,const char*secondary){return bs_map_sidecar_open(primary) || bs_map_sidecar_open(secondary);}
int bs_map_sidecar_open_default(void){return bs_map_sidecar_open_paths(MAP_SIDECAR_PATH_B,MAP_SIDECAR_PATH_A);}
const BSMapSidecarStats*bs_map_sidecar_stats(void){return &map_sidecar_stats;}
static int map_sidecar_tile(unsigned tile,unsigned char*data){
    const unsigned char*record;unsigned offset,stored,crc,type,allocation,i;
    if(!map_sidecar_stats.ready){++map_sidecar_stats.source_fallback_tiles;return 0;}
    if(tile>=MAPC_EXPECTED_ENTRIES || !tile_offsets[tile]){(void)map_sidecar_disable(MAP_SC_DIRECTORY);++map_sidecar_stats.source_fallback_tiles;return 0;}
    record=map_sidecar_directory()+tile*16u;offset=mapc_get32(record);stored=mapc_get32(record+4);crc=mapc_get32(record+8);type=mapc_get32(record+12);
    if(!stored || stored>65536u || (offset&3u) || offset<128u+MAPC_EXPECTED_ENTRIES*16u ||
       crc!=mapc_expected_raw_crc[tile] || (type!=MAPC_RAW && type!=MAPC_DELTA_ZRLE) ||
       (type==MAPC_RAW && stored!=65536u) || (type==MAPC_DELTA_ZRLE && stored>=65536u)){
        (void)map_sidecar_disable(MAP_SC_DIRECTORY);++map_sidecar_stats.source_fallback_tiles;return 0;
    }
    allocation=(stored+3u)&~3u;
    if(offset>baked_io_aux_size() || allocation>baked_io_aux_size()-offset){(void)map_sidecar_disable(MAP_SC_DIRECTORY);++map_sidecar_stats.source_fallback_tiles;return 0;}
    ++stats.read_requests;stats.bytes_requested+=allocation;
    if(!baked_io_aux_read_at(offset,data,allocation)){(void)map_sidecar_disable(MAP_SC_IO);++map_sidecar_stats.source_fallback_tiles;return 0;}
    for(i=stored;i<allocation;i++)if(data[i]){(void)map_sidecar_disable(MAP_SC_PADDING);++map_sidecar_stats.source_fallback_tiles;return 0;}
    if(type==MAPC_DELTA_ZRLE){
        if(map_decode(data,stored,map_sidecar_memory,65536u)!=65536u || map_inverse(map_sidecar_memory,data,65536u)!=65536u){(void)map_sidecar_disable(MAP_SC_DECODE);++map_sidecar_stats.source_fallback_tiles;return 0;}
    }
    if(mapc_crc32(data,65536u)!=crc){(void)map_sidecar_disable(MAP_SC_CRC);++map_sidecar_stats.source_fallback_tiles;return 0;}
    ++map_sidecar_stats.loaded_tiles;if(type==MAPC_RAW)++map_sidecar_stats.raw_tiles;else ++map_sidecar_stats.decoded_tiles;return 1;
}
#endif
