/* Compare every current/explored bit against a separately compiled, frozen
 * pre-union implementation. Test fixtures mutate inputs, never visibility. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game/sight.h"
#include "../game/trees.h"
#include "../game/match_time.h"
#include "../game/vision.h"
#include "../game/extra_heroes.h"
#include "../game/roshan_data.h"
#include "../platform/baked_io.h"
#include "../platform/item_assets.h"
#include "../platform/vision_assets.h"
unsigned old_dota_sight_workspace_bytes(void);
int old_dota_sight_attach(const int16_t*,const uint8_t*,void*,unsigned);
void old_dota_sight_detach(void),old_dota_sight_reset(void),old_dota_sight_invalidate(void);
int old_dota_sight_fog_state(const DotaGame*,int,int,int);
int old_dota_sight_team_position_visible(const DotaGame*,int,int,int);
int old_dota_sight_team_visible(const DotaGame*,int,int);
void old_copy_masks(void*);
void new_copy_masks(void*);
int new_static_changed(void);
void old_source_fixture(const DotaGame*,int,int,int);
void new_source_fixture(const DotaGame*,int,int,int);
static DotaGame game,other;
static int16_t terrain[65536];static uint8_t tree_bits[8192],reference_masks[32768],candidate_masks[32768];
static uint16_t coordinates[DOTA_STATIC_TREE_COUNT*2];
static void *new_memory,*old_memory;static unsigned checks,cases,union_hits,union_rebuilds;
static unsigned long long compared_bytes;static int allocation_fail,allocations,frees;
#define CHECK(expr) do{++checks;if(!(expr)){fprintf(stderr,"FAIL line%d %s case%u tick%d\n",__LINE__,#expr,cases,game.tick);exit(1);}}while(0)
void *cache_memory_alloc(unsigned bytes){void*p=allocation_fail?0:malloc(bytes);if(p)++allocations;return p;}
void cache_memory_free(void*p){if(p){++frees;free(p);}}
static void load(const char *path,void*dst,unsigned off,unsigned count){FILE*f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,(long)off,SEEK_SET));CHECK(fread(dst,1,count,f)==count);CHECK(!fclose(f));}
static void attach_both(const int16_t*h,const uint8_t*t){CHECK(dota_sight_attach(h,t,new_memory,dota_sight_workspace_bytes()));CHECK(old_dota_sight_attach(h,t,old_memory,old_dota_sight_workspace_bytes()));}
static void invalidate(void){dota_sight_invalidate();old_dota_sight_invalidate();}
static void compare(const char *label,int changed){int i,team;unsigned seed=71;
 (void)dota_sight_fog_state(&game,0,2008,2008);(void)old_dota_sight_fog_state(&game,0,2008,2008);
 old_copy_masks(reference_masks);new_copy_masks(candidate_masks);CHECK(!memcmp(reference_masks,candidate_masks,sizeof(reference_masks)));compared_bytes+=sizeof(reference_masks);
 if(changed>=0)CHECK(new_static_changed()==changed);
 if(new_static_changed())++union_rebuilds;else ++union_hits;
 for(team=0;team<2;++team){int observer=team?5:0;
  for(i=0;i<DOTA_MAX_UNITS;++i)CHECK(dota_sight_team_visible(&game,observer,i)==old_dota_sight_team_visible(&game,observer,i));
  for(i=0;i<2048;++i){int x,y;seed=seed*1664525u+1013904223u;x=(int)(seed>>20);seed=seed*1664525u+1013904223u;y=(int)(seed>>20);CHECK(dota_sight_team_position_visible(&game,team,x,y)==old_dota_sight_team_position_visible(&game,team,x,y));CHECK(dota_sight_fog_state(&game,team,x,y)==old_dota_sight_fog_state(&game,team,x,y));}
 }
 ++cases;printf("CASE %u %s static_%s\n",cases,label,new_static_changed()?"rebuild":"reuse");
}
static int tower(void){int i;for(i=10;i<DOTA_MAX_UNITS;++i)if(game.units[i].active&&game.units[i].hp>0&&game.units[i].type==DOTA_TOWER)return i;CHECK(0);return -1;}
static void mutations(void){int i,t,x,y;DotaUnit saved;
 dota_init(&game,DOTA_HERO_AXE,73);game.tick=2000;old_dota_sight_reset();compare("initial-real-terrain",1);
 for(i=0;i<8;++i){game.tick++;compare("unchanged-next-tick",0);}
 invalidate();compare("same-tick-explicit-invalidation",0);
 game.units[0].x++;invalidate();compare("hero-subcell-move",0);game.units[0].x+=16;invalidate();compare("hero-cross-cell",0);
 t=tower();saved=game.units[t];game.units[t].hp=0;invalidate();compare("tower-death",1);game.units[t]=saved;invalidate();compare("tower-restored",1);
 game.units[t].team=!saved.team;invalidate();compare("tower-team-change",1);game.units[t]=saved;game.units[t].x++;invalidate();compare("tower-subcell-change",1);
 game.units[t].x=4096;invalidate();compare("tower-outside-map",1);game.units[t]=saved;invalidate();compare("tower-return",1);
 game.units[t].type=DOTA_CREEP;invalidate();compare("same-slot-static-to-moving-kind",1);game.units[t]=saved;invalidate();compare("same-slot-static-return",1);
 game.tick=dota_match_elapsed_tick(5*60*DOTA_HZ);CHECK(dota_match_is_night(&game));invalidate();compare("night-radius",1);
 game.tick=dota_match_elapsed_tick(10*60*DOTA_HZ);CHECK(!dota_match_is_night(&game));invalidate();compare("day-radius",1);
 CHECK(dota_trees_cut(&game,966));old_dota_sight_invalidate();compare("source-tree-cut",1);
 game.tick+=DOTA_TREE_REGROW_TICKS;dota_trees_tick(&game);old_dota_sight_invalidate();compare("source-tree-regrown",1);
 game.item_world.tree[0].x=saved.x+16;game.item_world.tree[0].y=saved.y;game.item_world.tree[0].until=game.tick+20;game.item_world.tree[0].removed=0;invalidate();compare("planted-tree",1);
 game.item_world.tree[0].x++;invalidate();compare("planted-tree-subcell-change",1);game.item_world.tree[0].removed=1;invalidate();compare("planted-tree-cut",1);
 game.item_world.tree[0].removed=0;invalidate();compare("planted-tree-return",1);game.tick+=20;compare("planted-tree-expiry",1);
 game.units[t].x=dota_roshan_pit_positions[0][0];game.units[t].y=dota_roshan_pit_positions[0][1];CHECK(dota_roshan_region_at(game.units[t].x,game.units[t].y)==1);invalidate();compare("tower-in-roshan-region",1);
 game.units[t]=saved;invalidate();compare("tower-outside-roshan-region",1);
 game.objectives.watcher_owner[0]=0;game.objectives.watcher_until[0]=game.tick+100;invalidate();compare("watcher-active",0);game.objectives.watcher_owner[0]=1;invalidate();compare("watcher-changes-team",0);game.tick+=100;compare("watcher-expired",0);
 game.roster.field[0].source=0;game.roster.field[0].source_generation=game.units[0].generation;game.roster.field[0].x=saved.x;game.roster.field[0].y=saved.y;game.roster.field[0].starts_at=game.tick;game.roster.field[0].until=game.tick+100;game.roster.field[0].radius=1000;game.roster.field_mask|=1u;invalidate();compare("flying-spell-vision",0);game.roster.field[0].until=0;invalidate();compare("flying-spell-expired",0);
 game.units[0].hero_id=DOTA_HERO_SNIPER;game.units[0].learned[2]=4;game.roster.hero[0].generation=game.units[0].generation;game.roster.hero[0].aim_until=game.tick+100;game.roster.hero[0].vision_dx=1;game.roster.hero[0].vision_dy=0;invalidate();compare("sniper-cone",0);game.roster.hero[0].vision_dx=-1;invalidate();compare("sniper-cone-turn",0);
 game.item_world.trap[0].until=game.tick+100;game.item_world.trap[0].item=DOTA_ITEM_GEM;game.item_world.trap[0].owner=0;game.item_world.trap[0].x=saved.x;game.item_world.trap[0].y=saved.y;invalidate();compare("gem-field",0);
 other=game;(void)dota_sight_fog_state(&other,0,0,0);(void)old_dota_sight_fog_state(&other,0,0,0);compare("game-owner-switch-back",1);
 dota_sight_reset();old_dota_sight_reset();compare("explicit-reset-clears-exploration",1);
 dota_init(&game,DOTA_HERO_LINA,79);game.tick=2000;old_dota_sight_reset();compare("new-game-at-same-address",1);
 /* Re-attaching changed terrain is the supported terrain lifecycle. */
 x=saved.x/16;y=saved.y/16;terrain[y*256+x]=terrain[y*256+x]==DOTA_SIGHT_HEIGHT_UNKNOWN?64:(int16_t)(terrain[y*256+x]+64);attach_both(terrain,tree_bits);compare("new-terrain-height-attachment",1);
 terrain[y*256+x]=DOTA_SIGHT_HEIGHT_UNKNOWN;attach_both(terrain,tree_bits);compare("unknown-height-attachment",1);
}
static void fine_radius(void){int i;static const int radius[4]={263,264,264,263};static const int rebuilt[4]={1,1,0,1};
 dota_trees_detach();memset(terrain,0,sizeof(terrain));memset(tree_bits,0,sizeof(tree_bits));attach_both(terrain,tree_bits);
 /* The cell center (2072,2024) is sqrt(64^2+16^2) world units away:
  * 263 Source excludes it and 264 includes it, in the same shadow-mask radius. */
 for(i=0;i<4;++i){unsigned cell=(2024u/16u)*256u+2072u/16u;
  old_source_fixture(&game,2008,2008,radius[i]);new_source_fixture(&game,2008,2008,radius[i]);old_copy_masks(reference_masks);new_copy_masks(candidate_masks);
  CHECK(!memcmp(reference_masks,candidate_masks,sizeof(reference_masks)));compared_bytes+=sizeof(reference_masks);CHECK(new_static_changed()==rebuilt[i]);
  CHECK(((candidate_masks[cell/8]>>(cell&7))&1u)==(unsigned)(radius[i]==264));
  if(new_static_changed())++union_rebuilds;else ++union_hits;++cases;printf("CASE %u fine-Source-radius-%d static_%s\n",cases,radius[i],new_static_changed()?"rebuild":"reuse");
 }
}
static void slark_and_failure(const char*vision){int i,t;
 dota_trees_detach();memset(terrain,0,sizeof(terrain));memset(tree_bits,0,sizeof(tree_bits));attach_both(terrain,tree_bits);dota_init(&game,DOTA_HERO_SLARK,71);game.tick=2000;old_dota_sight_reset();for(i=1;i<DOTA_MAX_UNITS;++i)game.units[i].active=0;
 game.units[0].x=2008;game.units[0].y=2008;game.units[0].learned[3]=1;game.extra.hero[0].unseen_since=game.tick-20;
 game.units[5]=game.units[0];game.units[5].active=0;game.units[5].team=1;game.units[5].hero_id=DOTA_HERO_AXE;
 t=10;game.units[t]=game.units[5];game.units[t].active=1;game.units[t].type=DOTA_TOWER;game.units[t].x=2200;game.units[t].y=2008;
 invalidate();compare("enemy-static-tower-sees-slark",1);CHECK(!dota_extra_shadow_dance_passive_active(&game,0));CHECK(old_dota_sight_team_position_visible(&game,1,2008,2008));
 game.units[t].hp=0;invalidate();compare("tower-death-releases-slark-passive",1);CHECK(dota_extra_shadow_dance_passive_active(&game,0));CHECK(!old_dota_sight_team_position_visible(&game,1,2008,2008));
 game.units[t].hp=100;invalidate();compare("tower-restoration-disables-passive",1);CHECK(!dota_extra_shadow_dance_passive_active(&game,0));
 dota_sight_detach();CHECK(dota_sight_fog_state(&game,0,2008,2008)==0);CHECK(!dota_sight_attach(terrain,tree_bits,new_memory,old_dota_sight_workspace_bytes()));CHECK(!dota_sight_attach(terrain,tree_bits,(unsigned char*)new_memory+1,dota_sight_workspace_bytes()));CHECK(dota_sight_fog_state(&game,0,2008,2008)==0);
 attach_both(0,0);compare("flat-reattach-after-failed-attach",1);dota_sight_detach();old_dota_sight_detach();
 allocation_fail=1;CHECK(!vision_assets_open(vision));CHECK(vision_assets_stats()->allocation_failures>0&&vision_assets_stats()->heap_bytes==0&&!vision_assets_stats()->ready);CHECK(dota_sight_fog_state(&game,0,2008,2008)==0);
 allocation_fail=0;CHECK(vision_assets_open(vision));CHECK(vision_assets_stats()->workspace_bytes==dota_sight_workspace_bytes());CHECK(vision_assets_stats()->heap_bytes==139264u+dota_sight_workspace_bytes());vision_assets_close();CHECK(allocations==frees&&vision_assets_stats()->heap_bytes==0);
}
int main(int argc,char**argv){CHECK(argc==4);CHECK(baked_io_open(argv[1])&&item_assets_open());load(argv[2],terrain,64,sizeof(terrain));load(argv[2],tree_bits,64+sizeof(terrain),sizeof(tree_bits));load(argv[3],coordinates,128,sizeof(coordinates));new_memory=malloc(dota_sight_workspace_bytes());old_memory=malloc(old_dota_sight_workspace_bytes());CHECK(new_memory&&old_memory);CHECK(dota_sight_workspace_bytes()-old_dota_sight_workspace_bytes()>=16384);attach_both(terrain,tree_bits);CHECK(dota_trees_attach(coordinates,DOTA_STATIC_TREE_COUNT));mutations();fine_radius();slark_and_failure(argv[2]);free(new_memory);free(old_memory);item_assets_close();baked_io_close();CHECK(allocations==frees);printf("RESULT {\"passed\":true,\"cases\":%u,\"checks\":%u,\"compared_current_and_explored_bytes\":%llu,\"union_reuses\":%u,\"union_rebuilds\":%u,\"old_workspace_host\":%u,\"new_workspace_host\":%u,\"physical_h1_tested\":false}\n",cases,checks,compared_bytes,union_hits,union_rebuilds,old_dota_sight_workspace_bytes(),dota_sight_workspace_bytes());return 0;}
