#ifndef H1_DOTA_ROSTER_HEROES_H
#define H1_DOTA_ROSTER_HEROES_H
#include "game.h"
#include "roster_heroes_state.h"
enum { DOTA_ROSTER_PUDGE=6,DOTA_ROSTER_SNIPER=7,DOTA_ROSTER_SVEN=8,DOTA_ROSTER_TIDE=9 };
typedef struct {
    int primary,str100,agi100,int100,str_gain100,agi_gain100,int_gain100;
    int naked_damage,armor,range,speed,attack_speed100,bat_ticks,point_ms,projectile_speed,hp_regen100;
} DotaRosterBase;
typedef struct {int hero,skill,harmful,ticks,stacks;} DotaRosterStatus;
const DotaRosterBase *dota_roster_base(int hero_id);
int dota_roster_is_hero(const DotaUnit *unit);
int dota_roster_passive(int hero_id,int skill);
void dota_roster_init(DotaGame *game);
int dota_roster_mana(const DotaUnit *unit,int skill);
int dota_roster_cooldown(const DotaGame *game,int caster,int skill);
int dota_roster_range(const DotaGame *game,int caster,int skill);
int dota_roster_radius(const DotaGame *game,int caster,int skill);
int dota_roster_cast(DotaGame *game,int caster,int skill,int x,int y);
int dota_roster_cast_while_channel(const DotaGame *game,int caster,int skill);
void dota_roster_cancel_cast(DotaGame *game,int caster);
int dota_roster_cast_extra(DotaGame *game,int caster,int x,int y);
int dota_roster_extra_mana(const DotaGame *game,int caster);
int dota_roster_extra_range(const DotaGame *game,int caster);
int dota_roster_extra_cooldown(const DotaGame *game,int caster);
int dota_roster_swallowed_by(const DotaGame *game,int target);
int dota_roster_swallowed(const DotaGame *game,int pudge);
int dota_roster_try_leave(DotaGame *game,int target);
void dota_roster_tick(DotaGame *game);
void dota_roster_refresh(DotaGame *game,int caster);
void dota_roster_stat_delta(const DotaGame *game,int hero,int *str,int *agi,int *intel);
int dota_roster_max_health_bonus(const DotaGame *game,int hero);
int dota_roster_attack_range_bonus(const DotaGame *game,int hero);
int dota_roster_vision_bonus(const DotaGame *game,int hero);
/* Active Take Aim restricts this observer's complete sight to a140 degree
 * facing cone. Team sharing and strict Roshan separation stay with caller. */
int dota_roster_vision_point(const DotaGame *game,int hero,int x,int y,int base_range);
int dota_roster_area_vision(const DotaGame *game,int team,int x,int y);
int dota_roster_attack_speed_delta(const DotaGame *game,int unit);
int dota_roster_base_damage_percent(const DotaGame *game,int hero);
int dota_roster_base_damage_reduction(const DotaGame *game,int unit);
int dota_roster_bonus_damage(const DotaGame *game,int hero);
int dota_roster_armor(const DotaGame *game,int target);
int dota_roster_move_percent(const DotaGame *game,int target);
int dota_roster_slow_resist(const DotaGame *game,int hero);
int dota_roster_heal_percent(const DotaGame *game,int target);
int dota_roster_disarmed(const DotaGame *game,int target);
int dota_roster_stun(const DotaGame *game,int target);
int dota_roster_invulnerable(const DotaGame *game,int target);
int dota_roster_moving(const DotaGame *game,int target);
int dota_roster_allow_move(const DotaGame *game,int target,int x,int y);
int dota_roster_move_cap_source(const DotaGame *game,int target);
/* Exactly one call per landed ordinary attack, before target defenses. Misses
 * never roll Headshot. raw/dealt distinguish Cleave's outgoing base from HP. */
int dota_roster_attack_damage(DotaGame *game,int source,int target,int amount);
void dota_roster_attack_hit(DotaGame *game,int source,int target,int raw,int dealt);
int dota_roster_physical_block(const DotaGame *game,int source,int target);
int dota_roster_before_damage(DotaGame *game,int source,int target,int amount,int mode);
void dota_roster_after_damage(DotaGame *game,int source,int target,int dealt,int mode);
void dota_roster_on_kill(DotaGame *game,int victim,int source);
void dota_roster_dispel(DotaGame *game,int target,int positive,int strong);
int dota_roster_statuses(const DotaGame *game,int target,DotaRosterStatus *out,int capacity);
int dota_roster_controls_aux(const DotaGame *game,int target);
void dota_roster_aux_tick(DotaGame *game,int target);
/* Root-owned primitive uses the itemworld32-slot pool; fish/anchor kinds6/7.
 * Fish health1 is enemy-destroyable; anchor health4 uses one HP per attack. */
int dota_rules_roster_spawn_aux(DotaGame *game,int owner,int kind,int x,int y,int hp);
/* Real immediate attack snapshot with normal procs, bonus physical damage,
 * no attack-range/BAT consumption. Used by Anchor Smash, not a plain spell. */
int dota_rules_roster_attack(DotaGame *game,int source,int target,int bonus_damage);
int dota_rules_roster_attack_scaled(DotaGame *game,int source,int target,int bonus_damage,int percent);
void dota_rules_roster_interrupt(DotaGame *game,int target);
#endif
