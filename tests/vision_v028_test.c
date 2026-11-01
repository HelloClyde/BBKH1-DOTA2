#include "../game/game.h"
#include "../game/sight.h"
#include "../game/vision.h"
#include "../game/match_time.h"
#include "../game/extra_heroes.h"
#include "../game/item_world.h"
#include "../game/items.h"
#include "../game/items_internal.h"
#include "../game/items_actions.h"
#include "../game/items_ids.h"
#include "../game/roshan_data.h"
#include "../game/objectives.h"
#include "../platform/baked_io.h"
#include "../platform/item_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static DotaGame game;
static int16_t height[256*256];
static uint8_t trees[256*256/8];
static void *memory;
static unsigned memory_bytes;
static int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void attach(void){CHECK(dota_sight_attach(height,trees,memory,memory_bytes));}
static void setup(int hero){int i;memset(height,0,sizeof(height));memset(trees,0,sizeof(trees));attach();dota_init(&game,hero,71);game.tick=2000;for(i=2;i<DOTA_MAX_UNITS;++i)game.units[i].active=0;game.units[1].team=1;dota_rules_set_position(&game,0,2008,2008);dota_rules_set_position(&game,1,2300,2008);}
static int ward(int team,int kind,int x,int y,int life){int i=DOTA_ITEM_UNIT_FIRST;DotaUnit *u=&game.units[i];DotaItemWorldUnit *s=&game.item_world.unit[0];memset(u,0,sizeof(*u));memset(s,0,sizeof(*s));u->active=1;u->type=DOTA_SUMMON;u->team=team;u->hp=u->max_hp=200;u->generation=1;u->x=x;u->y=y;u->target=-1;s->kind=kind;s->owner=team?1:0;s->generation=game.units[s->owner].generation;s->until=game.tick+life;dota_sight_invalidate();return i;}
static void flat_ranges(void){
 setup(DOTA_HERO_AXE);dota_rules_set_position(&game,1,2458,2008);CHECK(dota_unit_visible(&game,0,1));dota_rules_set_position(&game,1,2459,2008);CHECK(!dota_unit_visible(&game,0,1));CHECK(dota_sight_fog_state(&game,0,2008,2008)==2);
 dota_rules_set_position(&game,0,2808,2008);CHECK(dota_sight_fog_state(&game,0,2008,2008)==1);dota_sight_reset();CHECK(dota_sight_fog_state(&game,0,2008,2008)==0);
 CHECK(dota_sight_team_position_visible(&game,0,2808,2008));CHECK(!dota_sight_team_position_visible(&game,0,-1,2008));
}
static void terrain(void){int x,y;
 setup(DOTA_HERO_AXE);for(y=0;y<256;++y)trees[(y*256+128)/8]|=(uint8_t)(1u<<((y*256+128)&7));attach();dota_rules_set_position(&game,1,2104,2008);CHECK(!dota_unit_visible(&game,0,1));CHECK(dota_sight_team_position_visible(&game,0,2040,2008));CHECK(dota_sight_fog_state(&game,0,2104,2008)!=2);
 memset(trees,0,sizeof(trees));for(y=0;y<256;++y)for(x=128;x<256;++x)height[y*256+x]=32;attach();CHECK(!dota_unit_visible(&game,0,1));dota_rules_set_position(&game,0,2104,2008);dota_rules_set_position(&game,1,2008,2008);CHECK(dota_unit_visible(&game,0,1));
 setup(DOTA_HERO_AXE);game.item_world.tree[0].x=2048;game.item_world.tree[0].y=2008;game.item_world.tree[0].until=game.tick+20;dota_sight_invalidate();dota_rules_set_position(&game,1,2120,2008);CHECK(!dota_unit_visible(&game,0,1));game.item_world.tree[0].removed=1;dota_sight_invalidate();CHECK(dota_unit_visible(&game,0,1));
}
static void wards(void){int w;
 setup(DOTA_HERO_AXE);dota_rules_set_position(&game,1,2700,2008);CHECK(!dota_unit_visible(&game,0,1));w=ward(0,ITEM_UNIT_OBSERVER,2500,2008,10);CHECK(dota_rules_unit_vision_source(&game,w)==1600);CHECK(dota_unit_visible(&game,0,1));CHECK(!dota_unit_visible(&game,1,w));
 game.tick+=10;dota_item_world_update_unit(&game,w);CHECK(!game.units[w].active);CHECK(!dota_unit_visible(&game,0,1));
 w=ward(0,ITEM_UNIT_OBSERVER,2500,2008,100);CHECK(dota_unit_visible(&game,0,1));CHECK(dota_item_world_on_death(&game,w,-1));CHECK(!dota_unit_visible(&game,0,1));
 setup(DOTA_HERO_AXE);dota_rules_set_position(&game,1,2250,2008);w=ward(1,ITEM_UNIT_OBSERVER,2200,2008,100);CHECK(!dota_unit_visible(&game,0,w));
 game.units[DOTA_ITEM_UNIT_FIRST+1]=game.units[w];game.units[DOTA_ITEM_UNIT_FIRST+1].team=0;game.item_world.unit[1]=game.item_world.unit[0];game.item_world.unit[1].kind=ITEM_UNIT_SENTRY;game.item_world.unit[1].owner=0;dota_sight_invalidate();CHECK(dota_unit_visible(&game,0,w));CHECK(dota_item_world_incoming_from(&game,w,0,700,0,1)==100);CHECK(dota_item_world_incoming_from(&game,w,0,700,1,0)==0);CHECK(dota_item_world_incoming_from(&game,w,0,700,0,0)==0);
 game.units[2]=game.units[0];game.units[2].type=DOTA_CREEP;game.units[2].active=1;CHECK(dota_item_world_incoming_from(&game,w,2,700,0,1)==25);
 dota_rules_set_position(&game,0,3000,2008);game.units[2].active=0;dota_sight_invalidate();CHECK(dota_rules_unit_vision_source(&game,DOTA_ITEM_UNIT_FIRST+1)==0);CHECK(!dota_unit_visible(&game,0,w));
}
static void shared_pit(void){int x=dota_roshan_pit_positions[0][0],y=dota_roshan_pit_positions[0][1];setup(DOTA_HERO_AXE);dota_rules_set_position(&game,1,x,y);CHECK(!dota_unit_visible(&game,0,1));CHECK(!dota_item_world_ward_placeable(&game,0,x,y));game.units[2].active=1;game.units[2].team=0;dota_rules_set_position(&game,2,x,y);CHECK(dota_unit_visible(&game,0,1));CHECK(dota_sight_position_visible(&game,0,x,y));}
static void slark(void){int w;
 setup(DOTA_HERO_SLARK);game.units[0].learned[3]=1;dota_rules_set_position(&game,1,3000,2008);dota_extra_tick(&game);game.tick+=10;CHECK(dota_extra_shadow_dance_passive_active(&game,0));
 w=ward(1,ITEM_UNIT_OBSERVER,2200,2008,100);CHECK(!dota_extra_shadow_dance_passive_active(&game,0));dota_extra_tick(&game);CHECK(game.extra.hero[0].unseen_since==0);CHECK(dota_item_world_on_death(&game,w,-1));dota_extra_tick(&game);game.tick+=9;CHECK(!dota_extra_shadow_dance_passive_active(&game,0));game.tick++;CHECK(dota_extra_shadow_dance_passive_active(&game,0));
 w=ward(1,ITEM_UNIT_OBSERVER,2200,2008,100);game.extra.hero[0].shadow_until=game.tick+80;CHECK(dota_extra_shadow_dance_passive_active(&game,0));CHECK(!dota_unit_visible(&game,1,0));game.extra.hero[0].shadow_until=0;CHECK(!dota_extra_shadow_dance_passive_active(&game,0));dota_item_world_on_death(&game,w,-1);
 dota_extra_tick(&game);game.tick+=10;game.extra.hero[0].neutral_until=game.tick+40;CHECK(!dota_extra_shadow_dance_passive_active(&game,0));game.tick+=40;CHECK(dota_extra_shadow_dance_passive_active(&game,0));
 setup(DOTA_HERO_SLARK);game.units[0].learned[3]=1;dota_rules_set_position(&game,0,dota_objective_positions[DOTA_OBJECTIVE_WATCHER_FIRST].x,dota_objective_positions[DOTA_OBJECTIVE_WATCHER_FIRST].y);dota_rules_set_position(&game,1,3000,2008);game.extra.hero[0].unseen_since=game.tick-20;game.objectives.watcher_owner[0]=1;game.objectives.watcher_until[0]=game.tick+100;dota_sight_invalidate();CHECK(!dota_extra_shadow_dance_passive_active(&game,0));
}
static void smoke(void){
 setup(DOTA_HERO_AXE);dota_rules_set_position(&game,1,2250,2008);ward(0,ITEM_UNIT_SENTRY,2200,2008,100);dota_items_apply_effect(&game,1,1,IE_INVISIBLE,900,2,DOTA_ITEM_SMOKE_OF_DECEIT);CHECK(!dota_unit_visible(&game,0,1));dota_items_apply_effect(&game,0,1,IE_REVEAL,100,1,DOTA_ITEM_DUST);CHECK(!dota_unit_visible(&game,0,1));dota_item_world_tick(&game);CHECK(!dota_items_invisible(&game,1));CHECK(dota_unit_visible(&game,0,1));
}
static void cone(void){setup(DOTA_HERO_SNIPER);game.units[0].learned[2]=4;game.roster.hero[0].generation=game.units[0].generation;game.roster.hero[0].aim_until=game.tick+100;game.roster.hero[0].vision_dx=1;game.roster.hero[0].vision_dy=0;dota_sight_invalidate();
 CHECK(dota_sight_team_position_visible(&game,0,2008,2008));CHECK(!dota_sight_team_position_visible(&game,0,1900,2008));CHECK(!dota_sight_team_position_visible(&game,0,2008,2300));CHECK(dota_sight_team_position_visible(&game,0,2600,2008));
 game.roster.hero[0].aim_until=0;dota_sight_invalidate();CHECK(dota_sight_team_position_visible(&game,0,1900,2008));CHECK(!dota_sight_team_position_visible(&game,0,2600,2008));
}
static void bitset_equivalence(void){unsigned seed=9127;int trial,i,team;
 setup(DOTA_HERO_AXE);
 for(trial=0;trial<6;++trial){int x=400+trial*590,y=500+trial*480;dota_rules_set_position(&game,0,x,y);dota_rules_set_position(&game,1,3500-x/2,3300-y/2);
  for(i=0;i<2048;++i){int xx,yy;seed=seed*1664525u+1013904223u;xx=(int)((seed>>16)&255u)*16+8;seed=seed*1664525u+1013904223u;yy=(int)((seed>>16)&255u)*16+8;for(team=0;team<2;++team)CHECK((dota_sight_fog_state(&game,team,xx,yy)==2)==dota_sight_team_position_visible(&game,team,xx,yy));}
 }
}
static void ward_attack(void){int i,w;DotaInput input;memset(&input,0,sizeof(input));setup(DOTA_HERO_AXE);dota_rules_set_position(&game,0,2190,2008);dota_rules_set_position(&game,1,3000,2008);w=ward(1,ITEM_UNIT_OBSERVER,2200,2008,1000);game.units[DOTA_ITEM_UNIT_FIRST+1]=game.units[w];game.units[DOTA_ITEM_UNIT_FIRST+1].team=0;game.item_world.unit[1]=game.item_world.unit[0];game.item_world.unit[1].kind=ITEM_UNIT_SENTRY;game.item_world.unit[1].owner=0;dota_sight_invalidate();
 dota_rules_item_damage(&game,0,w,900,0);CHECK(game.units[w].hp==200);dota_rules_item_damage(&game,0,w,900,1);CHECK(game.units[w].hp==200);game.units[0].attack_wait=0;dota_rules_attack_once(&game,0,w);for(i=0;i<12;++i)dota_tick(&game,&input);CHECK(game.units[w].hp==100);CHECK(game.units[w].active);
 CHECK(dota_item_world_incoming_from(&game,w,0,700,0,1)==100);game.units[0].hp=0;CHECK(dota_item_world_incoming_from(&game,w,0,700,0,1)==100);
}
static void failure_modes(void){setup(DOTA_HERO_AXE);dota_sight_detach();CHECK(dota_sight_fog_state(&game,0,2008,2008)==0);CHECK(!dota_unit_visible(&game,0,1));CHECK(dota_unit_visible(&game,0,0));CHECK(!dota_sight_attach(height,trees,(unsigned char*)memory+1,memory_bytes));CHECK(!dota_sight_attach(height,trees,memory,memory_bytes-1));CHECK(dota_sight_attach(0,0,memory,memory_bytes));CHECK(dota_unit_visible(&game,0,1));}
int main(int argc,char **argv){clock_t start;int i;
 if(argc!=2 || !baked_io_open(argv[1]) || !item_assets_open())return 2;
 memory_bytes=dota_sight_workspace_bytes();memory=malloc(memory_bytes);if(!memory)return 3;
 flat_ranges();terrain();wards();shared_pit();slark();smoke();cone();bitset_equivalence();ward_attack();failure_modes();
 setup(DOTA_HERO_AXE);start=clock();for(i=0;i<200;++i){++game.tick;(void)dota_sight_fog_state(&game,0,2008,2008);}printf("{\"passed\":%s,\"checks\":%d,\"failures\":%d,\"heap_bytes_host\":%u,\"stationary_200_tick_host_ms\":%.3f,\"game_bytes\":%u,\"physical_h1_tested\":false}\n",failures?"false":"true",checks,failures,memory_bytes,(double)(clock()-start)*1000/CLOCKS_PER_SEC,(unsigned)sizeof(game));
 dota_sight_detach();free(memory);item_assets_close();baked_io_close();return failures?1:0;
}
