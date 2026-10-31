#ifndef H1_MAP_SIDECAR_FORMAT_H
#define H1_MAP_SIDECAR_FORMAT_H
#include <stdint.h>
#define MAPC_MAGIC "MAP12H1C"
#define MAPC_VERSION 1u
#define MAPC_HEADER_BYTES 128u
#define MAPC_ENTRY_BYTES 16u
#define MAPC_TILE_BYTES 65536u
#define MAPC_SOURCE_VERSION 12u
#define MAPC_RAW 0u
#define MAPC_DELTA_ZRLE 1u
#define MAPC_HEADER_CRC_OFFSET 52u
/* Wire order is little endian. These shapes have no pointers or padding on
 * H1/O32; byte access helpers below also handle unaligned portable readers. */
typedef struct {
    unsigned char magic[8];
    uint32_t version,header_bytes,scene_bytes,columns,rows,entry_count;
    uint32_t index_offset,entry_bytes,data_offset,index_crc32,file_bytes,header_crc32;
    uint32_t scene_version,tile_bytes;
    unsigned char scene_sha256[32];
    uint32_t flags,reserved[7];
} MapSidecarHeader;
typedef struct {uint32_t offset,stored_bytes,raw_crc32,encoding;} MapSidecarEntry;
typedef char MapSidecarHeader_must_be128[sizeof(MapSidecarHeader)==128?1:-1];
typedef char MapSidecarEntry_must_be16[sizeof(MapSidecarEntry)==16?1:-1];
static inline uint32_t mapc_get32(const unsigned char*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline void mapc_put32(unsigned char*p,uint32_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
uint32_t mapc_crc32_update(uint32_t state,const unsigned char *p,unsigned n);
uint32_t mapc_crc32(const unsigned char *p,unsigned n);
uint32_t mapc_header_crc32(const unsigned char *p);
#endif
