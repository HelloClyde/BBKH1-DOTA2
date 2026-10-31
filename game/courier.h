#ifndef H1_DOTA_COURIER_H
#define H1_DOTA_COURIER_H
#include "game.h"

enum { DOTA_COURIER_EVENT_ORDERED=25,DOTA_COURIER_EVENT_DELIVERED=26,DOTA_COURIER_EVENT_STASH_FULL=27 };
typedef struct {
    int x,y,target_x,target_y,phase,moving,cargo,stash,eta_ticks;
} DotaCourierView;
void dota_couriers_init(DotaGame *game);
void dota_couriers_tick(DotaGame *game);
/* Purchase can succeed while dead. Gold is charged exactly once; in-flight
 * cargo never grants attributes or occupies a hero inventory slot early. */
int dota_courier_buy(DotaGame *game,int hero,int item);
int dota_courier_can_buy(const DotaGame *game,int hero,int item);
int dota_courier_pending_count(const DotaGame *game,int hero);
/* Nth pending actual item: cargo first, then fountain stash; -1 if exhausted.
 * The UI may map these to empty slots as ghosts; those slots are not reserved. */
int dota_courier_pending_item(const DotaGame *game,int hero,int nth);
int dota_courier_eta_ticks(const DotaGame *game,int hero);
int dota_courier_view(const DotaGame *game,int hero,DotaCourierView *view);
int dota_courier_speed_source(void);
/* Core bridge applies completed inventory changes only; a dead hero is never
 * given HP or revived by courier delivery. */
void dota_rules_recalc(DotaGame *game,int hero);

#endif
