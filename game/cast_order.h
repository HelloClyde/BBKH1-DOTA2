#ifndef H1_DOTA_CAST_ORDER_H
#define H1_DOTA_CAST_ORDER_H
#include "game.h"
enum {DOTA_ORDER_REJECTED=0,DOTA_ORDER_EXECUTED=1,DOTA_ORDER_QUEUED=2};
enum {DOTA_CAST_TARGET_NONE=0,DOTA_CAST_TARGET_POINT=1,DOTA_CAST_TARGET_ENEMY=2,DOTA_CAST_TARGET_ANY=3};
/* Player-facing orders. The original dota_cast/items_use APIs stay immediate. */
int dota_order_cast(DotaGame *g,int skill,int x,int y);
int dota_order_cast_on_unit(DotaGame *g,int skill,int target,int generation);
int dota_order_item_at(DotaGame *g,int slot,int x,int y,int target,int generation);
int dota_order_item_tree(DotaGame *g,int slot,int tree_id);
void dota_cast_order_cancel(DotaGame *g,int hero);
int dota_cast_order_active(const DotaGame *g,int hero);
/* Tick returns1 while it consumed this hero's movement/cast decision. */
int dota_cast_order_tick(DotaGame *g,int hero);
int dota_cast_order_skill_kind(const DotaGame *g,int hero,int skill);
/* Core bridges share the existing movement budget and actual spell executor. */
void dota_rules_cast_order_step(DotaGame *g,int hero);
int dota_rules_order_cast(DotaGame *g,int hero,int skill,int x,int y);
#endif
