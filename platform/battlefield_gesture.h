#ifndef DOTA_BATTLEFIELD_GESTURE_H
#define DOTA_BATTLEFIELD_GESTURE_H

/* A battlefield press is never an order until release. Crossing the eight
 * pixel threshold latches a camera drag, even if the finger returns to start. */
enum { DOTA_BATTLEFIELD_IDLE=0,DOTA_BATTLEFIELD_PENDING=1,DOTA_BATTLEFIELD_DRAG=2 };
enum { DOTA_BATTLEFIELD_NONE=0,DOTA_BATTLEFIELD_TAP=1,DOTA_BATTLEFIELD_PAN=2 };
typedef struct {
    int state,start_x,start_y,x,y,start_camera_x,start_camera_y,camera_x,camera_y;
} DotaBattlefieldGesture;
typedef struct {int kind,x,y,camera_x,camera_y;} DotaBattlefieldGestureAction;

static inline int dota_battlefield_clamp(int x){return x<0?0:x>4095?4095:x;}
static inline void dota_battlefield_gesture_cancel(DotaBattlefieldGesture *s){s->state=DOTA_BATTLEFIELD_IDLE;}
static inline void dota_battlefield_gesture_begin(DotaBattlefieldGesture *s,int x,int y,int camera_x,int camera_y){
    s->state=DOTA_BATTLEFIELD_PENDING;s->start_x=s->x=x;s->start_y=s->y=y;
    s->start_camera_x=s->camera_x=camera_x;s->start_camera_y=s->camera_y=camera_y;
}
static inline void dota_battlefield_gesture_motion(DotaBattlefieldGesture *s,int x,int y,int zoom,int pitch){
    int dx,dy;
    if(!s->state)return;
    if((unsigned)x>=480u || (unsigned)y>=272u || zoom<1 || pitch<1){dota_battlefield_gesture_cancel(s);return;}
    dx=x-s->start_x;dy=y-s->start_y;s->x=x;s->y=y;
    if(s->state==DOTA_BATTLEFIELD_PENDING && dx*dx+dy*dy>64)s->state=DOTA_BATTLEFIELD_DRAG;
    if(s->state==DOTA_BATTLEFIELD_DRAG){
        s->camera_x=dota_battlefield_clamp(s->start_camera_x-dx*256/zoom);
        s->camera_y=dota_battlefield_clamp(s->start_camera_y-dy*65536/(zoom*pitch));
    }
}
static inline DotaBattlefieldGestureAction dota_battlefield_gesture_release(DotaBattlefieldGesture *s,int x,int y,int zoom,int pitch){
    DotaBattlefieldGestureAction a={DOTA_BATTLEFIELD_NONE,0,0,0,0};
    dota_battlefield_gesture_motion(s,x,y,zoom,pitch);
    if(s->state){a.kind=s->state==DOTA_BATTLEFIELD_DRAG?DOTA_BATTLEFIELD_PAN:DOTA_BATTLEFIELD_TAP;a.x=s->state==DOTA_BATTLEFIELD_DRAG?s->x:s->start_x;a.y=s->state==DOTA_BATTLEFIELD_DRAG?s->y:s->start_y;a.camera_x=s->camera_x;a.camera_y=s->camera_y;}
    dota_battlefield_gesture_cancel(s);return a;
}
#endif
