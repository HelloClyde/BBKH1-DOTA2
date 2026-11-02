#ifndef H1_DOTA_GAME_H
#define H1_DOTA_GAME_H
#include "unit_limits.h"
#include "objective_data.h"
#include "courier_data.h"
#include "turbo_data.h"
#include "extra_heroes_state.h"
#include "roster_heroes_state.h"
#include "progression_state.h"
#include "items_data.h"
#include "items_ids.h"
#include "item_world_data.h"
#include "runes_state.h"
#include "trees_state.h"
#include "cast_order_state.h"
#include "match_history_state.h"

/* Standalone C99 fixed-step arena rules. No H1 API, heap, floating point or OS. */
#define DOTA_HZ 20
#define DOTA_SOURCE_UNITS_PER_WORLD 4
#define DOTA_MOVE_ONE 65536
#define DOTA_WORLD 4096
#define DOTA_HERO_COUNT 10
#define DOTA_MAX_EFFECTS 24
#define DOTA_MAX_SKILL_EVENTS 64 /* bounded visual-only successful-cast history */
#define DOTA_MAX_PROJECTILES 48
#define DOTA_MAX_ATTACK_EVENTS 64
#define DOTA_NEUTRAL_CAMPS 26
#define DOTA_NEUTRAL_FIRST 127
#define DOTA_MAX_SUMMONS 4
#define DOTA_SUMMON_FIRST 32
#define DOTA_TORMENTOR_INDEX 36
#define DOTA_TORMENTOR_FIRST_SPAWN (20*60*DOTA_HZ)
#define DOTA_TORMENTOR_RESPAWN (10*60*DOTA_HZ)
#define DOTA_OMNISLASH_CAST_TICKS 6
#define DOTA_OMNISLASH_CAST_RANGE_SOURCE 450
#define DOTA_OMNISLASH_JUMP_RANGE_SOURCE 425
#define DOTA_ASSIST_WINDOW_TICKS (15*DOTA_HZ)
#define DOTA_CREEP_FIRST 37
#define DOTA_CREEP_END 96 /* Preserve the original59 lane-creep slots. */
#define DOTA_ANCIENT_CAMPS 2
#define DOTA_ANCIENT_FIRST 96
#define DOTA_ROSHAN_INDEX 111
#define DOTA_HIGH_TOWERS_PER_TEAM 5
#define DOTA_FILLER_FIRST 113
#define DOTA_FILLER_COUNT 14
#define DOTA_HEALING_WARD_LIFE (24*DOTA_HZ) /* maximum; each ward stores its birth rank */
#define DOTA_XP_RADIUS_SOURCE 1500
#define DOTA_LANE_POINTS 16 /* capacity; each original team/lane has its own count */
#define DOTA_SHOP_RANGE 300 /* 1200 Source units around the original fountain */
#define DOTA_ITEM_SLOTS 6
#define DOTA_LEGACY_ITEM_TYPES 6
#define DOTA_SKILLS 4
#define DOTA_LEVEL_CAP 30
#define DOTA_MAX_PATH 64
#define DOTA_ROUTE_POINTS 12

enum { DOTA_RADIANT=0, DOTA_DIRE=1, DOTA_NEUTRAL_TEAM=2 };
enum { DOTA_PLAYING=0, DOTA_WON=1, DOTA_LOST=2 };
enum { DOTA_NONE=0, DOTA_HERO=1, DOTA_CREEP=2, DOTA_TOWER=3, DOTA_BASE=4, DOTA_NEUTRAL=5, DOTA_SUMMON=6 };
enum { DOTA_TORMENTOR=7,DOTA_ANCIENT=8,DOTA_ROSHAN=9,DOTA_BUILDING=10 };
enum { DOTA_SHOP_NONE=0, DOTA_SHOP_BASE=1, DOTA_SHOP_SECRET=2 };
enum { DOTA_OMNISLASH_OFF=0, DOTA_OMNISLASH_CASTING=1, DOTA_OMNISLASH_ACTIVE=2 };
enum { DOTA_HERO_AXE=0, DOTA_HERO_LINA=1, DOTA_HERO_JUGGERNAUT=2,
       DOTA_HERO_CRYSTAL_MAIDEN=3, DOTA_HERO_SLARK=4,
       DOTA_HERO_NECROPHOS=5,DOTA_HERO_PUDGE=6,DOTA_HERO_SNIPER=7,
       DOTA_HERO_SVEN=8,DOTA_HERO_TIDEHUNTER=9,DOTA_HERO_TYPES=10 };
