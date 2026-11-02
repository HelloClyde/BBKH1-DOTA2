#ifndef DOTA_ITEM_TAP_H
#define DOTA_ITEM_TAP_H
#include <stdint.h>
/* Time is wall-clock milliseconds, independent of paused match ticks. A held
 * key is still one edge; long-press dragging never calls this activation API. */
#define DOTA_ITEM_DOUBLE_TAP_MS 450u
typedef struct {uint32_t at;int slot,item,generation,valid;} DotaItemTap;
static inline void dota_item_tap_clear(DotaItemTap *s){s->valid=0;}
static inline int dota_item_tap(DotaItemTap *s,int slot,int item,int generation,uint32_t now){
    int second=s->valid&&s->slot==slot&&s->item==item&&s->generation==generation&&
        (uint32_t)(now-s->at)<=DOTA_ITEM_DOUBLE_TAP_MS;
    s->at=now;s->slot=slot;s->item=item;s->generation=generation;s->valid=!second;
    return second;
}
#endif
