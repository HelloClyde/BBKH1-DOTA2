#include "resource_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef union {double d;void*p;unsigned n[4];} Allocation;
static unsigned live_bytes,allocs,fail_at,checks;
void *cache_memory_alloc(unsigned n){Allocation*p;if(++allocs==fail_at)return 0;p=malloc(sizeof(*p)+n);if(!p)return 0;p->n[0]=n;live_bytes+=n;return p+1;}
void cache_memory_free(void *v){if(v){Allocation*p=(Allocation*)v-1;live_bytes-=p->n[0];free(p);}}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line%d %s (resource error%u heap%u live%u)\n",__LINE__,#x,resource_stats()->last_error,resource_heap_bytes(),live_bytes);exit(1);}}while(0)
typedef struct {ResourceFile*fx;FILE*source;unsigned bytes,calls;} Callback;
static void audio_callback(void *v){Callback*c=v;unsigned char a[64],b[64];unsigned at=(++c->calls*4099u)%(c->bytes-64u);CHECK(resource_read_at(c->fx,at,a,sizeof(a)));CHECK(!fseek(c->source,(long)at,SEEK_SET));CHECK(fread(b,1,sizeof(b),c->source)==sizeof(b));CHECK(!memcmp(a,b,sizeof(a)));}
static int failure_case(int argc,char**argv){
 ResourceFile *f,*sound;unsigned char b[16388];unsigned error,offset,length,i;int mounted;
 CHECK(argc==7);error=(unsigned)atoi(argv[3]);offset=(unsigned)strtoul(argv[5],0,0);length=(unsigned)strtoul(argv[6],0,0);CHECK(length<=16384);
 mounted=resource_mount(argv[2]);
 if(!strcmp(argv[1],"mount-failure")){CHECK(!mounted);CHECK(resource_stats()->last_error==error);}
 else {CHECK(mounted);f=resource_open((unsigned)atoi(argv[4]));sound=resource_open(RESOURCE_SOUND);CHECK(f&&sound);
  memset(b,0x69,sizeof(b));CHECK(!resource_read_at(f,offset,b+1,length));CHECK(resource_file_error(f)==(int)error);CHECK(resource_stats()->last_error==error);
  for(i=0;i<sizeof(b);++i){CHECK(b[i]==0x69);}CHECK(resource_read_at(sound,0,b,64));CHECK(resource_stats()->last_error==error);CHECK(resource_file_error(sound)==0);CHECK(!resource_close(f));CHECK(!resource_close(sound));
 }
 CHECK(resource_unmount());CHECK(!resource_is_mounted()&&!resource_heap_bytes()&&!live_bytes);printf("PASS malformed checks%u error%u\n",checks,error);return 0;
}
static int optional_cache_failure(int argc,char**argv){
 ResourceFile*f;FILE*source;unsigned got,bytes=0,base;unsigned char*a=malloc(65536),*b=malloc(65536);
 CHECK(argc==4&&a&&b);allocs=0;fail_at=7;CHECK(resource_mount(argv[2]));f=resource_open(RESOURCE_SCENE);source=fopen(argv[3],"rb");CHECK(f&&source);
 while((got=resource_read(f,a,65536))!=0){CHECK(fread(b,1,got,source)==got);CHECK(!memcmp(a,b,got));bytes+=got;}
 CHECK(bytes==resource_size(f)&&fgetc(source)==EOF);CHECK(resource_file_error(f)==0&&resource_stats()->last_error==0&&resource_stats()->errors==0);CHECK(allocs==7);CHECK(resource_heap_bytes()==live_bytes);CHECK(!resource_close(f));fclose(source);CHECK(resource_unmount());CHECK(!live_bytes&&!resource_heap_bytes());
 /* The tiny fixture starts with a64-byte canonical RAW chunk: after remount
  * an available optional allocation must be attempted again and own16KiB. */
 fail_at=0;CHECK(resource_mount(argv[2]));f=resource_open(RESOURCE_SCENE);CHECK(f);base=resource_heap_bytes();CHECK(resource_read_at(f,0,a,64));CHECK(resource_heap_bytes()==base+16384);CHECK(!resource_close(f));CHECK(resource_unmount());CHECK(!live_bytes&&!resource_heap_bytes());free(a);free(b);printf("PASS optional-cache-allocation-fallback checks%u bytes%u\n",checks,bytes);return 0;
}
static int optional_read_cache_failure(int argc,char**argv){
 unsigned pass,got,total,base,added,errors;unsigned char*a=malloc(65536),*b=malloc(65536);CHECK(argc==4&&a&&b);
 for(pass=0;pass<2;++pass){ResourceFile*f;FILE*source;allocs=0;fail_at=pass?0:7;CHECK(resource_mount(argv[2]));f=resource_open(RESOURCE_SCENE);source=fopen(argv[3],"rb");CHECK(f&&source);base=resource_heap_bytes();errors=resource_stats()->errors;
  if(pass){CHECK(resource_read_at(f,0,a,64));CHECK(fread(b,1,64,source)==64&&!memcmp(a,b,64));CHECK(!fseek(source,0,SEEK_SET));}
  added=resource_reserve_read_cache();CHECK(pass?added>0:added==0);CHECK(resource_stats()->last_error==0&&resource_stats()->errors==errors);if(pass)CHECK(resource_reserve_read_cache()==added);
  total=0;while((got=resource_read(f,a,65536))!=0){CHECK(fread(b,1,got,source)==got);CHECK(!memcmp(a,b,got));total+=got;}
  CHECK(total==resource_size(f)&&fgetc(source)==EOF);CHECK(!resource_file_error(f)&&!resource_stats()->last_error&&resource_stats()->errors==errors);CHECK(resource_heap_bytes()==base+16384+added);if(!pass)CHECK(allocs==8);
  CHECK(!resource_close(f));fclose(source);CHECK(resource_unmount());CHECK(!live_bytes&&!resource_heap_bytes());
 }
 free(a);free(b);printf("PASS optional-read-cache-fallback-and-late-reserve checks%u\n",checks);return 0;
}
int main(int argc,char**argv){
 ResourceFile*files[4],*fx;FILE*source[4];Callback callback;unsigned i,j,before,total=0;unsigned char *a,*b;
 if(argc>1&&(!strcmp(argv[1],"mount-failure")||!strcmp(argv[1],"read-failure")))return failure_case(argc,argv);
 if(argc>1&&!strcmp(argv[1],"optional-cache-failure"))return optional_cache_failure(argc,argv);
 if(argc>1&&!strcmp(argv[1],"optional-read-cache-failure"))return optional_read_cache_failure(argc,argv);
 a=malloc(65540);b=malloc(65536);CHECK(argc==6&&a&&b);CHECK(resource_mount(argv[1]));CHECK(resource_is_mounted());
 for(i=0;i<4;++i){files[i]=resource_open(i);source[i]=fopen(argv[i+2],"rb");CHECK(files[i]&&source[i]);}
 fx=resource_open(RESOURCE_SOUND);CHECK(fx);callback.fx=fx;callback.source=source[1];callback.bytes=resource_size(files[1]);callback.calls=0;resource_set_service(audio_callback,&callback);
 CHECK(resource_reserve_read_cache()>0);
 for(i=0;i<4;++i){unsigned got,n=0;CHECK(resource_seek(files[i],0,0)==0);CHECK(!fseek(source[i],0,SEEK_SET));before=callback.calls;
  while((got=resource_read(files[i],a,65536))!=0){CHECK(fread(b,1,got,source[i])==got);CHECK(!memcmp(a,b,got));n+=got;}
  CHECK(n==resource_size(files[i]));CHECK(fgetc(source[i])==EOF);CHECK(resource_file_error(files[i])==0);if(i==RESOURCE_SOUND)CHECK(callback.calls==before);total+=n;printf("SECTION%u exact %u\n",i,n);fflush(stdout);
 }
 CHECK(resource_stats()->last_error==0);CHECK(callback.calls>1);CHECK(resource_heap_bytes()==live_bytes);
 before=resource_seek(files[0],0,1);for(i=0;i<4;++i){CHECK(resource_read_at(files[0],0,a+i,64));CHECK(!fseek(source[0],0,SEEK_SET));CHECK(fread(b,1,64,source[0])==64);CHECK(!memcmp(a+i,b,64));}CHECK(resource_seek(files[0],0,1)==(int)before);
 memset(a,0x69,64);CHECK(!resource_read_at(files[0],resource_size(files[0]),a,1));for(i=0;i<64;++i)CHECK(a[i]==0x69);
 CHECK(!resource_unmount());CHECK(resource_is_mounted());
 for(i=0;i<4;++i){CHECK(!resource_close(files[i]));fclose(source[i]);}CHECK(!resource_close(fx));CHECK(resource_unmount());CHECK(!resource_is_mounted()&&!resource_heap_bytes()&&!live_bytes);
 for(j=1;j<=5;++j){allocs=0;fail_at=j;CHECK(!resource_mount(argv[1]));CHECK(!resource_is_mounted()&&!live_bytes&&!resource_heap_bytes());}
 allocs=0;fail_at=6;CHECK(resource_mount(argv[1]));CHECK(!resource_open(0));CHECK(resource_unmount());CHECK(!live_bytes);fail_at=0;
 free(a);free(b);printf("PASS checks%u bytes%u callbacks%u\n",checks,total,callback.calls);return 0;
}
