#include "bosses.h"
#include "navigation.h"
#include "vision.h"
#include "roshan_data.h"
#include "turbo.h"
#include "match_time.h"

#define CAMP_PERIOD (60*DOTA_HZ)
#define ROSHAN_SLAM_VISUAL 20
#define ROSHAN_ROAR_VISUAL 37
#define ROSHAN_BASH_VISUAL 40

static int alive(const DotaUnit *u){return u->active && u->hp>0;}
static int distance(int x,int y,int xx,int yy){int dx=x-xx,dy=y-yy;return dx*dx+dy*dy;}
static int within(int squared,int source){return (long long)squared*16<=(long long)source*source;}
static int clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static unsigned next(DotaGame *g){g->seed=g->seed*1664525u+1013904223u;return g->seed>>8;}
static void clear_unit(DotaUnit *u){unsigned i;unsigned char *p=(unsigned char *)u;int generation=u->generation+1;for(i=0;i<sizeof(*u);++i)p[i]=0;u->generation=generation;u->hero_id=-1;u->target=u->taunt_source=u->dot_source=-1;for(i=0;i<6;++i)u->inventory[i]=-1;}
static void place(DotaUnit *u,int x,int y){int xx,yy;if(dota_nav_nearest(x,y,256,&xx,&yy)){u->x=xx;u->y=yy;}else{u->x=clamp(x,0,4095);u->y=clamp(y,0,4095);}}

int dota_boss_species(const DotaUnit *u){if(!u)return -1;if(u->type==DOTA_ROSHAN)return 0;if(u->type==DOTA_ANCIENT && (unsigned)u->tier<10)return u->tier+1;return -1;}
static const DotaWildSourceDef *definition(const DotaUnit *u){int s=dota_boss_species(u);return s<0?0:&dota_wild_source_defs[s];}
int dota_boss_bat_ticks(const DotaUnit *u){const DotaWildSourceDef *d=definition(u);return d?d->bat_ticks:0;}
int dota_boss_attack_point_ms(const DotaUnit *u){const DotaWildSourceDef *d=definition(u);return d?d->point_ms:0;}
int dota_boss_attack_speed_percent(const DotaUnit *u){const DotaWildSourceDef *d=definition(u);return d?d->base_attack_speed:100;}
int dota_boss_projectile_speed(const DotaUnit *u){const DotaWildSourceDef *d=definition(u);return d && u->type!=DOTA_ROSHAN?d->projectile_source:0;}
int dota_boss_magic_resist(const DotaUnit *u){const DotaWildSourceDef *d=definition(u);return d?d->magic_resist_pct:25;}
int dota_boss_hull_source(const DotaUnit *u){const DotaWildSourceDef *d=definition(u);return d?d->hull_source:0;}
int dota_boss_status_resist(const DotaUnit *u){return u && u->type==DOTA_ROSHAN?DOTA_ROSHAN_STATUS_RESIST_PERCENT:0;}

