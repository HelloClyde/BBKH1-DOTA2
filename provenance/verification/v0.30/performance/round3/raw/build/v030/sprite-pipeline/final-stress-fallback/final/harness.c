/* The same actual renderer binary runs once on legacy files, once on H1R. */
#define main vision_ui_unused_main
#include "vision_ui.c"
#undef main
#include "tree_visual.h"
#include "trees.h"
#include "resource_pack.h"
/* vision_ui remaps this declaration while including native.c. */
int vision_assets_open_default(void);
static uint16_t baseline_color[480*192],baseline_depth[480*192],previous_color[480*192],previous_depth[480*192];
static unsigned decode_callbacks;
static void decoded(void *context){unsigned *count=context;++*count;}
static unsigned difference(const uint16_t*a,const uint16_t*b){unsigned i,n=0;for(i=0;i<480u*192u;++i)n+=a[i]!=b[i];return n;}
static void render_resource(const BSCamera *camera){DotaGame frozen=dota_game;bs_render_game(dota_pixels,&dota_game,camera);CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));CHECK(!tree_visual_error());CHECK(!bs_last_error());}
static void baseline(void){memcpy(baseline_color,dota_pixels+480*24,sizeof(baseline_color));memcpy(baseline_depth,bs_depth_buffer(),sizeof(baseline_depth));}
static int crown(const BSCamera *camera,int wx,int wy,int exclude,int *gx,int *gy,int *pixel){
 int h,ox,oy;bs_project(camera,0,0,0,&ox,&oy,0);
 for(h=24;h<=216;h+=24){int sx,sy,id;bs_project(camera,wx,wy,bs_ground_height(wx,wy)+h,&sx,&sy,0);
  if(sx<0||sx>=480||sy<24||sy>=216)continue;
  id=tree_visual_pick(&dota_game,sx-ox,sy-oy);if(id<0||id==exclude)continue;
  *gx=sx-ox;*gy=sy-oy;if(pixel)*pixel=(sy-24)*480+sx;return id;
 }return -1;
}
int main(int argc,char **argv){
 uint16_t *xy;BSCamera camera;FILE *frames,*depths,*meta;char path[2048];const char *tree;
 unsigned base_heap,reads,hits,changed_colors,changed_depths,neighbor_changes,queries,tree_heap,resource_peak,scene_reads;
 ResourceStats resource_result;int unified,i,near=-1,at=-1,neighbor=-1,gx=0,gy=0,ngx=0,ngy=0,npx=0,tx=0,ty=0,best=0x7fffffff;
 CHECK(argc==6);unified=!strcmp(argv[1],"unified");CHECK(unified||!strcmp(argv[1],"legacy"));output_dir=argv[5];tree=unified?0:argv[3];
 if(unified){CHECK(resource_mount(argv[2]));resource_set_service(decoded,&decode_callbacks);CHECK(baked_io_open_resource(0));}
 else CHECK(baked_io_open(argv[2]));
 CHECK(bs_initialize()&&item_assets_open());CHECK(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);CHECK(!bs_reserve_fog_cache(0));CHECK(bs_reserve_sprite_row_cache(128u)==128u);
 /* MAP12 is removed from both modes. Both read the original SCENE tile planes. */
 CHECK(!bs_map_sidecar_open_default());base_heap=bs_dynamic_storage_bytes();CHECK(tree_visual_open(tree));CHECK(bs_dynamic_storage_bytes()==base_heap+tree_visual_dynamic_bytes());
 xy=malloc(DOTA_STATIC_TREE_COUNT*4u);CHECK(xy);
 for(i=0;i<DOTA_STATIC_TREE_COUNT;++i){int x,y,d;CHECK(dota_trees_position((unsigned)i,&x,&y));xy[i*2]=(uint16_t)x;xy[i*2+1]=(uint16_t)y;d=(x-2400)*(x-2400)+(y-2050)*(y-2050);if(d<best){best=d;near=i;tx=x;ty=y;}}
 CHECK(near>=0);flat_workspace=malloc(dota_sight_workspace_bytes());CHECK(flat_workspace);setup(DOTA_HERO_LINA);
 /* Attach the actual original height/vision payload after fixture game setup. */
 CHECK(unified?vision_assets_open_default():vision_assets_open(argv[4]));CHECK(vision_assets_stats()->ready);CHECK(vision_assets_stats()->payload_crc32==VISION_EXPECTED_PAYLOAD_CRC32);
 dota_game.tick=1200;dota_game.units[0].x=tx-180;dota_game.units[0].y=ty+80;dota_sight_invalidate();camera.x=tx;camera.y=ty;camera.z=bs_ground_height(tx,ty);
 view.camera_x=tx;view.camera_y=ty;view.follow=0;
 {DotaGame frozen=dota_game;dota_render(&dota_game,&view);CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));CHECK(!bs_last_error());write_frame("full-ui");}
 reads=tree_visual_stats()->read_requests;render_resource(&camera);baseline();CHECK(tree_visual_stats()->read_requests==reads&&!tree_visual_stats()->cache_bytes);
 tree_visual_close();CHECK(dota_trees_attach(xy,DOTA_STATIC_TREE_COUNT));render_resource(&camera);CHECK(!memcmp(baseline_color,dota_pixels+480*24,sizeof(baseline_color)));CHECK(!memcmp(baseline_depth,bs_depth_buffer(),sizeof(baseline_depth)));
 CHECK(tree_visual_open(tree));CHECK(bs_reserve_fog_cache(184320)==184320);for(i=0;i<5;++i)render_resource(&camera);CHECK(bs_fog_cache_stats()->ready);baseline();write_frame("standing");
 at=crown(&camera,tx,ty,-1,&gx,&gy,0);CHECK(at>=0);
 for(i=0;i<DOTA_STATIC_TREE_COUNT&&neighbor<0;++i)if(i!=at){int dx=(int)xy[i*2]-tx,dy=(int)xy[i*2+1]-ty;if(dx*dx+dy*dy<=120*120)neighbor=crown(&camera,xy[i*2],xy[i*2+1],at,&ngx,&ngy,&npx);}
 CHECK(neighbor>=0&&neighbor!=at&&tree_visual_pick(&dota_game,gx,gy)==at);CHECK(dota_trees_cut(&dota_game,(unsigned)at));hits=bs_fog_cache_stats()->hits;queries=tree_visual_stats()->depth_queries;
 render_resource(&camera);CHECK(bs_fog_cache_stats()->hits==hits);CHECK(tree_visual_stats()->depth_queries>queries);changed_colors=difference(baseline_color,dota_pixels+480*24);changed_depths=difference(baseline_depth,bs_depth_buffer());CHECK(changed_colors&&changed_depths);write_frame("cut");
 CHECK(tree_visual_pick(&dota_game,gx,gy)!=at);CHECK(tree_visual_pick(&dota_game,ngx,ngy)==neighbor);CHECK(bs_depth_buffer()[npx]==baseline_depth[npx]);
 memcpy(previous_depth,bs_depth_buffer(),sizeof(previous_depth));CHECK(dota_trees_cut(&dota_game,(unsigned)neighbor));hits=bs_fog_cache_stats()->hits;render_resource(&camera);CHECK(bs_fog_cache_stats()->hits==hits&&dota_game.trees.cut_count==2);CHECK(tree_visual_pick(&dota_game,ngx,ngy)!=neighbor);neighbor_changes=difference(previous_depth,bs_depth_buffer());CHECK(neighbor_changes);write_frame("two-cut");
 CHECK(bs_dynamic_storage_bytes()==base_heap+184320u+tree_visual_dynamic_bytes());memcpy(previous_color,dota_pixels+480*24,sizeof(previous_color));memcpy(previous_depth,bs_depth_buffer(),sizeof(previous_depth));CHECK(!bs_reserve_fog_cache(0));render_resource(&camera);CHECK(!memcmp(previous_color,dota_pixels+480*24,sizeof(previous_color))&&!memcmp(previous_depth,bs_depth_buffer(),sizeof(previous_depth)));
 CHECK(bs_reserve_fog_cache(184320)==184320);for(i=0;i<5;++i)render_resource(&camera);CHECK(bs_fog_cache_stats()->ready);
 dota_game.tick+=DOTA_TREE_REGROW_TICKS;dota_trees_tick(&dota_game);CHECK(!dota_game.trees.cut_count);dota_game.tick=1200;dota_sight_invalidate();hits=bs_fog_cache_stats()->hits;render_resource(&camera);CHECK(bs_fog_cache_stats()->hits==hits);CHECK(!memcmp(baseline_color,dota_pixels+480*24,sizeof(baseline_color))&&!memcmp(baseline_depth,bs_depth_buffer(),sizeof(baseline_depth)));CHECK(tree_visual_pick(&dota_game,gx,gy)==at&&tree_visual_pick(&dota_game,ngx,ngy)==neighbor);write_frame("regrown");
 CHECK(dota_trees_cut(&dota_game,(unsigned)at));CHECK(!bs_reserve_fog_cache(0));
 snprintf(path,sizeof(path),"%s/frames.bin",output_dir);frames=fopen(path,"wb");CHECK(frames);snprintf(path,sizeof(path),"%s/depths.bin",output_dir);depths=fopen(path,"wb");CHECK(depths);snprintf(path,sizeof(path),"%s/frames.csv",output_dir);meta=fopen(path,"w");CHECK(meta);fprintf(meta,"frame,origin_x,origin_y,walking\n");
 for(i=0;i<97;++i){BSCamera moving=camera;int sx,sy;unsigned before;moving.x+=i;moving.y+=i/2;
  if(i>=65){dota_game.units[0].x=tx-180+(i-64)*3;dota_game.units[0].y=ty+80-(i-64);dota_sight_invalidate();}
  render_resource(&moving);before=tree_visual_stats()->read_requests;render_resource(&moving);CHECK(tree_visual_stats()->read_requests==before);bs_project(&moving,0,0,0,&sx,&sy,0);CHECK(fwrite(dota_pixels+480*24,480*192*2,1,frames)==1);CHECK(fwrite(bs_depth_buffer(),480*192*2,1,depths)==1);fprintf(meta,"%d,%d,%d,%d\n",i,240-sx,120-sy,i>=65);{int j;for(j=0;j<DOTA_MAX_UNITS;++j){int b[4]={0,0,0,0};uint16_t z=0;int valid=bs_unit_screen_bounds(j,b,&z);fprintf(meta,"base-bounds,%d,%d,%d,%d,%d,%d,%d,%u\n",i,j,valid,b[0],b[1],b[2],b[3],z);}}
 }

 /* Explicit test placement only; rendering remains read-only Game work. */
 {static const int edge[]={0,1,15,16,17,31,32,63,64,127,128,255,256,511,512,1023,1024,2047,2048,3071,3072,3583,3584,3967,3968,4031,4032,4063,4064,4079,4094,4095};
 static const int dx[]={0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237,256,237,181,98};
 static const int dy[]={256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
 unsigned rng=0x6b59271du;int fixture,j;
 for(fixture=0;fixture<256;++fixture){DotaUnit *u=&dota_game.units[0];BSCamera sample;const BSStats *rendered;
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  u->x=fixture<64?edge[fixture&31]:(int)(rng&4095u);
  u->y=fixture<32?edge[(fixture*7)&31]:fixture<64?edge[(fixture*13)&31]:(int)((rng>>12)&4095u);
  u->hero_id=fixture%10;u->generation+=1;u->move_order=1;
  u->move_x=u->x+dx[fixture&15];u->move_y=u->y+dy[fixture&15];
  dota_game.tick+=3;dota_sight_invalidate();sample.x=u->x;sample.y=u->y;sample.z=bs_ground_height(u->x,u->y);
  render_resource(&sample);rendered=bs_stats();
  CHECK(fwrite(dota_pixels+480*24,480*192*2,1,frames)==1);
  CHECK(fwrite(bs_depth_buffer(),480*192*2,1,depths)==1);
  fprintf(meta,"fixture,%d,%d,%d,%d,%u,%u,%u,%u\n",fixture,u->hero_id,u->x,u->y,rendered->sprite_pixels,rendered->blended_pixels,rendered->depth_rejects,rendered->units_drawn);
  for(j=0;j<DOTA_MAX_UNITS;++j){int b[4]={0,0,0,0};uint16_t z=0;int valid=bs_unit_screen_bounds(j,b,&z);fprintf(meta,"bounds,%d,%d,%d,%d,%d,%d,%d,%u\n",fixture,j,valid,b[0],b[1],b[2],b[3],z);}
 }
 }
 CHECK(!fclose(frames)&&!fclose(depths)&&!fclose(meta));tree_heap=tree_visual_dynamic_bytes();resource_peak=resource_heap_bytes();resource_result=*resource_stats();scene_reads=baked_io_stats()->raw_reads;
 if(unified){CHECK(decode_callbacks&&resource_result.service_calls==decode_callbacks);CHECK(resource_result.open_files==2u&&!resource_result.last_error&&!resource_result.errors);}else CHECK(!decode_callbacks&&!resource_is_mounted());
 tree_visual_close();vision_assets_close();dota_sight_detach();free(flat_workspace);free(xy);item_assets_close();bs_close();CHECK(!bs_dynamic_storage_bytes());resource_set_service(0,0);CHECK(!resource_stats()->open_files);CHECK(resource_unmount());CHECK(!resource_heap_bytes()&&!live_allocations);
 printf("RESULT {\"passed\":true,\"unified\":%s,\"checks\":%u,\"tree_id\":%d,\"neighbor_id\":%d,\"changed_colors\":%u,\"changed_depths\":%u,\"neighbor_changed_depths\":%u,\"tree_heap\":%u,\"resource_heap\":%u,\"service_callbacks\":%u,\"decoded_chunks\":%u,\"physical_reads\":%u,\"physical_bytes\":%u,\"scene_raw_reads\":%u,\"frames\":97,\"camera_frames\":65,\"walking_frames\":32,\"zero_cut_original_pixels\":true,\"zero_cut_tile_io\":0,\"crown_cut_regrow\":true,\"neighbor_preserved\":true,\"fog_cache_equal\":true,\"all_heap_released\":true}\n",unified?"true":"false",checks,at,neighbor,changed_colors,changed_depths,neighbor_changes,tree_heap,resource_peak,decode_callbacks,resource_result.decoded_chunks,resource_result.physical_reads,resource_result.physical_bytes,scene_reads);
 return 0;
}
