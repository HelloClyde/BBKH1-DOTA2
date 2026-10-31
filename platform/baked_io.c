#include "baked_io.h"
#include "cache_memory.h"
#include <stddef.h>

#ifdef BAKED_IO_HOST
#include <stdio.h>
typedef FILE BakedFile;
static BakedFile *file_open(const char *path){return fopen(path,"rb");}
static int file_close(BakedFile *file){return fclose(file);}
static int file_seek(BakedFile *file,int offset,int whence){
    long result;
    if(fseek(file,(long)offset,whence)!=0)return -1;
    result=ftell(file);
    return result<0 || result>0x7fffffffL?-1:(int)result;
}
static uint32_t file_read(void *dst,uint32_t bytes,BakedFile *file){
    return (uint32_t)fread(dst,1,bytes,file);
}
static int files_available(void){return 1;}
#else
#include "h1_filesystem.h"
typedef h1_file BakedFile;
static BakedFile *file_open(const char *path){return h1_fopen(path,"rb");}
static int file_close(BakedFile *file){return h1_fclose(file);}
static int file_seek(BakedFile *file,int offset,int whence){return h1_fseek(file,offset,whence);}
static uint32_t file_read(void *dst,uint32_t bytes,BakedFile *file){return h1_fread(dst,1,bytes,file);}
static int files_available(void){
    void *table=h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT);
    return table && h1_runtime_entry(table,H1_FS_FOPEN_OFFSET) &&
        h1_runtime_entry(table,H1_FS_FCLOSE_OFFSET) &&
        h1_runtime_entry(table,H1_FS_FREAD_OFFSET) &&
        h1_runtime_entry(table,H1_FS_FSEEK_OFFSET);
}
#endif

#define IO_PAGE_BYTES 4096u
#define IO_METADATA_MAX 1024u
#define IO_READ_CHUNK 65536u

static BakedFile *pack_file;
static uint32_t pack_bytes,file_position,page_offset,page_bytes;
static int position_known,last_error;
static unsigned char page[IO_PAGE_BYTES];
typedef struct {uint32_t offset,bytes,stamp;} IOPage;
static void *pages_allocation;
static unsigned char *pages_data;
static IOPage *pages_index;
static unsigned pages_count;
static uint32_t pages_clock;
static BakedIOStats io_stats;
#include "baked_aux_io.h"

static int fail(int code){last_error=code;++io_stats.errors;return 0;}

void baked_io_reset_stats(void){
    aux_reset_stats();
    unsigned char *p=(unsigned char *)&io_stats;unsigned i;
    for(i=0;i<sizeof(io_stats);++i)p[i]=0;
}

void baked_io_close(void){
    BakedFile *old=pack_file;
    baked_io_aux_close();
    pack_file=0;pack_bytes=0;position_known=0;page_bytes=0;
    cache_memory_free(pages_allocation);pages_allocation=0;
    pages_data=0;pages_index=0;pages_count=0;pages_clock=0;
    if(old && file_close(old)!=0)fail(BAKED_IO_CLOSE_FAILED);
}

int baked_io_open(const char *path){
    int length;
    baked_io_close();last_error=BAKED_IO_OK;++io_stats.open_attempts;
    if(!path || !*path)return fail(BAKED_IO_OPEN_FAILED);
    if(!files_available())return fail(BAKED_IO_UNAVAILABLE);
    pack_file=file_open(path);
    if(!pack_file)return fail(BAKED_IO_OPEN_FAILED);
    ++io_stats.seeks;length=file_seek(pack_file,0,2);
    if(length<0){baked_io_close();return fail(BAKED_IO_SIZE_FAILED);}
    pack_bytes=(uint32_t)length;file_position=pack_bytes;position_known=1;
    return 1;
}

int baked_io_open_default(void){
    return baked_io_open(BAKED_IO_PATH_A) || baked_io_open(BAKED_IO_PATH_B);
}

uint32_t baked_io_size(void){return pack_bytes;}
int baked_io_is_open(void){return pack_file!=0;}
int baked_io_error(void){return last_error;}
const BakedIOStats *baked_io_stats(void){return &io_stats;}