static int enemy(const DotaGame *g,int index,int target){
    const DotaUnit *u,*v;if((unsigned)target>=DOTA_MAX_UNITS || target==index)return 0;u=&g->units[index];v=&g->units[target];
    return alive(v) && v->team<2 && v->team!=u->team && v->type!=DOTA_TOWER && v->type!=DOTA_BASE && v->type!=DOTA_BUILDING && !dota_unit_invulnerable(g,target) && dota_pit_same_region(u->x,u->y,v->x,v->y);
}
static void copy_stats(DotaUnit *u,int species){
    const DotaWildSourceDef *d=&dota_wild_source_defs[species];u->active=1;u->team=DOTA_NEUTRAL_TEAM;u->hp=u->max_hp=d->hp;
    u->damage=d->damage_avg;u->armor=d->armor;u->range=d->range_source;u->speed=d->speed_source;u->level=species?3:30;
}
static int occupied(const DotaGame *g,int camp){
    int i,first=DOTA_ANCIENT_FIRST+camp*3,x=dota_ancient_camp_positions[camp][0],y=dota_ancient_camp_positions[camp][1];
    for(i=0;i<DOTA_MAX_UNITS;++i)if((i<first || i>=first+3) && alive(&g->units[i]) && g->units[i].type!=DOTA_BASE && g->units[i].type!=DOTA_TOWER && g->units[i].type!=DOTA_BUILDING && within(distance(x,y,g->units[i].x,g->units[i].y),600))return 1;
    return 0;
}
static void spawn_camp(DotaGame *g,int camp){
    static const int dx[3]={-18,18,0},dy[3]={12,12,-15};int j,group=(int)(next(g)%DOTA_ANCIENT_GROUP_COUNT);
    /* Original random camps avoid choosing the same subtype twice in a row. */
    while(group==g->bosses.ancient_group[camp])group=(int)(next(g)%DOTA_ANCIENT_GROUP_COUNT);
    g->bosses.ancient_group[camp]=group;g->bosses.ancient_state[camp]=DOTA_NEUTRAL_IDLE;
    for(j=0;j<3;++j){int species=dota_ancient_spawn_species[group][j];DotaUnit *u=&g->units[DOTA_ANCIENT_FIRST+camp*3+j];
        clear_unit(u);u->type=DOTA_ANCIENT;u->lane=camp;u->tier=species;copy_stats(u,species+1);
        place(u,dota_ancient_camp_positions[camp][0]+dx[j],dota_ancient_camp_positions[camp][1]+dy[j]);u->skill_x=u->x;u->skill_y=u->y;
    }
    g->bosses.ancient_spawn_tick[camp]=0;
}
static void spawn_roshan(DotaGame *g,int pit){
    DotaUnit *u=&g->units[DOTA_ROSHAN_INDEX];clear_unit(u);u->type=DOTA_ROSHAN;copy_stats(u,0);place(u,dota_roshan_pit_positions[pit][0],dota_roshan_pit_positions[pit][1]);
    u->max_hp+=DOTA_ROSHAN_HEALTH_PER_MINUTE*(dota_match_game_ticks(g)/CAMP_PERIOD);u->hp=u->max_hp;
    g->bosses.roshan_pit=pit;g->bosses.roshan_spawn_tick=0;g->bosses.roshan_slam_wait=g->bosses.roshan_roar_wait=g->bosses.roshan_spellblock_wait=0;g->bosses.roshan_regen_fraction=0;
    g->bosses.roshan_last_damage_tick=-200;u->skill_x=u->skill_y=0;
}
void dota_bosses_init(DotaGame *g){
    int i;unsigned char *p=(unsigned char *)&g->bosses;for(i=0;i<(int)sizeof(g->bosses);++i)p[i]=0;
    g->bosses.aegis_owner=-1;g->bosses.aegis_generation=-1;
    for(i=DOTA_ANCIENT_FIRST;i<DOTA_ANCIENT_FIRST+DOTA_ANCIENT_CAMPS*3;++i)clear_unit(&g->units[i]);
    for(i=0;i<DOTA_ANCIENT_CAMPS;++i){int j;g->bosses.ancient_group[i]=-1;g->bosses.ancient_spawn_tick[i]=dota_match_elapsed_tick(CAMP_PERIOD);for(j=0;j<3;++j){DotaUnit *u=&g->units[DOTA_ANCIENT_FIRST+i*3+j];u->type=DOTA_ANCIENT;u->lane=i;}}
    clear_unit(&g->units[DOTA_ROSHAN_INDEX]);g->units[DOTA_ROSHAN_INDEX].type=DOTA_ROSHAN;
    g->bosses.roshan_spawn_tick=dota_match_elapsed_tick(0);
}
static void camp_return(DotaGame *g,int camp){int i;g->bosses.ancient_state[camp]=DOTA_NEUTRAL_RETURN;for(i=DOTA_ANCIENT_FIRST+camp*3;i<DOTA_ANCIENT_FIRST+camp*3+3;++i)g->units[i].target=-1;}
void dota_bosses_on_damage(DotaGame *g,int victim,int source){
    DotaUnit *u;if((unsigned)victim>=DOTA_MAX_UNITS || !enemy(g,victim,source))return;u=&g->units[victim];
    if(u->type==DOTA_ROSHAN){u->target=source;g->bosses.roshan_last_damage_tick=g->tick;}
    else if(u->type==DOTA_ANCIENT && (unsigned)u->lane<DOTA_ANCIENT_CAMPS && g->bosses.ancient_state[u->lane]!=DOTA_NEUTRAL_RETURN){int i;
        g->bosses.ancient_state[u->lane]=DOTA_NEUTRAL_AGGRO;for(i=DOTA_ANCIENT_FIRST+u->lane*3;i<DOTA_ANCIENT_FIRST+u->lane*3+3;++i)if(alive(&g->units[i]))g->units[i].target=source;
    }
}
static void award_xp(DotaGame *g,int victim,int team,int amount){
    int i,count=0,share,remainder;DotaUnit *u=&g->units[victim];if((unsigned)team>=2)return;
    for(i=0;i<10;++i)if(alive(&g->units[i]) && g->units[i].type==DOTA_HERO && g->units[i].team==team && within(distance(u->x,u->y,g->units[i].x,g->units[i].y),DOTA_XP_RADIUS_SOURCE))++count;
    if(!count)return;
    share=amount/count;remainder=amount%count;
    for(i=0;i<10;++i)if(alive(&g->units[i]) && g->units[i].type==DOTA_HERO && g->units[i].team==team && within(distance(u->x,u->y,g->units[i].x,g->units[i].y),DOTA_XP_RADIUS_SOURCE)){dota_rules_grant_xp(g,i,share+(remainder>0));if(remainder)--remainder;}
}
int dota_bosses_on_killed(DotaGame *g,int victim,int source,int team){
    DotaUnit *u;const DotaWildSourceDef *d;int gold;if((unsigned)victim>=DOTA_MAX_UNITS)return 0;u=&g->units[victim];d=definition(u);if(!d)return 0;
    award_xp(g,victim,team,d->xp);gold=d->gold_min+(int)(next(g)%(unsigned)(d->gold_max-d->gold_min+1));
    if((unsigned)source<DOTA_HERO_COUNT && g->units[source].type==DOTA_HERO && g->units[source].team==team)g->units[source].gold+=dota_turbo_gold_reward(gold);
    dota_rules_effect(g,DOTA_FX_DEATH,team,u->x,u->y,u->x,u->y,50,16);u->active=0;u->hp=0;u->target=-1;u->channel_ticks=0;u->skill_ticks[0]=u->skill_ticks[1]=0;
    if(u->type==DOTA_ANCIENT && (unsigned)u->lane<DOTA_ANCIENT_CAMPS){int i,live=0;for(i=DOTA_ANCIENT_FIRST+u->lane*3;i<DOTA_ANCIENT_FIRST+u->lane*3+3;++i)if(alive(&g->units[i]))live=1;
        if(!live)g->bosses.ancient_spawn_tick[u->lane]=dota_match_elapsed_tick((dota_match_game_ticks(g)/CAMP_PERIOD+1)*CAMP_PERIOD);
    }else if(u->type==DOTA_ROSHAN){int i,recipient=-1,best=1500*1500;
        ++g->bosses.roshan_kills;g->bosses.roshan_spawn_tick=g->tick+DOTA_ROSHAN_RESPAWN_MIN_TICKS+(int)(next(g)%(DOTA_ROSHAN_RESPAWN_MAX_TICKS-DOTA_ROSHAN_RESPAWN_MIN_TICKS+1));
        u->respawn_ticks=g->bosses.roshan_spawn_tick-g->tick;
        for(i=0;i<10;++i)if(alive(&g->units[i]) && g->units[i].team==team && dota_pit_same_region(u->x,u->y,g->units[i].x,g->units[i].y)){
            int d2=distance(u->x,u->y,g->units[i].x,g->units[i].y);if(within(d2,1500) && d2<best){best=d2;recipient=i;}
        }
        if(recipient>=0){g->bosses.aegis_owner=recipient;g->bosses.aegis_generation=g->units[recipient].generation;g->bosses.aegis_expire_tick=g->tick+DOTA_TURBO_AEGIS_SECONDS*DOTA_HZ;g->bosses.aegis_revive_tick=0;g->last_event=16;g->last_event_ticks=60;}
    }
    return 1;
}
int dota_bosses_aegis_ticks(const DotaGame *g,int hero){
    return g && (unsigned)hero<10 && g->bosses.aegis_owner==hero && g->bosses.aegis_generation==g->units[hero].generation && alive(&g->units[hero]) && g->bosses.aegis_expire_tick>g->tick?g->bosses.aegis_expire_tick-g->tick:0;
}
int dota_bosses_aegis_on_death(DotaGame *g,int hero){
    DotaBossState *b=&g->bosses;if((unsigned)hero>=10)return 0;
    b->roar_disarm_ticks[hero]=b->roar_vulnerable_ticks[hero]=0;
    if(b->aegis_owner!=hero || b->aegis_generation!=g->units[hero].generation || b->aegis_expire_tick<=g->tick || b->aegis_revive_tick)return 0;
    b->aegis_x=g->units[hero].x;b->aegis_y=g->units[hero].y;b->aegis_revive_tick=g->tick+DOTA_AEGIS_REINCARNATE_TICKS;b->aegis_expire_tick=0;
    g->units[hero].respawn_ticks=DOTA_AEGIS_REINCARNATE_TICKS;g->last_event=17;g->last_event_ticks=60;return 1;
}
int dota_bosses_aegis_respawn(DotaGame *g,int hero,int old_generation,int *x,int *y){
    DotaBossState *b=&g->bosses;if(b->aegis_owner!=hero || b->aegis_generation!=old_generation || !b->aegis_revive_tick || g->tick<b->aegis_revive_tick)return 0;
    *x=b->aegis_x;*y=b->aegis_y;b->aegis_owner=b->aegis_generation=-1;b->aegis_revive_tick=0;return 1;
}
int dota_bosses_disarmed(const DotaGame *g,int index){return g && (unsigned)index<10 && g->bosses.roar_disarm_ticks[index]>0;}
int dota_bosses_incoming_damage_percent(const DotaGame *g,int index){return g && (unsigned)index<10 && g->bosses.roar_vulnerable_ticks[index]>0?125:100;}
int dota_bosses_spell_block(DotaGame *g,int target){
    if(target!=DOTA_ROSHAN_INDEX || g->units[target].type!=DOTA_ROSHAN || !alive(&g->units[target]) || g->bosses.roshan_spellblock_wait)return 0;
    g->bosses.roshan_spellblock_wait=DOTA_ROSHAN_SPELL_BLOCK_TICKS;g->last_event=18;g->last_event_ticks=60;return 1;
}
void dota_bosses_on_attack_hit(DotaGame *g,int source,int target){
    DotaUnit *u,*v;if(source!=DOTA_ROSHAN_INDEX || g->units[source].type!=DOTA_ROSHAN || !alive(&g->units[source]) || !enemy(g,source,target))return;u=&g->units[source];v=&g->units[target];++g->bosses.roshan_bash_count;
    if(next(g)%100u<DOTA_ROSHAN_BASH_CHANCE_PERCENT){
        if(v->stun_ticks<DOTA_ROSHAN_BASH_STUN_TICKS)v->stun_ticks=DOTA_ROSHAN_BASH_STUN_TICKS;
        dota_rules_damage(g,target,DOTA_ROSHAN_BASH_BONUS_DAMAGE,source,1);u->skill_x=3;u->skill_ticks[0]=ROSHAN_BASH_VISUAL;
    }
}
static void ancient_tick(DotaGame *g,int index){
    DotaUnit *u=&g->units[index];const DotaWildSourceDef *d=definition(u);int camp=u->lane,target=u->target,home=1,i;
    if(!alive(u) || !d || (unsigned)camp>=DOTA_ANCIENT_CAMPS)return;
    if(d->regen_per_sec_x100>0){int gain=(int)(((long long)g->tick*d->regen_per_sec_x100)/2000-((long long)(g->tick-1)*d->regen_per_sec_x100)/2000);u->hp=clamp(u->hp+gain,0,u->max_hp);}
    if(!within(distance(u->x,u->y,u->skill_x,u->skill_y),1200))camp_return(g,camp);
    if(g->bosses.ancient_state[camp]==DOTA_NEUTRAL_RETURN){
        dota_rules_move(g,index,u->skill_x,u->skill_y);u->hp=clamp(u->hp+(u->max_hp+199)/200,0,u->max_hp);
        for(i=DOTA_ANCIENT_FIRST+camp*3;i<DOTA_ANCIENT_FIRST+camp*3+3;++i)if(alive(&g->units[i]) && distance(g->units[i].x,g->units[i].y,g->units[i].skill_x,g->units[i].skill_y)>16*16)home=0;
        if(home)g->bosses.ancient_state[camp]=DOTA_NEUTRAL_IDLE;
        return;
    }
    if(u->taunt_ticks>0 && enemy(g,index,u->taunt_source))target=u->taunt_source;
    if(!enemy(g,index,target))target=-1;
    if(target<0)target=dota_rules_find_enemy(g,index,d->acquire_source);
    if(!enemy(g,index,target)){u->target=-1;if(g->bosses.ancient_state[camp]==DOTA_NEUTRAL_AGGRO)camp_return(g,camp);return;}
    if(!within(distance(g->units[target].x,g->units[target].y,u->skill_x,u->skill_y),1500)){camp_return(g,camp);return;}
    if(g->bosses.ancient_state[camp]==DOTA_NEUTRAL_IDLE)dota_bosses_on_damage(g,index,target);
    u->target=target;dota_rules_attack_or_move(g,index,target);
}
static void roshan_impact(DotaGame *g,int kind){
    DotaUnit *u=&g->units[DOTA_ROSHAN_INDEX];int i,minute=dota_match_game_ticks(g)/CAMP_PERIOD,radius=kind==1?DOTA_ROSHAN_SLAM_RADIUS_SOURCE:DOTA_ROSHAN_ROAR_RADIUS_SOURCE;
    for(i=0;i<DOTA_MAX_UNITS;++i)if(enemy(g,DOTA_ROSHAN_INDEX,i) && within(distance(u->x,u->y,g->units[i].x,g->units[i].y),radius)){
        DotaUnit *v=&g->units[i];int amount=kind==1?DOTA_ROSHAN_SLAM_BASE_DAMAGE+minute*DOTA_ROSHAN_SLAM_DAMAGE_PER_MINUTE:DOTA_ROSHAN_ROAR_BASE_DAMAGE+minute*DOTA_ROSHAN_ROAR_DAMAGE_PER_MINUTE;
        if(kind==1){if(v->type==DOTA_CREEP)amount+=100;v->slow_ticks=DOTA_ROSHAN_SLAM_SLOW_TICKS;v->buff_ticks=DOTA_ROSHAN_SLAM_SLOW_PERCENT;}
        dota_rules_damage(g,i,amount,DOTA_ROSHAN_INDEX,1);
        if(kind==2 && i<10 && alive(v)){g->bosses.roar_disarm_ticks[i]=DOTA_ROSHAN_ROAR_DISARM_TICKS;g->bosses.roar_vulnerable_ticks[i]=DOTA_ROSHAN_ROAR_VULNERABLE_TICKS;}
    }
    dota_rules_effect(g,DOTA_FX_BURST,DOTA_NEUTRAL_TEAM,u->x,u->y,u->x,u->y,(radius+3)/4,10);
}
static void roshan_tick(DotaGame *g){
    DotaBossState *b=&g->bosses;DotaUnit *u=&g->units[DOTA_ROSHAN_INDEX];int target,home_x,home_y,pit=b->roshan_pit;
    if(!alive(u)){
        if(b->roshan_spawn_tick>0){u->respawn_ticks=clamp(b->roshan_spawn_tick-g->tick,0,DOTA_ROSHAN_RESPAWN_MAX_TICKS);if(g->tick>=b->roshan_spawn_tick)spawn_roshan(g,dota_match_game_ticks(g)>=DOTA_ROSHAN_FIRST_MOVE_TICKS?dota_match_is_night(g):0);}
        return;
    }
    if(b->roshan_slam_wait)--b->roshan_slam_wait;
    if(b->roshan_roar_wait)--b->roshan_roar_wait;
    if(b->roshan_spellblock_wait)--b->roshan_spellblock_wait;
    if(u->skill_ticks[0]>0)--u->skill_ticks[0];
    if(u->skill_ticks[1]>0){
        if(u->stun_ticks || u->taunt_ticks){u->skill_ticks[0]=u->skill_ticks[1]=0;u->channel_ticks=0;}
        else if(--u->skill_ticks[1]==0)roshan_impact(g,u->skill_x);
    }
    {int desired_hp=dota_wild_source_defs[0].hp+DOTA_ROSHAN_HEALTH_PER_MINUTE*(dota_match_game_ticks(g)/CAMP_PERIOD);
     if(u->max_hp!=desired_hp && u->max_hp>0){u->hp=clamp((int)((long long)u->hp*desired_hp/u->max_hp),1,desired_hp);u->max_hp=desired_hp;}}
    b->roshan_regen_fraction+=dota_wild_source_defs[0].regen_per_sec_x100;
    if(b->roshan_regen_fraction>=2000){u->hp=clamp(u->hp+b->roshan_regen_fraction/2000,0,u->max_hp);b->roshan_regen_fraction%=2000;}
    if(u->taunt_ticks>0){
        u->target=enemy(g,DOTA_ROSHAN_INDEX,u->taunt_source)?u->taunt_source:-1;
        if(u->target>=0 && !u->stun_ticks && !u->channel_ticks)dota_rules_attack_or_move(g,DOTA_ROSHAN_INDEX,u->target);
        return;
    }
    if(dota_match_game_ticks(g)>=DOTA_ROSHAN_FIRST_MOVE_TICKS){
        int desired=dota_match_is_night(g);
        if(desired!=pit){
            home_x=dota_roshan_pit_positions[desired][0];home_y=dota_roshan_pit_positions[desired][1];u->target=-1;u->speed=dota_wild_source_defs[0].speed_source;
            dota_rules_move(g,DOTA_ROSHAN_INDEX,home_x,home_y);
            if(distance(u->x,u->y,home_x,home_y)<=24*24)b->roshan_pit=desired;
            return;
        }
    }
    u->speed=dota_wild_source_defs[0].speed_source;
    home_x=dota_roshan_pit_positions[pit][0];home_y=dota_roshan_pit_positions[pit][1];target=u->target;
    if(!enemy(g,DOTA_ROSHAN_INDEX,target))target=dota_rules_find_enemy(g,DOTA_ROSHAN_INDEX,dota_wild_source_defs[0].acquire_source);
    if(!enemy(g,DOTA_ROSHAN_INDEX,target) || !within(distance(u->x,u->y,home_x,home_y),1500)){
        u->target=-1;if(distance(u->x,u->y,home_x,home_y)>16*16)dota_rules_move(g,DOTA_ROSHAN_INDEX,home_x,home_y);return;
    }
    u->target=target;if(u->stun_ticks || u->channel_ticks || u->skill_ticks[1]>0)return;
    if(u->hp<=u->max_hp/2 && !b->roshan_roar_wait){u->skill_x=2;u->skill_ticks[0]=ROSHAN_ROAR_VISUAL;u->skill_ticks[1]=10;u->channel_ticks=10;b->roshan_roar_wait=DOTA_ROSHAN_ROAR_COOLDOWN_TICKS;return;}
    if(!b->roshan_slam_wait && within(distance(u->x,u->y,g->units[target].x,g->units[target].y),DOTA_ROSHAN_SLAM_RADIUS_SOURCE)){
        u->skill_x=1;u->skill_ticks[0]=ROSHAN_SLAM_VISUAL;u->skill_ticks[1]=(DOTA_ROSHAN_SLAM_POINT_MS*DOTA_HZ+999)/1000;u->channel_ticks=u->skill_ticks[1];b->roshan_slam_wait=DOTA_ROSHAN_SLAM_COOLDOWN_TICKS;return;
    }
    dota_rules_attack_or_move(g,DOTA_ROSHAN_INDEX,target);
}
void dota_bosses_tick(DotaGame *g){
    int camp,i;DotaBossState *b=&g->bosses;if(g->paused || g->state!=DOTA_PLAYING)return;
    for(i=0;i<10;++i){if(b->roar_disarm_ticks[i]>0)--b->roar_disarm_ticks[i];if(b->roar_vulnerable_ticks[i]>0)--b->roar_vulnerable_ticks[i];}
    if(b->aegis_owner>=0 && ((unsigned)b->aegis_owner>=10 || (!b->aegis_revive_tick && b->aegis_expire_tick<=g->tick) || g->units[b->aegis_owner].generation!=b->aegis_generation || g->units[b->aegis_owner].type!=DOTA_HERO)){b->aegis_owner=b->aegis_generation=-1;b->aegis_expire_tick=b->aegis_revive_tick=0;}
    for(camp=0;camp<DOTA_ANCIENT_CAMPS;++camp){int alive_count=0;
        for(i=DOTA_ANCIENT_FIRST+camp*3;i<DOTA_ANCIENT_FIRST+camp*3+3;++i)if(alive(&g->units[i]))++alive_count;
        if(!alive_count && b->ancient_spawn_tick[camp]>0 && g->tick>=b->ancient_spawn_tick[camp] && dota_match_ticks(g)%CAMP_PERIOD==0){if(!occupied(g,camp))spawn_camp(g,camp);else b->ancient_spawn_tick[camp]=g->tick+CAMP_PERIOD;}
        for(i=DOTA_ANCIENT_FIRST+camp*3;i<DOTA_ANCIENT_FIRST+camp*3+3;++i)ancient_tick(g,i);
    }
    roshan_tick(g);
}
