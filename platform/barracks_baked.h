/* Static original barracks become live selectable structures without drawing
 * them twice. Dead lane-pair states replace only verified original mesh pixels. */
#ifndef H1_DOTA_BARRACKS_BAKED_H
#define H1_DOTA_BARRACKS_BAKED_H
#include "barracks_patch.h"
static BSBarracksStats barracks_visual_stats;
static unsigned barracks_visual_source;
static void barracks_visual_reset(void){
    clear_bytes(&barracks_visual_stats,sizeof(barracks_visual_stats));barracks_visual_source=0;
}
static unsigned barracks_visual_static_bytes(void){
    return sizeof(dota_barracks_patches)+sizeof(dota_barracks_selections)+sizeof(dota_barracks_patch_bytes)+
        sizeof(dota_barracks_selection_runs)+sizeof(dota_barracks_bar_floor)+sizeof(barracks_visual_stats)+sizeof(barracks_visual_source);
}
const BSBarracksStats *bs_barracks_stats(void){return &barracks_visual_stats;}
static int barracks_visual_source_matches(void){
    if(!barracks_visual_source)barracks_visual_source=map_sidecar_source_matches()?1u:2u;
    return barracks_visual_source==1u;
}
static void draw_barracks_ground(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    unsigned pair,alive=dota_barracks_alive_mask(game);
    int ox=global_x(camera->x)-BS_WIDTH/2-pack.origin_pixel_x;
    int oy=global_y(camera->y,camera->z)-BS_HEIGHT/2-pack.origin_pixel_y;
    clear_bytes(&barracks_visual_stats,sizeof(barracks_visual_stats));
    if(alive==4095u || !barracks_visual_source_matches())return;
    for(pair=0;pair<6u;++pair){
        unsigned state=(alive>>(pair*2u))&3u;int result;
        if(state==3u)continue;
        result=barracks_patch_pair(pixels,depths,BS_WIDTH,BS_HEIGHT,BS_WIDTH,BS_TOP,ox,oy,pair,state);
        if(result<0)++barracks_visual_stats.errors;
        else if(result){++barracks_visual_stats.patched_pairs;barracks_visual_stats.patched_pixels+=(unsigned)result;}
    }
}
static void register_barracks_bounds(const DotaGame *game,const BSCamera *camera){
    unsigned pair,alive=dota_barracks_alive_mask(game);
    int ox=global_x(camera->x)-BS_WIDTH/2-pack.origin_pixel_x;
    int oy=global_y(camera->y,camera->z)-BS_HEIGHT/2-pack.origin_pixel_y;
    if(view_region || !barracks_visual_source_matches())return;
    for(pair=0;pair<6u;++pair){
        const DotaBarracksPatch *patch=&dota_barracks_patches[pair];unsigned state=(alive>>(pair*2u))&3u,member;
        if(!state || patch->x+patch->w<=ox || patch->x>=ox+BS_WIDTH || patch->y+patch->h<=oy || patch->y>=oy+BS_HEIGHT)continue;
        for(member=0;member<2u;++member){
            const DotaBarracksSelection *selection;UnitBounds *b;unsigned run;
            int index=dota_barracks_index((int)pair/3,(int)pair%3,(int)member+1);
            if(!(state&(1u<<member)) || (unsigned)index>=DOTA_MAX_UNITS ||
               !game->units[index].active || game->units[index].hp<=0 || dota_barracks_kind(&game->units[index])!=(int)member+1 ||
               !dota_unit_visible(game,game->player,index))continue;
            selection=&dota_barracks_selections[pair*4u+member*2u+(state==3u)];b=&bounds[index];
            if(selection->offset>sizeof(dota_barracks_selection_runs)/sizeof(unsigned short) ||
               selection->runs*2u>sizeof(dota_barracks_selection_runs)/sizeof(unsigned short)-selection->offset){++barracks_visual_stats.errors;continue;}
            for(run=0;run<selection->runs;++run){
                unsigned start=dota_barracks_selection_runs[selection->offset+run*2u];
                unsigned n=dota_barracks_selection_runs[selection->offset+run*2u+1u],k;
                int x=patch->x+(int)(start%(unsigned)patch->w)-ox,y=patch->y+(int)(start/(unsigned)patch->w)-oy;
                if(y<0 || y>=BS_HEIGHT)continue;
                for(k=0;k<n;++k){
                    int sx=x+(int)k,sy=y+BS_TOP;unsigned d;
                    if(sx<0 || sx>=BS_WIDTH)continue;
                    d=depths[y*BS_WIDTH+sx];if(d>=65535u)continue;
                    if(!b->valid){b->bounds[0]=b->bounds[2]=sx;b->bounds[1]=b->bounds[3]=sy;b->depth=(uint16_t)d;b->valid=1;}
                    else{
                        if(sx<b->bounds[0])b->bounds[0]=sx;
                        if(sx>b->bounds[2])b->bounds[2]=sx;
                        if(sy<b->bounds[1])b->bounds[1]=sy;
                        if(sy>b->bounds[3])b->bounds[3]=sy;
                        if(d<b->depth)b->depth=(uint16_t)d;
                    }
                }
            }
            if(b->valid)++barracks_visual_stats.selectable_buildings;
        }
    }
}
#endif
