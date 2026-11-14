#ifndef DOTA_EXTRA_HEROES_H
#define DOTA_EXTRA_HEROES_H
#include "game.h"
int dota_extra_is_hero(const DotaUnit *u);
int dota_extra_mana(const DotaUnit *u,int skill);
int dota_extra_cooldown(const DotaUnit *u,int skill);
void dota_extra_refresh(DotaGame *g,int caster);
int dota_extra_range(const DotaUnit *u,int skill);
int dota_extra_radius(const DotaUnit *u,int skill);
int dota_extra_effect_radius(const DotaGame *g,int caster,int skill);
int dota_extra_cast(DotaGame *g,int caster,int skill,int x,int y);
int dota_skill_autocast_supported(const DotaGame *g,int hero,int skill);
int dota_skill_autocast_enabled(const DotaGame *g,int hero,int skill);
int dota_skill_autocast_toggle(DotaGame *g,int hero,int skill);
/* Called only when a real basic attack reaches its release/impact phase. */
int dota_extra_autocast_attack(DotaGame *g,int source,int target);
int dota_extra_cast_shard(DotaGame *g,int caster,int x,int y);
int dota_extra_shard_mana(const DotaGame *g,int caster);
int dota_extra_shard_range(const DotaGame *g,int caster);
int dota_extra_shard_radius(const DotaGame *g,int caster);
int dota_extra_shard_cooldown(const DotaGame *g,int caster);
/* CM's Shard uses the existing summon and per-hero extra-state storage. */
int dota_rules_crystal_clone(DotaGame *g,int caster);
int dota_extra_invulnerable(const DotaGame *g,int hero);
void dota_extra_tick(DotaGame *g);
void dota_extra_on_attack_hit(DotaGame *g,int source,int target);
void dota_extra_on_kill(DotaGame *g,int victim,int source);
void dota_extra_on_damage(DotaGame *g,int victim,int source);
int dota_extra_kill_source(const DotaGame *g,int victim,int source);
void dota_extra_stat_delta(const DotaGame *g,int hero,int *str,int *agi,int *intel);
int dota_extra_base_attributes(const DotaUnit *u,int *str100,int *agi100,int *int100);
int dota_extra_attack_speed(const DotaGame *g,int hero);
int dota_extra_move_percent(const DotaGame *g,int hero);
int dota_extra_move_flat(const DotaGame *g,int hero);
int dota_extra_regen_flat100(const DotaGame *g,int hero);
int dota_extra_additional_regen100(const DotaGame *g,int hero);
int dota_extra_additional_mana_regen100(const DotaGame *g,int hero);
int dota_extra_attack_speed_delta(const DotaGame *g,int hero);
int dota_extra_shadow_dance_passive_active(const DotaGame *game,int hero);
int dota_extra_magic_resist_delta(const DotaGame *g,int hero);
int dota_extra_physical_immune(const DotaGame *g,int hero);
int dota_extra_disarmed(const DotaGame *g,int hero);
int dota_extra_target_hidden(const DotaGame *g,int observer,int target);
int dota_extra_allow_move(const DotaGame *g,int hero,int x,int y);
int dota_extra_moving(const DotaGame *g,int hero);
int dota_extra_heal_amount(const DotaGame *g,int hero,int amount);
void dota_extra_heal(DotaGame *g,int hero,int amount);
void dota_extra_mana_restore(DotaGame *g,int hero,int amount);
int dota_extra_mana_restore_amount(const DotaGame *g,int hero,int amount);
void dota_extra_dispel(DotaGame *g,int hero,int positive);
/* Core bridges retain normal combat ownership and the existing event ABI. */
void dota_rules_skill_event(DotaGame *g,int caster,int skill,int target,int x,int y,int duration,int kind);
void dota_rules_skill_effect(DotaGame *g,int caster,int skill,int kind,int x,int y,int tx,int ty,int radius,int ticks);
void dota_rules_recalc(DotaGame *g,int hero);
void dota_rules_attack_once(DotaGame *g,int source,int target);
void dota_rules_set_position(DotaGame *g,int hero,int x,int y);
int dota_rules_health_regen100(const DotaGame *g,int hero);
int dota_rules_aoe_radius(const DotaGame *g,int hero,int base);
int dota_rules_unit_vision_source(const DotaGame *g,int unit);
#endif
