#ifndef H1_DOTA_ROSTER_HEROES_STATE_H
#define H1_DOTA_ROSTER_HEROES_STATE_H
#include <stdint.h>
/* Keep this independent of game.h's declaration order. Actor identities use
 *16-bit storage;0..254 are real slots and65535 is the absent-target marker. */
#ifndef DOTA_ROSTER_TARGET_CAP
#define DOTA_ROSTER_TARGET_CAP 255
#endif
#define DOTA_ROSTER_MASK_WORDS ((DOTA_ROSTER_TARGET_CAP+31)/32)
#define DOTA_ROSTER_SPELLS 32
#define DOTA_ROSTER_FIELDS 16
#define DOTA_ROSTER_NO_TARGET 65535u
enum { DOTA_ROSTER_FISH_KIND=6,DOTA_ROSTER_ANCHOR_KIND=7 };
enum { RS_HOOK=1,RS_ASSASSINATE,RS_STORM,RS_GUSH,RS_GUSH_WAVE,RS_GRENADE,RS_ANCHOR };
#define RS_BOLT RS_STORM
typedef struct {
    int generation;
    int flesh_until,aim_until,gods_until,shell_until,extra_cooldown;
    int shrapnel_restore_at,cast_at,channel_until,next_pulse;
    int block_damage,block_reset_at,ravage_at,ravage_x,ravage_y,ravage_radius;
    int headshot_tick,cast_generation,headshot_generation,channel_generation;
    int pull_x_q16,pull_y_q16,hook_stun_until;
    int16_t cast_x,cast_y;
    int16_t vision_dx,vision_dy,seen_x,seen_y;
    uint32_t ravage_hit[DOTA_ROSTER_MASK_WORDS];
    uint16_t flesh_stacks,fish_count,cast_target,headshot_target,anchor_unit,channel_target;
    uint8_t rot_on,shrapnel_charges,cast_skill,cast_rank,pulses,level_seen,sven_fly,fly_spell,channel_rank,ravage_rank;
    uint16_t fraction;
    uint16_t channel_duration;
    uint8_t channel_pulse_total,padding3;
} DotaRosterHero;
typedef struct {
    int generation,shrapnel_damage_at;
    uint16_t gush_ticks,smash_ticks,warcry_ticks,rot_ticks,headshot_ticks,grenade_ticks,shrapnel_ticks;
    uint16_t stun_ticks,hold_ticks;
    uint16_t warcry_barrier,tide_mark_ticks,knock_ticks,tether_ticks,tether_anchor;
    int16_t knock_x,knock_y;
    uint8_t gush_rank,smash_rank,warcry_rank,rot_slow,shrapnel_slow,padding2;
    uint8_t warcry_owner,warcry_shard,tide_owner,tether_owner,stun_pierces,hold_owner,hook_spell,shrapnel_owner;
    uint8_t gush_slow,gush_armor,smash_reduction,warcry_armor,warcry_move;
} DotaRosterTarget;
typedef struct {
    int source_generation,target_generation;
    int x_q16,y_q16,to_x,to_y,origin_x,origin_y;
    int delay,life,damage,travelled,speed,radius,knockback;
    uint16_t source,target;
    uint8_t kind,rank,phase,upgrades;
    uint32_t hit[DOTA_ROSTER_MASK_WORDS];
} DotaRosterSpell;
typedef struct {
    int source_generation,x,y,starts_at,until,radius;
    uint16_t source;
    uint8_t rank,padding;
} DotaRosterField;
typedef struct {
    DotaRosterHero hero[10];
    DotaRosterTarget target[DOTA_ROSTER_TARGET_CAP];
    DotaRosterSpell spell[DOTA_ROSTER_SPELLS];
    DotaRosterField field[DOTA_ROSTER_FIELDS];
    uint32_t target_active[DOTA_ROSTER_MASK_WORDS];
    int last_tick,recursion;
    uint32_t field_mask;
} DotaRosterState;
#endif
