/* Initial configurations and independently computed full-host tree outcomes. */
#include "game.h"
#include "trees.h"
#include "navigation.h"
#include "sight.h"
#include "extra_heroes.h"
#include "items.h"
#include "items_internal.h"
#include "baked_io.h"
#include "item_assets.h"
#include "../platform/render.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DotaGame g;static uint16_t xy[DOTA_STATIC_TREE_COUNT*2];static const char *out;
static int16_t height[65536];static uint8_t mask[8192];static void *work;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"fixture line%d %s\n",__LINE__,#x);exit(2);}}while(0)
static void file(const char *name,const void *bytes,unsigned count){char path[2048];FILE*f;snprintf(path,sizeof(path),"%s/%s",out,name);f=fopen(path,"wb");CHECK(f&&fwrite(bytes,1,count,f)==count);CHECK(!fclose(f));}
static void save(const char *name){char n[80];snprintf(n,sizeof(n),"%s.bin",name);file(n,&g,sizeof(g));}
static void tree_result(const char *name){char n[80];snprintf(n,sizeof(n),"%s.trees",name);file(n,&g.trees,sizeof(g.trees));}
static void setup(int hero){int i;dota_init(&g,hero,71);g.tick=2000;for(i=1;i<DOTA_MAX_UNITS;++i)g.units[i].active=0;g.units[0].attack_wait=100000;dota_rules_set_position(&g,0,xy[800]+16,xy[801]);dota_nav_set_all_structures(0,0,0);}
static int regen(void){int i,n=0;for(i=0;i<DOTA_ITEMS_EFFECTS;++i)if(g.items.effects[i].ticks&&g.items.effects[i].tag==DOTA_ITEM_TANGO&&g.items.effects[i].ticks>n)n=g.items.effects[i].ticks;return n;}
static void values(const char *name,int first,int second){printf("\"%s\":[%d,%d,%d,%d,%d,%d,%u,%u]",name,first,second,dota_trees_is_cut(&g,400),g.items.charges[0][0],g.items.cooldown[0][0],regen(),g.trees.cut_count,g.trees.revision);tree_result(name);}
static void tick(void){DotaInput input;memset(&input,0,sizeof(input));dota_tick(&g,&input);}
int main(int argc,char**argv){FILE*f;unsigned char header[64];int i,a,b,x,y;int ids[3]={DOTA_ITEM_TANGO,DOTA_ITEM_QUELLING_BLADE,DOTA_ITEM_BFURY};const char*names[3]={"tango","quelling","battlefury"};
 CHECK(argc==5&&sizeof(g)==179008&&sizeof(DotaUnit)==240);out=argv[1];CHECK(baked_io_open(argv[2])&&item_assets_open());f=fopen(argv[3],"rb");CHECK(f&&fread(xy,1,sizeof(xy),f)==sizeof(xy)&&!fclose(f));CHECK(dota_trees_attach(xy,DOTA_STATIC_TREE_COUNT));f=fopen(argv[4],"rb");CHECK(f&&fread(header,1,64,f)==64&&fread(height,1,sizeof(height),f)==sizeof(height)&&fread(mask,1,sizeof(mask),f)==sizeof(mask)&&!fclose(f));work=malloc(dota_sight_workspace_bytes());CHECK(work&&dota_sight_attach(height,mask,work,dota_sight_workspace_bytes()));
 printf("{\"game_size\":%u,\"view_size\":%u,\"unit_size\":%u,\"input_size\":%u,\"tree_size\":%u,\"tree_offset\":%u,\"tree_count\":%u,\"tree_revision\":%u,\"units\":%u,\"charges\":%u,\"cooldown\":%u,\"effects\":%u,\"effect_size\":%u,\"effect_ticks\":%u,\"effect_tag\":%u,\"temporary_tree_offset\":%u,\"tango_id\":%d,\"target\":[%d,%d],\"expected\":{",(unsigned)sizeof(g),(unsigned)sizeof(DotaView),(unsigned)sizeof(DotaUnit),(unsigned)sizeof(DotaInput),(unsigned)sizeof(g.trees),(unsigned)offsetof(DotaGame,trees),(unsigned)offsetof(DotaTreesState,cut_count),(unsigned)offsetof(DotaTreesState,revision),(unsigned)offsetof(DotaGame,units),(unsigned)(offsetof(DotaGame,items)+offsetof(DotaItemsState,charges)),(unsigned)(offsetof(DotaGame,items)+offsetof(DotaItemsState,cooldown)),(unsigned)(offsetof(DotaGame,items)+offsetof(DotaItemsState,effects)),(unsigned)sizeof(DotaItemEffect),(unsigned)offsetof(DotaItemEffect,ticks),(unsigned)offsetof(DotaItemEffect,tag),(unsigned)(offsetof(DotaGame,item_world)+offsetof(DotaItemWorldState,tree)),DOTA_ITEM_TANGO,xy[800],xy[801]);
 for(i=0;i<3;++i){setup(DOTA_HERO_AXE);CHECK(dota_items_receive(&g,0,ids[i],201));save(names[i]);a=dota_use_item_at(&g,0,xy[800],xy[801],-1);b=dota_use_item_at(&g,0,xy[800],xy[801],-1);CHECK(a&&!b&&dota_trees_is_cut(&g,400)&&g.trees.cut_count==1);if(i)printf(",");values(names[i],a,b);}
 setup(DOTA_HERO_AXE);dota_rules_set_position(&g,0,2000,2000);g.item_world.tree[0].x=2016;g.item_world.tree[0].y=2000;g.item_world.tree[0].until=g.tick+400;g.item_world.tree[0].source_item=DOTA_ITEM_BRANCHES;CHECK(dota_items_receive(&g,0,DOTA_ITEM_TANGO,201));save("branch");a=dota_use_item_at(&g,0,2016,2000,-1);CHECK(a&&g.item_world.tree[0].removed&&regen()==640&&!g.trees.cut_count);printf(",");values("branch",a,0);
 setup(DOTA_HERO_AXE);dota_rules_set_position(&g,0,xy[800]+48,xy[801]);CHECK(dota_items_receive(&g,0,DOTA_ITEM_TANGO,201));save("out_of_range");a=dota_use_item_at(&g,0,xy[800]+8,xy[801],-1);CHECK(!a&&g.items.charges[0][0]==3&&!g.trees.cut_count);printf(",");values("out_of_range",a,0);
 setup(DOTA_HERO_LINA);g.units[0].learned[1]=1;g.units[0].mana=0;save("lina_no_mana");a=dota_cast(&g,1,xy[800],xy[801]);CHECK(!a&&!g.trees.cut_count);printf(",");values("lina_no_mana",a,0);
 setup(DOTA_HERO_LINA);g.units[0].learned[1]=1;g.units[0].mana=1000;save("lina");a=dota_cast(&g,1,xy[800],xy[801]);CHECK(a&&dota_trees_is_cut(&g,400));printf(",");values("lina",a,0);
 for(i=0;i<2;++i){const char*n=i?"wrap_regrow":"regrow";setup(DOTA_HERO_AXE);if(i)g.tick=65530;CHECK(dota_trees_cut(&g,400));g.tick+=3599;g.paused=1;save(n);a=dota_trees_is_cut(&g,400);tick();CHECK(g.paused&&dota_trees_is_cut(&g,400));dota_set_paused(&g,0);tick();b=dota_trees_is_cut(&g,400);CHECK(a&&!b&&!g.trees.cut_count);printf(",");values(n,a,b);}
 for(i=0;i<2;++i){const char*n=i?"pounce_impact":"pounce_landing";int k;setup(DOTA_HERO_SLARK);dota_rules_set_position(&g,0,xy[800]-175,xy[801]);g.units[0].learned[1]=1;g.units[0].mana=1000;
  if(i){g.units[1]=g.units[0];g.units[1].hero_id=DOTA_HERO_AXE;g.units[1].team=1;g.units[1].x=xy[800]+7;g.units[1].speed=0;g.units[1].attack_wait=100000;g.units[1].active=1;}
  save(n);a=dota_cast(&g,1,xy[800],xy[801]);for(k=0;k<18;++k)tick();CHECK(a&&dota_trees_is_cut(&g,400)&&!dota_extra_moving(&g,0));if(i)CHECK(g.extra.hero[1].leash_until>g.tick);printf(",");values(n,a,dota_extra_moving(&g,0));}
 /* Source terrain stays authoritative even after every static tree is cut. */
 setup(DOTA_HERO_AXE);save("all_cut");for(i=0;i<DOTA_STATIC_TREE_COUNT;++i)CHECK(dota_trees_cut(&g,(unsigned)i));x=xy[800];y=xy[801];a=dota_nav_walkable(x,y);b=dota_sight_team_position_visible(&g,0,x,y);printf(",\"all_cut\":[%d,%d,%u]",a,b,g.trees.cut_count);tree_result("all_cut");
 printf("}}\n");dota_sight_detach();free(work);dota_trees_detach();item_assets_close();baked_io_close();return 0;
}
