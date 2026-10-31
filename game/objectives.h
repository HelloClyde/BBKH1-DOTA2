#ifndef H1_DOTA_OBJECTIVES_H
#define H1_DOTA_OBJECTIVES_H
#include "game.h"

enum { DOTA_OBJECTIVE_EVENT_WISDOM=19,DOTA_OBJECTIVE_EVENT_LOTUS=20,
       DOTA_OBJECTIVE_EVENT_WATCHER_ON=21,DOTA_OBJECTIVE_EVENT_WATCHER_OFF=22,
       DOTA_OBJECTIVE_EVENT_UNAVAILABLE=23,DOTA_OBJECTIVE_EVENT_NO_LOTUS=24 };

extern const DotaPoint dota_objective_positions[DOTA_OBJECTIVES];
extern const short dota_objective_heights[DOTA_OBJECTIVES];
/* World radius supplied to nearest; click selection itself never grants loot. */
int dota_objective_nearest(const DotaGame *game,int x,int y,int world_radius);
int dota_objective_order(DotaGame *game,int objective);
void dota_objective_cancel(DotaGame *game);
/* Call at start of player processing. Returns1 while walking/channeling to a
 * watcher. Explicit move/attack/cast/stop input cancels it and returns0. */
int dota_objective_player(DotaGame *game,const DotaInput *input);
void dota_objectives_init(DotaGame *game);
void dota_objectives_tick(DotaGame *game);
int dota_objective_owner(const DotaGame *game,int objective);
int dota_objective_amount(const DotaGame *game,int objective);
int dota_objective_progress(const DotaGame *game,int objective); /* 0..1000. */
int dota_objective_vision(const DotaGame *game,int team,int x,int y);
int dota_objective_use_lotus(DotaGame *game,int hero);
int dota_objective_lotus_amount(const DotaGame *game,int hero); /* real9-slot ownership, basic equivalents. */
int dota_objective_lotus_heal(const DotaGame *game,int hero); /* next usable main-inventory125/400/900. */
int dota_objective_pool_grade(const DotaGame *game,int objective); /* 1/2/3. */
int dota_objective_pool_heal(const DotaGame *game,int objective); /* 125/400/900. */

#endif
