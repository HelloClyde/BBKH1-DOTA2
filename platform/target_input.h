#ifndef DOTA_TARGET_INPUT_H
#define DOTA_TARGET_INPUT_H
#include "game.h"
#include "items.h"
#include "extra_ability.h"

enum { DOTA_INPUT_TARGET_NONE=0,DOTA_INPUT_TARGET_POINT=1,
       DOTA_INPUT_TARGET_ENEMY=2,DOTA_INPUT_TARGET_ANY=3 };

/* Unit-target and point-target inputs deliberately stay separate. Point spells
 * use the clicked terrain position and never magnetize to a nearby enemy. */
static inline int dota_input_skill_target_kind(const DotaGame *g,int skill){
    const DotaUnit *u=&g->units[g->player];int h=u->hero_id;
    if(skill==4){
        if(!extra_available(g,g->player))return DOTA_INPUT_TARGET_NONE;
        if(h==DOTA_HERO_NECROPHOS)return DOTA_INPUT_TARGET_ANY;
        if(h==DOTA_HERO_TIDEHUNTER)return DOTA_INPUT_TARGET_ENEMY;
        return extra_range(g,g->player)?DOTA_INPUT_TARGET_POINT:DOTA_INPUT_TARGET_NONE;
    }
    if((unsigned)skill>=4u||dota_skill_is_passive(h,skill))return DOTA_INPUT_TARGET_NONE;
    if(h==DOTA_HERO_PUDGE&&skill==3&&dota_hero_has_shard(g,g->player))return DOTA_INPUT_TARGET_ANY;
    if((h==DOTA_HERO_AXE&&(skill==1||skill==3)) ||
       (h==DOTA_HERO_LINA&&skill==3) || (h==DOTA_HERO_JUGGERNAUT&&skill==3) ||
       (h==DOTA_HERO_CRYSTAL_MAIDEN&&skill==1) || (h==DOTA_HERO_SLARK&&skill==2) ||
       (h==DOTA_HERO_NECROPHOS&&skill==3) || (h==DOTA_HERO_PUDGE&&skill==3) ||
       (h==DOTA_HERO_SNIPER&&skill==3) || (h==DOTA_HERO_SVEN&&skill==0) ||
       (h==DOTA_HERO_TIDEHUNTER&&skill==0&&!dota_hero_has_scepter(g,g->player)))return DOTA_INPUT_TARGET_ENEMY;
    if((h==DOTA_HERO_JUGGERNAUT&&skill==1)||(h==DOTA_HERO_SLARK&&skill==1))return DOTA_INPUT_TARGET_POINT;
    return dota_skill_cast_range_source(g,g->player,skill)?DOTA_INPUT_TARGET_POINT:DOTA_INPUT_TARGET_NONE;
}
static inline int dota_input_skill_unit_valid(const DotaGame *g,int skill,int target){
    const DotaUnit *u;int kind=dota_input_skill_target_kind(g,skill);
    if((unsigned)target>=DOTA_MAX_UNITS||kind<DOTA_INPUT_TARGET_ENEMY||target==g->player)return 0;
    u=&g->units[target];
    if(!u->active||u->hp<=0||u->type==DOTA_TOWER||u->type==DOTA_BASE||u->type==DOTA_BUILDING||dota_unit_invulnerable(g,target))return 0;
    if(kind==DOTA_INPUT_TARGET_ENEMY&&u->team==g->units[g->player].team)return 0;
    if(skill==3&&g->units[g->player].hero_id==DOTA_HERO_PUDGE&&u->team==g->units[g->player].team&&u->type!=DOTA_HERO)return 0;
    if(skill==4&&g->units[g->player].hero_id==DOTA_HERO_TIDEHUNTER&&u->type!=DOTA_HERO)return 0;
    return dota_unit_visible(g,g->player,target);
}
#endif
