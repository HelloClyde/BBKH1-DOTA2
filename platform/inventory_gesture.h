#ifndef DOTA_INVENTORY_GESTURE_H
#define DOTA_INVENTORY_GESTURE_H
#include <stdint.h>
#include "hud_layout.h"
enum { DOTA_INVENTORY_GESTURE_IDLE=0,DOTA_INVENTORY_GESTURE_PENDING=1,DOTA_INVENTORY_GESTURE_DRAG=2 };
enum { DOTA_INVENTORY_GESTURE_NONE=0,DOTA_INVENTORY_GESTURE_TAP=1,DOTA_INVENTORY_GESTURE_MOVE=2 };
typedef struct {int state,source,hover,start_x,start_y,x,y,moved;uint32_t started;} DotaInventoryGesture;
typedef struct {int kind,source,target;} DotaInventoryGestureAction;
static inline void dota_inventory_gesture_cancel(DotaInventoryGesture *s){s->state=0;s->source=s->hover=-1;s->moved=0;}
static inline void dota_inventory_gesture_begin(DotaInventoryGesture *s,int slot,int x,int y,uint32_t now){if(slot<0||slot>=9){dota_inventory_gesture_cancel(s);return;}s->state=1;s->source=s->hover=slot;s->start_x=s->x=x;s->start_y=s->y=y;s->moved=0;s->started=now;}
static inline void dota_inventory_gesture_tick(DotaInventoryGesture *s,uint32_t now){if(s->state==1&&(uint32_t)(now-s->started)>=DOTA_HUD_ITEM_LONG_PRESS_MS)s->state=2;}
static inline void dota_inventory_gesture_motion(DotaInventoryGesture *s,int slot,int x,int y,uint32_t now){int dx=x-s->start_x,dy=y-s->start_y;if(!s->state)return;s->hover=slot;s->x=x;s->y=y;if(dx*dx+dy*dy>64)s->moved=1;dota_inventory_gesture_tick(s,now);}
static inline DotaInventoryGestureAction dota_inventory_gesture_release(DotaInventoryGesture *s,int slot,int x,int y,uint32_t now){
    DotaInventoryGestureAction a={0,-1,-1};if(!s->state)return a;dota_inventory_gesture_motion(s,slot,x,y,now);
    if(s->state==2&&slot>=0&&slot<9&&slot!=s->source){a.kind=2;a.source=s->source;a.target=slot;}
    else if(s->state==1&&!s->moved&&slot==s->source){a.kind=1;a.source=s->source;a.target=slot;}
    dota_inventory_gesture_cancel(s);return a;
}
#endif
