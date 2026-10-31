#ifndef H1_ISOLATED_CACHE_RLE_H
#define H1_ISOLATED_CACHE_RLE_H
#include <stdint.h>
static unsigned cache_rle_encoded_size(const unsigned char *src,unsigned bytes){
    unsigned at=0,out=0;
    while(at<bytes){unsigned run=0,begin=at;
        while(at+run<bytes && !src[at+run] && run<128u)++run;
        if(run>=2u){++out;at+=run;continue;}
        ++at;while(at<bytes && at-begin<128u){if(!src[at] && at+1u<bytes && !src[at+1u])break;++at;}
        out+=1u+at-begin;
    }
    return out;
}
/* Isolated lossless cache feasibility codecs. No renderer/device integration. */


static unsigned cache_rle_encode(const unsigned char *src,unsigned bytes,unsigned char *dst,unsigned capacity){
    unsigned at=0,out=0;
    while(at<bytes){
        unsigned run=0,begin=at;
        while(at+run<bytes && !src[at+run] && run<128u)++run;
        if(run>=2u){if(out>=capacity)return 0;dst[out++]=(unsigned char)(128u+run-1u);at+=run;continue;}
        ++at;
        while(at<bytes && at-begin<128u){if(!src[at] && at+1u<bytes && !src[at+1u])break;++at;}
        run=at-begin;if(out>=capacity || run>capacity-out-1u)return 0;
        dst[out++]=(unsigned char)(run-1u);while(begin<at)dst[out++]=src[begin++];
    }
    return out;
}
static unsigned cache_rle_decode(const unsigned char *src,unsigned bytes,unsigned char *dst,unsigned expected){
    unsigned at=0,out=0;const uint32_t zero=0;
    uintptr_t source=(uintptr_t)src,target=(uintptr_t)dst;
    /* Fixed-size memcpy is defined for disjoint objects. Preserve the old
     * bytewise behavior for any caller whose input/output ranges overlap. */
    if((source<=target && target-source<bytes) || (target<source && source-target<expected)){
        while(at<bytes){unsigned control=src[at++],run=(control&127u)+1u;
            if(run>expected-out)return 0;
            if(control&128u){while(run--)dst[out++]=0;}
            else {if(run>bytes-at)return 0;while(run--)dst[out++]=src[at++];}
        }
        return out==expected?out:0;
    }
    while(at<bytes){
        unsigned control=src[at++],run=(control&127u)+1u;
        if(run>expected-out)return 0;
        if(control&128u){
            if(run<8u){while(run--)dst[out++]=0;continue;}
            while(run && ((uintptr_t)(dst+out)&3u)){dst[out++]=0;--run;}
            while(run>=16u){
                unsigned char *p=(unsigned char *)__builtin_assume_aligned(dst+out,4);
                __builtin_memcpy(p,&zero,4);__builtin_memcpy(p+4,&zero,4);
                __builtin_memcpy(p+8,&zero,4);__builtin_memcpy(p+12,&zero,4);
                out+=16u;run-=16u;
            }
            while(run>=4u){unsigned char *p=(unsigned char *)__builtin_assume_aligned(dst+out,4);__builtin_memcpy(p,&zero,4);out+=4u;run-=4u;}
            while(run--)dst[out++]=0;
        }else{
            if(run>bytes-at)return 0;
            if(run<8u){while(run--)dst[out++]=src[at++];continue;}
            while(run && ((uintptr_t)(dst+out)&3u)){dst[out++]=src[at++];--run;}
            while(run>=16u){
                unsigned char *p=(unsigned char *)__builtin_assume_aligned(dst+out,4);
                __builtin_memcpy(p,src+at,4);__builtin_memcpy(p+4,src+at+4,4);
                __builtin_memcpy(p+8,src+at+8,4);__builtin_memcpy(p+12,src+at+12,4);
                out+=16u;at+=16u;run-=16u;
            }
            while(run>=4u){unsigned char *p=(unsigned char *)__builtin_assume_aligned(dst+out,4);__builtin_memcpy(p,src+at,4);out+=4u;at+=4u;run-=4u;}
            while(run--)dst[out++]=src[at++];
        }
    }
    return out==expected?out:0;
}

#endif
