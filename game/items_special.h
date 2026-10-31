#ifndef H1_DOTA_ITEMS_SPECIAL_H
#define H1_DOTA_ITEMS_SPECIAL_H
#include "items.h"
void dota_items_special_init(DotaGame *game);
void dota_items_special_tick(DotaGame *game);
/* Call before derived STR/INT/AGI stats. Never calls the stats dispatcher. */
void dota_items_special_stats(DotaGame *game,int hero,DotaItemStats *stats);
/* Called before main evasion roll, then damage after main crit calculations. */
int dota_items_special_true_strike(DotaGame *game,int source,int target);
int dota_items_special_attack_damage(DotaGame *game,int source,int target,int damage);
void dota_items_special_attack_hit(DotaGame *game,int source,int target,int dealt);
int dota_items_special_before_damage(DotaGame *game,int source,int target,int amount,int magical);
int dota_items_special_damage_percent(DotaGame *game,int source,int target,int amount,int magical);
int dota_items_special_outgoing_damage(DotaGame *game,int source,int target,int amount,int magical);
void dota_items_special_after_damage(DotaGame *game,int source,int target,int dealt,int magical);
void dota_items_special_on_cast(DotaGame *game,int caster);
void dota_items_special_on_craft(DotaGame *game,int hero,int previous,int item);
void dota_items_special_on_death(DotaGame *game,int hero);
/* Core bridge modifiers use100 as the unchanged result. */
int dota_items_special_bat_percent(const DotaGame *game,int hero);
int dota_items_special_max_hp_percent(const DotaGame *game,int hero);
int dota_items_special_max_mana_percent(const DotaGame *game,int hero);
int dota_items_special_cast_speed_percent(const DotaGame *game,int hero);
int dota_items_special_slow_resistance(const DotaGame *game,int hero);
int dota_items_special_aoe_percent(const DotaGame *game,int hero);
int dota_items_special_vision_percent(const DotaGame *game,int hero);
int dota_items_special_night_vision_bonus(const DotaGame *game,int hero);
int dota_items_special_debuff_percent(const DotaGame *game,int hero);
int dota_items_special_knockback_resistance(const DotaGame *game,int hero);
int dota_items_special_cooldown(const DotaGame *game,int hero,int item);
int dota_items_special_statuses(const DotaGame *game,int hero,DotaItemStatus *out,int capacity);
void dota_items_special_display_stats(int item,DotaItemRecord *record);
/* Only Source AbilityValues tagged apply_curio_bonus are scaled. */
int dota_items_special_parameter(DotaGame *game,int hero,int item,int key,int fallback_x100);
int dota_items_special_bounty_roll(DotaGame *game,int hero,int minimum,int maximum);
int dota_items_special_move_bonus(const DotaGame *game,int unit);
int dota_items_special_water_cooldown_step(DotaGame *game,int hero,int item,int slot);
int dota_items_special_charges(const DotaGame *game,int hero,int item,int slot);
void dota_items_special_dispel(DotaGame *game,int hero,int positive);
int dota_rules_unit_in_water(const DotaGame *game,int unit);
#endif
