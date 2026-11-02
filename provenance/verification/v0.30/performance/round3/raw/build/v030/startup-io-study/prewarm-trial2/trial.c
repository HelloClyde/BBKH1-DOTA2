#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource_pack.h"
#include "C:/Users/ASUS/Documents/ChatGPT/bbk H1/BBKH1-DOTA2/build/v030/sprite-pipeline/baked_scene.c"
static void fail(int n){fprintf(stderr,"failed line%d resource%u scene%d\n",n,resource_stats()->last_error,bs_last_error());exit(1);}
#define C(x) do{if(!(x))fail(__LINE__);}while(0)
static void phase(unsigned n,const ResourceStats*b){const ResourceStats*a=resource_stats();printf("{\"phase\":%u,\"requests\":%u,\"reads\":%u,\"physical_bytes\":%u,\"seeks\":%u,\"decoded\":%u,\"decoded_bytes\":%u}\n",n,a->read_requests-b->read_requests,a->physical_reads-b->physical_reads,a->physical_bytes-b->physical_bytes,a->seeks-b->seeks,a->decoded_chunks-b->decoded_chunks,a->decoded_bytes-b->decoded_bytes);}
static void replay(const char*path,FILE*reference,unsigned char*scratch){FILE*f=fopen(path,"rb");unsigned id;C(f);while(fread(&id,4,1,f)==1){const unsigned char*p=get_sprite(id);const RuntimeFrame*r=frame_info(id);unsigned bytes=rf_size(r);C(p&&r&&bytes<=524288);C(!fseek(reference,(long)r->offset,SEEK_SET));C(fread(scratch,1,bytes,reference)==bytes);C(!memcmp(p,scratch,bytes));}C(feof(f));fclose(f);}
int main(int argc,char**argv){ResourceStats b;FILE*ref;unsigned char*scratch=malloc(524288);C(argc==7&&scratch);ref=fopen(argv[2],"rb");C(ref);C(resource_mount(argv[1]));C(resource_reserve_read_cache()>0);C(baked_io_open_resource(0)&&bs_initialize());C(baked_io_reserve_cache(262144)==262144);C(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);C(bs_prewarm_fountain(0,0)==624);b=*resource_stats();replay(argv[3],ref,scratch);phase(0,&b);b=*resource_stats();if(atoi(argv[6]))replay(argv[5],ref,scratch);phase(1,&b);b=*resource_stats();replay(argv[4],ref,scratch);phase(2,&b);C(!bs_last_error()&&!resource_stats()->last_error);bs_close();C(resource_unmount()&&!resource_heap_bytes());fclose(ref);free(scratch);return 0;}
