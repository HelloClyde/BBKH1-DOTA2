/* Actual renderer translation and immediate-visibility observations.
 * Expected pixels come from another camera view of the same frozen world,
 * never from a duplicate fog implementation. */
#define main vision_ui_unused_main
#include "vision_ui.c"
#undef main

int main(int argc,char **argv){
    FILE *frames,*meta;char path[2048];int group,i,bounds[4];DotaGame frozen;
    CHECK(argc==3);output_dir=argv[2];
    CHECK(baked_io_open(argv[1])&&bs_initialize()&&item_assets_open());
    CHECK(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);
    CHECK(!bs_reserve_fog_cache(0));CHECK(bs_map_sidecar_open("assets/runtime/MAP12.H1C"));
    flat_workspace=malloc(dota_sight_workspace_bytes());CHECK(flat_workspace);
    snprintf(path,sizeof(path),"%s/frames.bin",output_dir);frames=fopen(path,"wb");CHECK(frames);
    snprintf(path,sizeof(path),"%s/frames.csv",output_dir);meta=fopen(path,"w");CHECK(meta);
    fprintf(meta,"group,frame,camera_x,camera_y,camera_z,origin_x,origin_y,queries,dimmed,soft\n");
    for(group=0;group<6;++group){
        const int observers[3][2]={{2048,2048},{1200,1900},{3000,1050}};
        int x=observers[group/2][0],y=observers[group/2][1];
        setup(DOTA_HERO_LINA);dota_game.tick=1200+group;
        dota_game.units[0].x=x;dota_game.units[0].y=y;dota_sight_invalidate();
        for(i=0;i<65;++i){
            BSCamera camera={x+310+i,y-80+(group%2?i:0),0};int sx,sy;
            camera.z=bs_ground_height(x+310,y-80)+(group%2?i/2:0);
            /* Warm history without advancing game time or animation. */
            bs_render_game(dota_pixels,&dota_game,&camera);frozen=dota_game;
            bs_render_game(dota_pixels,&dota_game,&camera);
            CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));
            CHECK(!bs_stats()->tile_misses);
            bs_project(&camera,0,0,0,&sx,&sy,0);
            CHECK(fwrite(dota_pixels+BS_TOP*BS_WIDTH,BS_WIDTH*BS_HEIGHT*2,1,frames)==1);
            fprintf(meta,"%d,%d,%d,%d,%d,%d,%d,%u,%u,%u\n",group,i,camera.x,camera.y,camera.z,BS_WIDTH/2-sx,BS_TOP+BS_HEIGHT/2-sy,bs_fog_stats()->queries,bs_fog_stats()->dimmed_pixels,bs_fog_stats()->soft_cells);
        }
    }
    CHECK(!fclose(frames)&&!fclose(meta));
    /* Walking changes real current sight every tick. Render that same state
     * from a following and a stationary camera to separate genuine newly
     * visible terrain from camera-dependent fog flicker. */
    snprintf(path,sizeof(path),"%s/walking.bin",output_dir);frames=fopen(path,"wb");CHECK(frames);
    snprintf(path,sizeof(path),"%s/walking.csv",output_dir);meta=fopen(path,"w");CHECK(meta);
    fprintf(meta,"frame,origin_x,origin_y,reference_x,reference_y\n");
    setup(DOTA_HERO_LINA);
    for(i=0;i<65;++i){
        BSCamera reference={2358,1968,bs_ground_height(2358,1968)},camera=reference;int sx,sy,rx,ry;
        dota_game.tick=1250+i;dota_game.units[0].x=2048+i;dota_game.units[0].y=2048+i/2;
        camera.x+=i;camera.y+=i/2;dota_sight_invalidate();frozen=dota_game;
        bs_render_game(dota_pixels,&dota_game,&camera);CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));
        CHECK(fwrite(dota_pixels+BS_TOP*BS_WIDTH,BS_WIDTH*BS_HEIGHT*2,1,frames)==1);
        bs_render_game(dota_pixels,&dota_game,&reference);CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));
        CHECK(fwrite(dota_pixels+BS_TOP*BS_WIDTH,BS_WIDTH*BS_HEIGHT*2,1,frames)==1);
        bs_project(&camera,0,0,0,&sx,&sy,0);bs_project(&reference,0,0,0,&rx,&ry,0);
        fprintf(meta,"%d,%d,%d,%d,%d\n",i,BS_WIDTH/2-sx,BS_TOP+BS_HEIGHT/2-sy,BS_WIDTH/2-rx,BS_TOP+BS_HEIGHT/2-ry);
    }
    CHECK(!fclose(frames)&&!fclose(meta));
    /* One unchanged camera: loss/restoration of sight must take effect in the
     * same render, including after a reusable fog background was captured. */
    setup(DOTA_HERO_LINA);dota_game.tick=1300;enemy(5,2100,2048);
    {BSCamera camera={2100,2048,bs_ground_height(2100,2048)};int visible[3]={1,0,1};
        CHECK(bs_reserve_fog_cache(184320)==184320);
        for(i=0;i<3;++i){int repeat;dota_game.tick=1300+i;dota_game.units[0].x=i==1?3900:2048;dota_sight_invalidate();
            CHECK(!!dota_unit_visible(&dota_game,0,5)==visible[i]);
            bs_render_game(dota_pixels,&dota_game,&camera);
            CHECK(!!bs_unit_screen_bounds(5,bounds,0)==visible[i]);
            for(repeat=0;repeat<5;++repeat)bs_render_game(dota_pixels,&dota_game,&camera);
            CHECK(!!bs_unit_screen_bounds(5,bounds,0)==visible[i]);
        }
    }
    printf("RESULT {\"passed\":true,\"camera_frames\":390,\"walking_pairs\":65,\"immediate_visibility_states\":3,\"checks\":%u}\n",checks);
    dota_sight_detach();free(flat_workspace);item_assets_close();bs_close();CHECK(!live_allocations);return 0;
}
