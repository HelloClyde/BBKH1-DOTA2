#ifndef DOTA_EXTRA_HEROES_STATE_H
#define DOTA_EXTRA_HEROES_STATE_H
#include "unit_limits.h"
/* Generation guarded, integer-only Slark/Necrophos rules. 768 independent
 * Essence Shift expiries retain the real per-hit lifetime (not refresh-all). */
#define DOTA_EXTRA_STACKS 768
#define DOTA_EXTRA_SHIVS 32
#define DOTA_EXTRA_PULSES 40
typedef struct { int at,pulse,rank; } DotaExtraPact;
typedef struct { int at,target,generation,rank,duration; } DotaExtraScythe;
typedef struct {
    unsigned short remaining,source_generation,target_generation;
    unsigned char source,target; /* target255: Necrophos Sadist stack */
} DotaExtraStack;
typedef struct {
    int generation,pact_at,pact_pulse,pact_rank;
    int leap_at,leap_x,leap_y,leap_to_x,leap_to_y,leap_rank,leap_duration;
    int shadow_until,unseen_since,neutral_until,ghost_until;
    int scythe_at,scythe_target,scythe_generation,scythe_rank,scythe_cast_at,scythe_duration;
    int permanent_agi,scythe_kills,essence_gain,essence_loss,sadist_stacks;
    int hp_fraction,mana_fraction;
    int shiv_target,shiv_generation,shiv_until,shiv_rank;
    int pounce_upgrade,pounce_charges,pounce_restore_at;
    int shard_cooldown,shroud_until,shroud_x,shroud_y,shroud_rank,shroud_radius;
    int seeker_at,seeker_duration,seeker_x,seeker_y,seeker_to_x,seeker_to_y;
    int leash_until,leash_x,leash_y,leash_source,leash_generation;
} DotaExtraHero;
typedef struct {
    int until,target_until; /* source buff and resisted target debuff are independent */
    unsigned short source_generation,target_generation;
    unsigned char source,target,power,padding;
} DotaExtraShiv;
typedef struct {
    int at,generation,rank,x,y;
    unsigned targets[(DOTA_MAX_UNITS+31)/32],hit[(DOTA_MAX_UNITS+31)/32];
    unsigned short target_generation[DOTA_MAX_UNITS];
} DotaDeathPulse;
typedef struct {
    DotaExtraHero hero[10];
    DotaExtraStack stacks[DOTA_EXTRA_STACKS];
    DotaExtraShiv shivs[DOTA_EXTRA_SHIVS];
    DotaDeathPulse pulse[DOTA_EXTRA_PULSES];
    int heart_fraction[DOTA_MAX_UNITS];
    DotaExtraPact pact_saved[10];
    DotaExtraScythe scythe_saved[10];
} DotaExtraHeroesState;
#endif
