/* DOTA2.H1R v1: one physical resource container, independent logical streams.
 * Index guides are small and resident; pages, compressed input, tinfl state
 * and two decoded chunks live in the firmware heap. Audio is always RAW. */
#include "resource_pack.h"
#include "cache_memory.h"
#include "map_sidecar_format.h"
#include <stddef.h>
#include "C:/Users/ASUS/Documents/ChatGPT/bbk H1/BBKH1-DOTA2/third_party/miniz/miniz_tinfl.c"

#ifdef BAKED_IO_HOST
#include <stdio.h>
typedef FILE NativeFile;
static NativeFile *native_open(const char *p){return fopen(p,"rb");}
static int native_close(NativeFile *f){return fclose(f);}
static int native_seek(NativeFile *f,uint32_t at){return fseek(f,(long)at,SEEK_SET)==0;}
static int native_size(NativeFile *f){long n;if(fseek(f,0,SEEK_END))return -1;n=ftell(f);return n<0||n>0x7fffffffL?-1:(int)n;}
static unsigned native_read(NativeFile *f,void *p,unsigned n){return (unsigned)fread(p,1,n,f);}
static int native_available(void){return 1;}
#else
#include "h1_filesystem.h"
typedef h1_file NativeFile;
static NativeFile *native_open(const char *p){return h1_fopen(p,"rb");}
static int native_close(NativeFile *f){return h1_fclose(f);}
static int native_seek(NativeFile *f,uint32_t at){return h1_fseek(f,(int)at,0)==(int)at;}
static int native_size(NativeFile *f){return h1_fseek(f,0,2);}
static unsigned native_read(NativeFile *f,void *p,unsigned n){return h1_fread(p,1,n,f);}
static int native_available(void){
 void *table=h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT);
 return table&&h1_runtime_entry(table,H1_FS_FOPEN_OFFSET)&&h1_runtime_entry(table,H1_FS_FCLOSE_OFFSET)&&h1_runtime_entry(table,H1_FS_FSEEK_OFFSET)&&h1_runtime_entry(table,H1_FS_FREAD_OFFSET);
}
#endif

#define RESOURCE_FILE_MAGIC 0x31465352u
#define HEADER_BYTES 128u
#define SECTION_BYTES 96u
#define GUIDE_BYTES 24u
#define SPAN_RECORD_BYTES 12u
#define SPAN_RECORDS_PER_PAGE 1024u
#define CHUNK_RECORD_BYTES 20u
#define CHUNK_RECORDS_PER_PAGE 512u
#define SPAN_PAGE_BYTES (SPAN_RECORD_BYTES*SPAN_RECORDS_PER_PAGE)
#define CHUNK_PAGE_BYTES (CHUNK_RECORD_BYTES*CHUNK_RECORDS_PER_PAGE)
#define INDEX_CACHE_SLOTS 4u
#define CHUNK_CACHE_SLOTS 4u
#define PAYLOAD_PAGE_BYTES 4096u
#define PAYLOAD_CACHE_SLOTS 4u
#define PAYLOAD_SMALL_LIMIT 2048u
#define PAYLOAD_CACHE_BYTES (PAYLOAD_PAGE_BYTES*PAYLOAD_CACHE_SLOTS)
#define GUIDE_LIMIT (1024u*1024u)
#define PATH_BYTES 240u
#define WORK_BYTES (INDEX_CACHE_SLOTS*(SPAN_PAGE_BYTES+CHUNK_PAGE_BYTES)+CHUNK_CACHE_SLOTS*RESOURCE_CHUNK_BYTES+RESOURCE_CHUNK_BYTES+sizeof(tinfl_decompressor))

