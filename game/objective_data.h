#ifndef H1_DOTA_OBJECTIVE_DATA_H
#define H1_DOTA_OBJECTIVE_DATA_H

/* Installed default map entities; generated and audited by extract_objectives.py.
 * Source horizontal coordinates /4, nearest whole world unit. */
#define DOTA_WISDOM_SHRINES 2
#define DOTA_LOTUS_POOLS 2
#define DOTA_WATCHERS 10
#define DOTA_OBJECTIVES 14
#define DOTA_OBJECTIVE_WISDOM_FIRST 0
#define DOTA_OBJECTIVE_LOTUS_FIRST 2
#define DOTA_OBJECTIVE_WATCHER_FIRST 4
#define DOTA_LOTUS_CARRY_MAX 18 /* Legacy constant; actual ownership uses9inventory slots. */

typedef struct {
    int wisdom_cycle[2],wisdom_progress[2],wisdom_team[2];
    int lotus_count[2],lotus_progress[2],lotus_team[2],lotus_pickup_ticks[2];
    int lotus_spawn_cycle,wisdom_spawn_cycle,lotus_grade;
    int watcher_until[10];
    signed char watcher_owner[10]; /* -1 dormant,-2 disabled,0/1 controlled. */
    unsigned short carried_lotus[10]; /* Reserved legacy pouch ABI; never grants items/use. */
    unsigned short lotus_cooldown[10]; /* Real famango item-family shared cooldown. */
    int order,order_generation,channel_ticks; /* player order,0none; otherwise id+1. */
} DotaObjectiveState;

#endif
