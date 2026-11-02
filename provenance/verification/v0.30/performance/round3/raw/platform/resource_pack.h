#ifndef H1_DOTA_RESOURCE_PACK_H
#define H1_DOTA_RESOURCE_PACK_H
#include <stdint.h>
enum {RESOURCE_SCENE=0,RESOURCE_SOUND=1,RESOURCE_VISION=2,RESOURCE_TREE=3,RESOURCE_SECTION_COUNT=4};
enum {RESOURCE_OK=0,RESOURCE_MISSING=1,RESOURCE_IO=2,RESOURCE_FORMAT=3,RESOURCE_CRC=4,RESOURCE_MEMORY=5,RESOURCE_RANGE=6,RESOURCE_NOT_MOUNTED=7,RESOURCE_BUSY=8,RESOURCE_DECODE=9,RESOURCE_SECTION=10};
#define RESOURCE_ASSET_ERROR 17
#define RESOURCE_CHUNK_BYTES 16384u
typedef struct ResourceFile ResourceFile;
typedef struct {
 uint32_t mounted,version,file_bytes,open_files,heap_bytes,last_error;
 uint32_t mount_attempts,open_attempts,read_requests,bytes_returned;
 uint32_t physical_reads,physical_bytes,seeks,index_hits,index_misses;
 uint32_t chunk_hits,chunk_misses,decoded_chunks,decoded_bytes,service_calls,errors;
} ResourceStats;
/* Mount reads/validates the header and small page guides. Actual index pages
 * and decoded chunks are heap cached. No full resource or TOC is resident. */
int resource_mount(const char *path);
int resource_mount_default(void);
int resource_unmount(void); /* 1 success;0 when live ResourceFiles still own it. */
int resource_is_mounted(void);
ResourceFile *resource_open(unsigned section);
uint32_t resource_size(const ResourceFile *file);
uint32_t resource_read(ResourceFile *file,void *dst,uint32_t bytes);
int resource_read_at(ResourceFile *file,uint32_t offset,void *dst,uint32_t bytes);
/* Like the H1 ABI, successful seek returns the resulting logical position.
 * close returns0 on success, -1 on an underlying filesystem failure. */
int resource_seek(ResourceFile *file,int offset,int whence);
int resource_close(ResourceFile *file);
int resource_file_error(const ResourceFile *file);
const ResourceStats *resource_stats(void);
unsigned resource_heap_bytes(void);
/* Optional 8-page indices/4 decoded chunks. Returns added heap bytes, or0
 * without an error when unavailable. The original2/2 caches remain usable. */
unsigned resource_reserve_read_cache(void);
/* Called after a fully validated decoded chunk. Raw audio never calls it;
 * service can read its independent raw stream while a visual read is active. */
void resource_set_service(void (*callback)(void *),void *context);
#endif
