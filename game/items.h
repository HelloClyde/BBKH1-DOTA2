#ifndef H1_DOTA_ITEMS_H
#define H1_DOTA_ITEMS_H
#include "game.h"
#include "items_data.h"
/* The platform supplies bounded read-only HIT1 record/parameter/recipe reads.
 * Parameter values are x100; rank<0 selects the record's source item level. */
int dota_items_read(int item,DotaItemRecord *record);
int dota_items_parameter(int item,int key,int rank,int fallback_x100);
int dota_items_recipe_read(int index,DotaItemRecipe *recipe);
int dota_items_count(void);
int dota_items_recipes_count(void);
typedef struct { int item,count,cost;int16_t missing[DOTA_ITEMS_MAX_PLAN]; } DotaItemPurchasePlan;
void dota_items_init(DotaGame *game);
void dota_items_tick(DotaGame *game);
void dota_items_stats(DotaGame *game,int hero,DotaItemStats *stats);
void dota_items_invalidate(DotaGame *game,int hero);
/* UI inventory:0..5 main,6..8 backpack,9 dedicated TP. Moves only0..8.
 * Legacy active/cooldown slots:0..5 main,6 neutral,7 dedicated TP. */
int dota_inventory_item(const DotaGame *game,int hero,int slot);
int dota_inventory_move(DotaGame *game,int hero,int from,int to);
int dota_inventory_cooldown(const DotaGame *game,int hero,int slot);
int dota_inventory_charges(const DotaGame *game,int hero,int slot);
int dota_inventory_inactive_ticks(const DotaGame *game,int hero,int slot);
/* Active stats/procs only; a6-second backpack return lock suppresses the
 * underlying raw inventory item. Use raw/UI getters for ownership/delivery. */
int dota_items_equipped_item(const DotaGame *game,int hero,int legacy_slot);
/* Active identity including dedicated TP; stats still use equipped_item. */
int dota_items_cast_item(const DotaGame *game,int hero,int legacy_slot);
int dota_items_self_target_valid(const DotaGame *game,int hero,int legacy_slot);
int dota_items_use_self(DotaGame *game,int hero,int legacy_slot);
enum {DOTA_ITEM_CAST_INVALID=0,DOTA_ITEM_CAST_READY=1,DOTA_ITEM_CAST_APPROACH=2};
/* Pure current-resource/target preflight. range_source is Source units;0
 * means no approach. Orders retain/recheck target generation and exact tree. */
int dota_items_cast_query(const DotaGame *game,int hero,int legacy_slot,int x,int y,int target,int *range_source);
/* Same96Source tree selection as the actual action. Dynamic IDs start at
 * DOTA_STATIC_TREE_COUNT and their stamp is their planted expiry tick. */
int dota_items_tree_target(const DotaGame *game,int x,int y,int *tx,int *ty,int *stamp);
int dota_items_purchase_plan(const DotaGame *game,int hero,int item,DotaItemPurchasePlan *plan);
/* Reserve limited shop stock after the caller atomically validates gold/queues.
 * This never deducts gold or moves inventory. Returns0 without mutation. */
int dota_items_purchase_commit(DotaGame *game,int hero,const DotaItemPurchasePlan *plan);
/* Atomic incoming delivery, including combine with a virtual seventh slot.
 * No mutation on failure; purchase_age is capped at201ticks (refund expired). */
int dota_items_receive(DotaGame *game,int hero,int item,int purchase_age);
int dota_items_receive_rapier(DotaGame *game,int hero,int bound_owner,int spell_mode);
/* Atomic courier handoff can combine 6 held + up to6 incoming pieces, so a
 * full bag never deadlocks on a missing component plus its paid recipe. */
int dota_items_receive_batch(DotaGame *game,int hero,const int16_t *items,const uint16_t *purchase_age,int count);
int dota_items_can_receive_batch(const DotaGame *game,int hero,const int16_t *items,int count);
int dota_items_can_receive(const DotaGame *game,int hero,int item);
/* Real lotus consumables, measured in basic-lotus equivalents;0 otherwise. */
int dota_items_lotus_value(int item);
int dota_items_cost(int item);
int dota_items_has_upgrade(const DotaGame *game,int hero,int shard);
int dota_items_auto_combine(DotaGame *game,int hero);
int dota_items_sell(DotaGame *game,int hero,int slot);
int dota_items_disassemble(DotaGame *game,int hero,int slot);
int dota_items_use(DotaGame *game,int hero,int slot,int x,int y,int target);
int dota_items_cooldown(const DotaGame *game,int hero,int slot);
int dota_items_charges(const DotaGame *game,int hero,int slot);
/* Point-only combined-ward selector; ordinary target=-1 prefers Observer.
 * The selector is not a unit index and never enables implicit targeting. */
#define DOTA_WARD_TARGET_SENTRY (-2)
int dota_items_ward_available(const DotaGame *game,int hero,int legacy_slot,int kind);
int dota_items_is_active(int item);
#define DOTA_TELEPORT_FOUNTAIN_TARGET(team) (DOTA_MAX_UNITS+(team))
/* Travel Boots are passive upgrades to the scroll, never a second active.
 * An explicit target>=0 is authoritative: an invalid target is not replaced.
 * target=-1 chooses a legal allied anchor within800Source units of the point. */
