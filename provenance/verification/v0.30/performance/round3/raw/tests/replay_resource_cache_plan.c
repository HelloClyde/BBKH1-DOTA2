#include "resource_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void fail(int line){fprintf(stderr,"failure line%d error%u\n",line,resource_stats()->last_error);exit(1);}
#define C(x) do{if(!(x))fail(__LINE__);}while(0)
static void emit(unsigned phase,const ResourceStats*b,unsigned requests,unsigned bytes){
 const ResourceStats*a=resource_stats();
 printf("{\"phase\":%u,\"requests\":%u,\"canonical_bytes\":%u,\"seeks\":%u,\"physical_reads\":%u,\"physical_bytes\":%u,\"index_hits\":%u,\"index_misses\":%u,\"chunk_hits\":%u,\"chunk_misses\":%u,\"decoded_chunks\":%u,\"decoded_bytes\":%u,\"heap_bytes\":%u}\n",phase,requests,bytes,a->seeks-b->seeks,a->physical_reads-b->physical_reads,a->physical_bytes-b->physical_bytes,a->index_hits-b->index_hits,a->index_misses-b->index_misses,a->chunk_hits-b->chunk_hits,a->chunk_misses-b->chunk_misses,a->decoded_chunks-b->decoded_chunks,a->decoded_bytes-b->decoded_bytes,a->heap_bytes);
}
int main(int argc,char**argv){
 FILE*plan,*refs[4];ResourceFile*files[4];ResourceStats before;unsigned rec[4],i,phase=0,requests=0,bytes=0;unsigned char*a=malloc(524288),*b=malloc(524288);
 C(argc==7&&a&&b);plan=fopen(argv[2],"rb");C(plan);C(resource_mount(argv[1]));
#ifdef USE_RESERVED_READ_CACHE
 C(resource_reserve_read_cache()>0);
#endif
 for(i=0;i<4;i++){refs[i]=fopen(argv[3+i],"rb");files[i]=resource_open(i);C(refs[i]&&files[i]);}
 before=*resource_stats();
 while(fread(rec,4,4,plan)==4){
  C(rec[0]>=phase&&rec[0]<3&&rec[1]<4&&rec[3]<=524288);
  if(rec[0]!=phase){emit(phase,&before,requests,bytes);before=*resource_stats();requests=bytes=0;phase=rec[0];}
  C(resource_read_at(files[rec[1]],rec[2],a,rec[3]));C(!fseek(refs[rec[1]],(long)rec[2],SEEK_SET));C(fread(b,1,rec[3],refs[rec[1]])==rec[3]);C(!memcmp(a,b,rec[3]));requests++;bytes+=rec[3];
 }
 C(feof(plan));emit(phase,&before,requests,bytes);C(!resource_stats()->last_error);
 for(i=0;i<4;i++){C(!resource_file_error(files[i]));C(!resource_close(files[i]));fclose(refs[i]);}
 C(resource_unmount());C(!resource_heap_bytes());fclose(plan);free(a);free(b);return 0;
}
