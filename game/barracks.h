#ifndef H1_DOTA_BARRACKS_H
#define H1_DOTA_BARRACKS_H
#include "game.h"

/* Existing BUILDING type; tier0 is a filler. No unit/game ABI expansion. */
enum {DOTA_BUILDING_FILLER=0,DOTA_BARRACKS_MELEE=1,DOTA_BARRACKS_RANGED=2};
#define DOTA_BARRACKS_COUNT 12
#define DOTA_BARRACKS_RANGED_FIRST 24
int dota_barracks_kind(const DotaUnit *unit);
int dota_barracks_ordinal(int team,int lane,int kind);
int dota_barracks_index(int team,int lane,int kind);
int dota_high_tower_alive(const DotaGame *game,int team,int slot);
unsigned dota_barracks_alive_mask(const DotaGame *game);
void dota_barracks_create(DotaGame *game,int team,int lane,int kind);
/* Called before the next simulation tick, after the complete tower-death
 * attack/status callback stack has unwound. Only kill_unit marks a slot. */
void dota_barracks_commit_tower_deaths(DotaGame *game);
#endif
