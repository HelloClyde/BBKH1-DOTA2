#ifndef H1_DOTA_MATCH_STORAGE_H
#define H1_DOTA_MATCH_STORAGE_H
#include "../game/match_history.h"
enum {MATCH_STORAGE_OK=0,MATCH_STORAGE_MEMORY=1,MATCH_STORAGE_IO=2,MATCH_STORAGE_CORRUPT=3,MATCH_STORAGE_INVALID=4,MATCH_STORAGE_BUSY=5};
typedef struct {
 uint32_t ready,heap_bytes,last_error,history_count,active_slot;
 uint32_t reads,writes,save_attempts,successful_saves,open_files,recovered_slots,released;
} DotaMatchStorageStats;
/* History + one record + one wire record, all firmware heap (~54KiB). */
int match_storage_open(void);
void match_storage_close(void);
const DotaMatchHistory *match_storage_history(void);
const DotaMatchRecord *match_storage_result(void);
uint32_t match_storage_begin_match(void);
/* Completed games only:1closed and read-back verified,0already saved,-1error.
 * Failed saves retain an unsaved RAM entry and never truncate the active slot. */
int match_storage_complete(const DotaGame *game);
int match_storage_error(void);
unsigned match_storage_heap_bytes(void);
const DotaMatchStorageStats *match_storage_stats(void);
void match_storage_set_service(void (*callback)(void *),void *context);
#ifdef MATCH_STORAGE_HOST
typedef struct {
 void *(*open)(const char*,const char*);
 int (*close)(void*);
 unsigned (*read)(void*,void*,unsigned);
 unsigned (*write)(void*,const void*,unsigned);
 int (*seek)(void*,int,int);
 void *(*allocate)(unsigned);
 void (*release)(void*);
} DotaMatchStorageHost;
/* Install only with no live allocation. Null restores ordinary host files. */
void match_storage_host_services(const DotaMatchStorageHost *services);
#endif
#endif