enum { DOTA_BOOTS=0, DOTA_BLADES=1, DOTA_CHAINMAIL=2, DOTA_VITALITY=3,
       DOTA_ENERGY=4, DOTA_FLASK=5, DOTA_ITEM_TYPES=DOTA_ITEM_CATALOG_COUNT };
enum { DOTA_INPUT_ATTACK=1u, DOTA_INPUT_Q=2u, DOTA_INPUT_W=4u,
       DOTA_INPUT_E=8u, DOTA_INPUT_R=16u, DOTA_INPUT_FLASK=32u,
       DOTA_INPUT_STOP=64u };
enum { DOTA_FX_ATTACK=1, DOTA_FX_BURST=2, DOTA_FX_LINE=3,
       DOTA_FX_HEAL=4, DOTA_FX_DEATH=5, DOTA_FX_GOLD=6 };
enum { DOTA_DAMAGE_PHYSICAL=0,DOTA_DAMAGE_MAGICAL=1,DOTA_DAMAGE_PURE=2,
       DOTA_DAMAGE_AURA=3,DOTA_DAMAGE_DEFERRED=4,DOTA_DAMAGE_PIERCING_AURA=5 };
enum { DOTA_SKILL_EVENT_CAST=1, DOTA_SKILL_EVENT_STRIKE=2,
       DOTA_SKILL_EVENT_PASSIVE=3 };
enum { DOTA_PROJECTILE_PENDING=1, DOTA_PROJECTILE_FLYING=2 };
enum { DOTA_ATTACK_MELEE=0, DOTA_ATTACK_LINA=1, DOTA_ATTACK_CM=2,
       DOTA_ATTACK_RADIANT_CREEP=3, DOTA_ATTACK_DIRE_CREEP=4,
       DOTA_ATTACK_NEUTRAL=5, DOTA_ATTACK_RADIANT_TOWER=6, DOTA_ATTACK_DIRE_TOWER=7,
       DOTA_ATTACK_NECROPHOS=8,DOTA_ATTACK_SNIPER=9 };
enum { DOTA_ATTACK_EVENT_START=1, DOTA_ATTACK_EVENT_RELEASE=2,
       DOTA_ATTACK_EVENT_IMPACT=3, DOTA_ATTACK_EVENT_CANCEL=4 };
enum { DOTA_NEUTRAL_IDLE=0, DOTA_NEUTRAL_AGGRO=1, DOTA_NEUTRAL_RETURN=2 };

typedef struct { int x,y; } DotaPoint;
typedef struct { int x,y,w,h; } DotaObstacle;

typedef struct {
    int move_x,move_y;       /* -1..1 per axis; direct movement cancels click order */
    int target_x,target_y;   /* cast/attack world coordinate, clamped to 0..4095 */
    unsigned actions;       /* edge-triggered by caller; simultaneous bits allowed */
} DotaInput;

typedef struct {
    int active,type,team,hero_id,lane,tier;
    int x,y,hp,max_hp,mana,max_mana;
    int damage,armor,range,speed; /* range Source units; speed Source units/second */
    int level,xp,gold;            /* xp is progress to next level */
    int inventory[DOTA_ITEM_SLOTS]; /* -1 empty, otherwise DOTA_* item ID */
    int cooldown[DOTA_SKILLS];   /* remaining fixed ticks */
    int attack_wait,respawn_ticks;
    int target,waypoint;         /* unit slot target, lane polyline progress */
    int move_order,move_x,move_y;
    int stun_ticks,slow_ticks,taunt_ticks,taunt_source;
    int dot_ticks,dot_damage,dot_source;
    int buff_ticks,channel_ticks,heal_ticks; /* legacy buff_ticks now actual slow percent */
    int skill_ticks[DOTA_SKILLS],skill_x,skill_y;
    int kills,deaths,last_damage_team,last_damage_source;
    int generation; /* increments on slot reuse; renderer may ignore */
    /* Actual learned ranks are independent of hero level. Kept at the end
     * so every preceding unit field retains its established offset. */
    unsigned char learned[DOTA_SKILLS],attribute_bonus_rank,progression_padding;
    unsigned short talents;
    int calculated_armor100; /* fractional ordinary armor, refreshed with stats */
} DotaUnit;

typedef struct {
    int active,kind,team,x,y,to_x,to_y,radius,ticks;
} DotaEffect;

/* Renderer messages, separate from damage/status rules and generic effects.
 * CAST is emitted exactly once after validation succeeds. STRIKE snapshots
 * an actual Omnislash jump before its target can die or its slot be reused.
 * Serial arithmetic is unsigned; count bounds valid history across wrap. */