int dota_items_is_teleport(int item);
int dota_items_teleport_target_valid(const DotaGame *game,int hero,int item,int target);
int dota_items_teleport_target(const DotaGame *game,int hero,int item,int x,int y,int target);
int dota_items_teleport_cooldown(const DotaGame *game,int hero);
int dota_items_teleport_charges(const DotaGame *game,int hero);
/* Returns1 only for a current valid channel; all outputs are optional.
 * The destination follows a living mobile anchor. total/remaining are20Hz. */
int dota_items_teleport_channel(const DotaGame *game,int hero,int *x,int *y,int *remaining,int *total);
void dota_items_cancel_teleport(DotaGame *game,int hero);
int dota_items_attack_damage(DotaGame *game,int source,int target,int amount);
void dota_items_attack_hit(DotaGame *game,int source,int target,int dealt);
int dota_items_before_damage(DotaGame *game,int source,int target,int amount,int magical);
/* One successful ordinary attack: largest applicable block, before armor. */
int dota_items_attack_block(DotaGame *game,int source,int target);
/* Called after outgoing amps and before target defenses. Also resolves
 * reflected damage here so a fully absorbed incoming hit still returns it. */
void dota_items_capture_incoming(DotaGame *game,int source,int target,int raw,int magical);
int dota_items_outgoing_spell_percent(const DotaGame *game,int hero);
void dota_items_after_damage(DotaGame *game,int source,int target,int dealt,int magical);
/* Central real hero death, after deciding that Aegis did not reincarnate. */
void dota_items_on_death(DotaGame *game,int victim,int killer);
void dota_items_on_cast(DotaGame *game,int caster);
void dota_items_on_target_spell(DotaGame *game,int caster,int target);
int dota_items_status_resist_percent(DotaGame *game,int hero);
int dota_items_blocks_spell(DotaGame *game,int source,int target);
int dota_items_reflects_spell(const DotaGame *game,int target);
int dota_items_silenced(const DotaGame *game,int hero);
int dota_items_disarmed(const DotaGame *game,int hero);
int dota_items_invulnerable(const DotaGame *game,int hero);
int dota_items_debuff_immune(const DotaGame *game,int hero);
int dota_items_free_path(const DotaGame *game,int hero);
/* Source WindWaker movement is granted only to a self-cycloned owner. */
int dota_items_wind_move_source(const DotaGame *game,int hero);
int dota_items_rooted(const DotaGame *game,int hero);
int dota_items_invisible(const DotaGame *game,int hero);
int dota_items_broken(const DotaGame *game,int hero);
void dota_items_purge(DotaGame *game,int hero,int strong);
/* positive1 removes beneficial effects;0 removes ordinary harmful effects. */
void dota_items_dispel(DotaGame *game,int unit,int positive);
int dota_items_status_percent(const DotaGame *game,int hero,int stat);
int dota_items_heal_amount(DotaGame *game,int source,int target,int amount);
int dota_items_regen_percent(const DotaGame *game,int target);
int dota_items_timed_regen100(const DotaGame *game,int target);
int dota_items_timed_mana_regen100(const DotaGame *game,int target);
int dota_rules_item_raw_regen100(const DotaGame *game,int hero);
int dota_items_mana_cost_percent(const DotaGame *game,int hero);
int dota_items_mana_regen_percent(DotaGame *game,int hero);
int dota_items_health_regen_amp_percent(DotaGame *game,int hero);
int dota_items_slow_resistance(DotaGame *game,int hero);
int dota_items_available_mana(const DotaGame *game,int hero);
int dota_items_spend_mana(DotaGame *game,int hero,int amount);
typedef struct {int item,harmful,ticks,stacks;} DotaItemStatus;
int dota_items_statuses(const DotaGame *game,int hero,DotaItemStatus *out,int capacity);
int dota_items_cooldown_percent(DotaGame *game,int hero);
int dota_items_range_bonus(DotaGame *game,int hero,int attack);
int dota_items_attack_range_for(const DotaGame *game,int source,int target,int normal_range);
int dota_items_spell_amp_percent(DotaGame *game,int hero);
/* Root's existing damage/move/xp wrappers can implement these bridge primitives.
 * Special original actions (illusion/domination/wards/trees) are explicit,
 * never silently accepted as no-ops. Return0 when a primitive cannot execute. */
int dota_rules_item_primitive(DotaGame *game,int action,int hero,int target,int x,int y,int amount,int duration);
void dota_rules_item_recalc(DotaGame *game,int hero);
int dota_rules_item_damage(DotaGame *game,int source,int target,int amount,int magical);
int dota_rules_item_move(DotaGame *game,int unit,int x,int y,int blink);
void dota_rules_item_effect(DotaGame *game,int kind,int team,int x,int y,int xx,int yy,int radius,int ticks);
int dota_rules_item_attack_range(const DotaGame *game,int hero);
int dota_rules_attack_critical(DotaGame *game,int source,int target);
void dota_rules_item_xp(DotaGame *game,int hero,int amount);
void dota_rules_item_finish_free_path(DotaGame *game,int hero);
#endif