typedef struct {
 uint32_t flags,bytes,crc,span_count,page_count,guide_offset,guide_crc,raw_offset,raw_bytes;
 unsigned char sha[32],*guides;
} ResourceSection;
struct ResourceFile {
 uint32_t magic,section,position,physical_position;
 int position_known,error;
 NativeFile *file;
};
typedef struct {uint32_t owner,page,stamp,valid;unsigned char *data;} IndexPage;
typedef struct {uint32_t id,bytes,stamp,valid;unsigned char *data;} DecodedChunk;
typedef struct {uint32_t key,bytes,stamp,valid;} PayloadPage;
static ResourceSection sections[RESOURCE_SECTION_COUNT];
static NativeFile *metadata_file;
static uint32_t metadata_position,physical_bytes,data_start,chunk_count,chunk_page_count,cache_clock;
static int metadata_position_known,in_service,visual_busy;
static unsigned char *chunk_guides,*work,*input;
static tinfl_decompressor *inflater;
static IndexPage span_pages[INDEX_CACHE_SLOTS],chunk_pages[INDEX_CACHE_SLOTS];
static DecodedChunk decoded[CHUNK_CACHE_SLOTS];
static char resource_path[PATH_BYTES];
static void (*service_callback)(void *);
static void *service_context;
static ResourceStats resource_status;
/* Optional physical read-ahead only: immutable payload bytes are shared by
 * visual streams. Directory metadata and independent RAW audio bypass it. */
static PayloadPage payload_pages[PAYLOAD_CACHE_SLOTS];
static unsigned char *payload_data;
static uint32_t payload_clock;
static int payload_cache_disabled;

