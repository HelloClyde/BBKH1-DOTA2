#ifndef H1_DOTA_VISION_H
#define H1_DOTA_VISION_H
#include <stdint.h>

/* Explicit H1 pit-only visibility. No global fog-of-war simulation. */
int dota_roshan_region_at(int x,int y); /* 0outside,1Top,2Bottom */
int dota_pit_same_region(int x1,int y1,int x2,int y2);
int dota_roshan_region_pixel(int global_px,int global_py);
/* Apply exactly the generated per-pixel pit mask to this viewport. Pixels
 * belonging to the observer's region retain their original colour. */
void dota_roshan_mask_pixels(uint16_t *pixels,int stride,int width,int height,
                            int global_x,int global_y,int region,uint16_t hidden);

#endif
