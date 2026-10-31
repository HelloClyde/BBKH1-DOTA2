#include "neutral.h"
#include "neutral_catalog.h"
#include "items.h"
#include "items_ids.h"
#include "items_special.h"
#include "jungle.h"
#include "roster_heroes.h"
/* The installed neutral_items.txt supplies the current pools/costs. Valve's
 * Wandering Waters rules supply camp bundles and Turbo's halved times. */
static unsigned rng(DotaGame*g){g->seed=g->seed*1664525u+1013904223u;return g->seed;}
static int hero(const DotaGame*g,int h){return h>=0&&h<10&&g->units[h].active&&g->units[h].type==DOTA_HERO;}
static int live(const DotaGame*g,int h){return h>=0&&h<DOTA_MAX_UNITS&&g->units[h].active&&g->units[h].hp>0;}
static int nearby(const DotaUnit*u,int x,int y,int range){int dx=u->x-x,dy=u->y-y,w=range/4;return dx*dx+dy*dy<=w*w;}
static int cap(const DotaGame*g){static const int at[5]={0,9000,15000,21000,36000};int i,tier=1;if(g->tick>=42000)return 0x7fffffff;for(i=1;i<5;i++)if(g->tick>=at[i])tier=i+1;return 6+(tier-1)*10;}
static int next_tier(const DotaGame*g,int h){return g->items.neutral.crafted[h]<5?g->items.neutral.crafted[h]+1:5;}
static int unlocked(const DotaGame*g,int tier){static const int at[6]={0,0,9000,15000,21000,36000};return tier>=1&&tier<=5&&g->tick>=at[tier];}
void dota_neutral_init(DotaGame*g){int h;for(h=0;h<10;h++){g->items.neutral_item[h]=-1;g->items.neutral.enchantment[h]=-1;g->items.neutral.pending[h]=-1;}}
int dota_neutral_stones(const DotaGame*g,int h){return hero(g,h)?g->items.neutral.stones[h]:0;}
int dota_neutral_tier(const DotaGame*g,int h){return hero(g,h)?g->items.neutral.crafted[h]:0;}
int dota_neutral_cost(const DotaGame*g,int h){if(!hero(g,h))return 0;return g->items.neutral.crafted[h]==5?20+g->items.neutral.recrafts[h]*10:(g->items.neutral.crafted[h]?10:6);}
int dota_neutral_equipped(const DotaGame*g,int h){return hero(g,h)?g->items.neutral_item[h]:-1;}
int dota_neutral_enchantment(const DotaGame*g,int h){return hero(g,h)?g->items.neutral.enchantment[h]:-1;}
int dota_neutral_phase(const DotaGame*g,int h){return hero(g,h)?g->items.neutral.phase[h]:0;}
int dota_neutral_offer_count(const DotaGame*g,int h){return hero(g,h)?g->items.neutral.offer_count[h]:0;}
int dota_neutral_offer_item(const DotaGame*g,int h,int i){return hero(g,h)&&i>=0&&i<g->items.neutral.offer_count[h]?g->items.neutral.offers[h][i]:-1;}
int dota_neutral_can_begin(const DotaGame*g,int h){if(!hero(g,h))return 0;if(g->items.neutral.phase[h])return 1;return unlocked(g,next_tier(g,h))&&(g->items.neutral.crafted[h]<5||g->tick>=42000)&&g->items.neutral.stones[h]>=dota_neutral_cost(g,h);}
int dota_neutral_begin(DotaGame*g,int h){int pool[49],n=0,i,count,tier;DotaNeutralState*s=&g->items.neutral;if(!dota_neutral_can_begin(g,h))return 0;if(s->phase[h])return 1;tier=next_tier(g,h);for(i=0;i<49;i++)if(neutral_artifacts[i][1]==tier&&neutral_artifacts[i][0]!=g->items.neutral_item[h])pool[n++]=neutral_artifacts[i][0];count=tier==1?4:4;if(n<count)return 0;for(i=0;i<count;i++){int pick=(int)(rng(g)%(unsigned)n);s->offers[h][i]=(short)pool[pick];pool[pick]=pool[--n];}if(tier>1&&g->items.neutral_item[h]>=0)s->offers[h][count++]=(short)g->items.neutral_item[h];s->phase[h]=1;s->next_tier[h]=(unsigned char)tier;s->offer_count[h]=(unsigned char)count;return 1;}
int dota_neutral_choose(DotaGame*g,int h,int item){DotaNeutralState*s=&g->items.neutral;int i,tier,found=0;if(!hero(g,h))return 0;for(i=0;i<s->offer_count[h];i++)if(s->offers[h][i]==item)found=1;if(!found)return 0;tier=s->next_tier[h];if(s->phase[h]==1){int attribute=g->units[h].hero_id==DOTA_HERO_AXE?2:((g->units[h].hero_id==DOTA_HERO_JUGGERNAUT||g->units[h].hero_id==DOTA_HERO_SLARK)?4:8);if(dota_roster_is_hero(&g->units[h]))attribute=2<<dota_roster_base(g->units[h].hero_id)->primary;s->pending[h]=(short)item;s->offer_count[h]=0;for(i=0;i<44;i++)if(neutral_enchantments[i][1]==tier&&(neutral_enchantments[i][2]&(1|attribute))){int at=s->offer_count[h];if(at<5){s->offers[h][at]=neutral_enchantments[i][0];s->offer_count[h]++;}}s->phase[h]=2;return 2;}
 if(s->phase[h]!=2||!unlocked(g,tier)||s->stones[h]<dota_neutral_cost(g,h))return 0;
 s->stones[h]-=dota_neutral_cost(g,h);if(s->crafted[h]==5)s->recrafts[h]++;{int previous=g->items.neutral_item[h];g->items.neutral_item[h]=s->pending[h];dota_items_special_on_craft(g,h,previous,s->pending[h]);}s->enchantment[h]=(short)item;s->crafted[h]=tier;s->phase[h]=0;s->offer_count[h]=0;s->pending[h]=-1;dota_items_invalidate(g,h);dota_rules_item_recalc(g,h);return 1;
}
static int grant(DotaGame*g,int h,int n){int room=cap(g)-g->items.neutral.earned[h],give=n<room?n:room;if(!hero(g,h)||give<=0)return 0;g->items.neutral.stones[h]+=give;g->items.neutral.earned[h]+=give;return give;}
static void award(DotaGame*g,int h,int primary,int allies){int left=primary-grant(g,h,primary),i;unsigned used=1u<<h;left+=allies;while(left>0){int eligible[10],n=0;for(i=0;i<10;i++)if(hero(g,i)&&g->units[i].team==g->units[h].team&&!(used&(1u<<i))&&g->items.neutral.earned[i]<cap(g))eligible[n++]=i;if(!n)break;i=eligible[rng(g)%(unsigned)n];used|=1u<<i;left-=grant(g,i,1);}dota_rules_item_effect(g,DOTA_FX_GOLD,g->units[h].team,g->units[h].x,g->units[h].y,g->units[h].x,g->units[h].y,8,18);}
static void bundle(DotaGame*g,int victim,int killer){int i,collector=-1,best=0x7fffffff,enemy=0,primary=g->units[victim].type==DOTA_ANCIENT?4:3,allies=primary-1,x=g->units[victim].x,y=g->units[victim].y;if(!hero(g,killer))return;for(i=0;i<10;i++)if(hero(g,i)&&live(g,i)){if(g->units[i].team!=g->units[killer].team&&nearby(&g->units[i],x,y,800))enemy=1;if(g->units[i].team==g->units[killer].team&&nearby(&g->units[i],x,y,1200)){int dx=g->units[i].x-x,dy=g->units[i].y-y,d=dx*dx+dy*dy;if(i==killer){collector=i;best=-1;}else if(d<best){collector=i;best=d;}}}if(collector>=0&&!enemy){award(g,collector,primary,allies);return;}for(i=0;i<16;i++)if(!g->items.neutral.drops[i].active){DotaMadstoneDrop*d=&g->items.neutral.drops[i];d->active=1;d->x=x;d->y=y;d->primary=primary;d->allies=allies;break;}}
void dota_neutral_on_kill(DotaGame*g,int victim,int killer){int i,first,last;if(victim<0||victim>=DOTA_MAX_UNITS||!hero(g,killer))return;if(g->units[victim].type==DOTA_NEUTRAL&&victim>=DOTA_NEUTRAL_FIRST&&victim<DOTA_ITEM_UNIT_FIRST){int camp=g->units[victim].lane;first=dota_jungle_first(camp);last=dota_jungle_end(camp);if(first<0||last<0)return;}else if(g->units[victim].type==DOTA_ANCIENT&&victim>=DOTA_ANCIENT_FIRST&&victim<DOTA_ANCIENT_FIRST+DOTA_ANCIENT_CAMPS*3){first=DOTA_ANCIENT_FIRST+(victim-DOTA_ANCIENT_FIRST)/3*3;last=first+3;}else return;for(i=first;i<last;i++)if(i!=victim&&live(g,i))return;bundle(g,victim,killer);}
void dota_neutral_midas(DotaGame*g,int victim,int killer){if(victim>=0&&victim<DOTA_MAX_UNITS&&(g->units[victim].type==DOTA_NEUTRAL||g->units[victim].type==DOTA_ANCIENT))bundle(g,victim,killer);}
void dota_neutral_tick(DotaGame*g){int i,h;for(i=0;i<16;i++)if(g->items.neutral.drops[i].active){DotaMadstoneDrop*d=&g->items.neutral.drops[i];for(h=0;h<10;h++)if(hero(g,h)&&live(g,h)&&nearby(&g->units[h],d->x,d->y,100)){award(g,h,d->primary,d->allies);d->active=0;break;}}}
