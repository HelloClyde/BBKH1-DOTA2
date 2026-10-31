#ifndef H1_DOTA_TURBO_H
#define H1_DOTA_TURBO_H
#include "game.h"
int dota_turbo_gold_reward(int base);
int dota_turbo_xp_award(DotaGame *game,int hero,int base);
int dota_turbo_level_xp(int level);
int dota_turbo_respawn_ticks(int level);
int dota_turbo_passive_gold_at_tick(int tick);
int dota_turbo_tower_damage(const DotaUnit *attacker,const DotaUnit *victim,int base);
#endif
