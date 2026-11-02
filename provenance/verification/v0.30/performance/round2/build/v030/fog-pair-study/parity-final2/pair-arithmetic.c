#include <stdint.h>
#include <stdio.h>
#include "vision_assets.h"
typedef uint32_t FogCopyWord;
static inline FogCopyWord fog_hidden_pair(FogCopyWord color){
    /* The two RGB565 pixels stay independent: each half-word sum is below
     * 65536, and both masks remove bits that could cross channel boundaries.
     * This is exactly two scalar light=3 samples, using one aligned word. */
    return ((color>>2)&0x39e739e7u)+((color>>3)&0x18e318e3u);
}
static unsigned checks;
static int check(uint32_t value){uint32_t expected=(uint32_t)vision_assets_fog_color((uint16_t)value,3)|((uint32_t)vision_assets_fog_color((uint16_t)(value>>16),3)<<16);++checks;if(fog_hidden_pair(value)!=expected){printf("FAIL %08x\n",value);return 0;}return 1;}
int main(void){static const uint16_t edges[]={0,65535,31,2016,63488,0x39e7,0x18e3,0x7bef};uint32_t color,i,rng=0x927a3f19;
for(color=0;color<65536u;++color)for(i=0;i<sizeof(edges)/sizeof(edges[0]);++i){if(!check(color|((uint32_t)edges[i]<<16))||!check(((uint32_t)color<<16)|edges[i]))return 1;}
for(i=0;i<2097152u;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;if(!check(rng))return 1;}
printf("{\"passed\":true,\"checks\":%u,\"all_65536_colors_in_both_lanes\":true,\"edge_neighbors\":8,\"random_pairs\":2097152}\n",checks);return 0;}
