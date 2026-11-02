#include "../game/match_history.h"
#include "../game/items.h"
#include "../game/item_world.h"
#include "../game/neutral.h"
#include "../game/courier.h"
#include "../platform/match_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static unsigned checks,cases,services,allocs,frees;static int allocation_fail;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int tp[10],tpc[10],stub_owner=-1;
int dota_item_world_owner(const DotaGame*g,int u){(void)g;return u==200?stub_owner:-1;}
int dota_items_cost(int id){return id>=0?100+id:0;}
int dota_inventory_item(const DotaGame*g,int h,int slot){return slot<6?g->units[h].inventory[slot]:slot<9?g->items.backpack[h][slot-6].item:slot==9?tp[h]:-1;}
int dota_inventory_charges(const DotaGame*g,int h,int slot){(void)g;(void)h;return slot==9?tpc[h]:1;}
int dota_items_teleport_charges(const DotaGame*g,int h){(void)g;return tpc[h];}
int dota_items_charges(const DotaGame*g,int h,int slot){(void)g;(void)h;(void)slot;return 1;}
int dota_neutral_equipped(const DotaGame*g,int h){return g->items.neutral_item[h];}
int dota_neutral_enchantment(const DotaGame*g,int h){(void)g;(void)h;return -1;}
int dota_courier_pending_count(const DotaGame*g,int h){(void)g;(void)h;return 0;}
int dota_courier_pending_item(const DotaGame*g,int h,int n){(void)g;(void)h;(void)n;return -1;}
int dota_hero_assists(const DotaGame*g,int h){return g->assists[h];}
static void setup(DotaGame*g,uint32_t id){unsigned i,j;memset(g,0,sizeof(*g));g->player=0;g->seed=123;g->state=DOTA_PLAYING;g->tick=1200+123*20;g->match_stats.match_id=id;
 for(i=0;i<10;i++){DotaUnit*u=&g->units[i];u->active=1;u->type=DOTA_HERO;u->team=i<5?0:1;u->hero_id=(int)i;u->level=10;u->gold=500;u->generation=1;u->hp=u->max_hp=1000;g->items.neutral_item[i]=-1;tp[i]=-1;tpc[i]=0;
  for(j=0;j<6;j++){u->inventory[j]=-1;}for(j=0;j<3;j++){g->items.backpack[i][j].item=-1;}
 }}
typedef struct {unsigned char*data;unsigned size;int exists;} Disk;
typedef struct {int active,slot;unsigned at;} Handle;
static Disk disk[4];static Handle handles[8];static unsigned write_budget=UINT_MAX;static int deny_writes;
static int path_slot(const char*p){if(strstr(p,"MATCH0"))return 0;if(strstr(p,"MATCH1"))return 1;if(strstr(p,"HS0"))return 2;if(strstr(p,"HS1"))return 3;return -1;}
static void *f_open(const char*p,const char*mode){int s=path_slot(p),i;if(s<0)return 0;if(mode[0]=='r'&&!disk[s].exists)return 0;if(mode[0]=='w'){if(deny_writes)return 0;disk[s].exists=1;disk[s].size=0;}
 for(i=0;i<8;i++){if(!handles[i].active){handles[i].active=1;handles[i].slot=s;handles[i].at=0;return &handles[i];}}return 0;}
static int f_close(void*p){Handle*h=p;if(!h||!h->active)return -1;h->active=0;return 0;}
static unsigned f_read(void*p,void*out,unsigned n){Handle*h=p;Disk*d=&disk[h->slot];if(h->at>d->size)return 0;if(n>d->size-h->at)n=d->size-h->at;memcpy(out,d->data+h->at,n);h->at+=n;return n;}
static unsigned f_write(void*p,const void*in,unsigned n){Handle*h=p;Disk*d=&disk[h->slot];if(deny_writes)return 0;if(n>write_budget)n=write_budget;if(n>DOTA_MATCH_SAVE_BYTES-h->at)n=DOTA_MATCH_SAVE_BYTES-h->at;
 memcpy(d->data+h->at,in,n);h->at+=n;if(h->at>d->size)d->size=h->at;if(write_budget!=UINT_MAX)write_budget-=n;return n;}
