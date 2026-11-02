#include "match_storage.h"
#include "cache_memory.h"
#ifdef __mips__
#include <stddef.h>
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
#else
#include <string.h>
#endif
#ifdef MATCH_STORAGE_HOST
#include <stdio.h>
static DotaMatchStorageHost host;
#elif defined(BAKED_IO_HOST)||!defined(__mips__)
#include <stdio.h>
#else
#include "h1_filesystem.h"
#endif

#define STORAGE_HEAP_BYTES (sizeof(DotaMatchHistory)+sizeof(DotaMatchRecord)+DOTA_MATCH_RECORD_BYTES)
static const char *const paths[4]={"B:\\DOTA2\\MATCH0.H1S","B:\\DOTA2\\MATCH1.H1S","A:\\DOTA2.HS0","A:\\DOTA2.HS1"};
static void *allocation;
static DotaMatchHistory *history;
static DotaMatchRecord *temporary;
static unsigned char *wire;
static int active_slot=-1,busy,dirty,read_only,in_service;
static uint32_t result_id;
static DotaMatchHistoryMeta slot_meta[4];
static unsigned slot_valid,slot_bad;
static DotaMatchStorageStats status;
static void (*service_callback)(void*);
static void *service_context;
static void service(void){if(service_callback&&!in_service){in_service=1;service_callback(service_context);in_service=0;}}
void match_storage_set_service(void(*callback)(void*),void*context){service_callback=callback;service_context=context;}
static int error(unsigned code){status.last_error=code;return -1;}
#ifdef MATCH_STORAGE_HOST
void match_storage_host_services(const DotaMatchStorageHost*s){if(allocation){(void)error(MATCH_STORAGE_BUSY);return;}if(s)host=*s;else memset(&host,0,sizeof(host));}
#endif
static void *allocate_memory(unsigned n){
#ifdef MATCH_STORAGE_HOST
 if(host.allocate)return host.allocate(n);
#endif
 return cache_memory_alloc(n);
}
static void release_memory(void*p){
#ifdef MATCH_STORAGE_HOST
 if(host.release){host.release(p);return;}
#endif
 cache_memory_free(p);
}
static int filesystem_available(void){
#if defined(MATCH_STORAGE_HOST)||defined(BAKED_IO_HOST)||!defined(__mips__)
 return 1;
#else
 void *t=h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT);
 return t&&h1_runtime_entry(t,H1_FS_FOPEN_OFFSET)&&h1_runtime_entry(t,H1_FS_FCLOSE_OFFSET)&&h1_runtime_entry(t,H1_FS_FREAD_OFFSET)&&h1_runtime_entry(t,H1_FS_FWRITE_OFFSET)&&h1_runtime_entry(t,H1_FS_FSEEK_OFFSET);
