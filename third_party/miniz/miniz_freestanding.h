/* H1 freestanding adapter for miniz 3.1.0. The upstream decoder and license
 * are preserved under upstream/. Only types, disabled allocators and the
 * project's existing byte primitives are supplied here; no libc is linked. */
#ifndef H1_MINIZ_FREESTANDING_H
#define H1_MINIZ_FREESTANDING_H
#include <stddef.h>
#include <stdint.h>
#define MINIZ_EXPORT
#define MINIZ_NO_MALLOC
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_LITTLE_ENDIAN 1
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_HAS_64BIT_REGISTERS 0
typedef unsigned char mz_uint8;
typedef int16_t mz_int16;
typedef uint16_t mz_uint16;
typedef uint32_t mz_uint32;
typedef uint32_t mz_uint;
typedef int64_t mz_int64;
typedef uint64_t mz_uint64;
typedef int mz_bool;
#define MZ_FALSE 0
#define MZ_TRUE 1
#define MZ_MACRO_END while(0)
#define MZ_ASSERT(x) ((void)0)
#define MZ_MALLOC(x) ((void)(x),(void *)0)
#define MZ_FREE(x) ((void)(x))
#define MZ_REALLOC(p,x) ((void)(p),(void)(x),(void *)0)
#define MZ_MAX(a,b) (((a)>(b))?(a):(b))
#define MZ_MIN(a,b) (((a)<(b))?(a):(b))
#define MZ_CLEAR_OBJ(x) memset(&(x),0,sizeof(x))
#define MZ_CLEAR_ARR(x) memset((x),0,sizeof(x))
#define MZ_CLEAR_PTR(x) memset((x),0,sizeof(*(x)))
#define MZ_READ_LE16(p) ((mz_uint32)((const mz_uint8 *)(p))[0]|((mz_uint32)((const mz_uint8 *)(p))[1]<<8))
#define MZ_READ_LE32(p) (MZ_READ_LE16(p)|(MZ_READ_LE16(((const mz_uint8 *)(p))+2)<<16))
#define MZ_READ_LE64(p) ((mz_uint64)MZ_READ_LE32(p)|((mz_uint64)MZ_READ_LE32(((const mz_uint8 *)(p))+4)<<32))
#define MZ_UINT16_MAX 0xffffu
#define MZ_UINT32_MAX 0xffffffffu
#if defined(__GNUC__)
#define MZ_FORCEINLINE __inline__ __attribute__((__always_inline__))
#else
#define MZ_FORCEINLINE inline
#endif
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
#endif