static void clear_bytes(void *p,unsigned n){unsigned char *b=p;while(n--)*b++=0;}
static void copy_bytes(void *dst,const void *src,unsigned n){unsigned char*d=dst;const unsigned char*s=src;while(n--)*d++=*s++;}
static int global_fail(unsigned code){resource_status.last_error=code;++resource_status.errors;return 0;}
static int file_fail(ResourceFile *f,unsigned code){if(f)f->error=(int)code;return global_fail(code);}
static void *allocate(unsigned n){void*p=cache_memory_alloc(n);if(p)resource_status.heap_bytes+=n;return p;}
static void release(void *p,unsigned n){if(p){cache_memory_free(p);resource_status.heap_bytes-=n;}}
static int within(uint32_t at,uint32_t n,uint32_t end){return at<=end&&n<=end-at;}
static int metadata_range(uint32_t at,uint32_t n){return at>=HEADER_BYTES+RESOURCE_SECTION_COUNT*SECTION_BYTES&&within(at,n,data_start);}
static uint32_t get32(const unsigned char *p){return mapc_get32(p);}
static int physical_read(NativeFile *f,uint32_t *position,int *known,uint32_t at,void *dst,uint32_t n){
 unsigned char *out=dst;
 if(!f||!within(at,n,physical_bytes))return global_fail(RESOURCE_RANGE);
 if(!*known||*position!=at){
  ++resource_status.seeks;
  if(!native_seek(f,at)){*known=0;return global_fail(RESOURCE_IO);}
  *known=1;*position=at;
 }
 while(n){unsigned want=n>65536u?65536u:n,got;
  ++resource_status.physical_reads;got=native_read(f,out,want);
  if(!got||got>want){*known=0;return global_fail(RESOURCE_IO);}
  resource_status.physical_bytes+=got;*position+=got;out+=got;n-=got;
 }
 return 1;
}
static int metadata_read(uint32_t at,void *dst,uint32_t n){return physical_read(metadata_file,&metadata_position,&metadata_position_known,at,dst,n);}
static int visual_payload_read(ResourceFile*f,uint32_t at,void*dst,uint32_t n,unsigned canonical){
 unsigned char*out=dst;
 /* Full decoded blocks include every map plane chunk. Keep those direct:
  * paging their already-small compressed input adds seeks/read amplification. */
 if(canonical>=RESOURCE_CHUNK_BYTES||n>PAYLOAD_SMALL_LIMIT||payload_cache_disabled||
    (f->section!=RESOURCE_SCENE&&f->section!=RESOURCE_TREE))return physical_read(f->file,&f->physical_position,&f->position_known,at,dst,n);
 if(!payload_data){
  payload_data=allocate(PAYLOAD_CACHE_BYTES);
  if(!payload_data){payload_cache_disabled=1;return physical_read(f->file,&f->physical_position,&f->position_known,at,dst,n);}
 }
 if(!within(at,n,physical_bytes))return global_fail(RESOURCE_RANGE);
 while(n){
  unsigned key=at&~(PAYLOAD_PAGE_BYTES-1u),i,slot=0,age=0,relative,take;PayloadPage*p;
  for(i=0;i<PAYLOAD_CACHE_SLOTS;++i){PayloadPage*c=&payload_pages[i];unsigned elapsed=payload_clock-c->stamp;
   if(c->valid&&c->key==key){slot=i;break;}
   if(!c->valid){slot=i;age=0xffffffffu;}else if(age!=0xffffffffu&&elapsed>=age){slot=i;age=elapsed;}
  }
  p=&payload_pages[slot];
  if(i==PAYLOAD_CACHE_SLOTS){unsigned count=physical_bytes-key;if(count>PAYLOAD_PAGE_BYTES)count=PAYLOAD_PAGE_BYTES;p->valid=0;
   if(!physical_read(f->file,&f->physical_position,&f->position_known,key,payload_data+slot*PAYLOAD_PAGE_BYTES,count))return 0;
   p->key=key;p->bytes=count;p->valid=1;
  }
  p->stamp=++payload_clock;relative=at-key;take=p->bytes-relative;if(take>n)take=n;
  if(!take)return global_fail(RESOURCE_FORMAT);
  copy_bytes(out,payload_data+slot*PAYLOAD_PAGE_BYTES+relative,take);out+=take;at+=take;n-=take;
 }
 /* Hits never change physical_position: it always tracks the actual fd. */
 return 1;
}
static void clear_mount(void){
 unsigned i;
 release(payload_data,PAYLOAD_CACHE_BYTES);payload_data=0;clear_bytes(payload_pages,sizeof(payload_pages));payload_clock=0;payload_cache_disabled=0;
 if(metadata_file)(void)native_close(metadata_file);
 metadata_file=0;
 release(chunk_guides,chunk_page_count*GUIDE_BYTES);chunk_guides=0;
 for(i=0;i<RESOURCE_SECTION_COUNT;++i){release(sections[i].guides,sections[i].page_count*GUIDE_BYTES);sections[i].guides=0;}
 release(work,(unsigned)WORK_BYTES);work=input=0;inflater=0;
 clear_bytes(sections,sizeof(sections));clear_bytes(span_pages,sizeof(span_pages));clear_bytes(chunk_pages,sizeof(chunk_pages));clear_bytes(decoded,sizeof(decoded));
 resource_status.mounted=resource_status.version=resource_status.file_bytes=0;
 physical_bytes=data_start=chunk_count=chunk_page_count=metadata_position=cache_clock=0;metadata_position_known=visual_busy=in_service=0;
 resource_path[0]=0;service_callback=0;service_context=0;
}
int resource_unmount(void){if(resource_status.open_files)return global_fail(RESOURCE_BUSY);clear_mount();return 1;}
int resource_is_mounted(void){return resource_status.mounted!=0;}
const ResourceStats *resource_stats(void){return &resource_status;}
unsigned resource_heap_bytes(void){return resource_status.heap_bytes;}
void resource_set_service(void (*callback)(void *),void *context){service_callback=callback;service_context=context;}