#endif
}
static void *open_file(const char*path,const char*mode){void*f;
#ifdef MATCH_STORAGE_HOST
 if(host.open)f=host.open(path,mode);else f=fopen(path,mode);
#elif defined(BAKED_IO_HOST)||!defined(__mips__)
 f=fopen(path,mode);
#else
 f=h1_fopen(path,mode);
#endif
 if(f){++status.open_files;}return f;
}
static int close_file(void*f){int r;if(!f)return 0;
#ifdef MATCH_STORAGE_HOST
 if(host.close)r=host.close(f);else r=fclose(f);
#elif defined(BAKED_IO_HOST)||!defined(__mips__)
 r=fclose(f);
#else
 r=h1_fclose(f);
#endif
 --status.open_files;return r;
}
static int seek_file(void*f,int off,int whence){
#ifdef MATCH_STORAGE_HOST
 if(host.seek)return host.seek(f,off,whence);
#endif
#if defined(MATCH_STORAGE_HOST)||defined(BAKED_IO_HOST)||!defined(__mips__)
 {long n;if(fseek(f,off,whence))return -1;n=ftell(f);return n<0||n>0x7fffffffL?-1:(int)n;}
#else
 return h1_fseek(f,off,whence);
#endif
}
static unsigned read_file(void*f,void*p,unsigned n){++status.reads;
#ifdef MATCH_STORAGE_HOST
 if(host.read)return host.read(f,p,n);
#endif
#if defined(MATCH_STORAGE_HOST)||defined(BAKED_IO_HOST)||!defined(__mips__)
 return (unsigned)fread(p,1,n,f);
#else
 return h1_fread(p,1,n,f);
#endif
}
static unsigned write_file(void*f,const void*p,unsigned n){++status.writes;
#ifdef MATCH_STORAGE_HOST
 if(host.write)return host.write(f,p,n);
#endif
#if defined(MATCH_STORAGE_HOST)||defined(BAKED_IO_HOST)||!defined(__mips__)
 return (unsigned)fwrite(p,1,n,f);
#else
 return h1_fwrite(p,1,n,f);
#endif
}
static int read_exact(void*f,void*p,unsigned n){unsigned char*b=p;while(n){unsigned want=n>1024?1024:n,got=read_file(f,b,want);service();if(got!=want)return 0;b+=want;n-=want;}return 1;}
static int write_exact(void*f,const void*p,unsigned n){const unsigned char*b=p;while(n){unsigned want=n>1024?1024:n,got=write_file(f,b,want);service();if(got!=want)return 0;b+=want;n-=want;}return 1;}
/* 0 missing,1 fully valid,-1 corrupt/short/unreadable. No write or truncation. */
static int snapshot(unsigned slot,DotaMatchHistoryMeta*out,int load){
 void*f=open_file(paths[slot],"rb");unsigned char header[64];DotaMatchHistoryMeta meta;unsigned i,j,seen_count=0;uint32_t c=0,seen[50];int ok=0;
 if(!f)return 0;
 if(seek_file(f,0,2)!=(int)DOTA_MATCH_SAVE_BYTES||seek_file(f,0,0)!=0||!read_exact(f,header,64)||!dota_match_header_decode(&meta,header))goto done;
 for(i=0;i<50;i++){
  int used=meta.count==50||i<meta.count;
  if(!read_exact(f,wire,DOTA_MATCH_RECORD_BYTES)||!dota_match_record_decode(temporary,wire)||(used?!temporary->match_id:temporary->match_id!=0))goto done;
  c=dota_match_crc_update(c,wire,DOTA_MATCH_RECORD_BYTES);
  if(used){for(j=0;j<seen_count;j++)if(seen[j]==temporary->match_id)goto done;seen[seen_count++]=temporary->match_id;}
  if(load)history->records[i]=*temporary;
 }
 if(c!=meta.payload_crc)goto done;
 *out=meta;ok=1;
done:
 if(close_file(f)!=0)ok=0;
 if(ok&&load){history->count=meta.count;history->next=meta.next;history->sequence=meta.sequence;history->next_match_id=meta.next_match_id;}
 return ok?1:-1;
}
static int newer(uint32_t a,uint32_t b){return (int32_t)(a-b)>0;}
int match_storage_open(void){
 unsigned i,bad=0,valid=0;int best=-1;
 if(busy)return error(MATCH_STORAGE_BUSY);
 if(allocation)return 1;
 memset(&status,0,sizeof(status));status.active_slot=UINT32_MAX;active_slot=-1;dirty=read_only=0;result_id=0;slot_valid=slot_bad=0;
 if(!filesystem_available())return error(MATCH_STORAGE_IO);
 allocation=allocate_memory((unsigned)STORAGE_HEAP_BYTES);if(!allocation)return error(MATCH_STORAGE_MEMORY);
 status.heap_bytes=(unsigned)STORAGE_HEAP_BYTES;history=allocation;temporary=(DotaMatchRecord*)((unsigned char*)allocation+sizeof(*history));wire=(unsigned char*)temporary+sizeof(*temporary);dota_match_history_init(history);
 busy=1;
 for(i=0;i<4;i++){
  int r=snapshot(i,&slot_meta[i],0);if(r==1){valid|=1u<<i;if(best<0||newer(slot_meta[i].sequence,slot_meta[best].sequence))best=(int)i;}else if(r<0){++bad;slot_bad|=1u<<i;}
 }
 while(best>=0){
  dota_match_history_init(history);
  if(snapshot((unsigned)best,&slot_meta[best],1)==1)break;
  valid&=~(1u<<(unsigned)best);slot_bad|=1u<<(unsigned)best;++bad;best=-1;
  for(i=0;i<4;i++)if((valid&(1u<<i))&&(best<0||newer(slot_meta[i].sequence,slot_meta[best].sequence)))best=(int)i;
 }
 active_slot=best;slot_valid=valid;status.active_slot=best>=0?(uint32_t)best:UINT32_MAX;status.recovered_slots=bad;status.ready=1;status.history_count=history->count;busy=0;
 if(best<0&&bad){read_only=slot_bad==15u;status.last_error=MATCH_STORAGE_CORRUPT;}
 return 1;
}
void match_storage_close(void){
 if(busy){(void)error(MATCH_STORAGE_BUSY);return;}
 if(allocation){release_memory(allocation);}allocation=0;history=0;temporary=0;wire=0;result_id=0;dirty=read_only=0;active_slot=-1;slot_valid=slot_bad=0;
 service_callback=0;service_context=0;status.ready=status.heap_bytes=status.history_count=0;status.active_slot=UINT32_MAX;status.released=1;
}
const DotaMatchHistory*match_storage_history(void){return history;}
const DotaMatchRecord*match_storage_result(void){return dota_match_history_find(history,result_id);}
int match_storage_error(void){return (int)status.last_error;}
unsigned match_storage_heap_bytes(void){return status.heap_bytes;}
const DotaMatchStorageStats*match_storage_stats(void){return &status;}
uint32_t match_storage_begin_match(void){
 uint32_t id;unsigned tries=0;if(busy){(void)error(MATCH_STORAGE_BUSY);return 0;}if(!allocation&&match_storage_open()<1)return 0;
 id=history->next_match_id;if(!id)id=1;
 while(dota_match_history_find(history,id)&&tries++<51){if(!++id)id=1;}
 history->next_match_id=id+1u;if(!history->next_match_id)history->next_match_id=1;result_id=0;return id;
}
static unsigned older_slot(unsigned base){
 if(!(slot_valid&(1u<<base)))return base;
 if(!(slot_valid&(1u<<(base+1u))))return base+1u;
 return newer(slot_meta[base].sequence,slot_meta[base+1u].sequence)?base+1u:base;
}
static int commit(unsigned slot){
 unsigned char header[64];unsigned i;uint32_t c=0;DotaMatchHistoryMeta verified;void*f;int ok=0;
 if((int)slot==active_slot)return 0;
 for(i=0;i<50;i++){
  if(!dota_match_record_encode(&history->records[i],wire))return 0;
  c=dota_match_crc_update(c,wire,DOTA_MATCH_RECORD_BYTES);service();
 }
 if(!dota_match_header_encode(history,c,header))return 0;
 if(active_slot<0&&(slot_bad&(1u<<slot)))return 0;
 f=open_file(paths[slot],"wb");if(!f)return 0;
 slot_valid&=~(1u<<slot);slot_bad|=1u<<slot;
 if(!write_exact(f,header,64))goto done;
 for(i=0;i<50;i++){if(!dota_match_record_encode(&history->records[i],wire)||!write_exact(f,wire,DOTA_MATCH_RECORD_BYTES))goto done;}
 ok=1;
done:
 if(close_file(f)!=0)ok=0;
 if(!ok)return 0;
 if(snapshot(slot,&verified,0)!=1||verified.sequence!=history->sequence||verified.payload_crc!=c||verified.count!=history->count||verified.next!=history->next||verified.next_match_id!=history->next_match_id)return 0;
 slot_meta[slot]=verified;slot_valid|=1u<<slot;slot_bad&=~(1u<<slot);active_slot=(int)slot;status.active_slot=slot;return 1;
}
int match_storage_complete(const DotaGame*g){
 uint32_t old_sequence;int added;unsigned first,second;
 if(busy)return error(MATCH_STORAGE_BUSY);
 if(!allocation&&match_storage_open()<1)return -1;
 if(!dota_match_capture(g,temporary))return error(MATCH_STORAGE_INVALID);
 added=dota_match_history_append(history,temporary);if(added<0)return error(MATCH_STORAGE_INVALID);
 result_id=g->match_stats.match_id;status.history_count=history->count;if(added)dirty=1;
 if(!dirty){status.last_error=MATCH_STORAGE_OK;return 0;}
 if(read_only)return error(MATCH_STORAGE_CORRUPT);
 busy=1;++status.save_attempts;old_sequence=history->sequence;history->sequence=old_sequence+1u;
 if(active_slot>=0){first=(unsigned)active_slot^1u;second=older_slot(first<2?2u:0u);}
 else{unsigned i;first=second=4;
  for(i=0;i<4;i++)if(!(slot_bad&(1u<<i))){first=i;break;}
  for(i=0;i<4;i++)if(i!=first&&!(slot_bad&(1u<<i))&&((i<2)!=(first<2))){second=i;break;}
  if(second==4)for(i=0;i<4;i++)if(i!=first&&!(slot_bad&(1u<<i))){second=i;break;}
 }
 if((first<4&&commit(first))||(second<4&&commit(second))){dirty=0;busy=0;++status.successful_saves;status.last_error=MATCH_STORAGE_OK;return 1;}
 history->sequence=old_sequence;busy=0;return error(MATCH_STORAGE_IO);
}