typedef struct {
    unsigned serial;
    int kind,caster,caster_generation,hero_id,skill,target,target_generation;
    int origin_x,origin_y,target_x,target_y,started_tick,duration_ticks,step;
} DotaSkillEvent;

/* Integer world-space attack simulation. state=0 is a free slot. Heights are
 * relative to baked ground and visual only. Renderer samples terrain at the
 * launch/aim anchors and interpolates height, never each intermediate point. */
typedef struct {
    int state,style,source,source_generation,source_type,team;
    int target,target_generation,origin_x,origin_y,x,y,previous_x,previous_y;
    int target_x,target_y,damage,speed,delay,ticks,started_tick;
    int launch_z,aim_z,age,initial_flight_ticks;
    /* speed Source units/s. In flight only, delay packs unsigned x/y16-bit
     * fractional world coordinates; pending delay remains windup ticks. */
} DotaProjectile;

typedef struct {
    unsigned serial;
    int kind,style,source,source_generation,source_type,team,target,target_generation;
    int origin_x,origin_y,target_x,target_y,started_tick,duration_ticks,launch_z,aim_z;
} DotaAttackEvent;

typedef struct {
    const char *name;
    const char *skill_names[DOTA_SKILLS];
    int hp,mana,damage,armor,range,speed;
    int cooldown_ticks[DOTA_SKILLS],mana_cost[DOTA_SKILLS];
} DotaHeroDef;

typedef struct { const char *name; int cost; } DotaItemDef;

typedef struct {
    unsigned short cells[DOTA_ROUTE_POINTS],goal_x,goal_y,count,index;
    int generation,repath_tick,complete;
} DotaUnitRoute;
typedef struct {
    int x_q16,y_q16,tick,generation,remaining_q16;
} DotaMovementState;
typedef struct {
    int spawn_tick,location,regen_fraction,reflect_wait,reflect_damage_q200;
    int last_damage_tick,growth_minutes;
    unsigned shard_mask;
} DotaTormentorState;
typedef struct {
    int generation,target,target_generation,phase;
    int phase_ticks,remaining_ticks,attack_wait_q16,step;
    /* phase_ticks is the windup countdown in phase1, fixed cast rank in phase2. */
} DotaOmnislashState;

typedef struct {
    int ancient_group[DOTA_ANCIENT_CAMPS],ancient_state[DOTA_ANCIENT_CAMPS],ancient_spawn_tick[DOTA_ANCIENT_CAMPS];
    int roshan_spawn_tick,roshan_pit,roshan_kills,roshan_last_damage_tick;
    int roshan_slam_wait,roshan_roar_wait,roshan_spellblock_wait,roshan_bash_count,roshan_regen_fraction;
    int aegis_owner,aegis_generation,aegis_expire_tick,aegis_revive_tick,aegis_x,aegis_y;
    int roar_disarm_ticks[DOTA_HERO_COUNT],roar_vulnerable_ticks[DOTA_HERO_COUNT];
} DotaBossState;
typedef struct {int until,speed_percent,armor100,permanent_armor100;} DotaCullingBonus;