unsigned baked_io_cache_bytes(void){return pages_count?pages_count*IO_PAGE_BYTES:IO_PAGE_BYTES;}
unsigned baked_io_dynamic_storage_bytes(void){return pages_count*(IO_PAGE_BYTES+sizeof(IOPage));}
unsigned baked_io_reserve_cache(unsigned bytes){
    unsigned count=4u,i;unsigned char *allocation=0;
    if(bytes>=4u*IO_PAGE_BYTES){
        while(count<1024u && count*2u<=bytes/IO_PAGE_BYTES)count*=2u;
        allocation=cache_memory_alloc(count*(IO_PAGE_BYTES+sizeof(IOPage)));
    }
    cache_memory_free(pages_allocation);pages_allocation=allocation;
    pages_data=allocation;pages_count=allocation?count:0;page_bytes=0;pages_clock=0;
    pages_index=allocation?(IOPage *)(allocation+count*IO_PAGE_BYTES):0;
    for(i=0;i<pages_count;++i){pages_index[i].bytes=0;pages_index[i].stamp=0;pages_index[i].offset=0;}
    return baked_io_cache_bytes();
}

static int raw_read(uint32_t offset,unsigned char *dst,uint32_t bytes){
    if(!position_known || file_position!=offset){
        ++io_stats.seeks;
        /* The H1 API returns the resulting absolute position, unlike stdio. */
        if(file_seek(pack_file,(int)offset,0)!=(int)offset){
            position_known=0;return fail(BAKED_IO_SEEK_FAILED);
        }
        file_position=offset;position_known=1;
    }
    while(bytes){
        uint32_t want=bytes>IO_READ_CHUNK?IO_READ_CHUNK:bytes,got;
        ++io_stats.raw_reads;got=file_read(dst,want,pack_file);
        if(!got || got>want){position_known=0;return fail(BAKED_IO_READ_FAILED);}
        io_stats.bytes_read+=got;file_position+=got;dst+=got;bytes-=got;
    }
    return 1;
}

int baked_io_read_at(uint32_t offset,void *dst,uint32_t bytes){
    unsigned char *out=(unsigned char *)dst;
    ++io_stats.read_requests;
    if(!pack_file)return fail(BAKED_IO_NOT_OPEN);
    if(offset>pack_bytes || bytes>pack_bytes-offset || (bytes && !dst))return fail(BAKED_IO_RANGE);
    if(!bytes){last_error=BAKED_IO_OK;return 1;}
    if(bytes>IO_METADATA_MAX){
        if(!raw_read(offset,out,bytes))return 0;
    }else{
        while(bytes){
            uint32_t available,copy,i,cached_offset,cached_bytes;unsigned char *cached;
            if(pages_count){
                unsigned first=((offset/IO_PAGE_BYTES)&(pages_count/4u-1u))*4u,slot=first;
                uint32_t key=offset&~(IO_PAGE_BYTES-1u),oldest=0xffffffffu;
                for(i=first;i<first+4u;++i){
                    IOPage *entry=&pages_index[i];
                    if(entry->bytes && entry->offset==key){slot=i;break;}
                    if(!entry->bytes){slot=i;oldest=0;}
                    else if(oldest && entry->stamp<oldest){slot=i;oldest=entry->stamp;}
                }
                cached=pages_data+slot*IO_PAGE_BYTES;
                if(i<first+4u){++io_stats.cache_hits;}
                else{
                    IOPage *entry=&pages_index[slot];entry->bytes=0;
                    available=pack_bytes-key;if(available>IO_PAGE_BYTES)available=IO_PAGE_BYTES;
                    if(!raw_read(key,cached,available))return 0;
                    entry->offset=key;entry->bytes=available;
                }
                pages_index[slot].stamp=++pages_clock;
                cached_offset=pages_index[slot].offset;cached_bytes=pages_index[slot].bytes;
            }else{
                if(page_bytes && offset>=page_offset && offset-page_offset<page_bytes){++io_stats.cache_hits;}
                else{
                    page_offset=offset&~(IO_PAGE_BYTES-1u);page_bytes=0;
                    available=pack_bytes-page_offset;
                    if(available>IO_PAGE_BYTES)available=IO_PAGE_BYTES;
                    if(!raw_read(page_offset,page,available))return 0;
                    page_bytes=available;
                }
                cached=page;cached_offset=page_offset;cached_bytes=page_bytes;
            }
            available=cached_bytes-(offset-cached_offset);copy=bytes<available?bytes:available;
            for(i=0;i<copy;++i)out[i]=cached[offset-cached_offset+i];
            offset+=copy;out+=copy;bytes-=copy;
        }
    }
    last_error=BAKED_IO_OK;return 1;
}
