#ifndef H1_DOTA_ITEMS_INTERNAL_H
#define H1_DOTA_ITEMS_INTERNAL_H
#include "items.h"
/* Shared bounded status primitives. HP/MP regen and DOT value use x100 per
 * second. Other values are ordinary percentage/armor/AS/damage units. */
enum {
 IE_REGEN_HP=1,IE_REGEN_MP,IE_DOT,IE_SILENCE,IE_ROOT,IE_DISARM,IE_CYCLONE,
 IE_GHOST,IE_IMMUNITY,IE_BARRIER_MAGIC,IE_BARRIER_PHYSICAL,IE_SPELLBLOCK,IE_REFLECT,
 IE_INVISIBLE,IE_HASTE,IE_ARMOR,IE_AS,IE_SLOW,IE_HEAL_REDUCTION,IE_VULNERABLE,
 IE_CRIMSON,IE_STATIC,IE_DAMAGE_AMP,IE_ETHEREAL,IE_REVEAL,IE_RESTORE_BOTH,
 IE_SPELL_LIFESTEAL,IE_LIFESTEAL,IE_EVASION,IE_DAMAGE,IE_MAXHP,IE_DELAY_TELEPORT,IE_BREAK,
 IE_AGILITY,IE_AEON,IE_HP_DRAIN,IE_REGEN_STEAL,IE_SLOW_RESIST,IE_DECAY_SLOW,IE_NULLIFY,IE_PIKE,
 IE_INCOMING_HEAL_AMP,IE_PERCENT_DOT,IE_BATTLE_HUNGER
};
/* Shard Hunger instances have independent durations and pure damage. */
void dota_items_battle_hunger(DotaGame *g,int source,int target,int duration,int dps100,int slow,int stack);
void dota_items_clear_hunger(DotaGame *g,int target);
void dota_items_apply_effect(DotaGame *g,int source,int target,int kind,int ticks,int value,int item);
/* Use this for every cancellation so same-tick queries see the real pool. */
void dota_items_remove_effect(DotaGame *g,int index);
int dota_items_effect_value(const DotaGame *g,int target,int kind);
/* Caller owns a current same-operation stats snapshot; never retain it across
 * HP/max-HP changes, item/effect changes or simulation ticks. NULL queries live. */
int dota_items_heal_amount_with_stats(DotaGame *g,int source,int target,int amount,const DotaItemStats *stats);
void dota_items_restore(DotaGame *g,int source,int target,int hp,int mana);
int dota_items_total_attribute(DotaGame *g,int hero,int attribute);
/* Source attribute at the hero's current level, excludes equipped items. */
int dota_items_base_attribute100(const DotaGame *g,int hero,int attribute);
#endif
