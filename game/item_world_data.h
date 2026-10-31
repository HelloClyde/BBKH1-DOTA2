#ifndef H1_DOTA_ITEM_WORLD_DATA_H
#define H1_DOTA_ITEM_WORLD_DATA_H
#include "unit_limits.h"
#include "teleport_state.h"
#define DOTA_ITEM_UNIT_FIRST 223
#define DOTA_ITEM_UNIT_COUNT 32
#define DOTA_ITEM_TREES 32
enum { ITEM_UNIT_ILLUSION=1,ITEM_UNIT_OBSERVER,ITEM_UNIT_SENTRY,ITEM_UNIT_SUMMON,ITEM_UNIT_DOMINATED,ITEM_UNIT_ROSTER_FISH,ITEM_UNIT_ROSTER_ANCHOR };
typedef struct { int owner,generation,kind,until,damage_percent,incoming_percent,source_item,source_type,source_team;
 int ability_ready[2],cast_at,cast_target,cast_generation,ground_until,ground_x,ground_y;
 unsigned short hp_fraction,mp_fraction;unsigned char native_id,ability_level,cast_skill,padding;
} DotaItemWorldUnit;
typedef struct { int start,until;unsigned short source_generation,target_generation,done;unsigned char source,target,kind,rank; } DotaItemNPCEffect;
typedef struct { int x,y,until,removed,source_item; } DotaItemTree;
typedef struct { int until,generation,item,x,y,origin_x,origin_y,kind,target,target_generation,started_tick; } DotaItemChannel;
typedef struct { int until,generation,owner,item,x,y; } DotaItemTrap;
typedef struct { int active,x,y,item,owner,ready_tick,spell_mode; } DotaItemDrop;
typedef struct {
 DotaItemWorldUnit unit[DOTA_ITEM_UNIT_COUNT];
 DotaItemTree tree[DOTA_ITEM_TREES];
 int hp_fraction[10],mana_fraction[10],last_tick;
 int last_mana_spent[10];
 DotaItemChannel channel[10];
 DotaTeleportState teleport;
 DotaItemTrap trap[8];
 DotaItemDrop drop[16];
 DotaItemNPCEffect npc_effect[32];
 unsigned native_mask;
 int dominate_lock[10];
 unsigned short aura_hp_generation[DOTA_MAX_UNITS],npc_hp_fraction[DOTA_MAX_UNITS];
 short aura_hp_bonus[DOTA_MAX_UNITS];
} DotaItemWorldState;
#endif
