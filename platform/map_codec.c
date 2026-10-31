/* Read-only feasibility codec. Source tiles always retain all65536 bytes. */
#include <stdint.h>
#include "cache_rle.h"
#define PLANE_PIXELS 16384u
#define TILE_BYTES 65536u
unsigned map_transform(const unsigned char *src,unsigned char *dst,unsigned bytes){
    unsigned plane,i;if(bytes!=TILE_BYTES)return 0;
    for(plane=0;plane<2;++plane){
        unsigned previous=0,base=plane*PLANE_PIXELS*2u;
        for(i=0;i<PLANE_PIXELS;++i){
            unsigned value=src[base+i*2u]|((unsigned)src[base+i*2u+1u]<<8);
            unsigned delta=(value-previous)&65535u;
            unsigned zigzag=((delta<<1)^(0u-(delta>>15)))&65535u;
            dst[base+i]=(unsigned char)zigzag;dst[base+PLANE_PIXELS+i]=(unsigned char)(zigzag>>8);previous=value;
        }
    }
    return bytes;
}
unsigned map_inverse(const unsigned char *src,unsigned char *dst,unsigned bytes){
    unsigned plane,i;if(bytes!=TILE_BYTES)return 0;
    for(plane=0;plane<2;++plane){
        unsigned previous=0,base=plane*PLANE_PIXELS*2u;
        for(i=0;i<PLANE_PIXELS;++i){
            unsigned zigzag=src[base+i]|((unsigned)src[base+PLANE_PIXELS+i]<<8);
            unsigned delta=((zigzag>>1)^(0u-(zigzag&1u)))&65535u;
            unsigned value=(previous+delta)&65535u;
            dst[base+i*2u]=(unsigned char)value;dst[base+i*2u+1u]=(unsigned char)(value>>8);previous=value;
        }
    }
    return bytes;
}
unsigned map_size(const unsigned char *src,unsigned bytes){return cache_rle_encoded_size(src,bytes);}
unsigned map_encode(const unsigned char *src,unsigned bytes,unsigned char *dst,unsigned capacity){return cache_rle_encode(src,bytes,dst,capacity);}
unsigned map_decode(const unsigned char *src,unsigned bytes,unsigned char *dst,unsigned expected){return cache_rle_decode(src,bytes,dst,expected);}
/* O32 four-register entry for exact complete function instruction counts.
 * p[0]=inputbytes, p[1]=destinationcapacity/expectedbytes. */
unsigned map_entry(unsigned mode,const unsigned char *src,unsigned char *dst,const uint32_t *p){
    if(mode==0)return map_transform(src,dst,p[0]);
    if(mode==1)return map_inverse(src,dst,p[0]);
    if(mode==2)return map_size(src,p[0]);
    if(mode==3)return map_encode(src,p[0],dst,p[1]);
    if(mode==4)return map_decode(src,p[0],dst,p[1]);
    return 0;
}
