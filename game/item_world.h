#ifndef H1_DOTA_ITEM_WORLD_H
#define H1_DOTA_ITEM_WORLD_H
#include "game.h"
#include "items.h"
typedef struct { int bat_ticks,attack_speed_x100,point_ms,projectile_speed; } DotaItemWorldAttackTraits;
void dota_item_world_init(DotaGame *game);
int dota_item_world_rune_illusions(DotaGame *game,int owner,int duration_ticks);
void dota_item_world_tick(DotaGame *game);
void dota_item_world_update_unit(DotaGame *game,int index);
int dota_item_world_incoming(DotaGame *game,int victim,int amount);
int dota_item_world_on_death(DotaGame *game,int victim,int source);
/* -1: ordinary world visibility applies,0: hidden,1: item grants true sight. */
int dota_item_world_visible(const DotaGame *game,int observer,int target);
int dota_item_world_shared_vision(const DotaGame *game,int observer,int target);
int dota_item_world_owner(const DotaGame *game,int unit);
int dota_item_world_dominated(const DotaGame *game,int unit);
int dota_item_world_dominate_cooldown(const DotaGame *game,int hero);
int dota_item_world_attack_traits(const DotaGame *game,int unit,DotaItemWorldAttackTraits *out);
int dota_item_world_armor_delta(const DotaGame *game,int unit);
int dota_item_world_magic_resist(const DotaGame *game,int unit,int original);
int dota_item_world_max_health_percent(const DotaGame *game,int unit);
int dota_item_world_accuracy(const DotaGame *game,int unit);
int dota_item_world_attack_damage(const DotaGame *game,int source,int target,int amount);
void dota_item_world_apply_stats(const DotaGame *game,int unit,DotaItemStats *out);
void dota_item_world_on_attack_hit(DotaGame *game,int source,int target,int dealt);
void dota_item_world_on_damage(DotaGame *game,int victim,int source,int dealt);
int dota_item_world_rooted(const DotaGame *game,int unit);
void dota_item_world_purge(DotaGame *game,int unit,int positive);
int dota_item_world_statuses(const DotaGame *game,int unit,DotaItemStatus *out,int capacity);
int dota_item_world_drop(DotaGame *game,int owner,int item,int x,int y);
int dota_item_world_drop_mode(DotaGame *game,int owner,int item,int x,int y,int spell_mode);
void dota_rules_item_attack_once(DotaGame *game,int source,int target);
void dota_rules_item_move_order(DotaGame *game,int unit,int x,int y);
void dota_rules_item_kill(DotaGame *game,int victim,int source);
#endif