typedef struct {
    DotaUnit units[DOTA_MAX_UNITS];
    DotaEffect effects[DOTA_MAX_EFFECTS];
    unsigned seed;
    int tick,state,paused,player,selected_hero;
    int wave,spawn_wait,kills[2],bases[2];
    int towers[2][3][2]; /* each lane outer tier 0 -> inner tier 1 */
    int last_event,last_event_ticks; /* 0 none, 1 mana, 2 cooldown, 3 locked ult,
                                      4 no target, 5 gold, 6 bag full, 7 too far from shop,
                                      8 purchased, 9 no flask, 10 victory/defeat,
                                      11 wrong shop stock, 12 shard recipient,
                                      13 all-shards team gold,14 UI status detail,
                                      15 target outside source cast range */
    int last_event_value;
    DotaPoint player_path[DOTA_MAX_PATH];
    int player_path_count,player_path_index;
    /* Append only: existing game/unit offsets are used by the native tester. */
    unsigned skill_event_serial,skill_event_count;
    DotaSkillEvent skill_events[DOTA_MAX_SKILL_EVENTS];
    unsigned skill_effect_tags[DOTA_MAX_EFFECTS]; /*0 generic,1+hero*4+skill */
    DotaProjectile projectiles[DOTA_MAX_PROJECTILES];
    unsigned attack_event_serial,attack_event_count;
    DotaAttackEvent attack_events[DOTA_MAX_ATTACK_EVENTS];
    int neutral_state[DOTA_NEUTRAL_CAMPS],neutral_respawn[DOTA_NEUTRAL_CAMPS];
    signed char neutral_group[DOTA_NEUTRAL_CAMPS];
    unsigned short neutral_regen_fraction[96];
    int summon_owner[DOTA_MAX_SUMMONS],summon_owner_generation[DOTA_MAX_SUMMONS];
    int summon_life[DOTA_MAX_SUMMONS]; /* ward lifetime, independent of owner HP */
    DotaUnitRoute unit_routes[DOTA_MAX_UNITS];
    int navigation_tick,navigation_searches,navigation_nodes,player_path_complete;
    /* E abilities are intrinsic passives. Preserve every earlier ABI field.
     * stacks: Lina stacks / Axe incoming attack counter. Fractions preserve
     * small movement and mana bonuses at the integer20Hz simulation rate. */
    int passive_stacks[DOTA_HERO_COUNT],passive_wait[DOTA_HERO_COUNT];
    int passive_mana_fraction[DOTA_HERO_COUNT],passive_generation[DOTA_HERO_COUNT];
    int passive_move_fraction[DOTA_HERO_COUNT]; /* legacy ABI name: now basic-attack Q16 carry */
    /* Actual walking positions and a shared per-tick distance budget. Keeping
     * subpixel position avoids rounding CM's3.5-world step or diagonals. */
    DotaMovementState movement[DOTA_MAX_UNITS];
    /* One source Tormentor, alternating source chasms with day/night.
     * spawn_tick: absolute next spawn when dead; location0Top/1Bottom.
     * last_damage_tick: last actual hit, or-200 until first hit. */
    DotaTormentorState tormentor;
    DotaOmnislashState omnislash[DOTA_HERO_COUNT];
    /* H1 assists: actual enemy-hero damage/control in the last15seconds.
     * Indexed by victim, then contributing hero; stamp=tick+1,0=none.
     * Generation changes clear both that life's incoming and outgoing history. */
    int assists[DOTA_HERO_COUNT];
    int assist_tick[DOTA_HERO_COUNT][DOTA_HERO_COUNT];
    int assist_generation[DOTA_HERO_COUNT];
    int hero_dot_source_generation[DOTA_HERO_COUNT];
    DotaBossState bosses;
    DotaObjectiveState objectives;
    DotaCourierState couriers;
    DotaTurboState turbo;
    DotaExtraHeroesState extra;
    DotaItemsState items;
    DotaItemWorldState item_world;
    DotaRosterState roster;
    DotaProgressionState progression;
    int attack_crit_percent[DOTA_HERO_COUNT];
    DotaCullingBonus culling[DOTA_HERO_COUNT];
    /* A synchronous player cast may pin a picked life. Zero preserves the
     * ordinary coordinate API used by AI and point-targeted spells. */
    int cast_target_plus_one,cast_target_generation;
    DotaRuneState runes;
    DotaTreesState trees;
    DotaCastOrder cast_orders[DOTA_HERO_COUNT];
    DotaMatchStats match_stats;
} DotaGame;

extern const DotaHeroDef dota_hero_defs[DOTA_HERO_TYPES];
/* Compatibility names for the original six items. Full IDs use HIT1 reads. */
extern const DotaItemDef dota_item_defs[DOTA_LEGACY_ITEM_TYPES];
extern const DotaPoint dota_lanes[2][3][DOTA_LANE_POINTS];
extern const int dota_lane_point_counts[2][3];
extern const DotaPoint dota_creep_spawns[2][3];
extern const DotaPoint dota_hero_spawns[2][5];
extern const DotaPoint dota_fountains[2],dota_ancients[2];
extern const DotaPoint dota_tower_positions[2][3][2];
extern const DotaPoint dota_high_tower_positions[2][DOTA_HIGH_TOWERS_PER_TEAM];
extern const DotaPoint dota_filler_positions[DOTA_FILLER_COUNT];
int dota_high_tower_index(int team,int slot);
extern const DotaPoint dota_neutral_camps[DOTA_NEUTRAL_CAMPS];
extern const DotaPoint dota_secret_shops[2],dota_tormentor_locations[2];
extern const DotaObstacle dota_obstacles[];
extern const int dota_obstacle_count;

