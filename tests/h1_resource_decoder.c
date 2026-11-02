/* Pure bounded H1 EXEC benchmark. Generated wrapper supplies an actual H1R
 * vector and includes this file. No filesystem, MMIO, allocation or writes
 * outside the EXEC module/output are used. The parent owns device execution. */
#include "module_abi.h"
#include "../third_party/miniz/miniz_tinfl.c"
#include "../platform/map_crc_table.h"
void *memcpy(void *dst,const void *src,size_t n){unsigned char*d=dst;const unsigned char*s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int value,size_t n){unsigned char*d=dst;while(n--)*d++=(unsigned char)value;return dst;}
static unsigned char decoded_output[BENCH_RAW_BYTES];
#if BENCH_CODEC
static tinfl_decompressor decoder_state;
#endif
#if BENCH_CODEC==2
static unsigned char inverse_output[BENCH_RAW_BYTES];
#endif
static uint32_t checksum(const unsigned char*p,unsigned n){uint32_t v=0xffffffffu;while(n--)v=mapc_crc_table[(v^*p++)&255u]^(v>>8);return v^0xffffffffu;}
static void put32(unsigned char*p,uint32_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static int decode(void){
#if BENCH_CODEC
 size_t in=sizeof(bench_vector),out=sizeof(decoded_output);tinfl_status status;
 tinfl_init(&decoder_state);status=tinfl_decompress(&decoder_state,bench_vector,&in,decoded_output,decoded_output,&out,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
 if(status!=TINFL_STATUS_DONE||in!=sizeof(bench_vector)||out!=sizeof(decoded_output))return 0;
#else
 memcpy(decoded_output,bench_vector,sizeof(decoded_output));
#endif
#if BENCH_CODEC==2
 {unsigned i,previous=0,count=sizeof(decoded_output)/2u;
  for(i=0;i<count;++i){unsigned z=decoded_output[i]|((unsigned)decoded_output[count+i]<<8),d=((z>>1)^(0u-(z&1u)))&65535u;previous=(previous+d)&65535u;inverse_output[i*2u]=(unsigned char)previous;inverse_output[i*2u+1]=(unsigned char)(previous>>8);}
  memcpy(decoded_output,inverse_output,sizeof(decoded_output));
 }
#endif
 return 1;
}
int h1_module_entry(const h1_exec_api*api,h1_exec_call*call){
 uint32_t begin,end,rounds=0,duration=160,flags=0,last_crc=0,decode_ticks=0,max_decode_ticks=0;unsigned i;
 if(!api||!call||api->version!=H1_EXEC_ABI||api->size!=sizeof(*api)||call->version!=H1_EXEC_ABI||call->size!=sizeof(*call)||!api->tick_80hz||!api->poll_cancel||call->output_capacity<80||call->opcode>1)return -1;
 if(call->arg_size){duration=call->args[0];if(!duration||duration>240)return -2;}
 begin=end=api->tick_80hz();
 while(rounds<4096u&&end-begin<duration){uint32_t start,after,delta;
  if(api->poll_cancel()){flags|=1;break;}
  start=api->tick_80hz();if(!decode()){flags|=2;break;}after=api->tick_80hz();delta=after-start;decode_ticks+=delta;if(delta>max_decode_ticks)max_decode_ticks=delta;
  ++rounds;if(api->poll_cancel()){flags|=1;break;}
  if(!call->opcode){last_crc=checksum(decoded_output,sizeof(decoded_output));if(last_crc!=BENCH_CANONICAL_CRC){flags|=4;break;}}
  end=api->tick_80hz();
 }
 if(call->opcode&&rounds){last_crc=checksum(decoded_output,sizeof(decoded_output));if(last_crc!=BENCH_CANONICAL_CRC)flags|=4;}
 end=api->tick_80hz();
 {const uint32_t values[20]={1,BENCH_CODEC,BENCH_CHUNK_ID,sizeof(decoded_output),sizeof(bench_vector),BENCH_CANONICAL_CRC,last_crc,rounds,begin,end,end-begin,decode_ticks,max_decode_ticks,rounds*sizeof(decoded_output),flags,call->opcode,sizeof(tinfl_decompressor),4096,duration,0};for(i=0;i<20;++i)put32(call->output+i*4u,values[i]);}
 call->output_size=80;return flags?-3:0;
}