static int f_seek(void*p,int off,int mode){Handle*h=p;int at=mode==0?off:mode==1?(int)h->at+off:mode==2?(int)disk[h->slot].size+off:-1;if(at<0||(unsigned)at>DOTA_MATCH_SAVE_BYTES)return -1;h->at=(unsigned)at;return at;}
static void *f_allocate(unsigned n){void*p;if(allocation_fail)return 0;p=calloc(1,n);if(p)++allocs;return p;}
static void f_free(void*p){if(p){++frees;free(p);}}
static void audio_service(void*p){(void)p;++services;}
int main(void){
 DotaGame*g=calloc(1,sizeof(*g));DotaMatchHistory*h=malloc(sizeof(*h)),*loaded=malloc(sizeof(*loaded)),*before=malloc(sizeof(*before));DotaMatchRecord*r=malloc(sizeof(*r));unsigned char*bytes=malloc(DOTA_MATCH_SAVE_BYTES);unsigned i;uint32_t old_crc;DotaMatchStorageHost host={f_open,f_close,f_read,f_write,f_seek,f_allocate,f_free};
 CHECK(g&&h&&loaded&&before&&r&&bytes);for(i=0;i<4;i++){disk[i].data=calloc(1,DOTA_MATCH_SAVE_BYTES);CHECK(disk[i].data!=0);}
 setup(g,1);g->units[0].inventory[0]=2;g->items.backpack[0][0].item=3;tp[0]=DOTA_ITEM_TPSCROLL;tpc[0]=2;g->state=DOTA_WON;
 CHECK(dota_match_capture(g,r));CHECK(r->duration_seconds==123);CHECK(r->heroes[0].items[0]==2&&r->heroes[0].items[6]==3&&r->heroes[0].items[10]==DOTA_ITEM_TPSCROLL&&r->heroes[0].charges[10]==2);CHECK(r->heroes[0].net_worth==500+102+103+2*(100+DOTA_ITEM_TPSCROLL));++cases;
 dota_match_history_init(h);CHECK(dota_match_history_append(h,r)==1);CHECK(dota_match_history_append(h,r)==0);CHECK(h->count==1);CHECK(dota_match_history_encode(h,bytes,DOTA_MATCH_SAVE_BYTES)==DOTA_MATCH_SAVE_BYTES);CHECK(dota_match_history_decode(loaded,bytes,DOTA_MATCH_SAVE_BYTES));CHECK(!memcmp(h,loaded,sizeof(*h)));++cases;
 *before=*loaded;bytes[64+90]^=1;CHECK(!dota_match_history_decode(loaded,bytes,DOTA_MATCH_SAVE_BYTES));CHECK(!memcmp(before,loaded,sizeof(*before)));bytes[64+90]^=1;bytes[44]^=1;CHECK(!dota_match_history_decode(loaded,bytes,DOTA_MATCH_SAVE_BYTES));CHECK(!memcmp(before,loaded,sizeof(*before)));bytes[44]^=1;CHECK(!dota_match_history_decode(loaded,bytes,DOTA_MATCH_SAVE_BYTES-1));++cases;
 for(i=2;i<=55;i++){r->match_id=i;CHECK(dota_match_history_append(h,r)==1);}CHECK(h->count==50);CHECK(dota_match_history_get(h,0)->match_id==55);CHECK(dota_match_history_get(h,49)->match_id==6);CHECK(!dota_match_history_get(h,50));CHECK(dota_match_history_encode(h,bytes,DOTA_MATCH_SAVE_BYTES)==DOTA_MATCH_SAVE_BYTES);CHECK(dota_match_history_decode(loaded,bytes,DOTA_MATCH_SAVE_BYTES));CHECK(!memcmp(h,loaded,sizeof(*h)));++cases;
 setup(g,56);g->state=DOTA_WON;CHECK(dota_match_capture(g,r));r->heroes[0].kills=10;r->heroes[0].hero_damage=1000;r->heroes[1].assists=16;r->heroes[1].hero_damage=15000;r->heroes[1].ally_healing=6000;r->heroes[1].building_damage=2000;r->heroes[9].kills=1000;dota_match_mvp(r);CHECK(r->mvp_hero==1);CHECK(r->heroes[9].mvp_score>r->heroes[1].mvp_score);++cases;
 setup(g,57);g->state=DOTA_WON;CHECK(dota_match_capture(g,r));CHECK(r->mvp_hero==0);r->heroes[0].deaths=1;dota_match_mvp(r);CHECK(r->mvp_hero==1);CHECK(strstr(dota_match_mvp_formula(),"Not Valve")!=0);++cases;
 setup(g,58);g->match_stats.hero_damage[0]=UINT32_MAX-2u;dota_match_stats_damage(g,0,5,99);CHECK(g->match_stats.hero_damage[0]==UINT32_MAX);dota_match_stats_damage(g,0,1,77);CHECK(g->match_stats.hero_damage[0]==UINT32_MAX);dota_match_record_heal(g,0,1,100);dota_match_record_heal(g,0,0,100);dota_match_record_heal(g,0,5,100);CHECK(g->match_stats.ally_healing[0]==100);g->units[37].type=DOTA_CREEP;g->units[37].team=1;dota_match_stats_kill(g,0,37);g->units[37].team=0;dota_match_stats_kill(g,0,37);CHECK(g->match_stats.last_hits[0]==1&&g->match_stats.denies[0]==1);g->units[200].team=0;stub_owner=0;dota_match_record_heal(g,200,1,50);CHECK(g->match_stats.ally_healing[0]==150);stub_owner=-1;++cases;
 match_storage_host_services(&host);match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_heap_bytes()==sizeof(DotaMatchHistory)+sizeof(DotaMatchRecord)+DOTA_MATCH_RECORD_BYTES);CHECK(match_storage_history()->count==0);setup(g,match_storage_begin_match());CHECK(match_storage_complete(g)==-1);CHECK(!disk[0].exists&&!disk[1].exists&&!disk[2].exists&&!disk[3].exists);g->state=DOTA_WON;CHECK(match_storage_complete(g)==1);CHECK(match_storage_history()->count==1);CHECK(match_storage_stats()->active_slot==0);old_crc=dota_match_crc_update(0,disk[0].data,disk[0].size);CHECK(match_storage_complete(g)==0);CHECK(match_storage_history()->count==1);CHECK(services>100);++cases;
 setup(g,match_storage_begin_match());g->state=DOTA_LOST;write_budget=1204;CHECK(match_storage_complete(g)==-1);CHECK(match_storage_error()==MATCH_STORAGE_IO);CHECK(match_storage_history()->count==2);CHECK(dota_match_crc_update(0,disk[0].data,disk[0].size)==old_crc);CHECK(match_storage_result()->match_id==g->match_stats.match_id);write_budget=UINT_MAX;CHECK(match_storage_complete(g)==1);CHECK(match_storage_history()->count==2);CHECK(match_storage_complete(g)==0);CHECK(match_storage_history()->count==2);++cases;
 match_storage_close();CHECK(match_storage_heap_bytes()==0&&match_storage_stats()->open_files==0&&allocs==frees);match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_history()->count==2);CHECK(dota_match_history_get(match_storage_history(),0)->winner_team==1);++cases;
 disk[1].data[64+90]^=1;match_storage_close();match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_history()->count==1);CHECK(match_storage_stats()->active_slot==0);CHECK(match_storage_stats()->recovered_slots>0);++cases;
 setup(g,match_storage_begin_match());g->state=DOTA_WON;write_budget=1204;CHECK(match_storage_complete(g)==-1);match_storage_close();write_budget=UINT_MAX;match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_history()->count==1);CHECK(match_storage_stats()->active_slot==0);++cases;
 disk[0].data[64+95]^=1;match_storage_close();match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_error()==MATCH_STORAGE_CORRUPT);old_crc=dota_match_crc_update(0,disk[0].data,disk[0].size);setup(g,match_storage_begin_match());g->state=DOTA_WON;CHECK(match_storage_complete(g)==1);CHECK(match_storage_stats()->active_slot==3);CHECK(match_storage_error()==MATCH_STORAGE_OK);CHECK(dota_match_crc_update(0,disk[0].data,disk[0].size)==old_crc);++cases;
 disk[3].data[64+95]^=1;match_storage_close();match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_error()==MATCH_STORAGE_CORRUPT);setup(g,match_storage_begin_match());g->state=DOTA_WON;CHECK(match_storage_complete(g)==-1);CHECK(match_storage_error()==MATCH_STORAGE_CORRUPT);++cases;
 match_storage_close();for(i=0;i<4;i++){disk[i].exists=0;disk[i].size=0;}match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);setup(g,match_storage_begin_match());g->state=DOTA_WON;write_budget=1204;CHECK(match_storage_complete(g)==-1);CHECK(disk[0].exists&&disk[2].exists&&!disk[1].exists&&!disk[3].exists);old_crc=dota_match_crc_update(0,disk[0].data,disk[0].size);match_storage_close();write_budget=UINT_MAX;match_storage_set_service(audio_service,0);CHECK(match_storage_open()==1);CHECK(match_storage_history()->count==0);setup(g,match_storage_begin_match());g->state=DOTA_WON;CHECK(match_storage_complete(g)==1);CHECK(match_storage_stats()->active_slot==1);CHECK(match_storage_history()->count==1);CHECK(dota_match_crc_update(0,disk[0].data,disk[0].size)==old_crc);++cases;
 match_storage_close();allocation_fail=1;CHECK(match_storage_open()==-1);CHECK(match_storage_error()==MATCH_STORAGE_MEMORY&&match_storage_heap_bytes()==0);allocation_fail=0;CHECK(allocs==frees);++cases;
 CHECK(match_storage_stats()->open_files==0);for(i=0;i<8;i++)CHECK(!handles[i].active);free(g);free(h);free(loaded);free(before);free(r);free(bytes);for(i=0;i<4;i++)free(disk[i].data);
 printf("{\"passed\":true,\"actual_core\":true,\"actual_mips32\":false,\"physical_h1_tested\":false,\"cases\":%u,\"checks\":%u,\"service_calls\":%u,\"allocations\":%u,\"frees\":%u,\"history_bytes\":%u,\"record_bytes\":%u,\"stats_bytes\":%u}\n",cases,checks,services,allocs,frees,(unsigned)sizeof(DotaMatchHistory),(unsigned)sizeof(DotaMatchRecord),(unsigned)sizeof(DotaMatchStats));return 0;
}
