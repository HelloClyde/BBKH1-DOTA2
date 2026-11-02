/* Original renderer SHA256 981faa446141e9844285d19fed22d0cbf5ef20b8ddd7e1719103989b32946959; optimized a51e6cb081e6c2c41dd9bb81f8458ce5c5ee5956f81e5dc68169e0b9cd8ce3ec. */
#include "module_abi.h"
static unsigned char shadow_curve[256][96];
static int shadow_curve_ready;
static void initialize_shadow_curve(void){
    unsigned a,c;if(shadow_curve_ready)return;
    for(a=0;a<256u;++a){
        for(c=0;c<32u;++c)shadow_curve[a][c]=(unsigned char)((c*(255u-a)+127u)/255u);
        for(c=0;c<64u;++c)shadow_curve[a][32u+c]=(unsigned char)((c*(255u-a)+127u)/255u);
    }
    shadow_curve_ready=1;
}
static inline uint16_t add_full_rgb565(uint16_t fg,uint16_t bg){
    unsigned rb=(fg&0xf81fu)+(bg&0xf81fu),g=(fg&0x07e0u)+(bg&0x07e0u),overflow;
    overflow=rb&0x10020u;rb=(rb&0xf81fu)|(overflow-(overflow>>5));
    overflow=g&0x0800u;g=(g&0x07e0u)|(overflow-(overflow>>6));
    return (uint16_t)(rb|g);
}
static uint16_t mix_original(uint16_t fg,uint16_t bg,unsigned alpha,int additive){
    unsigned r,g,b,inv=255-alpha;
    if(additive){
        r=((bg>>11)&31)+(((fg>>11)&31)*alpha+127)/255;
        g=((bg>>5)&63)+(((fg>>5)&63)*alpha+127)/255;
        b=(bg&31)+((fg&31)*alpha+127)/255;
        if(r>31)r=31;
        if(g>63)g=63;
        if(b>31)b=31;
    }else{
        r=(((fg>>11)&31)*alpha+((bg>>11)&31)*inv+127)/255;
        g=(((fg>>5)&63)*alpha+((bg>>5)&63)*inv+127)/255;
        b=((fg&31)*alpha+(bg&31)*inv+127)/255;
    }
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static uint16_t mix_optimized(uint16_t fg,uint16_t bg,unsigned alpha,int additive){
    unsigned r,g,b,inv=255u-alpha;
    if(additive){
        /* The existing shadow table stores round(c*(255-a)/255). Reversing
         * its alpha gives the exact old additive contribution, including
         * all 256 alpha values, without another table or per-pixel divide. */
        const unsigned char *curve=shadow_curve[inv];
        uint16_t scaled=(uint16_t)(((unsigned)curve[fg>>11]<<11)|
                                   ((unsigned)curve[32u+((fg>>5)&63u)]<<5)|curve[fg&31u]);
        return add_full_rgb565(scaled,bg);
    }else{
        r=(((fg>>11)&31)*alpha+((bg>>11)&31)*inv+127)/255;
        g=(((fg>>5)&63)*alpha+((bg>>5)&63)*inv+127)/255;
        b=((fg&31)*alpha+(bg&31)*inv+127)/255;
    }
    return (uint16_t)((r<<11)|(g<<5)|b);
}

typedef uint16_t (*MixFn)(uint16_t,uint16_t,unsigned,int);
typedef struct {uint32_t count,begin,end,ticks,crc,flags;} BenchCase;
static uint32_t crc_word(uint32_t crc,uint32_t value){unsigned b,k;for(b=0;b<4;++b){crc^=(value>>(b*8))&255u;for(k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&-(int)(crc&1u));}return crc;}
/* One batch remains bounded and both algorithms see the same deterministic
 * RGB/alpha sequence. Polling and the per-batch CRC cost are included. */
static __attribute__((noinline,noclone)) BenchCase measure(const h1_exec_api *api,unsigned variant,unsigned requested){
 BenchCase r={0,0,0,0,0,0};MixFn fn=variant?mix_optimized:mix_original;uint32_t rng=0x91d48a7bu,crc=0xffffffffu;
 r.begin=api->tick_80hz();
 while(r.count<requested){uint32_t n=requested-r.count,i,sum=0;if(n>16384u)n=16384u;
  for(i=0;i<n;++i){uint16_t fg,bg;unsigned a;rng=rng*1664525u+1013904223u;fg=(uint16_t)(rng>>16);bg=(uint16_t)(rng^(rng>>11));a=(rng>>8)&255u;sum+=(uint32_t)fn(fg,bg,a,1)+(r.count+i);}
  r.count+=n;crc=crc_word(crc,sum);
  if(api->poll_cancel()){r.flags|=1u;break;}
  if(api->tick_80hz()-r.begin>=24u && r.count<requested){r.flags|=2u;break;}
 }
 r.end=api->tick_80hz();r.ticks=r.end-r.begin;r.crc=crc^0xffffffffu;return r;
}
int h1_module_entry(const h1_exec_api *api,h1_exec_call *call){
 uint32_t requested=1048576u,*out;BenchCase old,new;unsigned i;
 if(!api || api->version!=H1_EXEC_ABI || !call || call->version!=H1_EXEC_ABI || call->opcode>1u || call->output_capacity<80u)return -1;
 if(call->arg_size){if(call->arg_size!=4u)return -2;requested=call->args[0]|((uint32_t)call->args[1]<<8)|((uint32_t)call->args[2]<<16)|((uint32_t)call->args[3]<<24);}
 if(!requested || requested>2097152u)return -3;
 initialize_shadow_curve();if(api->poll_cancel())return -4;
 if(call->opcode){new=measure(api,1,requested);old=measure(api,0,new.count);}else{old=measure(api,0,requested);new=measure(api,1,old.count);}
 out=(uint32_t *)(void *)call->output;for(i=0;i<20u;++i)out[i]=0;
 out[0]=1;out[1]=call->opcode;out[2]=requested;out[3]=old.count;out[4]=old.begin;out[5]=old.end;out[6]=old.ticks;out[7]=old.crc;out[8]=old.flags;
 out[9]=new.count;out[10]=new.begin;out[11]=new.end;out[12]=new.ticks;out[13]=new.crc;out[14]=new.flags;
 out[15]=old.count==new.count && old.crc==new.crc;out[16]=24576;out[17]=16384;out[18]=24;out[19]=(uint32_t)shadow_curve_ready;call->output_size=80;
 return out[15] && !(old.flags|new.flags)?0:-5;
}