static int validate_chunk_guides(void){
 unsigned i,expected=0;
 for(i=0;i<chunk_page_count;++i){const unsigned char *g=chunk_guides+i*GUIDE_BYTES;unsigned first=get32(g),count=get32(g+4),at=get32(g+8),bytes=get32(g+12),want=chunk_count-expected;
  if(want>CHUNK_RECORDS_PER_PAGE)want=CHUNK_RECORDS_PER_PAGE;
  if(first!=expected||count!=want||!count||bytes!=count*CHUNK_RECORD_BYTES||(at&3u)||!metadata_range(at,bytes)||get32(g+20))return global_fail(RESOURCE_FORMAT);
  expected+=count;
 }
 return expected==chunk_count?1:global_fail(RESOURCE_FORMAT);
}
static int validate_span_guides(unsigned section){
 ResourceSection *s=&sections[section];unsigned i,cursor=0,counted=0;
 for(i=0;i<s->page_count;++i){const unsigned char *g=s->guides+i*GUIDE_BYTES;unsigned begin=get32(g),end=get32(g+4),at=get32(g+8),bytes=get32(g+12),count=get32(g+16);
  if(begin!=cursor||begin>=end||end>s->bytes||!count||count>SPAN_RECORDS_PER_PAGE||bytes!=count*SPAN_RECORD_BYTES||(at&3u)||!metadata_range(at,bytes)||count>s->span_count-counted)return global_fail(RESOURCE_FORMAT);
  counted+=count;cursor=end;
 }
 return cursor==s->bytes&&counted==s->span_count?1:global_fail(RESOURCE_FORMAT);
}
int resource_mount(const char *path){
 unsigned char header[HEADER_BYTES],table[RESOURCE_SECTION_COUNT*SECTION_BYTES];uint32_t h[32];unsigned i,j,n=0,crc,guide_at,guide_bytes;int length;
 if(resource_status.open_files)return global_fail(RESOURCE_BUSY);
 clear_mount();++resource_status.mount_attempts;resource_status.last_error=RESOURCE_OK;
 if(!path)return global_fail(RESOURCE_MISSING);
 while(path[n]&&n<PATH_BYTES-1u){resource_path[n]=path[n];++n;}
 if(!n||path[n])return global_fail(RESOURCE_RANGE);
 resource_path[n]=0;
 if(!native_available())return global_fail(RESOURCE_IO);
 metadata_file=native_open(path);if(!metadata_file)return global_fail(RESOURCE_MISSING);
 length=native_size(metadata_file);
 if(length<(int)(HEADER_BYTES+RESOURCE_SECTION_COUNT*SECTION_BYTES)){global_fail(RESOURCE_FORMAT);goto bad;}
 physical_bytes=(unsigned)length;metadata_position=physical_bytes;metadata_position_known=1;
 if(!metadata_read(0,header,sizeof(header)))goto bad;
 for(i=0;i<8u;++i)if(header[i]!=(unsigned char)"DOTA2H1R"[i]){global_fail(RESOURCE_FORMAT);goto bad;}
 for(i=0;i<32u;++i)h[i]=get32(header+i*4u);
 crc=h[31];mapc_put32(header+124,0);
 if(mapc_crc32(header,sizeof(header))!=crc){global_fail(RESOURCE_CRC);goto bad;}
 if(h[2]!=1u||h[3]!=HEADER_BYTES||h[4]!=physical_bytes||h[5]!=RESOURCE_SECTION_COUNT||h[6]!=128u||h[7]!=SECTION_BYTES||!h[8]||h[8]>1048576u||h[9]!=(h[8]+CHUNK_RECORDS_PER_PAGE-1u)/CHUNK_RECORDS_PER_PAGE||h[11]!=GUIDE_BYTES||h[12]!=CHUNK_RECORD_BYTES||h[13]!=CHUNK_RECORDS_PER_PAGE||h[14]!=RESOURCE_CHUNK_BYTES||h[15]||h[16]<512u||h[16]>=physical_bytes||(h[16]&4095u)){global_fail(RESOURCE_FORMAT);goto bad;}
 for(i=19;i<31u;++i)if(h[i]){global_fail(RESOURCE_FORMAT);goto bad;}
 data_start=h[16];chunk_count=h[8];chunk_page_count=h[9];guide_at=h[10];guide_bytes=chunk_page_count*GUIDE_BYTES;
 if((guide_at&3u)||!metadata_range(guide_at,guide_bytes)||guide_bytes>GUIDE_LIMIT){global_fail(RESOURCE_FORMAT);goto bad;}
 if(!metadata_read(128u,table,sizeof(table)))goto bad;
 if(mapc_crc32(table,sizeof(table))!=h[17]){global_fail(RESOURCE_CRC);goto bad;}
 for(i=0;i<RESOURCE_SECTION_COUNT;++i){const unsigned char *p=table+i*SECTION_BYTES;ResourceSection*s=&sections[i];
  if(get32(p)!=i){global_fail(RESOURCE_FORMAT);goto bad;}
  s->flags=get32(p+4);s->bytes=get32(p+8);s->crc=get32(p+12);copy_bytes(s->sha,p+16,32);
  s->span_count=get32(p+48);s->page_count=get32(p+52);s->guide_offset=get32(p+56);s->guide_crc=get32(p+60);s->raw_offset=get32(p+64);s->raw_bytes=get32(p+68);
  if(!s->bytes||s->bytes>0x7fffffffu){global_fail(RESOURCE_FORMAT);goto bad;}
  for(j=18;j<24u;++j)if(get32(p+j*4u)){global_fail(RESOURCE_FORMAT);goto bad;}
  if(i==RESOURCE_SOUND){
   if(s->flags!=1u||s->span_count||s->page_count||s->guide_offset||s->guide_crc||s->raw_offset!=data_start||(s->raw_offset&4095u)||s->raw_bytes!=s->bytes||!within(s->raw_offset,s->raw_bytes,physical_bytes)){global_fail(RESOURCE_FORMAT);goto bad;}
  }else{
   if(s->flags||!s->span_count||s->span_count>1048576u||!s->page_count||s->page_count>s->span_count||s->page_count>(GUIDE_LIMIT/GUIDE_BYTES)||s->raw_offset||s->raw_bytes||(s->guide_offset&3u)||!metadata_range(s->guide_offset,s->page_count*GUIDE_BYTES)){global_fail(RESOURCE_FORMAT);goto bad;}
  }
 }
 chunk_guides=allocate(guide_bytes);if(!chunk_guides){global_fail(RESOURCE_MEMORY);goto bad;}
 if(!metadata_read(guide_at,chunk_guides,guide_bytes))goto bad;
 if(mapc_crc32(chunk_guides,guide_bytes)!=h[18]){global_fail(RESOURCE_CRC);goto bad;}
 if(!validate_chunk_guides())goto bad;
 for(i=0;i<RESOURCE_SECTION_COUNT;++i){ResourceSection*s=&sections[i];if(s->flags)continue;
  guide_bytes=s->page_count*GUIDE_BYTES;s->guides=allocate(guide_bytes);if(!s->guides){global_fail(RESOURCE_MEMORY);goto bad;}
  if(!metadata_read(s->guide_offset,s->guides,guide_bytes))goto bad;
  if(mapc_crc32(s->guides,guide_bytes)!=s->guide_crc){global_fail(RESOURCE_CRC);goto bad;}
  if(!validate_span_guides(i))goto bad;
 }
 work=allocate((unsigned)WORK_BYTES);if(!work){global_fail(RESOURCE_MEMORY);goto bad;}
 n=0;for(i=0;i<INDEX_CACHE_SLOTS;++i){span_pages[i].data=work+n;n+=SPAN_PAGE_BYTES;}
 for(i=0;i<INDEX_CACHE_SLOTS;++i){chunk_pages[i].data=work+n;n+=CHUNK_PAGE_BYTES;}
 for(i=0;i<CHUNK_CACHE_SLOTS;++i){decoded[i].data=work+n;n+=RESOURCE_CHUNK_BYTES;}
 input=work+n;n+=RESOURCE_CHUNK_BYTES;inflater=(tinfl_decompressor *)(void *)(work+n);
 resource_status.mounted=1;resource_status.version=1;resource_status.file_bytes=physical_bytes;resource_status.last_error=RESOURCE_OK;return 1;
bad:clear_mount();return 0;
}
int resource_mount_default(void){
 if(resource_is_mounted())return 1;
 if(resource_mount("B:\\DOTA2\\DOTA2.H1R"))return 1;
 return resource_status.last_error==RESOURCE_MISSING?resource_mount("A:\\DOTA2\\DOTA2.H1R"):0;
}

