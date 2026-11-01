#include "runes.h"
#include "../private/generated/dota_runes_data.h"
#include "bosses.h"
#include "items.h"
#include "items_internal.h"
#include "item_world.h"
#include "navigation.h"
#include "objectives.h"
#include "roster_heroes.h"
#include "turbo.h"
#include "vision.h"
#include "extra_heroes.h"
#include "match_time.h"

const DotaPoint dota_rune_positions[DOTA_RUNE_SPOTS]={{1638,1770},{2343,2352},{1799,940},{2197,3213}};
const short dota_rune_heights[DOTA_RUNE_SPOTS]={10,14,42,42};
static void clear(void *p,unsigned size){unsigned char *b=p;while(size--)*b++=0;}
static int hero_live(const DotaGame *g,int h){return g && (unsigned)h<DOTA_HERO_COUNT && g->units[h].type==DOTA_HERO && g->units[h].active && g->units[h].hp>0 && (unsigned)g->units[h].team<2;}
static int nearby(int x,int y,int xx,int yy,int radius){long long dx=x-xx,dy=y-yy;return (dx*dx+dy*dy)*16<=(long long)radius*radius;}
static unsigned draw(DotaGame *g){g->seed=g->seed*1664525u+1013904223u;return g->seed;}
static DotaRuneHero *touch(DotaGame *g,int h){DotaRuneHero *s=&g->runes.hero[h];if(s->generation!=g->units[h].generation){clear(s,sizeof(*s));s->generation=g->units[h].generation;}return s;}
static int has(const DotaGame *g,int h,int kind){return hero_live(g,h) && (unsigned)kind<DOTA_RUNE_COUNT && g->runes.hero[h].generation==g->units[h].generation && g->runes.hero[h].until[kind]>g->tick;}
static void spawn(DotaGame *g,int spot,int kind,int at){g->runes.kind[spot]=kind;g->runes.spawn_tick[spot]=at;}
static int power_type(DotaGame *g){
    unsigned mask=g->runes.remaining_power_types,eligible=mask;int count=0,i,pick;
    if(!mask){mask=DOTA_RUNE_POWER_TYPES_MASK;eligible=mask;if((unsigned)g->runes.last_power_type<DOTA_RUNE_COUNT)eligible&=~(1u<<g->runes.last_power_type);}
    for(i=0;i<DOTA_RUNE_COUNT;++i)if(eligible&(1u<<i))++count;
    pick=(int)(draw(g)%(unsigned)count);
    for(i=0;i<DOTA_RUNE_COUNT;++i)if(eligible&(1u<<i)){if(!pick){g->runes.remaining_power_types=mask&~(1u<<i);g->runes.last_power_type=i;return i;}--pick;}
    return DOTA_RUNE_AMPLIFY;
}
void dota_runes_init(DotaGame *g){
    int i;if(!g)return;clear(&g->runes,sizeof(g->runes));
    for(i=0;i<DOTA_RUNE_SPOTS;++i)spawn(g,i,-1,0);
    g->runes.next_power_tick=DOTA_RUNE_WATER_FIRST_SECONDS*DOTA_HZ;
    g->runes.next_bounty_tick=0;
    g->runes.last_power_spot=g->runes.last_power_type=-1;
}
int dota_rune_kind(const DotaGame *g,int spot){return g && (unsigned)spot<DOTA_RUNE_SPOTS?g->runes.kind[spot]:-1;}
int dota_rune_nearest(const DotaGame *g,int x,int y,int radius){
    int i,best=-1;long long best_d=(long long)radius*radius;
    if(!g || radius<0)return -1;
    for(i=0;i<DOTA_RUNE_SPOTS;++i)if(g->runes.kind[i]>=0 && dota_pit_same_region(g->units[g->player].x,g->units[g->player].y,dota_rune_positions[i].x,dota_rune_positions[i].y)){
        long long dx=x-dota_rune_positions[i].x,dy=y-dota_rune_positions[i].y,d=dx*dx+dy*dy;
        if(d<=best_d){best=i;best_d=d;}
    }return best;
}
void dota_rune_cancel(DotaGame *g){if(g)g->runes.order=0;}
int dota_rune_order(DotaGame *g,int spot){
    DotaUnit *u;if(!g || g->paused || g->state!=DOTA_PLAYING || (unsigned)spot>=DOTA_RUNE_SPOTS || g->runes.kind[spot]<0 || !hero_live(g,g->player))return 0;
    u=&g->units[g->player];dota_items_cancel_teleport(g,g->player);if(u->stun_ticks || dota_roster_stun(g,g->player) || u->taunt_ticks || u->channel_ticks || dota_omnislash_phase(g,g->player))return 0;
    if(!dota_pit_same_region(u->x,u->y,dota_rune_positions[spot].x,dota_rune_positions[spot].y))return 0;
    dota_objective_cancel(g);dota_move_to(g,dota_rune_positions[spot].x,dota_rune_positions[spot].y);
    g->runes.order=spot+1;g->runes.order_generation=u->generation;return 1;
}
int dota_rune_player(DotaGame *g,const DotaInput *input){
    DotaUnit *u;int spot;if(!g || !g->runes.order || g->paused || g->state!=DOTA_PLAYING)return 0;
    u=&g->units[g->player];spot=g->runes.order-1;
    if(!hero_live(g,g->player) || u->generation!=g->runes.order_generation || u->stun_ticks || dota_roster_stun(g,g->player) || u->taunt_ticks || u->channel_ticks || dota_omnislash_phase(g,g->player) ||
       (input && (input->move_x || input->move_y || input->actions)) || (unsigned)spot>=DOTA_RUNE_SPOTS || g->runes.kind[spot]<0){dota_rune_cancel(g);return 0;}
    if(nearby(u->x,u->y,dota_rune_positions[spot].x,dota_rune_positions[spot].y,DOTA_RUNE_PICKUP_RADIUS_SOURCE)){dota_rune_pickup(g,g->player,spot);dota_rune_cancel(g);return 1;}
    u->target=-1;u->move_order=0;dota_rules_move(g,g->player,dota_rune_positions[spot].x,dota_rune_positions[spot].y);return 1;
}
int dota_rune_pickup(DotaGame *g,int h,int spot){
    DotaRuneHero *s;int kind,i,amount;
    if(!g || g->paused || g->state!=DOTA_PLAYING || !hero_live(g,h) || (unsigned)spot>=DOTA_RUNE_SPOTS)return 0;
    kind=g->runes.kind[spot];if((unsigned)kind>=DOTA_RUNE_COUNT || kind==DOTA_RUNE_XP)return 0;
    if(!nearby(g->units[h].x,g->units[h].y,dota_rune_positions[spot].x,dota_rune_positions[spot].y,DOTA_RUNE_PICKUP_RADIUS_SOURCE) ||
       !dota_pit_same_region(g->units[h].x,g->units[h].y,dota_rune_positions[spot].x,dota_rune_positions[spot].y))return 0;
    s=touch(g,h);
    if(kind==DOTA_RUNE_ILLUSION){if(!dota_item_world_rune_illusions(g,h,75*DOTA_HZ))return 0;}
    else if(kind==DOTA_RUNE_WATER)dota_items_restore(g,h,h,40,80);
    else if(kind==DOTA_RUNE_BOUNTY){
        /* Native pickup uses the rune's birth time, quantized in4-minute
         * steps:40 +1.5*floor(spawn_minutes/4)*4, before Turbo gold filter. */
        amount=40+6*(g->runes.spawn_tick[spot]/(4*60*DOTA_HZ));
        for(i=0;i<DOTA_HERO_COUNT;++i)if(g->units[i].active && g->units[i].type==DOTA_HERO && g->units[i].team==g->units[h].team)g->units[i].gold+=dota_turbo_gold_reward(amount);
    }else{
        s->until[kind]=g->tick+dota_rune_duration_seconds[kind]*DOTA_HZ;
        if(kind==DOTA_RUNE_REGENERATION)s->regeneration_reduced_until=0;
        if(kind==DOTA_RUNE_INVISIBILITY)s->invisibility_fade_until=g->tick+2*DOTA_HZ;
        if(kind==DOTA_RUNE_SHIELD)s->shield=g->units[h].max_hp/2;
    }
    dota_rules_recalc(g,h);
    g->runes.kind[spot]=-1;g->runes.spawn_tick[spot]=0;
    if(g->runes.order==spot+1)dota_rune_cancel(g);
    return 1;
}
void dota_runes_tick(DotaGame *g){
    int h,kind,at,spot,match;if(!g || g->paused || g->state!=DOTA_PLAYING)return;match=dota_match_ticks(g);
    for(h=0;h<DOTA_HERO_COUNT;++h){DotaRuneHero *s=touch(g,h);
        if(!hero_live(g,h)){clear(s->until,sizeof(s->until));s->shield=0;continue;}
        for(kind=0;kind<DOTA_RUNE_COUNT;++kind)if(s->until[kind] && s->until[kind]<=g->tick){s->until[kind]=0;if(kind==DOTA_RUNE_SHIELD)s->shield=0;if(kind==DOTA_RUNE_AMPLIFY)dota_rules_recalc(g,h);}
        if(has(g,h,DOTA_RUNE_REGENERATION) && g->units[h].hp>=g->units[h].max_hp && g->units[h].mana>=g->units[h].max_mana)s->until[DOTA_RUNE_REGENERATION]=0;
    }
    /* Minute-zero river Bounties precede later Water/power spawns even when a
     * restored/boundary fixture asks the scheduler to catch up in one tick. */
    while(match>=g->runes.next_bounty_tick){at=g->runes.next_bounty_tick;if(!at){spawn(g,0,DOTA_RUNE_BOUNTY,at);spawn(g,1,DOTA_RUNE_BOUNTY,at);}spawn(g,2,DOTA_RUNE_BOUNTY,at);spawn(g,3,DOTA_RUNE_BOUNTY,at);g->runes.next_bounty_tick+=DOTA_RUNE_BOUNTY_INTERVAL_SECONDS*DOTA_HZ;}
    while(match>=g->runes.next_power_tick){
        at=g->runes.next_power_tick;g->runes.kind[0]=g->runes.kind[1]=-1;
        if(at<DOTA_RUNE_POWER_FIRST_SECONDS*DOTA_HZ){spawn(g,0,DOTA_RUNE_WATER,at);spawn(g,1,DOTA_RUNE_WATER,at);}
        else{spot=(int)((draw(g)>>16)&1u);spawn(g,spot,power_type(g),at);g->runes.last_power_spot=spot;}
        g->runes.next_power_tick+=DOTA_RUNE_POWER_INTERVAL_SECONDS*DOTA_HZ;
    }
}
int dota_rune_status(const DotaGame *g,int h,int kind){return has(g,h,kind)?g->runes.hero[h].until[kind]-g->tick:0;}
const char *dota_rune_name(int kind){static const char *const names[DOTA_RUNE_COUNT]={"强化伤害","急速","幻象","隐身","恢复","赏金","奥术","水符","智慧","护盾"};return (unsigned)kind<DOTA_RUNE_COUNT?names[kind]:"";}
int dota_rune_base_damage_percent(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_AMPLIFY)?80:0;}
int dota_rune_spell_amp_percent(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_AMPLIFY)?15:0;}
int dota_rune_mana_percent(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_ARCANE)?70:100;}
int dota_rune_cooldown_percent(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_ARCANE)?75:100;}
int dota_rune_haste_speed(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_HASTE)?550:0;}
int dota_rune_invisible(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_INVISIBILITY) && g->tick>=g->runes.hero[h].invisibility_fade_until;}
int dota_rune_hp_regen100(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_REGENERATION)?g->units[h].max_hp*(g->runes.hero[h].regeneration_reduced_until>g->tick?1:6):0;}
int dota_rune_mana_regen100(const DotaGame *g,int h){return has(g,h,DOTA_RUNE_REGENERATION)?g->units[h].max_mana*(g->runes.hero[h].regeneration_reduced_until>g->tick?1:6):0;}
int dota_rune_before_damage(DotaGame *g,int h,int amount){
    DotaRuneHero *s;int absorbed;if(amount<=0 || !has(g,h,DOTA_RUNE_SHIELD))return amount;s=&g->runes.hero[h];
    absorbed=s->shield<amount?s->shield:amount;s->shield-=absorbed;if(!s->shield)s->until[DOTA_RUNE_SHIELD]=0;return amount-absorbed;
}
void dota_rune_on_damage(DotaGame *g,int victim,int source,int dealt){
    int owner;if(dealt<=0 || !has(g,victim,DOTA_RUNE_REGENERATION) || (unsigned)source>=DOTA_MAX_UNITS || g->units[source].team==g->units[victim].team)return;
    owner=g->units[source].type==DOTA_HERO?source:dota_item_world_owner(g,source);
    if((unsigned)owner<DOTA_HERO_COUNT && g->units[owner].type==DOTA_HERO)g->runes.hero[victim].regeneration_reduced_until=g->tick+3*DOTA_HZ;
}
void dota_rune_on_action(DotaGame *g,int h){if(!g || (unsigned)h>=DOTA_HERO_COUNT)return;g->runes.hero[h].until[DOTA_RUNE_INVISIBILITY]=0;g->runes.hero[h].invisibility_fade_until=0;}
