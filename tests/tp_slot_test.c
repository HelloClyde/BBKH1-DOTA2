#include "../game/game.h"
#include "../game/items.h"
#include "../game/items_ids.h"
#include "../game/items_actions.h"
#include "../game/items_internal.h"
#include "../game/extra_heroes.h"
#include "../game/item_world.h"
#include "../game/courier.h"
#include "../game/match_history.h"
#include "../game/trees.h"
#include "../platform/baked_io.h"
#include "../platform/item_assets.h"
#include "../platform/item_tap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DotaGame g,before;
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void setup(void){int i;dota_init(&g,DOTA_HERO_AXE,73);for(i=1;i<DOTA_MAX_UNITS;i++)g.units[i].active=0;g.units[0].mana=g.units[0].max_mana=10000;g.units[0].gold=100000;dota_rules_set_position(&g,0,2000,2000);}
static void full(void){int i;for(i=0;i<9;i++)CHECK(dota_items_receive(&g,0,DOTA_ITEM_CROWN,201));for(i=0;i<9;i++)CHECK(dota_inventory_item(&g,0,i)==DOTA_ITEM_CROWN);}
static void slot_and_delivery(void){int i,n,gold;int16_t batch[2]={DOTA_ITEM_TPSCROLL,DOTA_ITEM_TPSCROLL},bad[2]={DOTA_ITEM_TPSCROLL,DOTA_ITEM_REAVER};uint16_t ages[2]={0,201};DotaItemRecord tp;
 setup();full();g.items.neutral_item[0]=DOTA_ITEM_CLOAK;CHECK(DOTA_ITEMS_EQUIP_SLOTS==7);CHECK(dota_items_can_receive_batch(&g,0,batch,2));CHECK(dota_items_receive_batch(&g,0,batch,ages,2));CHECK(dota_inventory_item(&g,0,9)==DOTA_ITEM_TPSCROLL&&dota_inventory_charges(&g,0,9)==2);CHECK(dota_items_cast_item(&g,0,7)==DOTA_ITEM_TPSCROLL);CHECK(dota_items_equipped_item(&g,0,7)<0&&dota_items_equipped_item(&g,0,6)==DOTA_ITEM_CLOAK);for(i=0;i<9;i++)CHECK(dota_inventory_item(&g,0,i)==DOTA_ITEM_CROWN);CHECK(!dota_inventory_move(&g,0,9,0)&&!dota_inventory_move(&g,0,0,9));
 before=g;CHECK(!dota_items_can_receive_batch(&g,0,bad,2));CHECK(!memcmp(&before,&g,sizeof(g)));CHECK(!dota_items_receive_batch(&g,0,bad,ages,2));CHECK(!memcmp(&before,&g,sizeof(g)));CHECK(dota_items_teleport_charges(&g,0)==2);for(i=0;i<9;i++)CHECK(dota_inventory_item(&g,0,i)==DOTA_ITEM_CROWN);
 CHECK(dota_items_read(DOTA_ITEM_TPSCROLL,&tp));gold=g.units[0].gold;CHECK(dota_items_sell(&g,0,9)>0);CHECK(g.units[0].gold==gold+tp.cost&&dota_inventory_item(&g,0,9)<0);g.item_world.teleport.ready_tick[0]=g.tick+100;CHECK(dota_inventory_cooldown(&g,0,9)==100);
 setup();full();{DotaPoint p=dota_fountains[g.units[0].team];dota_rules_set_position(&g,0,p.x,p.y);}gold=g.units[0].gold;CHECK(dota_courier_buy(&g,0,DOTA_ITEM_TPSCROLL));CHECK(dota_items_teleport_charges(&g,0)==1&&g.units[0].gold==gold-tp.cost);CHECK(dota_courier_buy(&g,0,DOTA_ITEM_TPSCROLL));CHECK(dota_items_teleport_charges(&g,0)==2);
 setup();full();CHECK(dota_courier_buy(&g,0,DOTA_ITEM_TPSCROLL));CHECK(dota_courier_buy(&g,0,DOTA_ITEM_TPSCROLL));CHECK(dota_courier_pending_count(&g,0)==2);for(n=0;n<2000&&dota_courier_pending_count(&g,0);n++){g.tick++;dota_couriers_tick(&g);}CHECK(dota_courier_pending_count(&g,0)==0&&dota_items_teleport_charges(&g,0)==2);for(i=0;i<9;i++)CHECK(dota_inventory_item(&g,0,i)==DOTA_ITEM_CROWN);
}
static void migration(void){int i;setup();g.units[0].inventory[0]=DOTA_ITEM_TPSCROLL;g.items.tracked[0][0]=DOTA_ITEM_TPSCROLL;g.items.charges[0][0]=2;g.items.cooldown[0][0]=99;g.items.buy_age[0][0]=25;g.items.backpack[0][1].item=DOTA_ITEM_TPSCROLL;g.items.backpack[0][1].charges=3;g.items.backpack[0][1].cooldown=100;g.items.backpack[0][1].buy_age=100;before=g;CHECK(dota_items_teleport_charges(&g,0)==5);CHECK(!memcmp(&before,&g,sizeof(g)));CHECK(dota_inventory_item(&g,0,0)<0&&dota_inventory_item(&g,0,7)<0);CHECK(dota_items_receive(&g,0,DOTA_ITEM_TPSCROLL,0));CHECK(dota_items_teleport_charges(&g,0)==6&&g.item_world.teleport.buy_age[0]==100&&dota_items_teleport_cooldown(&g,0)==100);CHECK(g.units[0].inventory[0]<0&&g.items.backpack[0][1].item<0);for(i=0;i<3;i++)CHECK(dota_items_receive(&g,0,DOTA_ITEM_CROWN,201));CHECK(dota_items_teleport_charges(&g,0)==6);}
static void teleport(void){int ready,remain,total,x,y;DotaPoint home;
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_TPSCROLL,201));home=dota_fountains[g.units[0].team];CHECK(dota_items_self_target_valid(&g,0,7));CHECK(dota_items_cast_query(&g,0,7,home.x,home.y,DOTA_TELEPORT_FOUNTAIN_TARGET(g.units[0].team),&x)==DOTA_ITEM_CAST_READY&&x==0);CHECK(dota_items_use_self(&g,0,7));CHECK(dota_items_teleport_charges(&g,0)==0);CHECK(dota_items_teleport_channel(&g,0,&x,&y,&remain,&total)&&remain>0&&total>=40);CHECK(g.item_world.channel[0].target==DOTA_TELEPORT_FOUNTAIN_TARGET(g.units[0].team));ready=g.item_world.teleport.ready_tick[0];CHECK(ready>g.tick&&dota_items_cooldown(&g,0,7)>0);g.units[0].hp=0;g.tick++;dota_item_world_tick(&g);CHECK(!dota_items_teleport_channel(&g,0,0,0,0,0)&&!g.units[0].channel_ticks);CHECK(g.item_world.teleport.ready_tick[0]==ready);g.units[0].hp=1000;CHECK(dota_items_receive(&g,0,DOTA_ITEM_TPSCROLL,201));CHECK(!dota_items_use_self(&g,0,7)&&dota_items_teleport_charges(&g,0)==1);
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_TPSCROLL,201));CHECK(dota_items_use_self(&g,0,7));g.tick=g.item_world.channel[0].until;dota_item_world_tick(&g);CHECK(!dota_items_teleport_channel(&g,0,0,0,0,0));CHECK(abs(g.units[0].x-home.x)<200&&abs(g.units[0].y-home.y)<200);
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_TRAVEL_BOOTS,201));CHECK(dota_items_receive(&g,0,DOTA_ITEM_TPSCROLL,201));CHECK(dota_items_use_self(&g,0,7));CHECK(dota_items_teleport_charges(&g,0)==1&&dota_items_teleport_cooldown(&g,0)>0);
}
static void self_and_query(void){int i,range,items[]={DOTA_ITEM_FLASK,DOTA_ITEM_FORCE_STAFF,DOTA_ITEM_CYCLONE,DOTA_ITEM_LOTUS_ORB},invalid[]={DOTA_ITEM_DAGON,DOTA_ITEM_WARD_OBSERVER,DOTA_ITEM_BRANCHES};DotaItemRecord r;
 for(i=0;i<4;i++){setup();CHECK(dota_items_receive(&g,0,items[i],201));CHECK(dota_items_read(items[i],&r));printf("SELF %d flags%u action%u\n",items[i],r.flags,r.action);CHECK(dota_items_self_target_valid(&g,0,0));before=g;CHECK(dota_items_cast_query(&g,0,0,g.units[0].x,g.units[0].y,0,&range)==DOTA_ITEM_CAST_READY&&range==0);CHECK(!memcmp(&before,&g,sizeof(g)));CHECK(dota_items_use_self(&g,0,0));}
 for(i=0;i<3;i++){setup();CHECK(dota_items_receive(&g,0,invalid[i],201));before=g;CHECK(!dota_items_self_target_valid(&g,0,0));CHECK(!dota_items_use_self(&g,0,0));CHECK(!memcmp(&before,&g,sizeof(g)));}
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_FORCE_STAFF,201));g.units[1]=g.units[0];g.units[1].active=1;g.units[1].x+=300;g.units[1].generation=2;before=g;CHECK(dota_items_cast_query(&g,0,0,g.units[1].x,g.units[1].y,1,&range)==DOTA_ITEM_CAST_APPROACH&&range>0);CHECK(!memcmp(&before,&g,sizeof(g)));g.units[1].x=g.units[0].x+10;before=g;CHECK(dota_items_cast_query(&g,0,0,g.units[1].x,g.units[1].y,1,&range)==DOTA_ITEM_CAST_READY&&range==0);CHECK(!memcmp(&before,&g,sizeof(g)));
}
static void double_tap(void){DotaItemTap tap={0};int team;
 CHECK(!dota_item_tap(&tap,0,100,1,1000));CHECK(dota_item_tap(&tap,0,100,1,1450));CHECK(!dota_item_tap(&tap,0,100,1,1451));CHECK(!dota_item_tap(&tap,0,100,1,1902));CHECK(dota_item_tap(&tap,0,100,1,1903));
 dota_item_tap_clear(&tap);CHECK(!dota_item_tap(&tap,0,100,1,2000));CHECK(!dota_item_tap(&tap,1,100,1,2001));CHECK(!dota_item_tap(&tap,1,101,1,2002));CHECK(!dota_item_tap(&tap,1,101,2,2003));CHECK(dota_item_tap(&tap,1,101,2,2004));
 dota_item_tap_clear(&tap);CHECK(!dota_item_tap(&tap,0,100,1,0xfffffff0u));CHECK(dota_item_tap(&tap,0,100,1,16));
 for(team=0;team<2;team++){setup();g.units[0].team=team;CHECK(dota_items_receive(&g,0,DOTA_ITEM_TPSCROLL,201));dota_item_tap_clear(&tap);CHECK(!dota_item_tap(&tap,7,DOTA_ITEM_TPSCROLL,g.units[0].generation,3000));CHECK(dota_items_teleport_charges(&g,0)==1);CHECK(dota_item_tap(&tap,7,DOTA_ITEM_TPSCROLL,g.units[0].generation,3100));CHECK(dota_items_use_self(&g,0,7));CHECK(dota_items_teleport_charges(&g,0)==0&&g.item_world.channel[0].target==DOTA_TELEPORT_FOUNTAIN_TARGET(team));}
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_FLASK,201));dota_item_tap_clear(&tap);CHECK(!dota_item_tap(&tap,0,DOTA_ITEM_FLASK,g.units[0].generation,4000));CHECK(dota_item_tap(&tap,0,DOTA_ITEM_FLASK,g.units[0].generation,4100));CHECK(dota_items_use_self(&g,0,0));CHECK(g.units[0].inventory[0]<0);before=g;CHECK(!dota_items_use_self(&g,0,0));CHECK(!memcmp(&before,&g,sizeof(g)));
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_DAGON,201));dota_item_tap_clear(&tap);CHECK(!dota_item_tap(&tap,0,DOTA_ITEM_DAGON,g.units[0].generation,5000));CHECK(dota_item_tap(&tap,0,DOTA_ITEM_DAGON,g.units[0].generation,5100));before=g;CHECK(!dota_items_use_self(&g,0,0));CHECK(!memcmp(&before,&g,sizeof(g)));
}
static void tree_identity_and_healing(void){int x,y,stamp;DotaItemTree*t;
 setup();dota_trees_detach();t=&g.item_world.tree[0];t->x=2000;t->y=2000;t->until=g.tick+400;t->source_item=DOTA_ITEM_BRANCHES;CHECK(dota_items_tree_target(&g,2000,2000,&x,&y,&stamp)==DOTA_STATIC_TREE_COUNT&&x==2000&&y==2000&&stamp==t->until);before=g;CHECK(dota_items_tree_target(&g,2000,2000,0,0,0)==DOTA_STATIC_TREE_COUNT);CHECK(!memcmp(&before,&g,sizeof(g)));t->removed=1;CHECK(dota_items_tree_target(&g,2000,2000,0,0,0)<0);t->removed=0;t->until++;CHECK(dota_items_tree_target(&g,2000,2000,0,0,&stamp)==DOTA_STATIC_TREE_COUNT&&stamp==t->until);g.tick=t->until;CHECK(dota_items_tree_target(&g,2000,2000,0,0,0)<0);
 setup();CHECK(dota_items_receive(&g,0,DOTA_ITEM_MEKANSM,201));g.units[1]=g.units[0];g.units[1].active=1;g.units[1].x+=10;g.units[1].max_hp=1000;g.units[1].hp=970;g.units[0].hp=100;CHECK(dota_items_use_self(&g,0,0));CHECK(g.units[1].hp==1000&&g.match_stats.ally_healing[0]==30);CHECK(g.units[0].hp>100);g.items.cooldown[0][0]=0;g.units[0].hp=100;CHECK(dota_items_use_self(&g,0,0));CHECK(g.match_stats.ally_healing[0]==30);
}
int main(int argc,char**argv){CHECK(argc==2);CHECK(baked_io_open(argv[1])&&item_assets_open());slot_and_delivery();migration();teleport();self_and_query();double_tap();tree_identity_and_healing();printf("RESULT {\"passed\":true,\"checks\":%d,\"game_bytes\":%u,\"physical_h1_tested\":false}\n",checks,(unsigned)sizeof(g));item_assets_close();baked_io_close();return 0;}