void dota_init(DotaGame *game,int hero_id,unsigned seed);
void dota_restart(DotaGame *game);
void dota_set_paused(DotaGame *game,int paused);
void dota_tick(DotaGame *game,const DotaInput *input);
void dota_move_to(DotaGame *game,int x,int y);
void dota_attack_target(DotaGame *game,int unit_index);
int dota_cast(DotaGame *game,int skill,int target_x,int target_y);
int dota_cast_on_unit(DotaGame *game,int skill,int target,int generation);
int dota_order_cast(DotaGame *game,int skill,int target_x,int target_y);
int dota_order_cast_on_unit(DotaGame *game,int skill,int target,int generation);
int dota_order_item_at(DotaGame *game,int slot,int x,int y,int target,int generation);
int dota_order_item_tree(DotaGame *game,int slot,int tree_id);
void dota_cast_order_cancel(DotaGame *game,int hero);
int dota_cast_order_active(const DotaGame *game,int hero);
int dota_rules_cast_target_matches(const DotaGame *game,int caster,int candidate);
int dota_skill_is_passive(int hero_id,int skill);
int dota_passive_rank(const DotaUnit *unit);
int dota_attack_speed_percent(const DotaGame *game,int unit_index);
int dota_move_speed_source(const DotaGame *game,int unit_index);
int dota_omnislash_rank(const DotaUnit *unit); /* Actual learned R rank; Source gates6/12/18. */
int dota_omnislash_phase(const DotaGame *game,int unit_index);
int dota_unit_invulnerable(const DotaGame *game,int unit_index);
int dota_skill_mana_cost(const DotaGame *game,int unit_index,int skill);
int dota_skill_cast_range_source(const DotaGame *game,int unit_index,int skill);
int dota_skill_effect_radius_source(const DotaGame *game,int unit_index,int skill);
int dota_skill_cooldown_ticks(const DotaGame *game,int unit_index,int skill);
int dota_effective_armor(const DotaGame *game,int unit_index);
int dota_unit_hull_source(const DotaUnit *unit);
int dota_attack_reach_source(const DotaUnit *attacker,const DotaUnit *target);
int dota_healing_ward_lifetime(const DotaGame *game,int owner);
int dota_healing_ward_radius_source(void);
int dota_hero_assists(const DotaGame *game,int hero_index);
/* Returns NULL for an expired/not-yet-emitted serial, including cold serial0.
 * With count>0, enumerate [latest-count+1,latest] using unsigned arithmetic. */
const DotaSkillEvent *dota_get_skill_event(const DotaGame *game,unsigned serial);
const DotaAttackEvent *dota_get_attack_event(const DotaGame *game,unsigned serial);
int dota_attack_style(const DotaUnit *unit);
/* W commands an existing ward instead of spending mana/cooldown again. The
 * lookup remains valid while its owner is dead, until owner generation changes. */
int dota_healing_ward(const DotaGame *game,int owner);
int dota_move_healing_ward(DotaGame *game,int owner,int x,int y);
int dota_buy(DotaGame *game,int item_id);
int dota_use_item(DotaGame *game,int inventory_slot);
int dota_use_item_at(DotaGame *game,int slot,int x,int y,int target);
int dota_hero_has_scepter(const DotaGame *game,int hero);
int dota_rules_health_regen100(const DotaGame *game,int hero);
int dota_rules_armor100(const DotaGame *game,int unit);
void dota_rules_attributes(const DotaGame *game,int hero,int *str,int *agi,int *intel);
int dota_rules_effective_health_regen100(const DotaGame *game,int hero);
int dota_rules_mana_regen100(const DotaGame *game,int hero);
int dota_rules_item_raw_regen100(const DotaGame *game,int hero);
int dota_rules_aoe_radius(const DotaGame *game,int hero,int radius);
int dota_rules_unit_vision_source(const DotaGame *game,int unit);
int dota_rules_unit_in_water(const DotaGame *game,int unit);
int dota_rules_last_mana_spent(const DotaGame *game,int hero);
void dota_rules_item_counterattack(DotaGame *game,int source,int target,int percent);
int dota_nearest_enemy(const DotaGame *game,int team,int x,int y,int radius);
int dota_shop_available(const DotaGame *game);
int dota_shop_kind(const DotaGame *game); /* Own fountain or either secret shop. */
int dota_shop_item_available(const DotaGame *game,int item_id);
int dota_tormentor_index(const DotaGame *game); /* Active, living boss or-1. */
int dota_hero_has_shard(const DotaGame *game,int hero_index);
int dota_next_level_xp(int level);
int dota_walkable(int x,int y);
int dota_unit_visible(const DotaGame *game,int observer,int target);
int dota_roshan_index(const DotaGame *game);
int dota_ancient_index(int camp,int member);

#endif
