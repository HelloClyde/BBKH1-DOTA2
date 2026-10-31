#ifndef H1_DOTA_ITEMS_SPECIAL_STATE_H
#define H1_DOTA_ITEMS_SPECIAL_STATE_H
#include <stdint.h>
#define DOTA_SPECIAL_EVENTS 64
#define DOTA_SPECIAL_MARKS 64
typedef struct {
 int due,expires,source_generation,target_generation;
 int amount,remaining,x_q16,y_q16;
 int16_t source,target,item;uint8_t kind,pulses;
} DotaItemSpecialEvent;
typedef struct {int until,generation,damage,cooldown_until;int16_t source,target;} DotaItemSpecialMark;
typedef struct {
 int last_tick,spell_guard,attack_guard;
 int generation[10],forager_next[10],forager_cooldown[10],forager_channel[10];
 int rattle_damage[10],rattle_last[10],attack_raw[10];
 int forager_stats[10][3];uint8_t forager_loot[10];
 int16_t forager_tree[10][3],forager_target[10],shiv_target[10],craft_item[10];
 int16_t forager_x[10],forager_y[10];
 uint16_t occult_ticks[10][5],cooldown[10][8],bauble_crafts[10],potency[10];
 uint8_t gauntlet_proc[10],maul_proc[10],shiv_proc[10],regalia_dead[10];
 int wrap_barrier[10],wrap_until[10],wrap_speed_until[10];
 uint16_t wrap_wait[10][7];
 uint8_t wrap_present[10][7],wrap_charges[10][7],water_cd_fraction[10][7];
 DotaItemSpecialEvent event[DOTA_SPECIAL_EVENTS];
 DotaItemSpecialMark mark[DOTA_SPECIAL_MARKS];
} DotaItemsSpecialState;
#endif
