#ifndef DOTA_TARGET_INPUT_H
#define DOTA_TARGET_INPUT_H
#include "game.h"
#include "items.h"
#include "extra_ability.h"
#include "cast_order.h"

enum { DOTA_INPUT_TARGET_NONE=0,DOTA_INPUT_TARGET_POINT=1,
       DOTA_INPUT_TARGET_ENEMY=2,DOTA_INPUT_TARGET_ANY=3 };

/* Unit-target and point-target inputs deliberately stay separate. Point spells
 * use the clicked terrain position and never magnetize to a nearby enemy. */
static inline int dota_input_skill_target_kind(const DotaGame *g,int skill){
    return g?dota_cast_order_skill_kind(g,g->player,skill):DOTA_INPUT_TARGET_NONE;
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
