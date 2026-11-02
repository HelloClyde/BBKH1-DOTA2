#include "vision_assets.h"
#include "vision_asset_identity.h"
#include "cache_memory.h"
#include "map_sidecar_format.h"
#include "sight.h"
#include <stddef.h>
#ifdef DOTA_RESOURCE_PACK
#include "resource_pack.h"
static ResourceFile *vision_resource;
#endif
#ifdef BAKED_IO_HOST
#include <stdio.h>
typedef FILE VisionFile;
static VisionFile *vopen(const char *path){return fopen(path,"rb");}
static int vclose(VisionFile *file){return fclose(file);}
static int vsize(VisionFile *file){long bytes;if(fseek(file,0,SEEK_END))return -1;bytes=ftell(file);return bytes<0||bytes>0x7fffffffL?-1:(int)bytes;}
static int vrewind(VisionFile *file){return fseek(file,0,SEEK_SET)==0;}
static unsigned vread(VisionFile *file,void *out,unsigned bytes){return (unsigned)fread(out,1,bytes,file);}
#else
#include "h1_filesystem.h"
typedef h1_file VisionFile;
static VisionFile *vopen(const char *path){
    void *table=h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT);
    return table&&h1_runtime_entry(table,H1_FS_FOPEN_OFFSET)&&h1_runtime_entry(table,H1_FS_FCLOSE_OFFSET)&&h1_runtime_entry(table,H1_FS_FREAD_OFFSET)&&h1_runtime_entry(table,H1_FS_FSEEK_OFFSET)?h1_fopen(path,"rb"):0;
}
static int vclose(VisionFile *file){return h1_fclose(file);}
static int vsize(VisionFile *file){return h1_fseek(file,0,2);}
static int vrewind(VisionFile *file){return h1_fseek(file,0,0)==0;}
static unsigned vread(VisionFile *file,void *out,unsigned bytes){return h1_fread(out,1,bytes,file);}
#endif
#define VISION_HEADER_BYTES 64u
#define VISION_HEIGHT_BYTES 131072u
#define VISION_PAYLOAD_BYTES 139264u
enum { VISION_OK=0,VISION_MISSING=1,VISION_HEADER=2,VISION_CRC=3,VISION_MEMORY=4,VISION_IO=5,VISION_ATTACH=6 };
static void *vision_memory;
static DotaVisionAssetStats vision_stats;
const DotaVisionAssetStats *vision_assets_stats(void){return &vision_stats;}
void vision_assets_close(void){
#ifdef DOTA_RESOURCE_PACK
    if(vision_resource){(void)resource_close(vision_resource);vision_resource=0;++vision_stats.closes;}
#endif
    dota_sight_detach();cache_memory_free(vision_memory);vision_memory=0;
    vision_stats.ready=0;vision_stats.heap_bytes=0;vision_stats.released=1;
}
static int vision_fail(unsigned reason){
    vision_stats.last_reason=reason;
    if(reason==VISION_HEADER||reason==VISION_CRC)++vision_stats.validation_errors;
    if(reason==VISION_MEMORY)++vision_stats.allocation_failures;
    if(reason==VISION_IO)++vision_stats.io_errors;
    if(reason==VISION_ATTACH)++vision_stats.attach_failures;
    vision_assets_close();return 0;
}
static int vision_read(VisionFile *file,unsigned char *out,unsigned bytes){
    while(bytes){unsigned want=bytes>65536u?65536u:bytes,got;++vision_stats.read_requests;
#ifdef DOTA_RESOURCE_PACK
        if(vision_resource)got=resource_read(vision_resource,out,want);else
#endif
        got=vread(file,out,want);
        if(!got||got>want)return 0;
        vision_stats.bytes_read+=got;out+=got;bytes-=got;
    }return 1;
}
int vision_assets_open(const char *path){
    static const uint32_t fields[16]={0x31563148u,1u,64u,4096u,256u,16u,64u,131072u,131136u,8192u,139264u,VISION_EXPECTED_PAYLOAD_CRC32,4u,VISION_EXPECTED_TREE_RECORDS,VISION_EXPECTED_UNKNOWN_HEIGHT_CELLS,0u};
    VisionFile *file;unsigned char header[VISION_HEADER_BYTES],*payload;unsigned i,workspace,reason=VISION_OK,unknown=0;int bytes;
    vision_assets_close();++vision_stats.open_attempts;vision_stats.last_reason=VISION_OK;vision_stats.payload_crc32=0;vision_stats.released=0;
    file=path&&*path?vopen(path):0;
#ifdef DOTA_RESOURCE_PACK
    if(!path||!*path){vision_resource=resource_open(RESOURCE_VISION);if(!vision_resource)return vision_fail(VISION_MISSING);bytes=(int)resource_size(vision_resource);}
    else
#endif
    {if(!file)return vision_fail(VISION_MISSING);bytes=vsize(file);}
    if(bytes!=(int)(VISION_HEADER_BYTES+VISION_PAYLOAD_BYTES)){reason=bytes<0?VISION_IO:VISION_HEADER;goto done;}
#ifdef DOTA_RESOURCE_PACK
    if(vision_resource){if(resource_seek(vision_resource,0,0)!=0){reason=VISION_IO;goto done;}}
    else
#endif
    if(!vrewind(file)){reason=VISION_IO;goto done;}
    if(!vision_read(file,header,sizeof(header))){reason=VISION_IO;goto done;}
    for(i=0;i<16u;++i)if(mapc_get32(header+i*4u)!=fields[i]){reason=VISION_HEADER;goto done;}
    workspace=dota_sight_workspace_bytes();
    if(!workspace||workspace>2u*1024u*1024u){reason=VISION_MEMORY;goto done;}
    vision_memory=cache_memory_alloc(VISION_PAYLOAD_BYTES+workspace);
    if(!vision_memory||((uintptr_t)vision_memory&3u)){reason=VISION_MEMORY;goto done;}
    vision_stats.heap_bytes=VISION_PAYLOAD_BYTES+workspace;vision_stats.workspace_bytes=workspace;vision_stats.payload_bytes=VISION_PAYLOAD_BYTES;
    payload=(unsigned char *)vision_memory;
    if(!vision_read(file,payload,VISION_PAYLOAD_BYTES)){reason=VISION_IO;goto done;}
    vision_stats.payload_crc32=mapc_crc32(payload,VISION_PAYLOAD_BYTES);
    if(vision_stats.payload_crc32!=VISION_EXPECTED_PAYLOAD_CRC32){reason=VISION_CRC;goto done;}
    /* Decode from explicit little-endian bytes even for a host test. The
     * production MIPS is little-endian, so each value is already identical. */
    for(i=0;i<VISION_HEIGHT_BYTES/2u;++i){int16_t value=(int16_t)((unsigned)payload[i*2u]|((unsigned)payload[i*2u+1u]<<8));((int16_t *)payload)[i]=value;if(value==DOTA_SIGHT_HEIGHT_UNKNOWN)++unknown;}
    vision_stats.unknown_height_cells=unknown;
    if(unknown!=VISION_EXPECTED_UNKNOWN_HEIGHT_CELLS){reason=VISION_HEADER;goto done;}
    if(!dota_sight_attach((const int16_t *)payload,payload+VISION_HEIGHT_BYTES,payload+VISION_PAYLOAD_BYTES,workspace)){reason=VISION_ATTACH;goto done;}
done:
    ++vision_stats.closes;
#ifdef DOTA_RESOURCE_PACK
    if(vision_resource){if(resource_close(vision_resource)!=0)reason=VISION_IO;vision_resource=0;}
    else
#endif
    if(vclose(file)!=0)reason=VISION_IO;
    if(reason!=VISION_OK)return vision_fail(reason);
    vision_stats.ready=1;vision_stats.released=0;return 1;
}
int vision_assets_open_default(void){
#ifdef DOTA_RESOURCE_PACK
    return vision_assets_open(0);
#else
    return vision_assets_open("B:\\DOTA2\\VISION.H1V")||vision_assets_open("A:\\DOTA2\\VISION.H1V");
#endif
}
