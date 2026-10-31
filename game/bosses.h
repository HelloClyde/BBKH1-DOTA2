#ifndef H1_DOTA_BOSSES_H
#define H1_DOTA_BOSSES_H
#include "game.h"

/* species0=Roshan/model48,1..10=Ancient tier0..9/models49..58. */
int dota_boss_species(const DotaUnit *unit);
int dota_boss_bat_ticks(const DotaUnit *unit);
int dota_boss_attack_point_ms(const DotaUnit *unit);
int dota_boss_attack_speed_percent(const DotaUnit *unit);
int dota_boss_projectile_speed(const DotaUnit *unit);
int dota_boss_magic_resist(const DotaUnit *unit);
int dota_boss_hull_source(const DotaUnit *unit);
int dota_boss_status_resist(const DotaUnit *unit);

void dota_bosses_init(DotaGame *game);
void dota_bosses_tick(DotaGame *game);
void dota_bosses_on_damage(DotaGame *game,int victim,int source);
int dota_bosses_on_killed(DotaGame *game,int victim,int source,int source_team);
void dota_bosses_on_attack_hit(DotaGame *game,int source,int target);
int dota_bosses_spell_block(DotaGame *game,int target);
int dota_bosses_aegis_on_death(DotaGame *game,int hero);
int dota_bosses_aegis_respawn(DotaGame *game,int hero,int old_generation,int *x,int *y);
int dota_bosses_aegis_ticks(const DotaGame *game,int hero);
int dota_bosses_disarmed(const DotaGame *game,int index);
int dota_bosses_incoming_damage_percent(const DotaGame *game,int index);

/* Core owns normal attacks, fixed-point movement, damage/kill attribution,
 * XP level-up, and visual effects. Boss AI uses those same public bridges. */
void dota_rules_attack_or_move(DotaGame *game,int index,int target);
void dota_rules_move(DotaGame *game,int index,int x,int y);
void dota_rules_damage(DotaGame *game,int victim,int amount,int source,int magical);
int dota_rules_find_enemy(DotaGame *game,int index,int source_radius);
void dota_rules_grant_xp(DotaGame *game,int index,int amount);
void dota_rules_effect(DotaGame *game,int kind,int team,int x,int y,int to_x,int to_y,int radius,int ticks);
#endif
