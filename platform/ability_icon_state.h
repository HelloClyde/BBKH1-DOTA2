#ifndef DOTA_ABILITY_ICON_STATE_H
#define DOTA_ABILITY_ICON_STATE_H
#include "extra_ability.h"
#include "items.h"
#include "progression.h"

/* Presentation only: use the same costs and special recast/charge gates as
 * the cast providers. Never infer readiness from the last failure event. */
typedef struct {
    int rank,passive,control,cooldown,total_cooldown,mana_cost,missing_mana,charges,toggled;
} DotaAbilityIconState;

static DotaAbilityIconState ability_icon_state(const DotaGame *g,int h,int skill){
    DotaAbilityIconState s={0,0,0,0,0,0,0,-1,0};
    const DotaUnit *u;
    if(!g || (unsigned)h>=DOTA_HERO_COUNT || (unsigned)skill>4)return s;
    u=&g->units[h];
    if(skill==4){
        if(!extra_available(g,h))return s;
        s.rank=1;s.cooldown=extra_cooldown(g,h);
        if(dota_roster_is_hero(u)){
            s.mana_cost=dota_roster_extra_mana(g,h)*dota_items_mana_cost_percent((DotaGame*)g,h)/100;
            s.total_cooldown=dota_roster_extra_cooldown(g,h);
            if(u->hero_id==DOTA_HERO_SVEN)s.toggled=g->roster.hero[h].sven_fly;
        }else{
            s.mana_cost=dota_extra_shard_mana(g,h)*dota_items_mana_cost_percent((DotaGame*)g,h)/100;
            s.total_cooldown=dota_progression_value100(u,4,"AbilityCooldown",u->hero_id==DOTA_HERO_SLARK?7500:u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN?1200:1900)/5;
            s.total_cooldown=s.total_cooldown*(100-dota_items_cooldown_percent((DotaGame*)g,h))/100;
        }
    }else{
        s.rank=dota_progression_rank(u,skill);s.passive=dota_skill_is_passive(u->hero_id,skill);
        if(!s.rank || s.passive)return s;
        s.cooldown=u->cooldown[skill];s.total_cooldown=dota_skill_cooldown_ticks(g,h,skill);
        s.mana_cost=dota_skill_mana_cost(g,h,skill);
        if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==1 && dota_healing_ward(g,h)>=0){
            s.control=1;s.cooldown=s.total_cooldown=s.mana_cost=0;
        }
        if(u->hero_id==DOTA_HERO_PUDGE && skill==1)s.toggled=g->roster.hero[h].rot_on;
        if(dota_skill_autocast_enabled(g,h,skill))s.toggled=1;
        if(u->hero_id==DOTA_HERO_SNIPER && skill==0){
            s.charges=g->roster.hero[h].shrapnel_charges;
            s.total_cooldown=dota_progression_value100(u,0,"AbilityChargeRestoreTime",3500)/5;
            s.total_cooldown=s.total_cooldown*(100-dota_items_cooldown_percent((DotaGame*)g,h))/100;
            if(!s.charges && s.cooldown<=0){
                s.cooldown=g->roster.hero[h].shrapnel_restore_at-g->tick;
                if(s.cooldown<1)s.cooldown=1;
            }
        }
        if(u->hero_id==DOTA_HERO_SLARK && skill==1){
            const DotaExtraHero *e=&g->extra.hero[h];
            if(dota_hero_has_scepter(g,h) && !e->pounce_upgrade){s.charges=2;s.cooldown=0;}
            else if(e->pounce_upgrade){s.charges=e->pounce_charges;if(s.charges>0)s.cooldown=0;}
            if(s.charges>=0){
                s.total_cooldown=240*(100-dota_items_cooldown_percent((DotaGame*)g,h))/100;
                if(s.total_cooldown<1)s.total_cooldown=1;
            }
        }
    }
    if(s.cooldown<0)s.cooldown=0;
    s.missing_mana=dota_items_available_mana(g,h)<s.mana_cost;
    return s;
}
#endif
