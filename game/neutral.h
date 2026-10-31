#ifndef H1_DOTA_NEUTRAL_H
#define H1_DOTA_NEUTRAL_H
#include "game.h"
void dota_neutral_init(DotaGame *game);
void dota_neutral_tick(DotaGame *game);
int dota_neutral_can_begin(const DotaGame *game,int hero);
int dota_neutral_begin(DotaGame *game,int hero);
int dota_neutral_phase(const DotaGame *game,int hero);
int dota_neutral_offer_count(const DotaGame *game,int hero);
int dota_neutral_offer_item(const DotaGame *game,int hero,int index);
/*2 artifact chosen, enchantment choices ready;1 equipped;0 rejected atomically.*/
int dota_neutral_choose(DotaGame *game,int hero,int item);
int dota_neutral_stones(const DotaGame *game,int hero);
int dota_neutral_tier(const DotaGame *game,int hero);
int dota_neutral_cost(const DotaGame *game,int hero);
int dota_neutral_equipped(const DotaGame *game,int hero);
int dota_neutral_enchantment(const DotaGame *game,int hero);
void dota_neutral_on_kill(DotaGame *game,int victim,int killer);
void dota_neutral_midas(DotaGame *game,int victim,int killer);
#endif
