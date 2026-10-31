#ifndef DOTA_PROGRESSION_H
#define DOTA_PROGRESSION_H
#include "game.h"
enum { DOTA_PROGRESSION_LEVEL_CAP=30,DOTA_TALENT_ROWS=4,DOTA_TALENTS=8 };
typedef struct {
    short primary,str100,agi100,int100,str_gain100,agi_gain100,int_gain100;
    short health,mana,hp_regen100,mp_regen100,weapon_damage100,armor100;
    short attack_speed100,bat_ticks,point_ms,projectile_speed,range,speed;
} DotaProgressionBase;
const DotaProgressionBase *dota_progression_base(int hero_id);
int dota_progression_primary(int hero_id);
void dota_progression_base_attributes100(const DotaUnit *unit,int *str,int *agi,int *intel);
/* Values use x100 Source units (seconds x100 for durations). Skill4 is the
 * actual extra T ability; skill5 is the selected innate. Applies actual
 * Source value, per-hero-level scaling and learned talent expressions. */
int dota_progression_value100(const DotaUnit *unit,int skill,const char *field,int fallback100);
/* Same units as DotaItemStats, including paid attribute ranks and generic
 * stat talents. Source naked/growth attributes are deliberately excluded. */
int dota_progression_stat_bonus(const DotaUnit *unit,int item_stat);
void dota_progression_stat_delta(const DotaGame *game,int hero,int *str,int *agi,int *intel);
void dota_progression_stat_delta100(const DotaGame *game,int hero,int *str,int *agi,int *intel);
int dota_progression_move_percent(const DotaGame *game,int hero);
int dota_progression_attack_critical(const DotaGame *game,int source,int target);
void dota_progression_tick(DotaGame *game);
void dota_progression_on_cast(DotaGame *game,int hero,int mana_spent);
void dota_progression_on_damage(DotaGame *game,int source,int target,int raw,int dealt,int mode);
int dota_progression_before_damage(DotaGame *game,int source,int target,int amount,int mode);
/* Only actual Lina Q/W/R damage, before outgoing spell amplification. The
 * generic item-damage pipeline must not call this for item damage. */
int dota_progression_ability_damage(DotaGame *game,int source,int target,int amount,int mode);
/* learned[] stores paid ordinary ability levels.0 means not learned. */
int dota_progression_rank(const DotaUnit *unit,int skill);
int dota_progression_points(const DotaGame *game,int hero);
/* Since Valve7.40 talents use a separate pool at10/15/20/25/27/28/29/30. */
int dota_progression_talent_points(const DotaGame *game,int hero);
int dota_progression_can_learn(const DotaGame *game,int hero,int skill);
int dota_progression_learn(DotaGame *game,int hero,int skill);
int dota_progression_skill_max(int hero_id,int skill);
int dota_progression_skill_required(const DotaUnit *unit,int skill);
int dota_progression_talent_level(int row);
int dota_progression_has_talent(const DotaUnit *unit,int index);
int dota_progression_can_talent(const DotaGame *game,int hero,int index);
int dota_progression_learn_talent(DotaGame *game,int hero,int index);
const char *dota_progression_talent_name(int hero_id,int index);
const char *dota_progression_talent_description(int hero_id,int index);
int dota_progression_can_attributes(const DotaGame *game,int hero);
int dota_progression_learn_attributes(DotaGame *game,int hero);
int dota_progression_attribute_bonus(const DotaUnit *unit);
int dota_progression_attribute_max(void);
int dota_progression_attribute_required(const DotaUnit *unit);
int dota_progression_innate_rank(const DotaUnit *unit);
const char *dota_progression_innate_name(int hero_id);
const char *dota_progression_innate_description(int hero_id);
/* Call on actual hero level changes (incl multi-level XP awards). Ordinary
 * skills remain manual. When nothing but +2 stats can be upgraded, the
 * otherwise unspendable ordinary points automatically enter attributes. */
void dota_progression_level_changed(DotaGame *game,int hero);
/* Deterministic computer-only choice, using the same paid/level-checked APIs. */
void dota_progression_bot(DotaGame *game,int hero);
#endif
