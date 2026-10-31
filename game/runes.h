#ifndef H1_DOTA_RUNES_H
#define H1_DOTA_RUNES_H
#include "game.h"
#include "runes_state.h"
extern const DotaPoint dota_rune_positions[DOTA_RUNE_SPOTS];
extern const short dota_rune_heights[DOTA_RUNE_SPOTS];
void dota_runes_init(DotaGame *game);
void dota_runes_tick(DotaGame *game);
int dota_rune_kind(const DotaGame *game,int spot);
int dota_rune_nearest(const DotaGame *game,int x,int y,int world_radius);
int dota_rune_order(DotaGame *game,int spot);
void dota_rune_cancel(DotaGame *game);
int dota_rune_player(DotaGame *game,const DotaInput *input);
int dota_rune_pickup(DotaGame *game,int hero,int spot);
int dota_rune_status(const DotaGame *game,int hero,int kind); /* remaining ticks */
const char *dota_rune_name(int kind);
/* Existing core query hooks; these functions do not mutate game rules. */
int dota_rune_base_damage_percent(const DotaGame *game,int hero); /* additive */
int dota_rune_spell_amp_percent(const DotaGame *game,int hero); /* additive */
int dota_rune_mana_percent(const DotaGame *game,int hero); /* multiplier */
int dota_rune_cooldown_percent(const DotaGame *game,int hero); /* multiplier */
int dota_rune_haste_speed(const DotaGame *game,int hero); /* Source units/s,0 off */
int dota_rune_invisible(const DotaGame *game,int hero);
int dota_rune_hp_regen100(const DotaGame *game,int hero);
int dota_rune_mana_regen100(const DotaGame *game,int hero);
int dota_rune_before_damage(DotaGame *game,int hero,int amount);
void dota_rune_on_damage(DotaGame *game,int victim,int source,int dealt);
void dota_rune_on_action(DotaGame *game,int hero); /* real attack/cast/item use */
#endif
