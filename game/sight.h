#ifndef H1_DOTA_SIGHT_H
#define H1_DOTA_SIGHT_H
#include "game.h"
void dota_sight_reset(void);
/* Pure status query: never refreshes or changes the shared-sight snapshot. */
int dota_sight_is_current(const DotaGame *game);
int dota_sight_team_visible(const DotaGame *game,int observer,int target);
int dota_sight_position_visible(const DotaGame *game,int observer,int x,int y);
int dota_rules_unit_vision_source(const DotaGame *game,int unit);
#endif
