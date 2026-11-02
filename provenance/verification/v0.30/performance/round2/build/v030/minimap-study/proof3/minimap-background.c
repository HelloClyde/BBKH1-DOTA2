
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "render.h"
#include "sight.h"
#include "hud_layout.h"
#include "vision_assets.h"
#define RGB(r,g,b) ((uint16_t)(((r)>>3)<<11|((g)>>2)<<5|((b)>>3)))
uint16_t dota_pixels[480*272];
static uint16_t reference[480*272];
static uint16_t dota_overview[DOTA_HUD_MAP_W*DOTA_HUD_MAP_W];
static uint32_t current[2][2048],explored[2][2048];
static DotaGame game;
static unsigned rng=0x98765432u,queries,getters,attached=1,checks;
static unsigned next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int dota_roshan_region_at(int x,int y){return (x/512+y/512)%3;}
int dota_sight_fog_state(const DotaGame *g,int team,int x,int y){
 unsigned c;++queries;if(!g||!attached||(unsigned)team>=2||(unsigned)x>=4096||(unsigned)y>=4096)return 0;
 c=(unsigned)(y/16)*256u+(unsigned)(x/16);
 return ((current[team][c>>5]>>(c&31))&1u)?2:((explored[team][c>>5]>>(c&31))&1u)?1:0;
}
const uint32_t *dota_sight_current_bits(const DotaGame *g,int team){
 ++getters;return g&&attached&&(unsigned)team<2?current[team]:0;
}
static void reference_background(const DotaGame *g,const DotaView *v){
    static unsigned char regions[DOTA_HUD_MAP_W*DOTA_HUD_MAP_W];
    static int regions_ready;
    int i,j,x=DOTA_HUD_MAP_X,y=DOTA_HUD_MAP_Y,w=DOTA_HUD_MAP_W;
    int observer_region=dota_roshan_region_at(g->units[g->player].x,g->units[g->player].y);
    if(!regions_ready){
        for(j=0;j<w;++j)for(i=0;i<w;++i)regions[j*w+i]=(unsigned char)dota_roshan_region_at(i*DOTA_WORLD/w,j*DOTA_WORLD/w);
        regions_ready=1;
    }
    for(j=0;j<w;++j)for(i=0;i<w;++i){
        int same_region=observer_region==regions[j*w+i];
        int visible=dota_sight_fog_state(g,g->units[g->player].team,i*DOTA_WORLD/w,j*DOTA_WORLD/w)==2;
        uint16_t color=dota_overview[j*w+i];
        dota_pixels[(y+j)*480+x+i]=same_region?(visible?color:vision_assets_fog_color(color,3)):RGB(8,8,8);
    }
}
static void current_background(const DotaGame *g,const DotaView *v){
    static unsigned char regions[DOTA_HUD_MAP_W*DOTA_HUD_MAP_W];
    static int regions_ready;
    int i,j,x=DOTA_HUD_MAP_X,y=DOTA_HUD_MAP_Y,w=DOTA_HUD_MAP_W;
    unsigned char columns[DOTA_HUD_MAP_W];
    const uint32_t *current=dota_sight_current_bits(g,g->units[g->player].team);
    int observer_region=dota_roshan_region_at(g->units[g->player].x,g->units[g->player].y);
    if(!regions_ready){
        for(j=0;j<w;++j)for(i=0;i<w;++i)regions[j*w+i]=(unsigned char)dota_roshan_region_at(i*DOTA_WORLD/w,j*DOTA_WORLD/w);
        regions_ready=1;
    }
    for(i=0;i<w;++i)columns[i]=(unsigned char)(i*DOTA_SIGHT_GRID_SIDE/w);
    for(j=0;j<w;++j){
      const uint32_t *row=current?current+(j*DOTA_SIGHT_GRID_SIDE/w)*(DOTA_SIGHT_GRID_SIDE/32):0;
      for(i=0;i<w;++i){
        int same_region=observer_region==regions[j*w+i];
        /* floor(floor(i*WORLD/w)/CELL) == floor(i*GRID/w). Borrow the
         * current mask once; no game tick runs during this draw. */
        unsigned cell=columns[i];
        int visible=row&&((row[cell>>5]>>(cell&31u))&1u);
        uint16_t color=dota_overview[j*w+i];
        dota_pixels[(y+j)*480+x+i]=same_region?(visible?color:vision_assets_fog_color(color,3)):RGB(8,8,8);
      }
    }
}

int main(void){
 unsigned n,i,team,region;DotaView view;memset(&view,0,sizeof(view));
 for(i=0;i<72;++i)if(((i*4096/72)/16)!=(i*256/72))return 2;
 for(n=0;n<64;++n){
  for(i=0;i<72*72;++i)dota_overview[i]=(uint16_t)next();
  for(i=0;i<2048;++i){current[0][i]=n==0?0:n==1?~0u:n==2?0xaaaaaaaau:next();current[1][i]=next();explored[0][i]=next();explored[1][i]=next();}
  attached=n!=3;
  for(team=0;team<3;++team)for(region=0;region<3;++region){
   game.player=0;game.units[0].team=(int)team;game.units[0].x=(int)(region*512);game.units[0].y=0;
   memset(dota_pixels,0xa5,sizeof(dota_pixels));queries=getters=0;
   reference_background(&game,&view);if(queries!=72*72||getters)return 3;
   memcpy(reference,dota_pixels,sizeof(reference));
   memset(dota_pixels,0xa5,sizeof(dota_pixels));queries=getters=0;
   current_background(&game,&view);if(queries||getters!=1)return 4;
   if(memcmp(reference,dota_pixels,sizeof(reference)))return 5;
   ++checks;
  }
 }
 printf("{\"passed\":true,\"cases\":%u,\"pixel_words_compared\":%u,\"old_queries_per_frame\":5184,\"new_getters_per_frame\":1}\n",checks,checks*480u*272u);
 return 0;
}
