#ifndef DOTA_MATCH_HISTORY_STATE_H
#define DOTA_MATCH_HISTORY_STATE_H
#include <stdint.h>
/* Per-game counters only: 204 bytes. History itself belongs to firmware heap. */
typedef struct {
 uint32_t match_id;
 uint32_t hero_damage[10],building_damage[10],ally_healing[10];
 uint32_t last_hits[10],denies[10];
} DotaMatchStats;
#endif
