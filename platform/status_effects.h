#ifndef H1_DOTA_STATUS_EFFECTS_H
#define H1_DOTA_STATUS_EFFECTS_H

#include "../game/game.h"

#define DOTA_STATUS_MAX 16
enum {
    DOTA_STATUS_NONE=0,DOTA_STATUS_STUN=1,DOTA_STATUS_SLOW,DOTA_STATUS_TAUNT,
    DOTA_STATUS_DOT,DOTA_STATUS_HUNGER,DOTA_STATUS_CALL,DOTA_STATUS_SPIN,
    DOTA_STATUS_FIERY,DOTA_STATUS_OMNI,DOTA_STATUS_FIELD,DOTA_STATUS_FLASK,
    DOTA_STATUS_WARD,DOTA_STATUS_ARCANE,DOTA_STATUS_SHARD,DOTA_STATUS_FOUNTAIN,
    DOTA_STATUS_TORMENTOR_BURN,DOTA_STATUS_TORMENTOR_HEAL,
    DOTA_STATUS_AEGIS,DOTA_STATUS_ROSHAN_DISARM,DOTA_STATUS_ROSHAN_VULNERABLE,
    DOTA_STATUS_PACT,DOTA_STATUS_LEASH,DOTA_STATUS_ESSENCE_GAIN,DOTA_STATUS_ESSENCE_LOSS,
    DOTA_STATUS_ESSENCE_PERMANENT,DOTA_STATUS_SHIV_GAIN,DOTA_STATUS_SHIV_LOSS,
    DOTA_STATUS_SHADOW,DOTA_STATUS_GHOST,DOTA_STATUS_GHOST_SLOW,DOTA_STATUS_HEARTSTOPPER,
    DOTA_STATUS_SADIST,DOTA_STATUS_SCYTHE_REGEN,DOTA_STATUS_DEPTH_SHROUD,DOTA_STATUS_REAPERS_SCYTHE,
    DOTA_STATUS_ITEM_GENERIC=1000,DOTA_STATUS_ROSTER=8192,DOTA_STATUS_RUNE=9000
};
typedef struct { int id,harmful,ticks,stacks; } DotaStatusEffect;

/* Pure UI projection of actual rules state. ticks0 means permanent or an aura
 * that lasts while its source remains applicable; stacks0 means no stack label.
 * Dead/inactive/nonhero units have no displayed effects. */
int dota_status_effects(const DotaGame *game,int hero,DotaStatusEffect *out,int capacity);
const char *dota_status_name(int id);
/* Only actual drawn icons/labels consume touch. The two-pixel gaps, unused
 * slots and absent groups stay available to world orders. */
int dota_status_hit(const DotaGame *game,int hero,int x,int y);

#endif
