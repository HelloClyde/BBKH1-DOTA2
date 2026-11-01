#include "../game/game.h"
#include "../game/item_world.h"
#include "../game/items.h"
#include "../game/items_ids.h"
#include "../game/items_actions.h"
#include "../game/items_internal.h"
#include "../game/navigation.h"
#include "../game/extra_heroes.h"
#include "../game/ward_placement_data.h"
#include "../platform/baked_io.h"
#include "../platform/item_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DotaGame g;
static int checks,failures;
static int16_t heights[65536];static unsigned char trees[8192];
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void setup(int team,int x,int y,int kind){int i;dota_init(&g,DOTA_HERO_AXE,73);for(i=1;i<DOTA_MAX_UNITS;++i)g.units[i].active=0;g.units[0].team=team;dota_rules_set_position(&g,0,x,y);
 CHECK(dota_items_receive(&g,0,kind==1?DOTA_ITEM_WARD_SENTRY:DOTA_ITEM_WARD_OBSERVER,201));if(kind==2)CHECK(dota_items_receive(&g,0,DOTA_ITEM_WARD_SENTRY,201));}
static void denied(int team,int x,int y,int kind){
 DotaUnit before_units[DOTA_ITEM_UNIT_COUNT];DotaItemWorldUnit before_world[DOTA_ITEM_UNIT_COUNT];int item,id,cd,charges;
 setup(team,x<4000?x+64:x-64,y,kind);item=g.units[0].inventory[0];cd=g.items.cooldown[0][0];charges=g.items.charges[0][0];memcpy(before_units,&g.units[DOTA_ITEM_UNIT_FIRST],sizeof(before_units));memcpy(before_world,g.item_world.unit,sizeof(before_world));
 CHECK(!dota_item_world_ward_placeable(&g,0,x,y));CHECK(!dota_use_item_at(&g,0,x,y,kind?DOTA_WARD_TARGET_SENTRY:-1));
 CHECK(g.units[0].inventory[0]==item&&g.items.charges[0][0]==charges&&g.items.cooldown[0][0]==cd);
 id=kind==1?DOTA_ITEM_WARD_SENTRY:DOTA_ITEM_WARD_OBSERVER;CHECK(!dota_rules_item_primitive(&g,ITEM_ACTION_WARD|(id<<8),0,-1,x,y,kind==1,7200));
 CHECK(!memcmp(before_units,&g.units[DOTA_ITEM_UNIT_FIRST],sizeof(before_units)));CHECK(!memcmp(before_world,g.item_world.unit,sizeof(before_world)));
}
static void zones(void){int seen[5]={0},h,team,kind;
 for(h=0;h<DOTA_WARD_FORBIDDEN_HULLS;++h){int first=dota_ward_forbidden_offsets[h],end=dota_ward_forbidden_offsets[h+1],i,entity=dota_ward_forbidden_entity[h],x=0,y=0;if(seen[entity])continue;seen[entity]=1;
  for(i=first;i<end;++i){x+=dota_ward_forbidden_q16[i][0]/(end-first);y+=dota_ward_forbidden_q16[i][1]/(end-first);}x/=65536;y/=65536;
  for(team=0;team<2;++team)for(kind=0;kind<3;++kind)denied(team,x,y,kind);
 }
 for(h=0;h<5;++h)CHECK(seen[h]);
 setup(0,2000,2000,0);CHECK(!dota_item_world_ward_placeable(&g,0,dota_fountains[0].x,dota_fountains[0].y));CHECK(!dota_item_world_ward_placeable(&g,0,dota_fountains[1].x,dota_fountains[1].y));
 CHECK(!dota_item_world_ward_placeable(&g,0,64,3472));CHECK(dota_item_world_ward_placeable(&g,0,495,3473));
 /* Original float projection excludes this point; Q8 incorrectly blocked it. */
 CHECK(dota_item_world_ward_placeable(&g,0,1307,1351));
}
static void invalid(void){setup(0,4095,2000,0);CHECK(!dota_use_item_at(&g,0,4096,2000,-1));CHECK(g.items.charges[0][0]==1&&g.items.cooldown[0][0]==0);CHECK(!dota_item_world_ward_placeable(&g,0,-1,2000));CHECK(!dota_item_world_ward_placeable(&g,0,2000,4096));CHECK(!dota_item_world_ward_placeable(0,0,2000,2000));CHECK(!dota_item_world_ward_placeable(&g,-1,2000,2000));CHECK(!dota_item_world_ward_placeable(&g,10,2000,2000));g.units[0].hp=0;CHECK(!dota_item_world_ward_placeable(&g,0,2000,2000));}
static void cliff(void){int cx,cy,dx,dy,x=0,y=0,ox=0,oy=0;
 setup(0,2000,2000,0);
 for(cy=8;cy<248&&!x;++cy)for(cx=8;cx<248&&!x;++cx){int at=cy*256+cx,xx=cx*16+8,yy=cy*16+8;
  if(heights[at]<64||((trees[at>>3]>>(at&7))&1)||dota_nav_walkable(xx,yy)||!dota_item_world_ward_placeable(&g,0,xx,yy))continue;
  for(dy=-5;dy<=5&&!x;++dy)for(dx=-5;dx<=5&&!x;++dx)if(dx*dx+dy*dy<=25&&dota_nav_walkable(xx+dx*16,yy+dy*16)){x=xx;y=yy;ox=xx+dx*16;oy=yy+dy*16;}
 }
 CHECK(x>0);if(!x)return;setup(0,ox,oy,0);CHECK(!dota_nav_walkable(x,y));CHECK(dota_item_world_ward_placeable(&g,0,x,y));CHECK(dota_use_item_at(&g,0,x,y,-1));CHECK(g.units[DOTA_ITEM_UNIT_FIRST].active&&g.units[DOTA_ITEM_UNIT_FIRST].x==x&&g.units[DOTA_ITEM_UNIT_FIRST].y==y);CHECK(g.units[0].inventory[0]<0&&g.items.charges[0][0]==0);
 printf("CLIFF %d %d OWNER %d %d\n",x,y,ox,oy);
}
int main(int argc,char **argv){FILE *f;unsigned char head[64];if(argc!=3||!baked_io_open(argv[1])||!item_assets_open())return 2;f=fopen(argv[2],"rb");if(!f||fread(head,1,64,f)!=64||fread(heights,1,sizeof(heights),f)!=sizeof(heights)||fread(trees,1,sizeof(trees),f)!=sizeof(trees))return 3;fclose(f);zones();invalid();cliff();printf("RESULT {\"passed\":%s,\"checks\":%d,\"failures\":%d,\"original_entities\":5,\"convex_hulls\":7,\"fixed_point_bits\":16,\"game_bytes\":%u,\"physical_H1_tested\":false}\n",failures?"false":"true",checks,failures,(unsigned)sizeof(g));item_assets_close();baked_io_close();return failures?1:0;}
