/* Real TREE/SCENE assets through the production renderer and sight system. */
#define main vision_ui_unused_main
#include "vision_ui.c"
#undef main
#include "tree_visual.h"
#include "trees.h"
static uint16_t saved_color[480*192],saved_depth[480*192],last_color[480*192],last_depth[480*192];
static unsigned differences(const uint16_t *a,const uint16_t *b,unsigned n){unsigned i,d=0;for(i=0;i<n;++i)d+=a[i]!=b[i];return d;}
static void render_scene(const BSCamera *camera){DotaGame frozen=dota_game;bs_render_game(dota_pixels,&dota_game,camera);CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));CHECK(!tree_visual_error());}
static void save_base(void){memcpy(saved_color,dota_pixels+480*24,sizeof(saved_color));memcpy(saved_depth,bs_depth_buffer(),sizeof(saved_depth));}
static int pick_crown(const BSCamera *camera,int wx,int wy,int exclude,int *gx,int *gy,int *screen){
    int h,ox,oy;bs_project(camera,0,0,0,&ox,&oy,0);
    for(h=24;h<=216;h+=24){int sx,sy,id;bs_project(camera,wx,wy,bs_ground_height(wx,wy)+h,&sx,&sy,0);
        if(sx<0||sx>=480||sy<24||sy>=216)continue;
        id=tree_visual_pick(&dota_game,sx-ox,sy-oy);if(id<0||id==exclude)continue;
        *gx=sx-ox;*gy=sy-oy;if(screen)*screen=(sy-24)*480+sx;return id;
    }return -1;
}
int main(int argc,char **argv){
    uint16_t *xy;BSCamera camera;FILE *frames,*meta;char path[2048];unsigned base_heap,read_before,hits_before,color_changes,depth_changes,neighbor_depth_changes,queries_before;int i,at=-1,near=-1,neighbor=-1,first_gx=0,first_gy=0,neighbor_gx=0,neighbor_gy=0,neighbor_screen=0,tx=0,ty=0,best=0x7fffffff;
    CHECK(argc==4);output_dir=argv[3];CHECK(baked_io_open(argv[1])&&bs_initialize()&&item_assets_open());
    CHECK(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);CHECK(!bs_reserve_fog_cache(0));CHECK(bs_map_sidecar_open("assets/runtime/MAP12.H1C"));
    base_heap=bs_dynamic_storage_bytes();CHECK(tree_visual_open(argv[2]));CHECK(bs_dynamic_storage_bytes()==base_heap+tree_visual_dynamic_bytes());
    xy=malloc(DOTA_STATIC_TREE_COUNT*4u);CHECK(xy);for(i=0;i<DOTA_STATIC_TREE_COUNT;++i){int x,y,d;CHECK(dota_trees_position((unsigned)i,&x,&y));xy[i*2]=(uint16_t)x;xy[i*2+1]=(uint16_t)y;d=(x-2400)*(x-2400)+(y-2050)*(y-2050);if(d<best){best=d;near=i;tx=x;ty=y;}}
    CHECK(near>=0);flat_workspace=malloc(dota_sight_workspace_bytes());CHECK(flat_workspace);setup(DOTA_HERO_LINA);dota_game.tick=1200;dota_game.units[0].x=tx-180;dota_game.units[0].y=ty+80;dota_sight_invalidate();camera.x=tx;camera.y=ty;camera.z=bs_ground_height(tx,ty);
    read_before=tree_visual_stats()->read_requests;render_scene(&camera);save_base();CHECK(tree_visual_stats()->read_requests==read_before&&!tree_visual_stats()->cache_bytes);
    /* Keep exactly the same authoritative natural-tree geometry while
     * disabling only the renderer's TREE provider. Zero cuts must retain the
     * original panorama and depth, despite the provider's different epoch. */
    tree_visual_close();CHECK(dota_trees_attach(xy,DOTA_STATIC_TREE_COUNT));render_scene(&camera);
    CHECK(!memcmp(saved_color,dota_pixels+480*24,sizeof(saved_color)));CHECK(!memcmp(saved_depth,bs_depth_buffer(),sizeof(saved_depth)));
    CHECK(tree_visual_open(argv[2]));CHECK(bs_reserve_fog_cache(184320)==184320);
    for(i=0;i<5;++i)render_scene(&camera);
    CHECK(bs_fog_cache_stats()->ready);save_base();write_frame("standing");
    /* Select a crown from its actual source pixels, then use that exact tree
     * ID. The candidate location only aims the test camera. */
    at=pick_crown(&camera,tx,ty,-1,&first_gx,&first_gy,0);CHECK(at>=0);
    for(i=0;i<DOTA_STATIC_TREE_COUNT&&neighbor<0;++i)if(i!=at){int dx=(int)xy[i*2]-tx,dy=(int)xy[i*2+1]-ty;
        if(dx*dx+dy*dy<=120*120)neighbor=pick_crown(&camera,xy[i*2],xy[i*2+1],at,&neighbor_gx,&neighbor_gy,&neighbor_screen);
    }CHECK(neighbor>=0&&neighbor!=at);CHECK(tree_visual_pick(&dota_game,first_gx,first_gy)==at);
    CHECK(dota_trees_cut(&dota_game,(unsigned)at));hits_before=bs_fog_cache_stats()->hits;queries_before=tree_visual_stats()->depth_queries;
    render_scene(&camera);CHECK(bs_fog_cache_stats()->hits==hits_before);CHECK(tree_visual_stats()->depth_queries>queries_before);
    color_changes=differences(saved_color,dota_pixels+480*24,480u*192u);depth_changes=differences(saved_depth,bs_depth_buffer(),480u*192u);CHECK(color_changes&&depth_changes);write_frame("cut");
    CHECK(tree_visual_pick(&dota_game,first_gx,first_gy)!=at);CHECK(tree_visual_pick(&dota_game,neighbor_gx,neighbor_gy)==neighbor);CHECK(bs_depth_buffer()[neighbor_screen]==saved_depth[neighbor_screen]);
    memcpy(last_depth,bs_depth_buffer(),sizeof(last_depth));CHECK(dota_trees_cut(&dota_game,(unsigned)neighbor));hits_before=bs_fog_cache_stats()->hits;render_scene(&camera);
    CHECK(bs_fog_cache_stats()->hits==hits_before&&dota_game.trees.cut_count==2);CHECK(tree_visual_pick(&dota_game,neighbor_gx,neighbor_gy)!=neighbor);neighbor_depth_changes=differences(last_depth,bs_depth_buffer(),480u*192u);CHECK(neighbor_depth_changes);write_frame("two-cut");
    CHECK(bs_dynamic_storage_bytes()==base_heap+184320u+tree_visual_dynamic_bytes());
    memcpy(last_color,dota_pixels+480*24,sizeof(last_color));CHECK(!bs_reserve_fog_cache(0));render_scene(&camera);CHECK(!memcmp(last_color,dota_pixels+480*24,sizeof(last_color)));
    CHECK(bs_reserve_fog_cache(184320)==184320);for(i=0;i<5;++i)render_scene(&camera);CHECK(bs_fog_cache_stats()->ready);
    /* Reach the real regrow deadline through the tree subsystem. Keep the
     * already frozen animation tick afterward solely for pixel comparison. */
    dota_game.tick+=DOTA_TREE_REGROW_TICKS;dota_trees_tick(&dota_game);CHECK(!dota_game.trees.cut_count&&!dota_trees_is_cut(&dota_game,(unsigned)at));dota_game.tick=1200;dota_sight_invalidate();hits_before=bs_fog_cache_stats()->hits;
    render_scene(&camera);CHECK(bs_fog_cache_stats()->hits==hits_before);CHECK(!memcmp(saved_color,dota_pixels+480*24,sizeof(saved_color)));CHECK(!memcmp(saved_depth,bs_depth_buffer(),sizeof(saved_depth)));CHECK(tree_visual_pick(&dota_game,first_gx,first_gy)==at);CHECK(tree_visual_pick(&dota_game,neighbor_gx,neighbor_gy)==neighbor);write_frame("regrown");
    CHECK(dota_trees_cut(&dota_game,(unsigned)at));CHECK(!bs_reserve_fog_cache(0));
    snprintf(path,sizeof(path),"%s/frames.bin",output_dir);frames=fopen(path,"wb");CHECK(frames);snprintf(path,sizeof(path),"%s/frames.csv",output_dir);meta=fopen(path,"w");CHECK(meta);fprintf(meta,"frame,origin_x,origin_y\n");
    for(i=0;i<65;++i){BSCamera moving=camera;int sx,sy;unsigned reads;moving.x+=i;moving.y+=i/2;render_scene(&moving);reads=tree_visual_stats()->read_requests;render_scene(&moving);CHECK(tree_visual_stats()->read_requests==reads);bs_project(&moving,0,0,0,&sx,&sy,0);CHECK(fwrite(dota_pixels+480*24,480*192*2,1,frames)==1);fprintf(meta,"%d,%d,%d\n",i,240-sx,120-sy);}
    CHECK(!fclose(frames)&&!fclose(meta));
    printf("RESULT {\"passed\":true,\"tree_id\":%d,\"neighbor_id\":%d,\"camera_x\":%d,\"camera_y\":%d,\"changed_colors\":%u,\"changed_depths\":%u,\"neighbor_changed_depths\":%u,\"crown_hit_cut_regrow\":true,\"neighbor_preserved_after_first_cut\":true,\"cut_camera_frames\":65,\"zero_cut_original_pixels\":true,\"zero_cut_tile_io\":0,\"regrow_original_pixels\":true,\"tree_heap\":%u,\"checks\":%u}\n",at,neighbor,tx,ty,color_changes,depth_changes,neighbor_depth_changes,tree_visual_dynamic_bytes(),checks);
    tree_visual_close();dota_sight_detach();free(flat_workspace);free(xy);item_assets_close();bs_close();CHECK(!live_allocations&&!bs_dynamic_storage_bytes());return 0;
}
