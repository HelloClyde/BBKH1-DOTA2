/* Parent integration snippet for platform/baked_scene.c (HBK12 only).
 * Insert after draw_sprite / before bs_render_game. In the existing fountain
 * block use if(pack.version>=12u) draw_fountains_v022(...); else retain the
 * HBK7..11 clip5 loop byte-for-byte. No generic frame_at rate changes.
 * Materials are sampled by independent original rates; particle births and
 * flow animation are baked in private/baked/v022/fountain/manifest.json.
 */
static unsigned fountain_phase_v022(unsigned tick,unsigned numerator,unsigned denominator,unsigned count){
    unsigned period=denominator*20u;
    return (((tick%period)*numerator*count)/period)%count;
}
static unsigned fountain_track_v022(uint16_t *pixels,int model,int clip,int direction,unsigned phase,int sx,int sy,uint16_t depth){
    const ClipGroup *group=exact_group(model,clip,direction);
    if(!group || !group->count){++lighting_stats.missing_lighting_frames;return 0;}
    return draw_sprite(pixels,(int)group->first+(int)(phase%group->count),sx,sy,depth,256,-1);
}
static void draw_fountains_v022(uint16_t *pixels,const DotaGame *game,const BSCamera *camera,int view_region){
    unsigned i,tick=(unsigned)(game->tick<0?0:game->tick);
    for(i=0;i<2u;++i){
        int sx,sy;uint16_t depth;unsigned drawn,uv;
        if(dota_roshan_region_at(dota_fountains[i].x,dota_fountains[i].y)!=view_region)continue;
        bs_project(camera,dota_fountains[i].x,dota_fountains[i].y,0,&sx,&sy,&depth);
        uv=i?fountain_phase_v022(tick,15,8,16):fountain_phase_v022(tick,21,40,16);
        drawn=fountain_track_v022(pixels,6+(int)i,5,0,uv,sx,sy,depth);
        lighting_stats.fountain_frame[i]=uv;
        if(!i){
            /* -.525, -2.25 and -1.875 original U cycles/s are encoded as
             * negative spatial phases in the baked mesh frames. */
            drawn+=fountain_track_v022(pixels,6,6,0,fountain_phase_v022(tick,9,4,16),sx,sy,depth);
            /* Source rope rotates -30deg/s and V scrolls at
             * -86.36364/909.090942 ~= -.095 cycles/s (error5.61e-10).
             * Independent direction and UV phase prevent a false common loop. */
            drawn+=fountain_track_v022(pixels,6,7,(int)fountain_phase_v022(tick,1,12,16),fountain_phase_v022(tick,19,200,16),sx,sy,depth);
            /* Bot roll .15rad/s. Q32 phase error <.0000263rad over24h;
             * unsigned64 multiplication has no long-session32bit overflow. */
            drawn+=fountain_track_v022(pixels,6,8,(int)((((uint64_t)tick*82027833ull)>>32)&15u),fountain_phase_v022(tick,1,5,16),sx,sy,depth);
            drawn+=fountain_track_v022(pixels,6,9,0,tick%40u,sx,sy,depth);
            drawn+=fountain_track_v022(pixels,6,10,0,tick%40u,sx,sy,depth);
        }
        if(drawn){++lighting_stats.fountain_instances;lighting_stats.fountain_pixels+=drawn;}
    }
}
