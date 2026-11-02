#ifndef H1_DOTA_TELEPORT_STATE_H
#define H1_DOTA_TELEPORT_STATE_H
#include <stdint.h>
/* Source keeps the scroll cooldown when the last charge disappears. Recent
 * normal-scroll destinations last25s; upgraded scrolls do not add a record. */
typedef struct { int until,x,y; } DotaTeleportRecent;
#define DOTA_TELEPORT_HISTORY 4 /* 40s*25%itemCD*75%Arcane >=7.5s. */
typedef struct {
    int ready_tick[10];
    DotaTeleportRecent recent[10][DOTA_TELEPORT_HISTORY];
    uint16_t charges[10],buy_age[10];
} DotaTeleportState;
#endif
