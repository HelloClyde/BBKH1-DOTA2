#ifndef H1_DOTA_VISION_ASSETS_H
#define H1_DOTA_VISION_ASSETS_H
#include <stdint.h>
typedef struct {
    uint32_t open_attempts,ready,heap_bytes,read_requests,bytes_read,closes;
    uint32_t validation_errors,io_errors,allocation_failures,last_reason;
    uint32_t workspace_bytes,payload_bytes,payload_crc32,unknown_height_cells;
    uint32_t attach_failures,released;
} DotaVisionAssetStats;
int vision_assets_open(const char *path);
int vision_assets_open_default(void);
void vision_assets_close(void);
const DotaVisionAssetStats *vision_assets_stats(void);
/* Eight equal RGB565 light steps, without a per-color table. All terrain is
 * known in Dota: both unexplored and explored-but-unseen retain its outline. */
static inline uint16_t vision_assets_fog_color(uint16_t color,unsigned light){
    unsigned a=(color>>1)&0x7befu,b=(color>>2)&0x39e7u,c=(color>>3)&0x18e3u;
    return light>=8u?color:(uint16_t)(light==7u?a+b+c:light==6u?a+b:light==5u?a+c:light==4u?a:b+c);
}
#endif
