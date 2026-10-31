#include "turbo.h"

/* Original default noncumulative table, server.dll raw4a8a620. */
static const unsigned short level_xp[30]={0,240,400,520,600,680,760,800,900,1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000,2200,2400,2600,2800,3000,4000,5000,6000,7000,8000};
int dota_turbo_level_xp(int level){return level>0 && level<30?level_xp[level]:0;}
int dota_turbo_gold_reward(int base){return base>0?base*2:base;}
int dota_turbo_tower_damage(const DotaUnit *a,const DotaUnit *v,int base){
    /* Native mode311: IsTower, no backdoor-active modifier, IsCreep,
     * !IsSummoned. H1 has dependency protection but no active backdoor buff;
     * its ordinary lane/small-neutral/ancient units are not summoned. */
    return a && v && v->type==DOTA_TOWER && (a->type==DOTA_CREEP || a->type==DOTA_NEUTRAL || a->type==DOTA_ANCIENT)?base*2:base;
}

/* GoldPerTick1, normal GoldTickTime.75s, Turbo time scale.75 and gold2x.
 * At20Hz this is2 gold per45/4 ticks. A floor accumulator preserves the
 * exact213 1/3GPM rate without accumulating rounding drift or floats. */
static int passive_total(int tick){return tick/45*8+((tick%45)*4/45)*2;}
int dota_turbo_passive_gold_at_tick(int tick){return tick>0?passive_total(tick)-passive_total(tick-1):0;}

int dota_turbo_respawn_ticks(int level){
    static const unsigned char seconds[30]={12,15,18,21,24,26,28,30,32,34,36,44,46,48,50,52,54,65,70,75,80,85,90,95,100,100,100,100,100,100};
    if(level<1)level=1;
    if(level>30)level=30;
    return seconds[level-1]*DOTA_HZ*3/4;
}

int dota_turbo_xp_award(DotaGame *g,int hero,int base){
    DotaUnit *u;int amount,level,progress,highest=1,i,gap,need,span,remaining,factor;
    if(!g || (unsigned)hero>=DOTA_HERO_COUNT || base<=0)return 0;
    u=&g->units[hero];amount=base*2;
    if(u->level>=25 || g->tick-g->turbo.last_xp_snap_tick[hero]<=5*DOTA_HZ)return amount;
    for(i=0;i<DOTA_HERO_COUNT;++i)if(g->units[i].active && g->units[i].type==DOTA_HERO && g->units[i].level>highest)highest=g->units[i].level;
    level=u->level;progress=u->xp+amount;
    /* Native projection uses strict greater-than at a level boundary. */
    while(level<30 && progress>dota_turbo_level_xp(level)){progress-=dota_turbo_level_xp(level);++level;}
    gap=highest-level;if(gap<4 || level>=30)return amount;
    span=dota_turbo_level_xp(level);need=span-progress;if(need<0)need=0;
    /* Native clamp((gap-progressFraction-4)/2,0,1), then missingXP*factor.
     * Integer rational arithmetic rounds the final bonus down, like its
     * native int-XP filter, without lossy per-step percent quantization. */
    remaining=need;factor=(gap-5)*span+remaining;
    if(factor<0)factor=0;
    if(factor>2*span)factor=2*span;
    amount+=(int)((long long)need*factor/(2*span));
    g->turbo.last_xp_snap_tick[hero]=g->tick;
    return amount;
}
