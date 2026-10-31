#ifndef H1_DOTA_BAKED_IO_H
#define H1_DOTA_BAKED_IO_H

#include <stdint.h>

#define BAKED_IO_PATH_A "A:\\DOTA2\\SCENE.H1P"
#define BAKED_IO_PATH_B "B:\\DOTA2\\SCENE.H1P"

enum {
    BAKED_IO_OK=0, BAKED_IO_UNAVAILABLE=1, BAKED_IO_OPEN_FAILED=2,
    BAKED_IO_SIZE_FAILED=3, BAKED_IO_NOT_OPEN=4, BAKED_IO_RANGE=5,
    BAKED_IO_SEEK_FAILED=6, BAKED_IO_READ_FAILED=7, BAKED_IO_CLOSE_FAILED=8
};

typedef struct {
    uint32_t open_attempts,read_requests,seeks,raw_reads,bytes_read;
    uint32_t cache_hits,errors;
} BakedIOStats;

typedef struct {
    uint32_t open_attempts,open_failures,read_requests,seeks,raw_reads,bytes_read,errors,closes;
} BakedIOAuxStats;
/* Optional independent direct file. Main open_attempts/errors/cache_hits keep
 * main-pack semantics. read_requests/seeks/raw_reads/bytes_read aggregate
 * both handles; auxiliary failures remain visible in own stats/error. */
int baked_io_aux_open(const char *path);
void baked_io_aux_close(void);
uint32_t baked_io_aux_size(void);
int baked_io_aux_is_open(void);
int baked_io_aux_read_at(uint32_t offset,void *dst,uint32_t bytes);
int baked_io_aux_error(void);
const BakedIOAuxStats *baked_io_aux_stats(void);

/* One read-only resource file. Native H1 paths use ASCII
 * directory/file names. The host build uses -DBAKED_IO_HOST and explicit paths. */
int baked_io_open(const char *path);
int baked_io_open_default(void); /* A:\DOTA2\SCENE.H1P, then B: */
void baked_io_close(void);
uint32_t baked_io_size(void);
int baked_io_is_open(void);
/* Optional four-way set-associative metadata pages, rounded down to a power
 * of two. Allocation failure/zero keeps the static4KiB fallback. Opening or
 * closing a file releases this cache; request it after opening the resource. */
unsigned baked_io_reserve_cache(unsigned bytes);
unsigned baked_io_cache_bytes(void);
unsigned baked_io_dynamic_storage_bytes(void);

/* Return 1 only after copying every requested byte, 0 on failure. A failed
 * physical read may partially modify dst; malformed ranges never touch it.
 * File offsets are limited to INT32_MAX by the H1 firmware seek ABI.
 * Metadata reads <=1024 bytes use the bounded metadata page cache. Larger
 * requests go directly into the caller's tile/sprite cache in <=64KiB reads. */
int baked_io_read_at(uint32_t offset,void *dst,uint32_t bytes);
int baked_io_error(void);
const BakedIOStats *baked_io_stats(void);
void baked_io_reset_stats(void);

#endif
