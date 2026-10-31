#ifndef H1_DOTA_CACHE_MEMORY_H
#define H1_DOTA_CACHE_MEMORY_H

/* Optional asset caches use the firmware heap, never the application's fixed
 * BSS/stack window. Native test builds use the ordinary host allocator. */
#ifdef CACHE_MEMORY_CUSTOM
void *cache_memory_alloc(unsigned bytes);
void cache_memory_free(void *pointer);
#elif defined(BAKED_IO_HOST) || !defined(__mips__)
#include <stdlib.h>
static inline void *cache_memory_alloc(unsigned bytes){return bytes?malloc(bytes):0;}
static inline void cache_memory_free(void *pointer){free(pointer);}
#else
#include "h1_memory.h"
static inline void *cache_memory_alloc(unsigned bytes){
    void *table=h1_runtime_table(H1_RUNTIME_MEM_TABLE_SLOT);
    if(!bytes || !table || !h1_runtime_entry(table,H1_MEM_ALLOC_OFFSET) ||
       !h1_runtime_entry(table,H1_MEM_FREE_OFFSET))return 0;
    return h1_alloc(bytes);
}
static inline void cache_memory_free(void *pointer){
    void *table;
    if(!pointer)return;
    table=h1_runtime_table(H1_RUNTIME_MEM_TABLE_SLOT);
    if(table && h1_runtime_entry(table,H1_MEM_FREE_OFFSET))h1_free(pointer);
}
#endif

#endif
