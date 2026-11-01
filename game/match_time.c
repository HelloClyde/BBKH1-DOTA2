#include "match_time.h"
#include <limits.h>
#define PREGAME_TICKS (60*DOTA_HZ)
#define DAY_PHASE_TICKS (300*DOTA_HZ)
int dota_match_pregame_ticks(void){return PREGAME_TICKS;}
int dota_match_ticks(const DotaGame *g){return (g?g->tick:0)-PREGAME_TICKS;}
int dota_match_seconds(const DotaGame *g){int t=dota_match_ticks(g);return t<0?-((-t+DOTA_HZ-1)/DOTA_HZ):t/DOTA_HZ;}
int dota_match_started(const DotaGame *g){return g&&g->tick>=PREGAME_TICKS;}
int dota_match_game_ticks(const DotaGame *g){int t=dota_match_ticks(g);return t>0?t:0;}
int dota_match_is_night(const DotaGame *g){return (dota_match_game_ticks(g)/DAY_PHASE_TICKS)&1;}
int dota_match_elapsed_tick(int match_tick){if(match_tick>INT_MAX-PREGAME_TICKS)return INT_MAX;if(match_tick<-PREGAME_TICKS)return 0;return match_tick+PREGAME_TICKS;}
