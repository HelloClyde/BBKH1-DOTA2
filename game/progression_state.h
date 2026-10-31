#ifndef DOTA_PROGRESSION_STATE_H
#define DOTA_PROGRESSION_STATE_H
#include <stdint.h>
#define DOTA_PROGRESSION_BURNS 64
#define DOTA_PROGRESSION_SHIELDS 32
typedef struct {
 int generation,axe_linger_until,axe_strength100;
 int blade_next_at,blade_linger_until,blade_stacks;
 int glacial_until,glacial_barrier;
} DotaProgressionHero;
typedef struct {
 int source_generation,target_generation,total100,fraction,duration,remaining;
 uint16_t source,target;
} DotaProgressionBurn;
typedef struct {int generation,until,amount;uint16_t hero,padding;} DotaProgressionShield;
typedef struct {
 DotaProgressionHero hero[10];
 DotaProgressionBurn burn[DOTA_PROGRESSION_BURNS];
 DotaProgressionShield shield[DOTA_PROGRESSION_SHIELDS];
 int last_tick,recursion,skill_source,skill_target,skill_pending;
} DotaProgressionState;
#endif
