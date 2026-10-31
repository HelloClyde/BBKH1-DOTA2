#ifndef DOTA_ABILITY_ICON_OVERLAY_H
#define DOTA_ABILITY_ICON_OVERLAY_H
#include "ability_icon_mask.h"

/* Icons are repainted before this call, so expired states leave no residue.
 * Blue tint precedes the radial darkening; badges and numbers are drawn last. */
static void ability_icon_overlay(int x,int y,int size,const DotaAbilityIconState *s){
    int xx,yy,remaining=0,total=s->total_cooldown;
    if(s->rank && (s->passive || (!s->missing_mana && !s->cooldown)))return;
    if(s->rank && s->cooldown>0){
        if(total<s->cooldown)total=s->cooldown;
        remaining=(s->cooldown*256+total-1)/total;
    }
    for(yy=0;yy<size;++yy)for(xx=0;xx<size;++xx){
        int sx,sy,at;uint16_t c;
        if((unsigned)(x+xx)>=480u || y+yy<clip_top || y+yy>clip_bottom)continue;
        at=(y+yy)*480+x+xx;c=dota_pixels[at];
        if(!s->rank){
            int l=(((c>>11)*2+((c>>5)&63)+(c&31)*2)/6)/2;
            c=(uint16_t)((l<<11)|(l*2<<5)|l);
        }else{
            if(s->missing_mana){
                int r=(c>>11)/2,g=((c>>5)&63)/2+8,b=((c&31)+31)/2;
                c=(uint16_t)((r<<11)|(g<<5)|b);
            }
            sx=((xx*2+1)*24)/(size*2);sy=((yy*2+1)*24)/(size*2);
            if(remaining && ability_icon_angles[sy*24+sx]>=256-remaining)c=(uint16_t)((c>>1)&0x7bef);
        }
        dota_pixels[at]=c;
    }
}
#endif
