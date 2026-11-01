#ifndef H1_DOTA_SIGHT_H
#define H1_DOTA_SIGHT_H
#include "game.h"
#include <stdint.h>
#define DOTA_SIGHT_GRID_SIDE 256
#define DOTA_SIGHT_CELL_WORLD 16
#define DOTA_SIGHT_HEIGHT_UNKNOWN (-32768)
/* Caller-owned four-byte-aligned heap; borrowed immutable terrain planes.
 * Both planes may be NULL for flat sight. Detach before freeing any memory. */
unsigned dota_sight_workspace_bytes(void);
int dota_sight_attach(const int16_t *height_world,const uint8_t *tree_bits,void *workspace,unsigned bytes);
void dota_sight_detach(void);
void dota_sight_reset(void);
/* Keep explored terrain, discard this tick's shared snapshot. */
void dota_sight_invalidate(void);
/* Pure status query: never refreshes or changes the shared-sight snapshot. */
int dota_sight_is_current(const DotaGame *game);
int dota_sight_team_visible(const DotaGame *game,int observer,int target);
int dota_sight_position_visible(const DotaGame *game,int observer,int x,int y);
/* Ignores target invisibility. */
int dota_sight_team_position_visible(const DotaGame *game,int team,int x,int y);
/* O(1) after lazy tick refresh: 0 unseen, 1 explored, 2 current sight.
 * Missing workspace returns 0 rather than scanning units per fog pixel. */
int dota_sight_fog_state(const DotaGame *game,int team,int x,int y);
int dota_rules_unit_vision_source(const DotaGame *game,int unit);
#endif
