#ifndef H1_MAP_STUDY_CODEC_H
#define H1_MAP_STUDY_CODEC_H
#include <stdint.h>
/* Disjoint source/destination buffers. Byte alignment0..3 is supported. */
unsigned map_transform(const unsigned char *src,unsigned char *dst,unsigned bytes);
unsigned map_inverse(const unsigned char *src,unsigned char *dst,unsigned bytes);
unsigned map_size(const unsigned char *src,unsigned bytes);
unsigned map_encode(const unsigned char *src,unsigned bytes,unsigned char *dst,unsigned capacity);
unsigned map_decode(const unsigned char *src,unsigned bytes,unsigned char *dst,unsigned expected);
unsigned map_entry(unsigned mode,const unsigned char *src,unsigned char *dst,const uint32_t *parameters);
#endif
