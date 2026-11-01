#define main vision_ui_unused_main
#include "vision_ui.c"
#undef main
#include "barracks.h"
static unsigned query_count;
static uint64_t query_digest;
int __real_dota_sight_fog_state(const DotaGame *,int,int,int);
int __wrap_dota_sight_fog_state(const DotaGame *g,int team,int x,int y){
    ++query_count;query_digest=(query_digest^(unsigned)team)*1099511628211ull;
    query_digest=(query_digest^(unsigned)x)*1099511628211ull;query_digest=(query_digest^(unsigned)y)*1099511628211ull;
    return __real_dota_sight_fog_state(g,team,x,y);
}
int main(int argc,char **argv){
    FILE *f;int mode,i,j,shots=0;unsigned hits=0;DotaGame frozen;
    int camera[14][2]={{776,3280},{2048,2048},{1262,1448},{2756,2733},{0,0},{4095,4095},{1200,1900},{3000,1050}};
    CHECK(argc==4);mode=atoi(argv[2]);CHECK(mode>=0&&mode<=2);
    CHECK(baked_io_open(argv[1])&&bs_initialize()&&item_assets_open());CHECK(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);
    if(!mode)CHECK(!bs_reserve_fog_cache(0));
    if(mode==2){CHECK(!bs_reserve_fog_cache(0));fail_alloc=1;CHECK(!bs_reserve_fog_cache(184320));fail_alloc=0;}
    if(mode==1)CHECK(bs_fog_cache_stats()->heap_bytes==184320);
    CHECK(bs_map_sidecar_open("assets/runtime/MAP12.H1C"));flat_workspace=malloc(dota_sight_workspace_bytes());CHECK(flat_workspace);
    setup(DOTA_HERO_LINA);view.follow=0;view.camera_x=776;view.camera_y=3280;
    for(j=0;j<2;++j){int at=dota_barracks_index(j,0,1);
        camera[8+j][0]=dota_secret_shops[j].x;camera[8+j][1]=dota_secret_shops[j].y;
        camera[10+j][0]=dota_objective_positions[DOTA_OBJECTIVE_LOTUS_FIRST+j].x;camera[10+j][1]=dota_objective_positions[DOTA_OBJECTIVE_LOTUS_FIRST+j].y;
        camera[12+j][0]=dota_game.units[at].x;camera[12+j][1]=dota_game.units[at].y;
    }
    f=fopen(argv[3],"wb");CHECK(f);
    for(i=0;i<196;++i){
        int group=i/14,phase=i%14,c=group%14;
        /* Camera/key/vision changes are fixture inputs, never expected pixels. */
        view.camera_x=camera[c][0];view.camera_y=camera[c][1];
        if(phase==7)++view.camera_x;
        dota_game.tick=1200+i;dota_game.units[0].x=camera[c][0];dota_game.units[0].y=camera[c][1];
        if(phase==8||phase==9)dota_game.units[0].x=4095-camera[c][0];
        if(phase==10)dota_game.units[0].team=1;else dota_game.units[0].team=0;
        for(j=0;j<DOTA_LOTUS_POOLS;++j)dota_game.objectives.lotus_count[j]=phase>=11?(phase-10):0;
        if(phase==12){int at=dota_barracks_index(0,0,1);dota_game.units[at].active=1;dota_game.units[at].hp=0;}
        if(phase==13){int at=dota_barracks_index(0,0,1);dota_game.units[at].active=1;dota_game.units[at].hp=100;}
        dota_sight_invalidate();query_count=0;query_digest=1469598103934665603ull;frozen=dota_game;
        dota_render(&dota_game,&view);CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));
        CHECK(fwrite(dota_pixels,sizeof(dota_pixels),1,f)==1);CHECK(fwrite(bs_depth_buffer(),480*192*2,1,f)==1);
        CHECK(fwrite(bs_fog_stats(),sizeof(BSFogStats),1,f)==1);CHECK(fwrite(&query_count,sizeof(query_count),1,f)==1);CHECK(fwrite(&query_digest,sizeof(query_digest),1,f)==1);
        if(mode==1){hits=bs_fog_cache_stats()->hits;if(hits)++shots;}
    }
    CHECK(!fclose(f));if(mode==1)CHECK(hits>0&&bs_fog_cache_stats()->captures>0);
    printf("RESULT {\"passed\":true,\"frames\":196,\"hits\":%u,\"captures\":%u,\"heap\":%u,\"hit_frames_seen\":%d}\n",hits,bs_fog_cache_stats()->captures,bs_fog_cache_stats()->heap_bytes,shots);
    dota_sight_detach();free(flat_workspace);item_assets_close();bs_close();CHECK(!live_allocations&&!bs_dynamic_storage_bytes()&&!bs_fog_cache_stats()->heap_bytes);return 0;
}
