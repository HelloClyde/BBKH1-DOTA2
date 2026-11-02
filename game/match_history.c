#include "match_history.h"
#include "items.h"
#include "item_world.h"
#include "neutral.h"
#include "courier.h"
#include "match_time.h"
#ifdef __mips__
#include <stddef.h>
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
#else
#include <string.h>
#endif
#include <limits.h>
#define MATCH_SAVE_MAGIC 0x3148534du
typedef char MatchHero100[(sizeof(DotaMatchHeroRecord)==100)?1:-1];
typedef char MatchRecord1040[(sizeof(DotaMatchRecord)==DOTA_MATCH_RECORD_BYTES)?1:-1];
typedef char MatchStats204[(sizeof(DotaMatchStats)==204)?1:-1];
static uint32_t sat_add(uint32_t a,uint32_t b){return b>UINT32_MAX-a?UINT32_MAX:a+b;}
static uint32_t sat_mul(uint32_t a,uint32_t b){return b&&a>UINT32_MAX/b?UINT32_MAX:a*b;}
static uint32_t positive(int n){return n>0?(uint32_t)n:0;}
static int owner(const DotaGame*g,int unit){
 int h,s;
 if(!g||(unsigned)unit>=DOTA_MAX_UNITS)return -1;
 if(unit<10&&g->units[unit].type==DOTA_HERO)return unit;
 h=dota_item_world_owner(g,unit);if((unsigned)h<10u&&g->units[h].type==DOTA_HERO)return h;
 s=unit-DOTA_SUMMON_FIRST;
 if((unsigned)s<DOTA_MAX_SUMMONS){h=g->summon_owner[s];
  if((unsigned)h<10u&&g->units[h].type==DOTA_HERO&&g->units[h].active&&g->units[h].generation==g->summon_owner_generation[s]&&g->units[h].team==g->units[unit].team)return h;
 }return -1;
}
void dota_match_stats_reset(DotaMatchStats*s){if(s)memset(s,0,sizeof(*s));}
void dota_match_stats_damage(DotaGame*g,int source,int target,int actual){
 int h=owner(g,source);const DotaUnit*t;
 if(!g||g->state!=DOTA_PLAYING||h<0||actual<=0||(unsigned)target>=DOTA_MAX_UNITS)return;
 t=&g->units[target];if(g->units[h].team==t->team)return;
 if(target<10&&t->type==DOTA_HERO)g->match_stats.hero_damage[h]=sat_add(g->match_stats.hero_damage[h],(uint32_t)actual);
 else if(t->type==DOTA_TOWER||t->type==DOTA_BUILDING||t->type==DOTA_BASE)g->match_stats.building_damage[h]=sat_add(g->match_stats.building_damage[h],(uint32_t)actual);
}
void dota_match_stats_kill(DotaGame*g,int killer,int victim){
 int h=owner(g,killer);const DotaUnit*v;
 if(!g||g->state!=DOTA_PLAYING||h<0||(unsigned)victim>=DOTA_MAX_UNITS)return;
 v=&g->units[victim];
 if(v->type!=DOTA_CREEP&&v->type!=DOTA_NEUTRAL&&v->type!=DOTA_ANCIENT)return;
 if(v->team==g->units[h].team){if(v->type==DOTA_CREEP)g->match_stats.denies[h]=sat_add(g->match_stats.denies[h],1);}
 else g->match_stats.last_hits[h]=sat_add(g->match_stats.last_hits[h],1);
}
void dota_match_record_heal(DotaGame*g,int source,int target,int actual){
 int h=owner(g,source);
 if(!g||g->state!=DOTA_PLAYING||h<0||actual<=0||(unsigned)target>=10u||h==target||g->units[target].type!=DOTA_HERO||g->units[target].team!=g->units[h].team)return;
 g->match_stats.ally_healing[h]=sat_add(g->match_stats.ally_healing[h],(uint32_t)actual);
}
/* floor(500*a/b), with a<=b, using only bounded32-bit arithmetic. */
static uint32_t participation(uint32_t a,uint32_t b){
 uint32_t q=0,r=0,i;if(!b)return 0;if(a>b)a=b;
 for(i=0;i<500;i++){if(r>=b-a){r-=b-a;++q;}else r+=a;}return q;
}
void dota_match_mvp(DotaMatchRecord*r){
 unsigned i,best=10;uint32_t team_kills[2]={0,0};
 if(!r)return;
 for(i=0;i<10;i++)if(r->heroes[i].team<2)team_kills[r->heroes[i].team]=sat_add(team_kills[r->heroes[i].team],r->heroes[i].kills);
 for(i=0;i<10;i++){
  DotaMatchHeroRecord*h=&r->heroes[i];uint32_t score=sat_add(sat_mul(h->kills,300),sat_mul(h->assists,200)),penalty=sat_mul(h->deaths,75);
  score=sat_add(score,h->hero_damage/20);score=sat_add(score,h->building_damage/10);score=sat_add(score,h->ally_healing/15);
  if(h->team<2)score=sat_add(score,participation(sat_add(h->kills,h->assists),team_kills[h->team]));
  h->mvp_score=score>penalty?score-penalty:0;
  if(h->team!=r->winner_team)continue;
  if(best==10||h->mvp_score>r->heroes[best].mvp_score||(h->mvp_score==r->heroes[best].mvp_score&&
   (h->deaths<r->heroes[best].deaths||(h->deaths==r->heroes[best].deaths&&(h->assists>r->heroes[best].assists||
   (h->assists==r->heroes[best].assists&&h->hero_damage>r->heroes[best].hero_damage))))))best=i;
 }
 r->mvp_hero=best<10?best:0;
}
const char*dota_match_mvp_formula(void){return "Local MVP: winning team first. K*300 + A*200 + hero damage/20 + building damage/10 + effective ally healing/15 + kill participation*500 - D*75. Saturated32-bit; ties: fewer deaths, more assists, more hero damage, lower slot. Not Valve's algorithm.";}
static uint16_t charge(int n){return n<0?0:n>65535?65535:(uint16_t)n;}
static int16_t item_id(int n){return n>=0&&n<DOTA_ITEM_CATALOG_COUNT?(int16_t)n:-1;}
static uint32_t worth_add(uint32_t n,int item){int cost;if(item<0)return n;cost=dota_items_cost(item);return cost>0?sat_add(n,(uint32_t)cost):n;}
int dota_match_capture(const DotaGame*g,DotaMatchRecord*r){
 unsigned i,j;int seconds;
 if(!g||!r||!g->match_stats.match_id||(g->state!=DOTA_WON&&g->state!=DOTA_LOST)||(unsigned)g->player>=10u)return 0;
 memset(r,0,sizeof(*r));r->match_id=g->match_stats.match_id;r->seed=g->seed;r->winner_team=g->state==DOTA_WON?0u:1u;r->player=(uint32_t)g->player;r->flags=1;
 seconds=dota_match_seconds(g);r->duration_seconds=positive(seconds);r->team_kills[0]=positive(g->kills[0]);r->team_kills[1]=positive(g->kills[1]);
 for(i=0;i<10;i++){
  const DotaUnit*u=&g->units[i];DotaMatchHeroRecord*h=&r->heroes[i];uint32_t nw=positive(u->gold);int item,count;
  if(u->type!=DOTA_HERO||(unsigned)u->hero_id>=10u||(unsigned)u->team>=2u||u->level<1||u->level>30)return 0;
  h->hero_id=(uint32_t)u->hero_id;h->team=(uint32_t)u->team;h->level=(uint32_t)u->level;h->kills=positive(u->kills);h->deaths=positive(u->deaths);h->assists=positive(dota_hero_assists(g,(int)i));
  h->last_hits=g->match_stats.last_hits[i];h->denies=g->match_stats.denies[i];h->hero_damage=g->match_stats.hero_damage[i];h->building_damage=g->match_stats.building_damage[i];h->ally_healing=g->match_stats.ally_healing[i];
  for(j=0;j<9;j++){item=dota_inventory_item(g,(int)i,(int)j);h->items[j]=item_id(item);h->charges[j]=charge(dota_inventory_charges(g,(int)i,(int)j));nw=worth_add(nw,item);}
  item=dota_neutral_equipped(g,(int)i);h->items[9]=item_id(item);h->charges[9]=charge(dota_items_charges(g,(int)i,6));nw=worth_add(nw,item);
  item=dota_inventory_item(g,(int)i,DOTA_INVENTORY_TP_SLOT);h->items[10]=item_id(item);h->charges[10]=charge(dota_items_teleport_charges(g,(int)i));
  count=h->charges[10]?h->charges[10]:1;if(item>=0)nw=sat_add(nw,sat_mul(positive(dota_items_cost(item)),(uint32_t)count));
  h->neutral_enchantment=item_id(dota_neutral_enchantment(g,(int)i));
  count=dota_courier_pending_count(g,(int)i);if(count>18)count=18;for(j=0;j<(unsigned)(count>0?count:0);j++)nw=worth_add(nw,dota_courier_pending_item(g,(int)i,(int)j));
  h->net_worth=nw;
 }
 dota_match_mvp(r);return dota_match_record_valid(r);
}
int dota_match_record_valid(const DotaMatchRecord*r){
 unsigned i,j;if(!r||!r->match_id||r->winner_team>1||r->player>=10||r->mvp_hero>=10||r->flags!=1||r->reserved)return 0;
 for(i=0;i<10;i++){const DotaMatchHeroRecord*h=&r->heroes[i];if(h->hero_id>=10||h->team>1||!h->level||h->level>30||h->reserved||h->neutral_enchantment< -1||h->neutral_enchantment>=DOTA_ITEM_CATALOG_COUNT)return 0;
  for(j=0;j<11;j++)if(h->items[j]< -1||h->items[j]>=DOTA_ITEM_CATALOG_COUNT)return 0;
 }return r->heroes[r->mvp_hero].team==r->winner_team;
}
void dota_match_history_init(DotaMatchHistory*h){if(h){memset(h,0,sizeof(*h));h->next_match_id=1;}}
const DotaMatchRecord*dota_match_history_get(const DotaMatchHistory*h,unsigned newest){
 unsigned slot;if(!h||h->count>50||h->next>=50||newest>=h->count)return 0;slot=(h->next+49u-newest)%50u;return &h->records[slot];
}
const DotaMatchRecord*dota_match_history_find(const DotaMatchHistory*h,uint32_t id){unsigned i;if(!h||!id)return 0;for(i=0;i<h->count&&i<50;i++){const DotaMatchRecord*r=dota_match_history_get(h,i);if(r&&r->match_id==id)return r;}return 0;}
int dota_match_history_append(DotaMatchHistory*h,const DotaMatchRecord*r){
 if(!h||h->count>50||h->next>=50||!dota_match_record_valid(r))return -1;
 if(dota_match_history_find(h,r->match_id))return 0;
 h->records[h->next]=*r;h->next=(h->next+1u)%50u;if(h->count<50)++h->count;
 if(r->match_id>=h->next_match_id){h->next_match_id=r->match_id+1u;if(!h->next_match_id)h->next_match_id=1;}
 return 1;
}
uint32_t dota_match_crc_update(uint32_t previous,const void*data,unsigned n){const unsigned char*b=data;uint32_t c=~previous;while(n--){unsigned k;c^=*b++;for(k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static void put32(unsigned char*p,uint32_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static uint32_t get32(const unsigned char*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put16(unsigned char*p,uint16_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static uint16_t get16(const unsigned char*p){return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
int dota_match_record_encode(const DotaMatchRecord*r,unsigned char out[DOTA_MATCH_RECORD_BYTES]){
 unsigned i,j,at=40;uint32_t word;
 if(!r||!out)return 0;
 if(!r->match_id){const unsigned char*empty=(const unsigned char*)(const void*)r;for(i=0;i<sizeof(*r);i++)if(empty[i])return 0;memset(out,0,DOTA_MATCH_RECORD_BYTES);return 1;}
 if(!dota_match_record_valid(r))return 0;
 for(i=0;i<10;i++){memcpy(&word,(const unsigned char*)(const void*)r+i*4,4);put32(out+i*4,word);}
 for(i=0;i<10;i++){const DotaMatchHeroRecord*h=&r->heroes[i];
  for(j=0;j<13;j++){memcpy(&word,(const unsigned char*)(const void*)h+j*4,4);put32(out+at+j*4,word);}at+=52;
  for(j=0;j<11;j++){put16(out+at+j*2,(uint16_t)h->items[j]);}at+=22;
  for(j=0;j<11;j++){put16(out+at+j*2,h->charges[j]);}at+=22;put16(out+at,(uint16_t)h->neutral_enchantment);put16(out+at+2,0);at+=4;
 }return at==DOTA_MATCH_RECORD_BYTES;
}
int dota_match_record_decode(DotaMatchRecord*r,const unsigned char in[DOTA_MATCH_RECORD_BYTES]){
 unsigned i,j,at=40;uint32_t word;
 if(!r||!in)return 0;
 memset(r,0,sizeof(*r));
 if(!get32(in)){for(i=0;i<DOTA_MATCH_RECORD_BYTES;i++)if(in[i])return 0;return 1;}
 for(i=0;i<10;i++){word=get32(in+i*4);memcpy((unsigned char*)(void*)r+i*4,&word,4);}
 for(i=0;i<10;i++){DotaMatchHeroRecord*h=&r->heroes[i];
  for(j=0;j<13;j++){word=get32(in+at+j*4);memcpy((unsigned char*)(void*)h+j*4,&word,4);}at+=52;
  for(j=0;j<11;j++){h->items[j]=(int16_t)get16(in+at+j*2);}at+=22;
  for(j=0;j<11;j++){h->charges[j]=get16(in+at+j*2);}at+=22;h->neutral_enchantment=(int16_t)get16(in+at);h->reserved=get16(in+at+2);at+=4;
 }return at==DOTA_MATCH_RECORD_BYTES&&dota_match_record_valid(r);
}
int dota_match_header_encode(const DotaMatchHistory*h,uint32_t payload_crc,unsigned char out[64]){
 if(!h||!out||h->count>50||h->next>=50||(h->count<50&&h->next!=h->count)||!h->next_match_id)return 0;
 memset(out,0,64);put32(out,MATCH_SAVE_MAGIC);put32(out+4,1);put32(out+8,64);put32(out+12,DOTA_MATCH_SAVE_BYTES);put32(out+16,DOTA_MATCH_RECORD_BYTES);put32(out+20,50);
 put32(out+24,h->count);put32(out+28,h->next);put32(out+32,h->sequence);put32(out+36,h->next_match_id);put32(out+40,payload_crc);put32(out+48,1);put32(out+44,dota_match_crc_update(0,out,64));return 1;
}
int dota_match_header_decode(DotaMatchHistoryMeta*m,const unsigned char in[64]){
 unsigned char b[64];uint32_t c;if(!m||!in)return 0;memcpy(b,in,64);c=get32(b+44);put32(b+44,0);
 if(get32(b)!=MATCH_SAVE_MAGIC||get32(b+4)!=1||get32(b+8)!=64||get32(b+12)!=DOTA_MATCH_SAVE_BYTES||get32(b+16)!=DOTA_MATCH_RECORD_BYTES||get32(b+20)!=50||get32(b+48)!=1||get32(b+52)||get32(b+56)||get32(b+60)||dota_match_crc_update(0,b,64)!=c)return 0;
 m->count=get32(b+24);m->next=get32(b+28);m->sequence=get32(b+32);m->next_match_id=get32(b+36);m->payload_crc=get32(b+40);
 return m->count<=50&&m->next<50&&(m->count==50||m->next==m->count)&&m->next_match_id!=0;
}
unsigned dota_match_history_encode(const DotaMatchHistory*h,void*memory,unsigned cap){
 unsigned char*out=memory;unsigned i;uint32_t c;if(!h||!out||cap<DOTA_MATCH_SAVE_BYTES)return 0;
 for(i=0;i<50;i++)if(!dota_match_record_encode(&h->records[i],out+64+i*DOTA_MATCH_RECORD_BYTES))return 0;
 c=dota_match_crc_update(0,out+64,50*DOTA_MATCH_RECORD_BYTES);return dota_match_header_encode(h,c,out)?DOTA_MATCH_SAVE_BYTES:0;
}
int dota_match_history_decode(DotaMatchHistory*h,const void*memory,unsigned n){
 const unsigned char*in=memory;DotaMatchHistoryMeta m;unsigned i,j;
 if(!h||!in||n!=DOTA_MATCH_SAVE_BYTES||!dota_match_header_decode(&m,in)||dota_match_crc_update(0,in+64,n-64)!=m.payload_crc)return 0;
 /* Validate all records before changing the caller's valid history. */
 for(i=0;i<50;i++){DotaMatchRecord r;int used=m.count==50||i<m.count;if(!dota_match_record_decode(&r,in+64+i*DOTA_MATCH_RECORD_BYTES)||(used?!r.match_id:r.match_id!=0))return 0;
  if(r.match_id)for(j=0;j<i;j++)if(get32(in+64+j*DOTA_MATCH_RECORD_BYTES)==r.match_id)return 0;
 }
 for(i=0;i<50;i++)if(!dota_match_record_decode(&h->records[i],in+64+i*DOTA_MATCH_RECORD_BYTES))return 0;
 h->count=m.count;h->next=m.next;h->sequence=m.sequence;h->next_match_id=m.next_match_id;return 1;
}
