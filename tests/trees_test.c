#include "../game/game.h"
#include "../game/trees.h"
#include "../game/navigation.h"
#include "../game/sight.h"
#include "../game/items.h"
#include "../game/items_actions.h"
#include "../game/items_internal.h"
#include "../game/extra_heroes.h"
#include "../platform/baked_io.h"
#include "../platform/item_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DotaGame g,copy;
static uint16_t xy[DOTA_STATIC_TREE_COUNT*2],fake[DOTA_STATIC_TREE_COUNT*2];
static unsigned char before[65536],allclear[65536],living[65536],vision_tree[8192];
static int16_t heights[65536];
static int checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"line%d: %s\n",__LINE__,#x);}}while(0)
static int load(const char *path,void *p,unsigned n){FILE*f=fopen(path,"rb");int ok=f&&fread(p,1,n,f)==n;if(f)fclose(f);return ok;}
static void init(void){int i;dota_init(&g,DOTA_HERO_AXE,71);g.tick=2000;for(i=1;i<DOTA_MAX_UNITS;++i)g.units[i].active=0;dota_nav_set_all_structures(0,0,0);dota_nav_set_static_trees(&g);}
static void lifetime(void){int x,y;unsigned rev;DotaInput in;memset(&in,0,sizeof(in));init();CHECK(dota_trees_position(0,&x,&y));CHECK(x==xy[0]&&y==xy[1]);CHECK(dota_trees_pick(&g,x,y,0)==0);CHECK(!dota_trees_cut(&g,DOTA_STATIC_TREE_COUNT));CHECK(dota_trees_cut(&g,0));CHECK(g.trees.cut_count==1&&dota_trees_is_cut(&g,0));rev=g.trees.revision;CHECK(!dota_trees_cut(&g,0)&&g.trees.revision==rev);copy=g;g.tick+=DOTA_TREE_REGROW_TICKS-1;dota_trees_tick(&g);CHECK(dota_trees_is_cut(&g,0));++g.tick;dota_trees_tick(&g);CHECK(!g.trees.cut_count&&!dota_trees_is_cut(&g,0)&&g.trees.revision==rev+1);CHECK(dota_trees_is_cut(&copy,0));dota_trees_tick(&g);CHECK(g.trees.revision==rev+1);
 init();g.tick=65530;CHECK(dota_trees_cut(&g,2112));g.tick+=3599;dota_trees_tick(&g);CHECK(dota_trees_is_cut(&g,2112));++g.tick;dota_trees_tick(&g);CHECK(!dota_trees_is_cut(&g,2112));
 init();CHECK(dota_trees_cut(&g,0));dota_set_paused(&g,1);rev=g.tick;dota_tick(&g,&in);CHECK((unsigned)g.tick==rev&&dota_trees_is_cut(&g,0));dota_restart(&g);CHECK(!g.trees.cut_count&&!dota_trees_is_cut(&g,0));
}
static void navigation(void){unsigned i;init();dota_nav_set_all_structures((1u<<22)-1,(1u<<14)-1,4095);for(i=0;i<65536;++i)CHECK(dota_nav_walkable((i%256)*16+8,(i/256)*16+8)==before[i]);
 dota_nav_set_all_structures(0,0,0);for(i=0;i<DOTA_STATIC_TREE_COUNT;++i)CHECK(dota_trees_cut(&g,i));CHECK(g.trees.cut_count==DOTA_STATIC_TREE_COUNT);for(i=0;i<65536;++i)CHECK(dota_nav_walkable((i%256)*16+8,(i/256)*16+8)==allclear[i]);
 dota_nav_set_all_structures((1u<<22)-1,(1u<<14)-1,4095);for(i=0;i<65536;++i)CHECK(dota_nav_walkable((i%256)*16+8,(i/256)*16+8)==living[i]);
 g.tick+=3600;dota_trees_tick(&g);CHECK(!g.trees.cut_count);for(i=0;i<65536;++i)CHECK(dota_nav_walkable((i%256)*16+8,(i/256)*16+8)==before[i]);
}
static void items(void){int ids[3]={DOTA_ITEM_TANGO,DOTA_ITEM_QUELLING_BLADE,DOTA_ITEM_BFURY},i,j,x,y;
 for(i=0;i<3;++i){int slot=-1,charges,cd;init();dota_trees_position(400,&x,&y);dota_rules_set_position(&g,0,x+16,y);CHECK(dota_items_receive(&g,0,ids[i],201));for(j=0;j<6;++j)if(g.units[0].inventory[j]==ids[i])slot=j;CHECK(slot>=0);if(slot<0)continue;
 CHECK(dota_use_item_at(&g,slot,x,y,-1));CHECK(dota_trees_is_cut(&g,400));if(ids[i]==DOTA_ITEM_TANGO){CHECK(g.items.charges[0][slot]==2);CHECK(dota_items_timed_regen100(&g,0)==700);for(j=0;j<DOTA_ITEMS_EFFECTS;++j)if(g.items.effects[j].ticks&&g.items.effects[j].tag==DOTA_ITEM_TANGO)CHECK(g.items.effects[j].ticks==320);}else CHECK(g.items.cooldown[0][slot]>0);
 charges=g.items.charges[0][slot];g.items.cooldown[0][slot]=0;cd=g.items.cooldown[0][slot];CHECK(!dota_use_item_at(&g,slot,x,y,-1));CHECK(g.items.charges[0][slot]==charges&&g.items.cooldown[0][slot]==cd);
 }
 init();dota_rules_set_position(&g,0,2000,2000);g.item_world.tree[0].x=2016;g.item_world.tree[0].y=2000;g.item_world.tree[0].until=g.tick+400;g.item_world.tree[0].source_item=DOTA_ITEM_BRANCHES;CHECK(dota_items_receive(&g,0,DOTA_ITEM_TANGO,201));CHECK(dota_use_item_at(&g,0,2016,2000,-1));CHECK(g.item_world.tree[0].removed);for(j=0;j<DOTA_ITEMS_EFFECTS;++j)if(g.items.effects[j].ticks&&g.items.effects[j].tag==DOTA_ITEM_TANGO)CHECK(g.items.effects[j].ticks==640);
}
static void overlap(void){unsigned i,bytes=dota_sight_workspace_bytes();void*mem=malloc(bytes);CHECK(mem!=0);if(!mem)return;
 for(i=0;i<DOTA_STATIC_TREE_COUNT;++i){fake[i*2]=4000;fake[i*2+1]=4000;}fake[0]=2048;fake[1]=2008;fake[2]=2048;fake[3]=2008;CHECK(dota_trees_attach(fake,DOTA_STATIC_TREE_COUNT));CHECK(dota_sight_attach(heights,vision_tree,mem,bytes));init();dota_rules_set_position(&g,0,2008,2008);g.units[1]=g.units[0];g.units[1].x=2104;g.units[1].team=1;g.units[1].active=1;CHECK(!dota_sight_team_position_visible(&g,0,2104,2008));CHECK(!dota_nav_walkable(2040,2008));CHECK(dota_trees_cut(&g,0));CHECK(!dota_nav_walkable(2040,2008));CHECK(!dota_sight_team_position_visible(&g,0,2104,2008));CHECK(dota_trees_cut(&g,1));CHECK(dota_nav_walkable(2040,2008));CHECK(dota_sight_team_position_visible(&g,0,2104,2008));g.tick+=3600;dota_trees_tick(&g);CHECK(!dota_nav_walkable(2040,2008));CHECK(!dota_sight_team_position_visible(&g,0,2104,2008));
 /* Clicking inside range must not select a tree whose centre is outside it. */
 init();dota_rules_set_position(&g,0,2000,2008);CHECK(dota_items_receive(&g,0,DOTA_ITEM_TANGO,201));CHECK(!dota_use_item_at(&g,0,2040,2008,-1));CHECK(g.items.charges[0][0]==3&&!g.items.cooldown[0][0]&&!g.trees.cut_count);
 /* The public actual Lina cast cuts both overlapped IDs, not only a visual. */
 dota_init(&g,DOTA_HERO_LINA,71);g.tick=2000;for(i=1;i<DOTA_MAX_UNITS;++i)g.units[i].active=0;dota_rules_set_position(&g,0,2008,2008);g.units[0].learned[1]=1;g.units[0].mana=0;CHECK(!dota_cast(&g,1,2048,2008));CHECK(!g.trees.cut_count);g.units[0].mana=1000;CHECK(dota_cast(&g,1,2048,2008));CHECK(dota_trees_is_cut(&g,0)&&dota_trees_is_cut(&g,1)&&g.trees.cut_count==2);
 /* Pounce clears its original120-Source landing circle on natural expiry
  * and on an early hero collision, before nearest-point navigation. */
 for(i=0;i<2;++i){DotaInput input;unsigned n;memset(&input,0,sizeof(input));dota_init(&g,DOTA_HERO_SLARK,71);g.tick=2000;for(n=1;n<DOTA_MAX_UNITS;++n)g.units[n].active=0;dota_rules_set_position(&g,0,1873,2008);g.units[0].learned[1]=1;g.units[0].mana=1000;
  if(i){g.units[1]=g.units[0];g.units[1].hero_id=DOTA_HERO_AXE;g.units[1].team=1;g.units[1].x=2055;g.units[1].y=2008;g.units[1].speed=0;g.units[1].attack_wait=100000;g.units[1].active=1;}
  CHECK(dota_cast(&g,1,2048,2008));for(n=0;n<18;++n)dota_tick(&g,&input);CHECK(dota_trees_is_cut(&g,0)&&dota_trees_is_cut(&g,1));CHECK(!g.extra.hero[0].leap_at);if(i)CHECK(g.extra.hero[1].leash_until>g.tick);
 }
 dota_sight_detach();free(mem);CHECK(dota_trees_attach(xy,DOTA_STATIC_TREE_COUNT));
}
int main(int argc,char**argv){if(argc!=6||!baked_io_open(argv[1])||!item_assets_open()||!load(argv[2],xy,sizeof(xy))||!load(argv[3],before,sizeof(before))||!load(argv[4],allclear,sizeof(allclear))||!load(argv[5],living,sizeof(living)))return 2;CHECK(dota_trees_attach(xy,DOTA_STATIC_TREE_COUNT));CHECK(sizeof(DotaTreesState)==4500);CHECK(sizeof(DotaGame)==178044);lifetime();navigation();items();overlap();dota_trees_detach();item_assets_close();baked_io_close();printf("{\"passed\":%s,\"checks\":%d,\"failures\":%d,\"game_bytes\":%u,\"tree_state_bytes\":%u,\"host_only\":true}\n",failures?"false":"true",checks,failures,(unsigned)sizeof(g),(unsigned)sizeof(g.trees));return failures?1:0;}
