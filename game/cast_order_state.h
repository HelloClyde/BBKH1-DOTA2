#ifndef H1_DOTA_CAST_ORDER_STATE_H
#define H1_DOTA_CAST_ORDER_STATE_H
enum {DOTA_CAST_ORDER_NONE=0,DOTA_CAST_ORDER_SKILL=1,DOTA_CAST_ORDER_ITEM=2};
enum {DOTA_CAST_POINT=1,DOTA_CAST_UNIT=2,DOTA_CAST_TREE=3};
/* Per-life, bounded input intent. It owns no pointers, allocations or costs. */
typedef struct {
 int kind,caster_generation,action,item_id,target_kind,target,target_generation;
 int x,y,range_source,goal_x,goal_y,next_plan_tick,started_tick,progress_tick;
 int last_x,last_y,executing;
} DotaCastOrder;
#endif
