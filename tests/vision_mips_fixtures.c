/* Initial full-core fixtures and independent host outcome receipts. */
#include "game.h"
#include "sight.h"
#include "vision.h"
#include "extra_heroes.h"
#include "item_world.h"
#include "items.h"
#include "items_internal.h"
#include "items_actions.h"
#include "items_ids.h"
#include "roshan_data.h"
#include "navigation.h"
#include "ward_placement_data.h"
#include "baked_io.h"
#include "item_assets.h"
#include "../platform/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
static DotaGame g;
static int16_t heights[256*256];static uint8_t trees[8192];static void *work;
static const char *outdir;static int treepair[4],hillpair[4],cliffpair[4],blocked[5][2];
#define CHECK(x) do{if(!(x)){fprintf(stderr,"fixture %d: %s\n",__LINE__,#x);exit(2);}}while(0)
static int tree(int x,int y){int n=y*256+x;return (trees[n>>3]>>(n&7))&1;}
static int level(int x,int y){int h=heights[y*256+x];return h>=0?(h+16)/32:-((-h+16)/32);}
static int safe(int x,int y){int t;if(dota_roshan_region_at(x,y))return 0;for(t=0;t<2;t++){int dx=x-dota_fountains[t].x,dy=y-dota_fountains[t].y;if(dx*dx+dy*dy<700*700)return 0;}return 1;}
static void setup(int hero,int x,int y,int xx,int yy){int i;dota_init(&g,hero,20261101);g.tick=2000;for(i=2;i<DOTA_MAX_UNITS;++i)g.units[i].active=0;g.units[0].attack_wait=100000;g.units[1].team=1;g.units[1].speed=0;g.units[1].attack_wait=100000;for(i=0;i<4;i++)g.units[1].learned[i]=0;dota_rules_set_position(&g,0,x,y);dota_rules_set_position(&g,1,xx,yy);}
static void save(const char *name){char p[2048];FILE *f;snprintf(p,sizeof(p),"%s/%s.bin",outdir,name);f=fopen(p,"wb");CHECK(f&&fwrite(&g,1,sizeof(g),f)==sizeof(g));CHECK(!fclose(f));}
static int ward(int slot,int team,int kind,int x,int y,int life){int i=DOTA_ITEM_UNIT_FIRST+slot;DotaUnit *u=&g.units[i];DotaItemWorldUnit *s=&g.item_world.unit[slot];memset(u,0,sizeof(*u));memset(s,0,sizeof(*s));u->active=1;u->type=DOTA_SUMMON;u->team=team;u->hero_id=0;u->hp=u->max_hp=200;u->generation=1;u->x=x;u->y=y;u->target=-1;s->owner=team?1:0;s->generation=g.units[s->owner].generation;s->kind=kind;s->until=g.tick+life;s->source_item=kind==ITEM_UNIT_SENTRY?DOTA_ITEM_WARD_SENTRY:DOTA_ITEM_WARD_OBSERVER;s->incoming_percent=100;dota_sight_invalidate();return i;}
static void tick(int n){DotaInput input;memset(&input,0,sizeof(input));while(n--)dota_tick(&g,&input);}
static void discover(void){int x,y,k,ok;
 for(y=45;y<210&&!treepair[0];y++)for(x=45;x<210&&!treepair[0];x++)if(tree(x,y)&&safe((x-3)*16+8,y*16+8)&&safe((x+3)*16+8,y*16+8)){
  ok=!tree(x-3,y)&&!tree(x+3,y)&&heights[y*256+x-3]!=-32768;for(k=-3;k<=3;k++)if(level(x+k,y)!=level(x-3,y))ok=0;
  if(ok){setup(0,(x-3)*16+8,y*16+8,(x+3)*16+8,y*16+8);if(!dota_unit_visible(&g,0,1)){treepair[0]=g.units[0].x;treepair[1]=g.units[0].y;treepair[2]=g.units[1].x;treepair[3]=g.units[1].y;}}
 }
 for(y=45;y<210&&!hillpair[0];y++)for(x=45;x<210&&!hillpair[0];x++)if(safe((x-3)*16+8,y*16+8)&&safe((x+3)*16+8,y*16+8)&&level(x+3,y)>level(x-3,y)&&heights[y*256+x-3]!=-32768&&heights[y*256+x+3]!=-32768){
  ok=1;for(k=-3;k<=3;k++)if(tree(x+k,y))ok=0;
  if(ok){setup(0,(x-3)*16+8,y*16+8,(x+3)*16+8,y*16+8);if(!dota_unit_visible(&g,0,1)){setup(0,(x+3)*16+8,y*16+8,(x-3)*16+8,y*16+8);if(dota_unit_visible(&g,0,1)){hillpair[0]=g.units[1].x;hillpair[1]=g.units[1].y;hillpair[2]=g.units[0].x;hillpair[3]=g.units[0].y;}}}
 }
 CHECK(treepair[0]&&hillpair[0]);
 for(y=8;y<248&&!cliffpair[0];++y)for(x=8;x<248&&!cliffpair[0];++x){int dx,dy,xx=x*16+8,yy=y*16+8;if(heights[y*256+x]<64||tree(x,y)||dota_nav_walkable(xx,yy)||!dota_item_world_ward_placeable(&g,0,xx,yy))continue;
  for(dy=-5;dy<=5&&!cliffpair[0];++dy)for(dx=-5;dx<=5&&!cliffpair[0];++dx)if(dx*dx+dy*dy<=25&&dota_nav_walkable(xx+dx*16,yy+dy*16)){cliffpair[0]=xx;cliffpair[1]=yy;cliffpair[2]=xx+dx*16;cliffpair[3]=yy+dy*16;}}
 CHECK(cliffpair[0]);
 for(x=0;x<DOTA_WARD_FORBIDDEN_HULLS;++x){int entity=dota_ward_forbidden_entity[x],first=dota_ward_forbidden_offsets[x],end=dota_ward_forbidden_offsets[x+1],i;if(blocked[entity][0])continue;for(i=first;i<end;++i){blocked[entity][0]+=dota_ward_forbidden_q16[i][0]/(end-first);blocked[entity][1]+=dota_ward_forbidden_q16[i][1]/(end-first);}blocked[entity][0]/=65536;blocked[entity][1]/=65536;}
}
int main(int argc,char **argv){FILE *f;unsigned char header[64];int a,b,c,d,e,w;int hx,hy,lx,ly;
 CHECK(sizeof(DotaGame)==178044 && sizeof(DotaUnit)==240 && sizeof(DotaView)==112);
 CHECK(argc==4);outdir=argv[1];CHECK(baked_io_open(argv[2])&&item_assets_open());f=fopen(argv[3],"rb");CHECK(f&&fread(header,1,64,f)==64&&fread(heights,1,sizeof(heights),f)==sizeof(heights)&&fread(trees,1,sizeof(trees),f)==sizeof(trees));CHECK(!fclose(f));work=malloc(dota_sight_workspace_bytes());CHECK(work&&dota_sight_attach(heights,trees,work,dota_sight_workspace_bytes()));discover();lx=hillpair[0];ly=hillpair[1];hx=hillpair[2];hy=hillpair[3];
 printf("{\"game_size\":%u,\"unit_size\":%u,\"input_size\":%u,\"units\":%u,\"item_first\":%d,\"world_stride\":%u,\"world_first\":%u,\"world_kind\":%u,\"world_until\":%u,\"charges\":%u,\"unit_active\":%u,\"unit_hp\":%u,\"unit_inventory\":%u,\"tree_pair\":[%d,%d,%d,%d],\"hill_pair\":[%d,%d,%d,%d],",(unsigned)sizeof(g),(unsigned)sizeof(DotaUnit),(unsigned)sizeof(DotaInput),(unsigned)offsetof(DotaGame,units),DOTA_ITEM_UNIT_FIRST,(unsigned)sizeof(DotaItemWorldUnit),(unsigned)(offsetof(DotaGame,item_world)+offsetof(DotaItemWorldState,unit)),(unsigned)offsetof(DotaItemWorldUnit,kind),(unsigned)offsetof(DotaItemWorldUnit,until),(unsigned)(offsetof(DotaGame,items)+offsetof(DotaItemsState,charges)),(unsigned)offsetof(DotaUnit,active),(unsigned)offsetof(DotaUnit,hp),(unsigned)offsetof(DotaUnit,inventory),treepair[0],treepair[1],treepair[2],treepair[3],lx,ly,hx,hy);
 printf("\"unit_x\":%u,\"unit_y\":%u,\"item_cooldown\":%u,\"ward_primitive_encoded\":%d,\"blocked_positions\":[",(unsigned)offsetof(DotaUnit,x),(unsigned)offsetof(DotaUnit,y),(unsigned)(offsetof(DotaGame,items)+offsetof(DotaItemsState,cooldown)),ITEM_ACTION_WARD|(DOTA_ITEM_WARD_OBSERVER<<8));for(a=0;a<5;++a)printf("%s[%d,%d]",a?",":"",blocked[a][0],blocked[a][1]);printf("],\"cliff_pair\":[%d,%d,%d,%d],\"expected\":{",cliffpair[0],cliffpair[1],cliffpair[2],cliffpair[3]);
 setup(0,hx,hy,4000,4000);CHECK(dota_items_receive(&g,0,DOTA_ITEM_WARD_OBSERVER,201)&&dota_items_receive(&g,0,DOTA_ITEM_WARD_SENTRY,201));CHECK(g.units[0].inventory[0]==DOTA_ITEM_WARD_DISPENSER&&g.items.charges[0][0]==257);save("combined");
 a=dota_use_item_at(&g,0,hx,hy,DOTA_WARD_TARGET_SENTRY);b=g.items.charges[0][0];c=dota_use_item_at(&g,0,hx,hy,DOTA_WARD_TARGET_SENTRY);d=dota_use_item_at(&g,0,hx,hy,-1);e=g.items.charges[0][0];CHECK(a==1&&b==1&&c==0&&d==1&&e==0&&g.units[0].inventory[0]==-1);CHECK(g.item_world.unit[0].kind==ITEM_UNIT_SENTRY&&g.item_world.unit[1].kind==ITEM_UNIT_OBSERVER);printf("\"combined\":[%d,%d,%d,%d,%d,%d,%d,%d]",a,b,c,d,e,g.units[0].inventory[0],g.item_world.unit[0].until,g.item_world.unit[1].until);
 setup(0,treepair[0],treepair[1],treepair[2],treepair[3]);save("tree");a=dota_unit_visible(&g,0,1);setup(0,lx,ly,hx,hy);save("hill_low");b=dota_unit_visible(&g,0,1);setup(0,hx,hy,lx,ly);save("hill_high");c=dota_unit_visible(&g,0,1);CHECK(!a&&!b&&c);printf(",\"terrain\":[%d,%d,%d]",a,b,c);
 setup(0,2008,2008,dota_roshan_pit_positions[0][0],dota_roshan_pit_positions[0][1]);save("pit_unlit");a=dota_unit_visible(&g,0,1);c=dota_item_world_ward_placeable(&g,0,g.units[1].x,g.units[1].y);g.units[2].active=1;g.units[2].team=0;dota_rules_set_position(&g,2,g.units[1].x,g.units[1].y);save("pit_lit");b=dota_unit_visible(&g,0,1);CHECK(!a&&b&&!c);printf(",\"pit\":[%d,%d,%d]",a,b,c);
 setup(0,lx,ly,lx<2000?lx+800:lx-800,ly);ward(0,0,ITEM_UNIT_OBSERVER,g.units[1].x,g.units[1].y,1);save("lifetime");a=dota_unit_visible(&g,0,1);tick(2);b=dota_unit_visible(&g,0,1);c=g.units[DOTA_ITEM_UNIT_FIRST].active;CHECK(a&&!b&&!c);printf(",\"lifetime\":[%d,%d,%d]",a,b,c);
 setup(0,hx,hy,4000,4000);w=ward(0,1,ITEM_UNIT_OBSERVER,hx,hy,100);ward(1,0,ITEM_UNIT_SENTRY,hx,hy,100);save("detected");a=dota_unit_visible(&g,0,w);setup(0,hx<2000?hx+800:hx-800,hy,4000,4000);w=ward(0,1,ITEM_UNIT_OBSERVER,hx,hy,100);ward(1,0,ITEM_UNIT_SENTRY,hx,hy,100);save("no_ground_sight");b=dota_unit_visible(&g,0,w);setup(0,hx,hy,hx,hy);dota_items_apply_effect(&g,1,1,IE_INVISIBLE,900,2,DOTA_ITEM_SMOKE_OF_DECEIT);dota_items_apply_effect(&g,0,1,IE_REVEAL,100,1,DOTA_ITEM_DUST);save("smoke");c=dota_unit_visible(&g,0,1);dota_item_world_tick(&g);d=dota_unit_visible(&g,0,1);CHECK(a&&!b&&!c&&d);printf(",\"detection\":[%d,%d,%d,%d]",a,b,c,d);
 setup(DOTA_HERO_SLARK,hx,hy,lx,ly);g.units[0].level=6;g.units[0].learned[3]=1;g.extra.hero[0].generation=g.units[0].generation;g.extra.hero[0].unseen_since=g.tick-20;CHECK(dota_items_receive(&g,1,DOTA_ITEM_WARD_OBSERVER,201));save("slark_cast");a=dota_extra_shadow_dance_passive_active(&g,0);b=dota_items_use(&g,1,0,hx,hy,-1);c=dota_extra_shadow_dance_passive_active(&g,0);d=dota_cast(&g,3,hx,hy);e=dota_extra_shadow_dance_passive_active(&g,0);w=dota_unit_visible(&g,1,0);CHECK(a&&b&&!c&&d&&e&&!w);printf(",\"slark_cast\":[%d,%d,%d,%d,%d,%d]",a,b,c,d,e,w);
 setup(DOTA_HERO_SLARK,hx,hy,4000,4000);g.units[0].level=6;g.units[0].learned[3]=1;g.extra.hero[0].generation=g.units[0].generation;g.extra.hero[0].unseen_since=0;ward(0,1,ITEM_UNIT_OBSERVER,hx,hy,1);save("slark_delay");a=dota_extra_shadow_dance_passive_active(&g,0);tick(1);b=dota_extra_shadow_dance_passive_active(&g,0);tick(8);c=dota_extra_shadow_dance_passive_active(&g,0);tick(6);d=dota_extra_shadow_dance_passive_active(&g,0);CHECK(!a&&!b&&!c&&d);printf(",\"slark_delay\":[%d,%d,%d,%d]",a,b,c,d);
 printf(",\"blocked_wards\":[");for(a=0;a<5;++a){int team;for(team=0;team<2;++team){char name[32];int x=blocked[a][0],y=blocked[a][1],q,r,p,slot_count=0,i;setup(0,x<4000?x+64:x-64,y,4000,4000);g.units[0].team=team;CHECK(dota_items_receive(&g,0,DOTA_ITEM_WARD_OBSERVER,201)&&dota_items_receive(&g,0,DOTA_ITEM_WARD_SENTRY,201));snprintf(name,sizeof(name),"blocked_%d_%d",a,team);save(name);q=dota_item_world_ward_placeable(&g,0,x,y);r=dota_use_item_at(&g,0,x,y,DOTA_WARD_TARGET_SENTRY);p=dota_rules_item_primitive(&g,ITEM_ACTION_WARD|(DOTA_ITEM_WARD_OBSERVER<<8),0,-1,x,y,0,7200);for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;++i)slot_count+=g.units[i].active;CHECK(!q&&!r&&!p&&g.items.charges[0][0]==257&&g.items.cooldown[0][0]==0&&!slot_count);printf("%s[%d,%d,%d,%d,%d,%d]",a||team?",":"",q,r,p,g.items.charges[0][0],g.items.cooldown[0][0],slot_count);}}printf("]");
 setup(0,cliffpair[2],cliffpair[3],4000,4000);CHECK(dota_items_receive(&g,0,DOTA_ITEM_WARD_OBSERVER,201));save("ward_cliff");a=dota_item_world_ward_placeable(&g,0,cliffpair[0],cliffpair[1]);b=dota_use_item_at(&g,0,cliffpair[0],cliffpair[1],-1);CHECK(a&&b&&!dota_nav_walkable(cliffpair[0],cliffpair[1])&&g.units[DOTA_ITEM_UNIT_FIRST].x==cliffpair[0]&&g.units[DOTA_ITEM_UNIT_FIRST].y==cliffpair[1]);printf(",\"ward_cliff\":[%d,%d,%d,%d,%d]",a,b,g.items.charges[0][0],g.units[DOTA_ITEM_UNIT_FIRST].x,g.units[DOTA_ITEM_UNIT_FIRST].y);
 printf("}}\n");dota_sight_detach();free(work);item_assets_close();baked_io_close();return 0;
}
