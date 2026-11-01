#ifndef H1_DOTA_MATCH_TIME_H
#define H1_DOTA_MATCH_TIME_H
#include "game.h"
/* g->tick remains a nonnegative elapsed simulation clock. Skill/item/status
 * deadlines keep that domain, including during the playable pre-game minute.
 * Only scheduled match events and the HUD use this signed horn-relative clock.
 * Installed Turbo SetPreGameTime(60) and day rate receipts: provenance/match-time. */
int dota_match_pregame_ticks(void);
int dota_match_ticks(const DotaGame *game);
/* -1 remaining tick displays -1 second; positive seconds round down. */
int dota_match_seconds(const DotaGame *game);
int dota_match_started(const DotaGame *game);
int dota_match_game_ticks(const DotaGame *game); /* zero throughout pre-game */
int dota_match_is_night(const DotaGame *game);
/* Convert an absolute horn-relative schedule to an elapsed-clock deadline. */
int dota_match_elapsed_tick(int match_tick);
#endif
