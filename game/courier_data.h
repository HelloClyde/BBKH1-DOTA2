#ifndef H1_DOTA_COURIER_DATA_H
#define H1_DOTA_COURIER_DATA_H
#include <stdint.h>

#define DOTA_COURIER_HEROES 10
#define DOTA_COURIER_SLOTS 6
#define DOTA_COURIER_STASH_SLOTS 12
#define DOTA_COURIER_SPEED_SOURCE 1100
#define DOTA_COURIER_HANDOFF_SOURCE 400
#define DOTA_COURIER_AUTO_SECONDS 5

enum { DOTA_COURIER_HOME=0,DOTA_COURIER_DELIVER=1,DOTA_COURIER_RETURN=2,DOTA_COURIER_WAIT_DEAD=3 };
typedef struct {
    int x_q16,y_q16,target_x,target_y,phase,previous_x,previous_y;
    int16_t cargo[DOTA_COURIER_SLOTS],stash[DOTA_COURIER_STASH_SLOTS];
    uint16_t cargo_age[DOTA_COURIER_SLOTS],stash_age[DOTA_COURIER_STASH_SLOTS];
} DotaCourier;
typedef struct { DotaCourier hero[DOTA_COURIER_HEROES];int last_tick; } DotaCourierState;

#endif