static unsigned char *get_index_page(unsigned kind,unsigned owner,unsigned page){
 IndexPage *slots=kind?chunk_pages:span_pages,*chosen=&slots[0];const unsigned char *g;unsigned i,bytes,at,crc,age=0;
 for(i=0;i<INDEX_CACHE_SLOTS;++i){IndexPage *s=&slots[i];unsigned elapsed=cache_clock-s->stamp;
  if(s->valid&&s->owner==owner&&s->page==page){s->stamp=++cache_clock;++resource_status.index_hits;return s->data;}
  if(!s->valid){chosen=s;age=0xffffffffu;}else if(age!=0xffffffffu&&elapsed>=age){chosen=s;age=elapsed;}
 }
 ++resource_status.index_misses;chosen->valid=0;
 g=kind?chunk_guides+page*GUIDE_BYTES:sections[owner].guides+page*GUIDE_BYTES;
 at=get32(g+8);bytes=get32(g+12);crc=get32(g+(kind?16u:20u));
 if(!metadata_read(at,chosen->data,bytes))return 0;
 if(mapc_crc32(chosen->data,bytes)!=crc){global_fail(RESOURCE_CRC);return 0;}
 if(kind){unsigned count=get32(g+4);for(i=0;i<count;++i){const unsigned char *r=chosen->data+i*CHUNK_RECORD_BYTES;unsigned p=get32(r),stored=get32(r+4),raw=get32(r+8),codec=get32(r+16);
   if((p&3u)||p<data_start||!stored||stored>raw||raw>RESOURCE_CHUNK_BYTES||codec>2u||!within(p,stored,physical_bytes)||(codec==0u?stored!=raw:stored>=raw)||(codec==2u&&(raw&1u))){global_fail(RESOURCE_FORMAT);return 0;}
  }}else{unsigned count=get32(g+16),cursor=get32(g),end=get32(g+4);for(i=0;i<count;++i){const unsigned char*r=chosen->data+i*SPAN_RECORD_BYTES;unsigned begin=get32(r),raw=get32(r+4),id=get32(r+8);
   if(begin!=cursor||!raw||raw>RESOURCE_CHUNK_BYTES||raw>end-cursor||id>=chunk_count){global_fail(RESOURCE_FORMAT);return 0;}cursor+=raw;
  }if(cursor!=end){global_fail(RESOURCE_FORMAT);return 0;}}
 chosen->owner=owner;chosen->page=page;chosen->stamp=++cache_clock;chosen->valid=1;return chosen->data;
}
static const unsigned char *get_span(unsigned section,uint32_t at){
 ResourceSection*s=&sections[section];unsigned lo=0,hi=s->page_count,page,count;const unsigned char*g;unsigned char*data;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2u;if(get32(s->guides+mid*GUIDE_BYTES)<=at)lo=mid+1u;else hi=mid;}
 if(!lo){global_fail(RESOURCE_FORMAT);return 0;}page=lo-1u;g=s->guides+page*GUIDE_BYTES;
 if(at>=get32(g+4)){global_fail(RESOURCE_FORMAT);return 0;}
 data=get_index_page(0,section,page);if(!data)return 0;lo=0;hi=count=get32(g+16);
 while(lo<hi){unsigned mid=lo+(hi-lo)/2u;if(get32(data+mid*SPAN_RECORD_BYTES)<=at)lo=mid+1u;else hi=mid;}
 if(!lo||lo>count){global_fail(RESOURCE_FORMAT);return 0;}
 data+=(lo-1u)*SPAN_RECORD_BYTES;
 if(at-get32(data)>=get32(data+4)){global_fail(RESOURCE_FORMAT);return 0;}
 return data;
}
static const unsigned char *get_chunk_record(unsigned id){
 unsigned page=id/CHUNK_RECORDS_PER_PAGE;unsigned char *data;
 if(id>=chunk_count||page>=chunk_page_count){global_fail(RESOURCE_FORMAT);return 0;}
 data=get_index_page(1,0,page);return data?data+(id%CHUNK_RECORDS_PER_PAGE)*CHUNK_RECORD_BYTES:0;
}
static void inverse16(unsigned char *data,unsigned raw){
 unsigned i,count=raw/2u,previous=0;
 for(i=0;i<count;++i){unsigned z=data[i]|((unsigned)data[count+i]<<8),d=((z>>1)^(0u-(z&1u)))&65535u;previous=(previous+d)&65535u;input[i*2u]=(unsigned char)previous;input[i*2u+1]=(unsigned char)(previous>>8);}
 copy_bytes(data,input,raw);
}
static void service(void){
 if(service_callback&&!in_service){in_service=1;++resource_status.service_calls;service_callback(service_context);in_service=0;}
}
static const unsigned char *get_chunk(ResourceFile *f,unsigned id,unsigned *raw_out){
 unsigned i,age=0,raw,stored,codec,at,crc;DecodedChunk*chosen=&decoded[0];const unsigned char*r;
 for(i=0;i<CHUNK_CACHE_SLOTS;++i){DecodedChunk*s=&decoded[i];unsigned elapsed=cache_clock-s->stamp;
  if(s->valid&&s->id==id){s->stamp=++cache_clock;++resource_status.chunk_hits;*raw_out=s->bytes;return s->data;}
  if(!s->valid){chosen=s;age=0xffffffffu;}else if(age!=0xffffffffu&&elapsed>=age){chosen=s;age=elapsed;}
 }
 ++resource_status.chunk_misses;chosen->valid=0;r=get_chunk_record(id);if(!r)return 0;
 at=get32(r);stored=get32(r+4);raw=get32(r+8);crc=get32(r+12);codec=get32(r+16);
 if(codec){size_t source_bytes=stored,output_bytes=raw;tinfl_status status;unsigned physical_stored=(stored+3u)&~3u;
  /* Consume the existing alignment padding so physically consecutive chunks
   * keep their cursor. v1 also permits an unpadded final chunk; the decoder
   * always sees exactly stored bytes, never padding or trailing file data. */
  if(!within(at,physical_stored,physical_bytes))physical_stored=stored;
  if(!visual_payload_read(f,at,input,physical_stored,raw))return 0;
  tinfl_init(inflater);status=tinfl_decompress(inflater,input,&source_bytes,chosen->data,chosen->data,&output_bytes,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if(status!=TINFL_STATUS_DONE||source_bytes!=stored||output_bytes!=raw){global_fail(RESOURCE_DECODE);return 0;}
  if(codec==2u)inverse16(chosen->data,raw);
  ++resource_status.decoded_chunks;resource_status.decoded_bytes+=raw;
 }else if(!visual_payload_read(f,at,chosen->data,raw,raw))return 0;
 if(mapc_crc32(chosen->data,raw)!=crc){global_fail(RESOURCE_CRC);return 0;}
 chosen->id=id;chosen->bytes=raw;chosen->stamp=++cache_clock;chosen->valid=1;*raw_out=raw;service();return chosen->data;
}
ResourceFile *resource_open(unsigned section){
 ResourceFile*f;int n;++resource_status.open_attempts;
 if(section>=RESOURCE_SECTION_COUNT){global_fail(RESOURCE_SECTION);return 0;}
 if(!resource_is_mounted()&&!resource_mount_default())return 0;
 f=allocate(sizeof(*f));if(!f){global_fail(RESOURCE_MEMORY);return 0;}clear_bytes(f,sizeof(*f));
 f->file=native_open(resource_path);if(!f->file){release(f,sizeof(*f));global_fail(RESOURCE_IO);return 0;}
 n=native_size(f->file);if(n!=(int)physical_bytes){(void)native_close(f->file);release(f,sizeof(*f));global_fail(RESOURCE_FORMAT);return 0;}
 f->physical_position=physical_bytes;f->position_known=1;f->section=section;f->magic=RESOURCE_FILE_MAGIC;++resource_status.open_files;return f;
}
uint32_t resource_size(const ResourceFile *f){return f&&f->magic==RESOURCE_FILE_MAGIC?sections[f->section].bytes:0;}
int resource_file_error(const ResourceFile *f){return f&&f->magic==RESOURCE_FILE_MAGIC?f->error:(int)resource_status.last_error;}
int resource_read_at(ResourceFile *f,uint32_t at,void *dst,uint32_t n){
 ResourceSection*s;unsigned char*out=dst;unsigned requested=n;
 ++resource_status.read_requests;
 if(!f||f->magic!=RESOURCE_FILE_MAGIC||!resource_is_mounted())return file_fail(f,RESOURCE_NOT_MOUNTED);
 s=&sections[f->section];
 if(!within(at,n,s->bytes)||(n&&!dst))return file_fail(f,RESOURCE_RANGE);
 if(!n){f->error=RESOURCE_OK;return 1;}
 if(s->flags){
  if(!physical_read(f->file,&f->physical_position,&f->position_known,s->raw_offset+at,out,n)){f->error=(int)resource_status.last_error;return 0;}
 }else{
  if(visual_busy||in_service)return file_fail(f,RESOURCE_BUSY);
  visual_busy=1;
  while(n){const unsigned char*span=get_span(f->section,at),*data;unsigned begin,raw,id,chunk_raw,take;
   if(!span)goto bad;
   begin=get32(span);raw=get32(span+4);id=get32(span+8);take=raw-(at-begin);if(take>n)take=n;
   data=get_chunk(f,id,&chunk_raw);if(!data)goto bad;
   if(chunk_raw!=raw){global_fail(RESOURCE_FORMAT);goto bad;}
   copy_bytes(out,data+at-begin,take);out+=take;at+=take;n-=take;
  }
  visual_busy=0;
 }
 resource_status.bytes_returned+=requested;f->error=RESOURCE_OK;return 1;
bad:visual_busy=0;f->error=(int)resource_status.last_error;return 0;
}
uint32_t resource_read(ResourceFile *f,void *dst,uint32_t n){
 uint32_t size=resource_size(f);
 if(!f||f->magic!=RESOURCE_FILE_MAGIC){file_fail(f,RESOURCE_NOT_MOUNTED);return 0;}
 if(f->position>size){file_fail(f,RESOURCE_RANGE);return 0;}
 if(n>size-f->position)n=size-f->position;
 if(!n)return 0;
 if(!resource_read_at(f,f->position,dst,n))return 0;
 f->position+=n;return n;
}
int resource_seek(ResourceFile *f,int offset,int whence){
 int64_t base,result;
 if(!f||f->magic!=RESOURCE_FILE_MAGIC){file_fail(f,RESOURCE_NOT_MOUNTED);return -1;}
 if(whence==0)base=0;else if(whence==1)base=f->position;else if(whence==2)base=resource_size(f);else{file_fail(f,RESOURCE_RANGE);return -1;}
 result=base+(int64_t)offset;
 if(result<0||result>resource_size(f)){file_fail(f,RESOURCE_RANGE);return -1;}
 f->position=(uint32_t)result;f->error=RESOURCE_OK;return (int)result;
}
int resource_close(ResourceFile *f){
 int result;if(!f)return 0;
 if(f->magic!=RESOURCE_FILE_MAGIC){global_fail(RESOURCE_NOT_MOUNTED);return -1;}
 result=native_close(f->file);f->magic=0;--resource_status.open_files;release(f,sizeof(*f));
 if(result){global_fail(RESOURCE_IO);return -1;}return 0;
}
