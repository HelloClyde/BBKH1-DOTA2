#ifndef H1_DOTA_ITEMS_DATA_H
#define H1_DOTA_ITEMS_DATA_H
#include <stdint.h>
#include "neutral_state.h"
#include "items_special_state.h"
/* HIT1 is little endian. Header64B, item record96B, parameter8B, recipe20B.
 * Names/descriptions/icons remain external. First six IDs preserve v0.19. */
#define DOTA_ITEMS_MAGIC 0x31544948u
#define DOTA_ITEMS_VERSION 1u
#define DOTA_ITEMS_MAX_PLAN 12
#define DOTA_ITEMS_EQUIP_SLOTS 7
#define DOTA_INVENTORY_SLOTS 9
#define DOTA_BACKPACK_SLOTS 3
/* Dedicated active-only TP slot. Never an equipment/stat array index. */
#define DOTA_ITEMS_TP_SLOT 7
#define DOTA_INVENTORY_TP_SLOT 9
#define DOTA_ITEMS_EFFECTS 128
#define DOTA_ITEMS_EFFECT_WORDS ((DOTA_ITEMS_EFFECTS+31)/32)
#define DOTA_ITEMS_STOCKS 24
enum { ITEM_F_BUY=1,ITEM_F_RECIPE=2,ITEM_F_AUTO_RECIPE=4,ITEM_F_NEUTRAL=8,
 ITEM_F_ENCHANTMENT=16,ITEM_F_CONSUMABLE=32,ITEM_F_STACKABLE=64,ITEM_F_SELLABLE=128,
 ITEM_F_DISASSEMBLE=256,ITEM_F_BOOTS=512,ITEM_F_ENEMY=1024,ITEM_F_FRIENDLY=2048,
 ITEM_F_POINT=4096,ITEM_F_TOGGLE=8192,ITEM_F_CHANNEL=16384,ITEM_F_NO_TARGET=32768 };
enum { ITEM_STAT_HEALTH,ITEM_STAT_MANA,ITEM_STAT_DAMAGE,ITEM_STAT_ARMOR,
 ITEM_STAT_STRENGTH,ITEM_STAT_AGILITY,ITEM_STAT_INTELLECT,ITEM_STAT_ATTACK_SPEED,
 ITEM_STAT_MOVE_FLAT,ITEM_STAT_MOVE_PERCENT,ITEM_STAT_HEALTH_REGEN,ITEM_STAT_MANA_REGEN,
 ITEM_STAT_MAGIC_RESIST,ITEM_STAT_EVASION,ITEM_STAT_LIFESTEAL,ITEM_STAT_SPELL_AMP,
 ITEM_STAT_COOLDOWN_REDUCTION,ITEM_STAT_CAST_RANGE,ITEM_STAT_ATTACK_RANGE,
 ITEM_STAT_STATUS_RESIST,ITEM_STAT_SPELL_LIFESTEAL,ITEM_STAT_HEAL_AMP,
 ITEM_STAT_ARMOR_REDUCTION,ITEM_STAT_AOE_BONUS,ITEM_STAT_COUNT };
/* Armor, attributes, percentage and regen stats use x100. HP/mana/damage,
 * flat movespeed/ranges and attack-speed use ordinary Source gameplay units. */
typedef struct {
 uint16_t cost,flags,cooldown_ticks,mana_cost,cast_range,radius,duration_ticks;
 uint8_t action,category;
 uint16_t param_first,param_count,recipe_first,recipe_count;
 uint8_t level,shared_cooldown,initial_charges,max_charges;
 uint32_t passive_flags;
 int16_t stats[ITEM_STAT_COUNT];
 uint16_t stock_initial,stock_max,stock_ticks,stock_start_ticks;
 uint8_t disassemble,tier;uint16_t icon;uint32_t reserved;
} DotaItemRecord;
typedef char DotaItemRecord96[(sizeof(DotaItemRecord)==96)?1:-1];
typedef struct { uint16_t key,rank;int32_t value_x100; } DotaItemParameter;
typedef struct { uint16_t recipe,result,component[6];uint8_t count,star_mask;uint16_t reserved; } DotaItemRecipe;
typedef char DotaItemRecipe20[(sizeof(DotaItemRecipe)==20)?1:-1];
typedef struct { uint32_t magic,version,bytes,item_count,record_offset,record_bytes,param_offset,param_count,recipe_offset,recipe_count,text_index_offset,text_offset,icon_index_offset,icon_offset,flags,reserved; } DotaItemsHeader;
typedef struct { int value[ITEM_STAT_COUNT];uint32_t passive_flags; } DotaItemStats;
/* Rates may exceed327.67HP/s (for example Seeds snapshots from late-game
 * regeneration); keep the value32-bit instead of truncating a valid buff. */
typedef struct { int generation;uint16_t ticks,interval;int32_t value;uint16_t tag;uint8_t source,target,kind,pad; } DotaItemEffect;
typedef struct { int last_tick;int16_t item;uint16_t count;uint8_t team,pad[3]; } DotaItemStock;
/* An owned backpack item keeps the same cooldowns/charges and instance data
 * when it crosses the main-inventory boundary. Legacy slot6 is still neutral. */
typedef struct {
 int16_t item;uint16_t cooldown,active_ticks,charges,buy_age,proc_wait,inactive_ticks,wrap_wait;
 uint8_t uses,toggle,backpack_seen,rate_phase,wrap_present,wrap_charges,water_cd_fraction;
 int8_t rapier_owner;int grace_until;
} DotaBackpackItem;
typedef struct {
 int16_t tracked[10][7];uint16_t cooldown[10][7],active_ticks[10][7],charges[10][7],buy_age[10][7];
 uint16_t proc_wait[10][7];
 uint8_t uses[10][7],toggle_bits[10];uint16_t pad;
 uint8_t hit_proc[10];uint16_t hit_pad;
 int soul_mana[10],soul_until[10];
 int8_t rapier_owner[10][7];
 int generation[10];uint32_t inventory_hash[10],permanent[10];DotaItemStats cache[10];
 DotaItemStats aura[10];
 int income_fraction[10],regen_transfer_fraction[10];
 int neutral_item[10];DotaNeutralState neutral;
 /* Lives with this Game (including save/copy/reset), never a global tick cache. */
 uint32_t effect_active[DOTA_ITEMS_EFFECT_WORDS];
 DotaItemEffect effects[DOTA_ITEMS_EFFECTS];DotaItemStock stocks[DOTA_ITEMS_STOCKS];
 int last_tick,recursion,reflection_guard;
 DotaItemsSpecialState special;
 DotaBackpackItem backpack[10][DOTA_BACKPACK_SLOTS];
 uint16_t inactive_ticks[10][6];uint8_t backpack_seen[10][6],backpack_rate_phase[10][6];
 int backpack_grace_until[10][6];
} DotaItemsState;
#endif
