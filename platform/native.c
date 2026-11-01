#include <stdint.h>
#include <stddef.h>
#include "h1_sdk.h"
#include "game.h"
#include "render.h"
#include "objectives.h"
#include "courier.h"
#include "extra_heroes.h"
#include "items.h"
#include "item_world.h"
#include "items_actions.h"
#include "item_assets.h"
#include "shop_catalog.h"
#include "neutral.h"
#include "cache_memory.h"
#include "dota_audio.h"
#include "roster_heroes.h"
#include "extra_ability.h"
#include "progression.h"
#include "inventory_gesture.h"
#include "battlefield_gesture.h"
#include "target_input.h"
#include "runes.h"
#if defined(__mips__)
#include "dota_mxu_exact.h"
#endif
#ifdef DOTA_BAKED
#include "baked_scene.h"
#include "baked_io.h"
#include "vision_assets.h"
#endif

/* Observable state is also used by the MIPS controller tests. */
typedef struct {
    uint32_t magic,frames,screen,hero,shop_item,keys,ticks,error;
} DotaDebug;
volatile DotaDebug h1_dota_debug;
typedef struct {
    uint32_t magic,version,rgb_bytes,sprite_bytes,resident_frames,scene_heap_bytes;
    uint32_t io_bytes,io_heap_bytes,rules_bytes,total_heap_bytes,fallback_flags,released;
    uint32_t icon_slots,icon_heap_bytes,audio_heap_bytes,vision_heap_bytes;
} DotaCacheDebug;
volatile DotaCacheDebug h1_dota_cache;
DotaGame dota_game;
static DotaView view;
static unsigned char held[45],pressed[45];
static uint32_t *rgb565_table;
static int mxu_ready;
static volatile uint32_t *lcd;
static int opened,closing,timer_started,touch_down,touch_x,touch_y;
static int touch_pending;
static int minimap_drag,shop_return_screen=DOTA_SCREEN_PLAY;
static DotaInventoryGesture inventory_gesture;
static DotaInventoryGestureAction inventory_action;
static DotaBattlefieldGesture battlefield_gesture;
static DotaBattlefieldGestureAction battlefield_action;
static int target_controls_frame;
static int battlefield_skill=-1,battlefield_item=-1,battlefield_target=-1,battlefield_target_generation,battlefield_item_unit;
static int aimed_item(DotaItemRecord *r);
static int item_target_relation(const DotaItemRecord *r);
static unsigned pending_actions;
static uint32_t game_clock,clock_native_previous,clock_audio_previous;
static int clock_ready,clock_has_audio;
static uint32_t frame_clock_ms(void){
    uint32_t native_now=h1_timer_read_ms(),audio_now,delta;int audio=dota_audio_clock_ms(&audio_now);
    if(!clock_ready){clock_ready=1;clock_native_previous=native_now;clock_has_audio=audio;if(audio)clock_audio_previous=audio_now;return game_clock;}
    delta=native_now-clock_native_previous;clock_native_previous=native_now;
    if(audio){if(clock_has_audio)delta=audio_now-clock_audio_previous;clock_audio_previous=audio_now;}
    clock_has_audio=audio;game_clock+=delta;return game_clock;
}

void *memcpy(void *dst,const void *src,size_t count) {
    unsigned char *d=dst;const unsigned char *s=src;
    while(count--)*d++=*s++;
    return dst;
}
void *memset(void *dst,int value,size_t count) {
    unsigned char *d=dst;while(count--)*d++=(unsigned char)value;return dst;
}
int memcmp(const void *a,const void *b,size_t count) {
    const unsigned char *aa=a,*bb=b;
    while(count--){if(*aa!=*bb)return *aa-*bb;++aa;++bb;}return 0;
}
static int clamp(int value,int lo,int hi){return value<lo?lo:(value>hi?hi:value);}
static uint32_t color32(unsigned value){
    unsigned r=value>>11,g=(value>>5)&63,b=value&31;
    return (((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|(b<<3)|(b>>2);
}
#if defined(__mips__)
static int mxu_color_self_test(void){
    uint16_t input[480] __attribute__((aligned(32)));
    uint32_t output[480] __attribute__((aligned(32)));
    unsigned base,i,n,control=dota_mxu_exact_control();
    for(base=0;base<65536u;base+=480u){
        n=65536u-base;if(n>480u)n=480u;
        for(i=0;i<n;++i){input[i]=(uint16_t)(base+i);output[i]=0x5a5aa5a5u;}
        dota_mxu_exact_rgb565(output,input,n);
        for(i=0;i<n;++i)if(output[i]!=color32(input[i]))return 0;
    }
    return dota_mxu_exact_control()==control;
}
#endif
static void *gui(unsigned offset) {
    return h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),offset);
}
static unsigned native_code(unsigned key) {
    static const unsigned char codes[43]={0,16,17,18,19,20,21,22,30,31,32,33,34,35,36,104,44,45,46,47,48,49,109,57,1,28,105,108,106,23,24,25,37,38,86,106,50,103,111,28,105,1,42};
    return key<43?codes[key]:0;
}
static int key(unsigned k) {
    typedef int (*Fn)(unsigned);
    unsigned code=native_code(k);Fn query=(Fn)gui(0x9d8);
    return code && query?query(code)!=0:(k<45&&held[k]);
}
static int sample_touch(void) {
    typedef void (*Fn)(h1_u16 *,h1_u16 *);
    const uint32_t *code=gui(0x6c0);h1_u16 x=65535,y=65535;
    /* This candidate touch reader is bound to the verified V1.41 prologue. */
    if(!code || code[0]!=0x27bdffa8u || code[1]!=0xafbf0050u ||
       code[4]!=0x00a0b821u || code[5]!=0x0080b021u)return 0;
    ((Fn)code)(&x,&y);
    if(x>=480 || y>=272)return 0;
    touch_x=x;touch_y=y;return 1;
}
static int battlefield_touch_region(int x,int y){
    return view.screen==DOTA_SCREEN_PLAY&&!view.scoreboard&&!view.inventory_mode&&
           x>=0&&x<480&&y>=24&&y<DOTA_HUD_STATUS_Y;
}
static void minimap_pan(int x,int y){
    x=clamp(x-DOTA_HUD_MAP_X,0,DOTA_HUD_MAP_W-1);y=clamp(y-DOTA_HUD_MAP_Y,0,DOTA_HUD_MAP_H-1);
    view.camera_x=x*4096/DOTA_HUD_MAP_W;view.camera_y=y*4096/DOTA_HUD_MAP_H;view.follow=0;
}
static void touch_event(int code,int x,int y){
    touch_x=x;touch_y=y;
    if(code==11){
        int slot=dota_hud_item_at(x,y);touch_down=1;
        dota_battlefield_gesture_cancel(&battlefield_gesture);battlefield_action.kind=0;
        if(view.screen==DOTA_SCREEN_PLAY&&!view.scoreboard&&!view.inventory_mode&&view.aim_skill<0&&view.aim_item<0&&dota_hud_contains(x,y,DOTA_HUD_MAP_X,DOTA_HUD_MAP_Y,DOTA_HUD_MAP_W,DOTA_HUD_MAP_H)){
            minimap_drag=1;minimap_pan(x,y);touch_pending=0;
        }
        else if(view.screen==DOTA_SCREEN_PLAY&&!view.scoreboard&&slot>=0){dota_inventory_gesture_begin(&inventory_gesture,slot,x,y,frame_clock_ms());touch_pending=0;}
        else if(battlefield_touch_region(x,y)){
            int kind;
            dota_battlefield_gesture_begin(&battlefield_gesture,x,y,view.camera_x,view.camera_y);touch_pending=0;
            battlefield_skill=view.aim_skill;battlefield_item=view.aim_item;battlefield_target=-1;battlefield_item_unit=0;
            kind=view.aim_skill>=0?dota_input_skill_target_kind(&dota_game,view.aim_skill):DOTA_INPUT_TARGET_NONE;
            if(kind>=DOTA_INPUT_TARGET_ENEMY)battlefield_target=dota_hit_unit(&dota_game,&view,x,y,kind==DOTA_INPUT_TARGET_ANY?DOTA_HIT_ANY:DOTA_HIT_ENEMY,1);
            else if(view.aim_item>=0){DotaItemRecord r;int item=aimed_item(&r);
                battlefield_item_unit=item>=0&&!dota_items_is_teleport(item)&&!(r.flags&ITEM_F_POINT)&&r.action!=ITEM_ACTION_TREE;
                if(item>=0&&(dota_items_is_teleport(item)||battlefield_item_unit))battlefield_target=dota_hit_unit(&dota_game,&view,x,y,dota_items_is_teleport(item)?DOTA_HIT_FRIENDLY:item_target_relation(&r),0);
            }
            if(battlefield_target>=0)battlefield_target_generation=dota_game.units[battlefield_target].generation;
        }
        else touch_pending=1;
    }else if(code==12&&touch_down){
        if(minimap_drag&&(unsigned)x<480u&&(unsigned)y<272u)minimap_pan(x,y);
        if(inventory_gesture.state)dota_inventory_gesture_motion(&inventory_gesture,dota_hud_item_at(x,y),x,y,frame_clock_ms());
        if(battlefield_gesture.state)dota_battlefield_gesture_motion(&battlefield_gesture,x,y,180,181);
    }else if(code==8){
        if(inventory_gesture.state)inventory_action=dota_inventory_gesture_release(&inventory_gesture,dota_hud_item_at(x,y),x,y,frame_clock_ms());
        if(battlefield_gesture.state)battlefield_action=dota_battlefield_gesture_release(&battlefield_gesture,x,y,180,181);
        minimap_drag=0;touch_down=0;
    }
}
static void pump(void) {
    int i;
    for(i=0;i<64;++i) {
        int code=-1,value=-1;h1_event_fetch(&code,&value);
        if(code==-1 && value==-1)break;
        if(code==H1_EVENT_KEY_DOWN && value>0 && value<45) {
            if(!held[value]){pressed[value]=1;++h1_dota_debug.keys;}
            held[value]=1;if(value==H1_KEY_BACK&&view.screen==DOTA_SCREEN_MENU)closing=1;
        } else if(code==H1_EVENT_KEY_UP && value>0 && value<45)held[value]=0;
        else if(code==11 && sample_touch()){
            touch_event(code,touch_x,touch_y);
        }else if(code==12 && touch_down){
            if(!sample_touch())touch_x=touch_y=-1;
            touch_event(code,touch_x,touch_y);
        }else if(code==8)touch_event(code,touch_x,touch_y);
    }
}
static int open_video(void) {
    typedef int (*Open)(void);typedef volatile uint32_t *(*Buffer)(h1_u16 *,void *);
    h1_u16 info[6]={0};unsigned i;
    if(!gui(0x84c) || !gui(0x8f4) || !((Open)gui(0x84c))())return 0;
    opened=1;lcd=((Buffer)gui(0x8f4))(info,0);
    if(!lcd || info[1]!=480 || info[2]!=272 || info[3]!=1920 || info[5]!=32)return 0;
#if defined(__mips__)
    /* No audio/file/GUI operation occurs inside a bounded conversion leaf.
     * Validate all RGB565 values in private stack RAM before trusting MXU. */
    mxu_ready=mxu_color_self_test();
    if(!mxu_ready)
#endif
    {
        rgb565_table=cache_memory_alloc(65536u*sizeof(uint32_t));
        if(rgb565_table)for(i=0;i<65536;++i)rgb565_table[i]=color32(i);
    }
    return 1;
}
/* Real entry point for observing frame completion; no test-only state writes. */
__attribute__((noinline)) void h1_dota_present(const uint16_t *pixels) {
    typedef volatile uint32_t *(*Buffer)(h1_u16 *,void *);
    h1_u16 info[6]={0};unsigned i;
    volatile uint32_t *current=((Buffer)gui(0x8f4))(info,0);
    if(!current || info[1]!=480 || info[2]!=272 || info[3]!=1920 || info[5]!=32) {
        h1_dota_debug.error=12;closing=1;return;
    }
    lcd=current;
#if defined(__mips__)
    if(mxu_ready&&!(((uintptr_t)pixels|(uintptr_t)lcd)&3u)){
        for(i=0;i<272u;++i)dota_mxu_exact_rgb565((uint32_t*)(uintptr_t)(lcd+i*480u),pixels+i*480u,480u);
    }else
#endif
    if(((uintptr_t)pixels&1u)||((uintptr_t)lcd&3u)){
        /* Rare unaligned service buffers still have a safe bytewise path. */
        const uint8_t *src=(const uint8_t*)pixels;volatile uint8_t *dst=(volatile uint8_t*)lcd;
        for(i=0;i<480u*272u;++i){unsigned p=(unsigned)src[i*2u]|(unsigned)src[i*2u+1u]<<8;uint32_t c=color32(p);dst[i*4u]=(uint8_t)c;dst[i*4u+1u]=(uint8_t)(c>>8);dst[i*4u+2u]=(uint8_t)(c>>16);dst[i*4u+3u]=0;}
    }else if(rgb565_table)for(i=0;i<480*272;i+=8){
        lcd[i]=rgb565_table[pixels[i]];lcd[i+1]=rgb565_table[pixels[i+1]];
        lcd[i+2]=rgb565_table[pixels[i+2]];lcd[i+3]=rgb565_table[pixels[i+3]];
        lcd[i+4]=rgb565_table[pixels[i+4]];lcd[i+5]=rgb565_table[pixels[i+5]];
        lcd[i+6]=rgb565_table[pixels[i+6]];lcd[i+7]=rgb565_table[pixels[i+7]];
    }
    else for(i=0;i<480*272;++i)lcd[i]=color32(pixels[i]);
    ++h1_dota_debug.frames;
}
static void screen(int value) {
    if(value==DOTA_SCREEN_SHOP&&view.screen!=DOTA_SCREEN_SHOP)shop_return_screen=view.screen==DOTA_SCREEN_PAUSE?DOTA_SCREEN_PAUSE:DOTA_SCREEN_PLAY;
    view.screen=value;
    /* Shop browsing and transactions pause simulation; delivery resumes later. */
    dota_set_paused(&dota_game,value==DOTA_SCREEN_SKILLS?view.skills_return_screen==DOTA_SCREEN_PAUSE:value!=DOTA_SCREEN_PLAY);
    if(value!=DOTA_SCREEN_PLAY){view.aim_skill=-1;view.aim_item=-1;view.scoreboard=0;}
    pending_actions=0;
    dota_inventory_gesture_cancel(&inventory_gesture);inventory_action.kind=0;
    dota_battlefield_gesture_cancel(&battlefield_gesture);battlefield_action.kind=0;touch_pending=0;minimap_drag=0;
    view.inventory_mode=0;view.inventory_source=view.inventory_hover=-1;
}
static void start_game(void) {
    dota_audio_preload_hero((unsigned)view.hero);
    dota_init(&dota_game,view.hero,0x484431u+(unsigned)view.hero*37u+h1_dota_debug.frames);
    view.camera_x=dota_game.units[0].x;view.camera_y=dota_game.units[0].y;
    view.target_x=clamp(dota_game.units[0].x+240,0,4095);
    view.target_y=clamp(dota_game.units[0].y-240,0,4095);view.aim_skill=-1;view.aim_item=-1;view.follow=1;view.scoreboard=0;
    view.skills_selected=view.skills_details=view.skills_scroll=0;
    screen(DOTA_SCREEN_PLAY);
}
static void choose_attack_target(void) {
    view.aim_item=-1;
    DotaUnit *p=&dota_game.units[dota_game.player];
    int target=-1,i,best=850*850;
    for(i=0;i<DOTA_MAX_UNITS;++i) {
        DotaUnit *u=&dota_game.units[i];int dx,dy,d;
        if(!u->active || u->hp<=0 || dota_unit_invulnerable(&dota_game,i) || u->team==p->team)continue;
        if(!dota_unit_visible(&dota_game,dota_game.player,i))continue;
        dx=u->x-p->x;dy=u->y-p->y;d=dx*dx+dy*dy;
        if(d<best){best=d;target=i;}
    }
    if(target>=0){view.target_x=dota_game.units[target].x;view.target_y=dota_game.units[target].y;}
    else {
        view.target_x=clamp(p->x+240,0,4095);view.target_y=clamp(p->y-240,0,4095);
    }
}
static void target_cursor_begin(void){
    view.target_x=clamp(view.camera_x+40,0,4095);view.target_y=clamp(view.camera_y-20,0,4095);
}
static void invalid_target(int skill){dota_game.last_event=4;dota_game.last_event_value=skill;dota_game.last_event_ticks=40;}
static void begin_skill(int skill){
    int kind=dota_input_skill_target_kind(&dota_game,skill);
    if((unsigned)skill<4u&&dota_skill_is_passive(dota_game.selected_hero,skill))return;
    if(skill==4&&!extra_available(&dota_game,dota_game.player))return;
    view.aim_item=-1;
    if(kind==DOTA_INPUT_TARGET_NONE){
        view.target_x=dota_game.units[dota_game.player].x;view.target_y=dota_game.units[dota_game.player].y;
        if(skill==4)dota_cast(&dota_game,4,view.target_x,view.target_y);
        else pending_actions|=DOTA_INPUT_Q<<skill;
        view.aim_skill=-1;
    }else{view.aim_skill=skill;target_controls_frame=1;target_cursor_begin();}
}
static int skill_cursor_unit(int skill){
    int i,target=-1,best=32*32+1;
    for(i=0;i<DOTA_MAX_UNITS;++i)if(dota_input_skill_unit_valid(&dota_game,skill,i)){
        int dx=dota_game.units[i].x-view.target_x,dy=dota_game.units[i].y-view.target_y,d=dx*dx+dy*dy;
        if(d<best){best=d;target=i;}
    }
    return target;
}
static void confirm_skill(int x,int y,int target){
    int skill=view.aim_skill,kind=dota_input_skill_target_kind(&dota_game,skill);
    if(kind>=DOTA_INPUT_TARGET_ENEMY){
        if(!dota_input_skill_unit_valid(&dota_game,skill,target)){invalid_target(skill);return;}
        view.target_x=dota_game.units[target].x;view.target_y=dota_game.units[target].y;
        if(dota_cast_on_unit(&dota_game,skill,target,dota_game.units[target].generation))view.aim_skill=-1;
        return;
    }
    view.target_x=x;view.target_y=y;
    if(skill==4){if(dota_cast(&dota_game,4,x,y))view.aim_skill=-1;}
    else if((unsigned)skill<4u){pending_actions|=DOTA_INPUT_Q<<skill;view.aim_skill=-1;}
}
static void use_inventory(int slot){
    int i,n=0;DotaItemRecord r;DotaUnit *u=&dota_game.units[dota_game.player];
    int item;if((unsigned)slot>6u)return;
    item=slot==6?dota_neutral_equipped(&dota_game,dota_game.player):u->inventory[slot];
    if(slot<6 && item<0){
        for(i=0;i<slot;++i)if(u->inventory[i]<0)++n;
        if(dota_courier_pending_item(&dota_game,dota_game.player,n)>=0){
            dota_game.last_event=28;dota_game.last_event_value=slot;dota_game.last_event_ticks=50;return;
        }
    }
    if(!dota_items_read(item,&r) || !r.action || !dota_items_is_active(item))return;
    if(r.action==ITEM_ACTION_WARD){
        int was_aiming=view.aim_item==slot,mode=dota_items_ward_available(&dota_game,dota_game.player,slot,0)>0?0:1;
        if(view.aim_item==slot&&item==DOTA_ITEM_WARD_DISPENSER){mode=!view.ward_mode;if(!dota_items_ward_available(&dota_game,dota_game.player,slot,mode))mode=view.ward_mode;}
        if(!dota_items_ward_available(&dota_game,dota_game.player,slot,mode))return;
        view.ward_mode=mode;view.aim_item=slot;view.aim_skill=-1;target_controls_frame=1;if(!was_aiming)target_cursor_begin();return;
    }
    if(item==DOTA_ITEM_FAMANGO||item==DOTA_ITEM_GREAT_FAMANGO||item==DOTA_ITEM_GREATER_FAMANGO){dota_use_item(&dota_game,slot);return;}
    if((r.flags&(ITEM_F_ENEMY|ITEM_F_POINT|ITEM_F_FRIENDLY)) || r.action==ITEM_ACTION_TREE || dota_items_is_teleport(item)){
        view.aim_item=slot;view.aim_skill=-1;target_controls_frame=1;target_cursor_begin();
    }else dota_use_item(&dota_game,slot);
}
static int aimed_item(DotaItemRecord *r){
    int item=view.aim_item==6?dota_neutral_equipped(&dota_game,dota_game.player):
        (unsigned)view.aim_item<6u?dota_game.units[dota_game.player].inventory[view.aim_item]:-1;
    return dota_items_read(item,r)?item:-1;
}
static int item_target_relation(const DotaItemRecord *r){
    return (r->flags&ITEM_F_FRIENDLY)?((r->flags&ITEM_F_ENEMY)?DOTA_HIT_ANY:DOTA_HIT_FRIENDLY):DOTA_HIT_ENEMY;
}
static int item_cursor_unit(const DotaItemRecord *r){
    int i,target=-1,best=32*32+1,relation=item_target_relation(r),team=dota_game.units[dota_game.player].team;
    for(i=0;i<DOTA_MAX_UNITS;++i){
        const DotaUnit *u=&dota_game.units[i];int dx,dy,d;
        if(!u->active||u->hp<=0||(relation==DOTA_HIT_ENEMY&&u->team==team)||(relation==DOTA_HIT_FRIENDLY&&u->team!=team)||!dota_unit_visible(&dota_game,dota_game.player,i))continue;
        if(u->team!=team&&dota_unit_invulnerable(&dota_game,i))continue;
        dx=u->x-view.target_x;dy=u->y-view.target_y;d=dx*dx+dy*dy;if(d<best){best=d;target=i;}
    }
    return target;
}
static void confirm_item(int x,int y,int target){
    DotaItemRecord r;int item=aimed_item(&r);
    if(item<0){view.aim_item=-1;return;}
    if(r.action==ITEM_ACTION_WARD){
        if(!dota_items_ward_available(&dota_game,dota_game.player,view.aim_item,view.ward_mode))return;
        view.target_x=x;view.target_y=y;
        if(!dota_item_world_ward_placeable(&dota_game,dota_game.player,x,y))return;
        target=view.ward_mode?DOTA_WARD_TARGET_SENTRY:-1;
    }
    if(!dota_items_is_teleport(item)&&!(r.flags&ITEM_F_POINT)&&r.action!=ITEM_ACTION_TREE&&target<0){invalid_target(0);return;}
    view.target_x=x;view.target_y=y;
    if(dota_use_item_at(&dota_game,view.aim_item,x,y,target))view.aim_item=-1;
}
static void confirm_cursor(void){
    if(view.aim_skill>=0)confirm_skill(view.target_x,view.target_y,skill_cursor_unit(view.aim_skill));
    else if(view.aim_item>=0){DotaItemRecord r;int item=aimed_item(&r);if(item>=0)confirm_item(view.target_x,view.target_y,dota_items_is_teleport(item)||(r.flags&ITEM_F_POINT)||r.action==ITEM_ACTION_TREE?-1:item_cursor_unit(&r));}
}
static void open_progression(void){
    view.skills_return_screen=view.screen==DOTA_SCREEN_PAUSE?DOTA_SCREEN_PAUSE:DOTA_SCREEN_PLAY;
    view.skills_details=view.skills_scroll=0;screen(DOTA_SCREEN_SKILLS);
}
static void progression_apply(int cell){
    int result=0;view.skills_selected=cell;
    if(cell>=0&&cell<4)result=dota_progression_learn(&dota_game,dota_game.player,cell);
    else if(cell>=4&&cell<12)result=dota_progression_learn_talent(&dota_game,dota_game.player,cell-4);
    else if(cell==12)result=dota_progression_learn_attributes(&dota_game,dota_game.player);
    else if(cell==13){view.skills_details=1;view.skills_scroll=0;}
    if(result)dota_audio_ui(DOTA_AUDIO_CONFIRM);
}
static void inventory_clear(void){
    dota_inventory_gesture_cancel(&inventory_gesture);inventory_action.kind=0;
    view.inventory_mode=0;view.inventory_source=view.inventory_hover=-1;
}
static void inventory_choose(int slot){
    if(view.inventory_mode==1){if(dota_inventory_item(&dota_game,dota_game.player,slot)>=0){view.inventory_source=slot;view.inventory_hover=slot;view.inventory_mode=3;}}
    else if(view.inventory_mode==3){if(slot!=view.inventory_source&&dota_inventory_move(&dota_game,dota_game.player,view.inventory_source,slot))dota_audio_ui(DOTA_AUDIO_CONFIRM);inventory_clear();}
    else if(slot<6){int old=touch_pending;touch_pending=1;use_inventory(slot);touch_pending=old;}
}
static void inventory_touch_update(void){
    if(inventory_gesture.state){
        dota_inventory_gesture_tick(&inventory_gesture,frame_clock_ms());
        if(inventory_gesture.state==DOTA_INVENTORY_GESTURE_DRAG){view.inventory_mode=2;view.inventory_source=inventory_gesture.source;view.inventory_hover=inventory_gesture.hover;view.drag_x=inventory_gesture.x;view.drag_y=inventory_gesture.y;view.aim_skill=view.aim_item=-1;pending_actions=0;}
    }
    if(inventory_action.kind){DotaInventoryGestureAction action=inventory_action;inventory_action.kind=0;
        if(action.kind==DOTA_INVENTORY_GESTURE_MOVE){if(dota_inventory_move(&dota_game,dota_game.player,action.source,action.target))dota_audio_ui(DOTA_AUDIO_CONFIRM);inventory_clear();}
        else inventory_choose(action.source);
    }
    if(view.inventory_mode==2&&!inventory_gesture.state)inventory_clear();
}
static void battlefield_touch_update(void){
    if(battlefield_gesture.state==DOTA_BATTLEFIELD_DRAG){view.follow=0;view.camera_x=battlefield_gesture.camera_x;view.camera_y=battlefield_gesture.camera_y;}
    if(battlefield_action.kind){
        if(battlefield_action.kind==DOTA_BATTLEFIELD_PAN){view.follow=0;view.camera_x=battlefield_action.camera_x;view.camera_y=battlefield_action.camera_y;}
        else if(view.aim_skill==battlefield_skill&&view.aim_item==battlefield_item){
            if(view.aim_skill>=0&&dota_input_skill_target_kind(&dota_game,view.aim_skill)>=DOTA_INPUT_TARGET_ENEMY){
                if(battlefield_target<0||dota_game.units[battlefield_target].generation!=battlefield_target_generation)invalid_target(view.aim_skill);
                else confirm_skill(0,0,battlefield_target);
            }else if(view.aim_item>=0&&battlefield_target>=0){
                if(dota_game.units[battlefield_target].generation!=battlefield_target_generation||!dota_game.units[battlefield_target].active||dota_game.units[battlefield_target].hp<=0)invalid_target(0);
                else confirm_item(dota_game.units[battlefield_target].x,dota_game.units[battlefield_target].y,battlefield_target);
            }else if(view.aim_item>=0&&battlefield_item_unit){invalid_target(0);
            }else{touch_pending=1;touch_x=battlefield_action.x;touch_y=battlefield_action.y;}
        }
        battlefield_action.kind=0;
    }
}
static int inventory_controls(int enter,int escape){
    if(pressed[H1_KEY_I]){
        if(view.inventory_mode)inventory_clear();
        else{inventory_clear();view.inventory_mode=1;view.inventory_source=-1;view.inventory_hover=0;view.aim_skill=view.aim_item=-1;pending_actions=0;}
        return 1;
    }
    if(!view.inventory_mode)return 0;
    if(escape){inventory_clear();return 1;}
    if(view.inventory_mode!=2){int delta=pressed[H1_KEY_LEFT]?-1:pressed[H1_KEY_RIGHT]?1:pressed[H1_KEY_UP]?-3:pressed[H1_KEY_DOWN]?3:0;
        if(delta)view.inventory_hover=(view.inventory_hover+delta+9)%9;
        if(enter)inventory_choose(view.inventory_hover);
    }
    pending_actions=0;return 1;
}
static void shop_purchase(void){
    if(view.shop_category==5 || view.shop_category==6){
        int phase=dota_neutral_phase(&dota_game,dota_game.player),result=0;
        if(phase && view.shop_category!=(phase==1?5:6)){view.shop_category=phase==1?5:6;result=1;}
        else if(phase){result=dota_neutral_choose(&dota_game,dota_game.player,view.shop_item);if(result==2)view.shop_category=6;else if(result==1)view.shop_category=5;}
        else {view.shop_category=5;result=dota_neutral_begin(&dota_game,dota_game.player);}
        if(result){view.shop_item=shop_catalog_item(view.shop_category,0);view.shop_details=0;view.shop_scroll=0;}
    }else dota_audio_purchase(dota_buy(&dota_game,view.shop_item));
}
static void shop_category(int category){
    if(category==SHOP_UPGRADES)shop_catalog_upgrade_from(view.shop_item);
    view.shop_category=category;view.shop_item=shop_catalog_item(category,0);view.shop_details=0;view.shop_scroll=0;
}
static void shop_stage(int stage){
    shop_catalog_recommend_set_stage((stage+SHOP_RECOMMEND_STAGES)%SHOP_RECOMMEND_STAGES);
    view.shop_item=shop_catalog_item(SHOP_RECOMMENDED,0);view.shop_details=0;view.shop_scroll=0;
}
static void shop_vision(void){
    int item=view.shop_item==DOTA_ITEM_WARD_OBSERVER?DOTA_ITEM_WARD_SENTRY:DOTA_ITEM_WARD_OBSERVER;
    shop_category(3);
    if(shop_catalog_index(3,item)>=0)view.shop_item=item;
}
static void play_touch(int x,int y) {
    int wx,wy,i;
    if(x>=416 && x<444 && y>=174 && y<200 && !view.scoreboard){use_inventory(6);return;}
    if(view.scoreboard){
        if(dota_hud_contains(x,y,DOTA_HUD_PAUSE_X,DOTA_HUD_PAUSE_Y,DOTA_HUD_PAUSE_W,DOTA_HUD_PAUSE_H)){screen(DOTA_SCREEN_PAUSE);return;}
        if(dota_hud_contains(x,y,DOTA_HUD_KDA_X,DOTA_HUD_KDA_Y,DOTA_HUD_KDA_W,DOTA_HUD_KDA_H) ||
           dota_hud_contains(x,y,DOTA_HUD_SCORE_CLOSE_X,DOTA_HUD_SCORE_CLOSE_Y,DOTA_HUD_SCORE_CLOSE_W,DOTA_HUD_SCORE_CLOSE_H) ||
           !dota_hud_contains(x,y,DOTA_HUD_SCORE_X,DOTA_HUD_SCORE_Y,DOTA_HUD_SCORE_W,DOTA_HUD_SCORE_H))view.scoreboard=0;
        pending_actions=0;return;
    }
    if(dota_hud_contains(x,y,DOTA_HUD_KDA_X,DOTA_HUD_KDA_Y,DOTA_HUD_KDA_W,DOTA_HUD_KDA_H)){
        inventory_clear();view.scoreboard=1;view.aim_skill=-1;view.aim_item=-1;pending_actions=0;return;
    }
    if(y<24){
        if(x>=DOTA_HUD_PAUSE_X)screen(DOTA_SCREEN_PAUSE);
        return;
    }
    if(dota_hud_contains(x,y,DOTA_HUD_MAP_X,DOTA_HUD_MAP_Y,DOTA_HUD_MAP_W,DOTA_HUD_MAP_H)) {
        wx=(x-DOTA_HUD_MAP_X)*4096/DOTA_HUD_MAP_W;wy=(y-DOTA_HUD_MAP_Y)*4096/DOTA_HUD_MAP_H;
        if(view.aim_skill>=0){view.target_x=wx;view.target_y=wy;confirm_skill(wx,wy,skill_cursor_unit(view.aim_skill));return;}
        if(view.aim_item>=0){DotaItemRecord r;int item=aimed_item(&r);view.target_x=wx;view.target_y=wy;if(item>=0&&dota_items_is_teleport(item))confirm_item(wx,wy,-1);else confirm_cursor();return;}
        minimap_pan(x,y);return;
    }
    if(dota_hud_contains(x,y,DOTA_HUD_PROGRESS_X,DOTA_HUD_PROGRESS_Y,DOTA_HUD_PROGRESS_W,DOTA_HUD_PROGRESS_H)){open_progression();return;}
    if(dota_hud_contains(x,y,DOTA_HUD_ATTR_X,DOTA_HUD_TOP,DOTA_HUD_ATTR_W,72)||
       dota_hud_contains(x,y,DOTA_HUD_SKILLS_X+1,DOTA_HUD_HEALTH_Y,DOTA_HUD_ITEMS_X-DOTA_HUD_SKILLS_X-1,272-DOTA_HUD_HEALTH_Y)){
        view.follow=1;view.camera_x=dota_game.units[dota_game.player].x;view.camera_y=dota_game.units[dota_game.player].y;return;
    }
    i=dota_hud_skill_upgrade_at_count(x,y,extra_available(&dota_game,dota_game.player)?5:4);
    if(i>=0){if(dota_progression_can_learn(&dota_game,dota_game.player,i))progression_apply(i);else open_progression();return;}
    i=dota_hud_skill_at_count(x,y,extra_available(&dota_game,dota_game.player)?5:4);
    if(i>=0) {
        begin_skill(i);
        return;
    }
    i=dota_hud_item_at(x,y);
    if(i>=0){inventory_choose(i);return;}
    if(dota_hud_contains(x,y,DOTA_HUD_SHOP_X,DOTA_HUD_SHOP_Y,DOTA_HUD_SHOP_W,DOTA_HUD_SHOP_H)){screen(DOTA_SCREEN_SHOP);return;}
    if(y>=DOTA_HUD_TOP)return;
    if(dota_status_info_visible() && dota_hud_contains(x,y,DOTA_HUD_STATUS_INFO_X,DOTA_HUD_STATUS_INFO_Y,DOTA_HUD_STATUS_INFO_W,DOTA_HUD_STATUS_INFO_H)){
        dota_game.last_event=dota_game.last_event_ticks=0;return;
    }
    i=dota_status_hit(&dota_game,dota_game.player,x,y);
    if(i){
        if(i>0){dota_game.last_event=DOTA_STATUS_INFO_EVENT;dota_game.last_event_value=i;dota_game.last_event_ticks=3*DOTA_HZ;}
        return;
    }
    if(view.aim_skill<0 && view.aim_item<0 && dota_hit_owned_ward(&dota_game,x,y)>=0){begin_skill(1);return;}
    if(view.aim_skill<0 && view.aim_item<0 && dota_hit_shop(x,y)>=0){screen(DOTA_SCREEN_SHOP);return;}
    if(view.aim_skill<0 && view.aim_item<0 && dota_hit_enemy(&dota_game,&view,x,y,0)<0 && (i=dota_hit_rune(&dota_game,&view,x,y))>=0){dota_rune_order(&dota_game,i);return;}
    if(view.aim_skill<0 && view.aim_item<0 && dota_hit_enemy(&dota_game,&view,x,y,0)<0 && (i=dota_hit_objective(&dota_game,&view,x,y))>=0){
        if(dota_objective_order(&dota_game,i)){view.target_x=dota_objective_positions[i].x;view.target_y=dota_objective_positions[i].y;}
        return;
    }
    if(view.aim_skill>=0){
        int kind=dota_input_skill_target_kind(&dota_game,view.aim_skill);
        if(kind>=DOTA_INPUT_TARGET_ENEMY){
            i=dota_hit_unit(&dota_game,&view,x,y,kind==DOTA_INPUT_TARGET_ANY?DOTA_HIT_ANY:DOTA_HIT_ENEMY,1);
            confirm_skill(0,0,i);return;
        }
        if(dota_screen_to_world(&view,x,y,&wx,&wy))confirm_skill(wx,wy,-1);
        return;
    }
    if(view.aim_item>=0){
        DotaItemRecord r;int item=aimed_item(&r);if(item<0){view.aim_item=-1;return;}
        i=dota_hit_unit(&dota_game,&view,x,y,dota_items_is_teleport(item)?DOTA_HIT_FRIENDLY:item_target_relation(&r),0);
        if(!dota_screen_to_world(&view,x,y,&wx,&wy))return;
        if(i>=0&&!(r.flags&ITEM_F_POINT)){wx=dota_game.units[i].x;wy=dota_game.units[i].y;}
        confirm_item(wx,wy,i);return;
    }
    i=dota_hit_enemy(&dota_game,&view,x,y,0);
    if(i>=0){wx=dota_game.units[i].x;wy=dota_game.units[i].y;}
    else if(!dota_screen_to_world(&view,x,y,&wx,&wy))return;
    view.target_x=wx;view.target_y=wy;
    if(i>=0)dota_attack_target(&dota_game,i);else dota_move_to(&dota_game,wx,wy);
}
static void process_ui(void) {
    int enter=pressed[H1_KEY_ENTER]||pressed[H1_KEY_CONFIRM];
    int back=pressed[H1_KEY_BACK],escape=pressed[H1_KEY_ESCAPE]||back;int i;
    static const unsigned skill_keys[4]={H1_KEY_Q,H1_KEY_W,H1_KEY_E,H1_KEY_R};
    static const unsigned item_keys[6]={H1_KEY_Z,H1_KEY_X,H1_KEY_C,H1_KEY_V,H1_KEY_B,H1_KEY_N};
    target_controls_frame=view.aim_skill>=0||view.aim_item>=0;
    if(view.screen==DOTA_SCREEN_MENU) {
        if(pressed[H1_KEY_LEFT])view.hero=(view.hero+DOTA_HERO_TYPES-1)%DOTA_HERO_TYPES;
        if(pressed[H1_KEY_RIGHT])view.hero=(view.hero+1)%DOTA_HERO_TYPES;
        if(pressed[H1_KEY_UP]||pressed[H1_KEY_DOWN])view.hero=(view.hero+5)%DOTA_HERO_TYPES;
        if(pressed[H1_KEY_H])screen(DOTA_SCREEN_HELP);
        if(enter)start_game();
        if(touch_pending) {
            if(touch_y>=70 && touch_y<212 && touch_x>=12 && touch_x<468){int row=(touch_y-70)/74,col=(touch_x-12)/92;if((touch_y-70)%74<68 && (touch_x-12)%92<88)view.hero=clamp(row*5+col,0,DOTA_HERO_TYPES-1);}
            else if(touch_x>=142 && touch_x<=338 && touch_y>=236 && touch_y<=266)start_game();
            else if(touch_x>=360 && touch_y>=236)screen(DOTA_SCREEN_HELP);
        }
    } else if(view.screen==DOTA_SCREEN_PLAY) {
        if(pressed[H1_KEY_P]||back)screen(DOTA_SCREEN_PAUSE);
        else if(pressed[H1_KEY_H]){inventory_clear();dota_battlefield_gesture_cancel(&battlefield_gesture);battlefield_action.kind=0;minimap_drag=0;view.scoreboard=!view.scoreboard;view.aim_skill=-1;view.aim_item=-1;pending_actions=0;}
        else if(escape){if(view.inventory_mode||inventory_gesture.state)inventory_clear();else if(view.scoreboard){view.scoreboard=0;pending_actions=0;}else if(view.aim_skill>=0)view.aim_skill=-1;else if(view.aim_item>=0)view.aim_item=-1;else screen(DOTA_SCREEN_PAUSE);}
        else if(!view.scoreboard&&pressed[H1_KEY_U])open_progression();
        else if(!view.scoreboard && pressed[H1_KEY_M])screen(DOTA_SCREEN_SHOP);
        if(view.screen==DOTA_SCREEN_PLAY && view.scoreboard){if(touch_pending)play_touch(touch_x,touch_y);}
        else if(view.screen==DOTA_SCREEN_PLAY && !escape) {
            battlefield_touch_update();
            inventory_touch_update();
            if(inventory_controls(enter,escape)){memset(pressed,0,sizeof pressed);touch_pending=0;return;}
            for(i=0;i<4;++i)if(pressed[skill_keys[i]]&&key(H1_KEY_SHIFT))progression_apply(i);
            else if(pressed[skill_keys[i]] && !dota_skill_is_passive(dota_game.selected_hero,i)){
                begin_skill(i);
            }
            if(pressed[H1_KEY_T])begin_skill(4);
            if(pressed[H1_KEY_J]){choose_attack_target();pending_actions|=DOTA_INPUT_ATTACK;}
            if(pressed[H1_KEY_K]){pending_actions=DOTA_INPUT_STOP;view.aim_skill=-1;view.aim_item=-1;}
            if(pressed[H1_KEY_L])dota_objective_use_lotus(&dota_game,dota_game.player);
            for(i=0;i<6;++i)if(pressed[item_keys[i]])use_inventory(i);
            if(pressed[H1_KEY_Y])use_inventory(6);
            if(view.aim_skill>=0||view.aim_item>=0){
                view.target_x=clamp(view.target_x+12*(key(H1_KEY_RIGHT)-key(H1_KEY_LEFT)),0,4095);
                view.target_y=clamp(view.target_y+12*(key(H1_KEY_DOWN)-key(H1_KEY_UP)),0,4095);
                if(enter)confirm_cursor();
            }
            if(pressed[H1_KEY_SPACE]){view.follow=1;view.camera_x=dota_game.units[dota_game.player].x;view.camera_y=dota_game.units[dota_game.player].y;}
            if(key(H1_KEY_A)||key(H1_KEY_S)||key(H1_KEY_D)||key(H1_KEY_F)) {
                view.follow=0;view.camera_x+=36*(key(H1_KEY_D)-key(H1_KEY_A));
                view.camera_y+=36*(key(H1_KEY_S)-key(H1_KEY_F));
            }
            if(touch_pending)play_touch(touch_x,touch_y);
        }
    } else if(view.screen==DOTA_SCREEN_PAUSE) {
        if(enter||escape||pressed[H1_KEY_P])screen(DOTA_SCREEN_PLAY);
        if(pressed[H1_KEY_M])screen(DOTA_SCREEN_SHOP);
        if(pressed[H1_KEY_H])screen(DOTA_SCREEN_MENU);
        if(pressed[H1_KEY_X])closing=1;
        if(pressed[H1_KEY_U])open_progression();
        if(touch_pending&&touch_y>=208&&touch_y<241){
            if(touch_x>=24&&touch_x<160)screen(DOTA_SCREEN_PLAY);
            else if(touch_x>=172&&touch_x<308)screen(DOTA_SCREEN_SHOP);
            else if(touch_x>=320&&touch_x<456)open_progression();
        }
        if(touch_pending&&touch_y>=243&&touch_y<259){if(touch_x>=24&&touch_x<160)screen(DOTA_SCREEN_MENU);else if(touch_x>=344&&touch_x<456)closing=1;}
    } else if(view.screen==DOTA_SCREEN_SKILLS){
        int selected=view.skills_selected;
        if(escape||pressed[H1_KEY_U]){if(escape&&view.skills_details)view.skills_details=0;else screen(view.skills_return_screen);}
        else if(view.skills_details){
            if(pressed[H1_KEY_I])view.skills_details=0;
            if(pressed[H1_KEY_UP])view.skills_scroll=clamp(view.skills_scroll-1,0,1000);
            if(pressed[H1_KEY_DOWN])view.skills_scroll=clamp(view.skills_scroll+1,0,1000);
            if(touch_pending)view.skills_details=0;
        }else{
            for(i=0;i<4;++i)if(pressed[skill_keys[i]])progression_apply(i);
            if(pressed[H1_KEY_LEFT]){if(selected<4)selected=selected?selected-1:13;else if(selected<12)selected=4+(selected-4)/2*2;else selected=selected==13?3:12;}
            if(pressed[H1_KEY_RIGHT]){if(selected<4)selected=selected==3?13:selected+1;else if(selected<12)selected=5+(selected-4)/2*2;else selected=13;}
            if(pressed[H1_KEY_UP]){if(selected>=4&&selected<6)selected=selected==4?0:3;else if(selected>=6&&selected<12)selected-=2;else if(selected>=12)selected=selected==12?10:11;}
            if(pressed[H1_KEY_DOWN]){if(selected<4)selected=selected<2?4:5;else if(selected>=4&&selected<10)selected+=2;else if(selected<12)selected=12;else selected=0;}
            view.skills_selected=selected;
            if(enter)progression_apply(selected);
            if(pressed[H1_KEY_I]){view.skills_details=1;view.skills_scroll=0;}
            if(touch_pending){
                int cell=dota_hud_progression_cell(touch_x,touch_y);
                if(touch_x>=438&&touch_y<24)screen(view.skills_return_screen);
                else if(touch_y>=246&&touch_x>=300){view.skills_details=1;view.skills_scroll=0;}
                else if(cell>=0)progression_apply(cell);
            }
        }
    } else if(view.screen==DOTA_SCREEN_SHOP) {
        if(pressed[H1_KEY_P]){screen(DOTA_SCREEN_PAUSE);memset(pressed,0,sizeof pressed);touch_pending=0;return;}
        int count=shop_catalog_count(view.shop_category),index=shop_catalog_index(view.shop_category,view.shop_item),delta=0;
        if(index<0)view.shop_item=shop_catalog_item(view.shop_category,0);
        if(pressed[H1_KEY_Q] || pressed[H1_KEY_W])shop_category((view.shop_category+(pressed[H1_KEY_Q]?SHOP_CATEGORIES-1:1))%SHOP_CATEGORIES);
        if(pressed[H1_KEY_T])shop_category(SHOP_RECOMMENDED);
        if(pressed[H1_KEY_G])shop_category(SHOP_UPGRADES);
        if(pressed[H1_KEY_L])shop_vision();
        if(view.shop_category==SHOP_RECOMMENDED&&(pressed[H1_KEY_A]||pressed[H1_KEY_D]))shop_stage(shop_catalog_recommend_stage()+(pressed[H1_KEY_A]?-1:1));
        if(pressed[H1_KEY_LEFT])delta=-1;
        if(pressed[H1_KEY_RIGHT])delta=1;
        if(pressed[H1_KEY_UP])delta=-2;
        if(pressed[H1_KEY_DOWN])delta=2;
        if(pressed[H1_KEY_PAGE_UP])delta=-6;
        if(pressed[H1_KEY_PAGE_DOWN])delta=6;
        if(view.shop_details){if(delta){view.shop_scroll=clamp(view.shop_scroll+(delta>0?1:-1),0,1000);delta=0;}}
        if(delta)view.shop_item=shop_catalog_move(view.shop_category,view.shop_item,delta);
        if(pressed[H1_KEY_I]){view.shop_details=!view.shop_details;view.shop_scroll=0;}
        if(enter)shop_purchase();
        for(i=0;i<6;++i)if(pressed[item_keys[i]]){if(key(H1_KEY_SHIFT))dota_items_disassemble(&dota_game,dota_game.player,i);else dota_items_sell(&dota_game,dota_game.player,i);}
        if(escape||pressed[H1_KEY_M]){if(view.shop_details && escape&&!back)view.shop_details=0;else screen(shop_return_screen);}
        if(touch_pending){
            if(touch_y<24 && touch_x>=440)screen(shop_return_screen);
            else if(touch_y<24&&touch_x>=216&&touch_x<253)shop_vision();
            else if(touch_y>=26 && touch_y<49)shop_category(clamp(touch_x/48,0,SHOP_CATEGORIES-1));
            else if(view.shop_category==SHOP_RECOMMENDED&&touch_y>=50&&touch_y<69&&touch_x>=184&&touch_x<462)shop_stage((touch_x-184)/70);
            else if(!view.shop_details && touch_y>=72 && touch_y<192 && touch_x>=12 && touch_x<468){int col=(touch_x-12)/230,row=(touch_y-72)/40;index=shop_catalog_index(view.shop_category,view.shop_item);index=(index<0?0:index/6*6)+row*2+col;if(index<count)view.shop_item=shop_catalog_item(view.shop_category,index);}
            else if(touch_y>=192 && touch_y<216){if(touch_x<80)view.shop_item=shop_catalog_move(view.shop_category,view.shop_item,-6);else if(touch_x>=400)view.shop_item=shop_catalog_move(view.shop_category,view.shop_item,6);else{view.shop_details=!view.shop_details;view.shop_scroll=0;}}
            else if(touch_y>=220 && touch_y<246){if(view.shop_category!=5&&view.shop_category!=6&&touch_x>=326&&touch_x<468)shop_category(SHOP_UPGRADES);else if(touch_x>=140 && touch_x<=(view.shop_category==5||view.shop_category==6?340:314))shop_purchase();else if(view.shop_details)view.shop_scroll=clamp(view.shop_scroll+(touch_x<140?-4:4),0,1000);}
        }
    } else if(view.screen==DOTA_SCREEN_RESULT) {
        if(enter)start_game();else if(escape)screen(DOTA_SCREEN_MENU);
        if(touch_pending){if(touch_y>=190 && touch_y<=226)start_game();else if(touch_y>=240)screen(DOTA_SCREEN_MENU);}
    } else if(view.screen==DOTA_SCREEN_HELP) {
        if(enter||escape||touch_pending)screen(DOTA_SCREEN_MENU);
    }
    memset(pressed,0,sizeof pressed);touch_pending=0;
}
static void play_input(DotaInput *input){
    int blocked=view.screen!=DOTA_SCREEN_PLAY||view.scoreboard||view.inventory_mode||target_controls_frame||view.aim_skill>=0||view.aim_item>=0;
    input->move_x=blocked?0:key(H1_KEY_RIGHT)-key(H1_KEY_LEFT);
    input->move_y=blocked?0:key(H1_KEY_DOWN)-key(H1_KEY_UP);
    input->target_x=view.target_x;input->target_y=view.target_y;input->actions=view.screen==DOTA_SCREEN_PLAY?pending_actions:0;
}
static int prewarm_cancel_poll(void){pump();return closing;}
int h1_app_main(void) {
    uint32_t last,accumulator=0,frame_phase=0;int i;
    h1_dota_debug.magic=0x31415444u;
    view.screen=DOTA_SCREEN_MENU;view.hero=0;view.aim_skill=-1;view.aim_item=-1;view.follow=1;
    dota_init(&dota_game,0,0x484431u);dota_set_paused(&dota_game,1);
    if(!open_video()){h1_dota_debug.error=11;goto finish;}
#ifdef DOTA_BAKED
    {int asset_error=0;
    if(!bs_open_default())asset_error=bs_last_error();
    else if(bs_pack_header()->version!=BS_VERSION)asset_error=13;
    else if(!item_assets_open())asset_error=14;
    else {
        unsigned sprite;
        baked_io_reserve_cache(256u*1024u);
        item_assets_reserve_rules_cache(64u*1024u);
        item_assets_reserve_icon_cache(64);
        sprite=bs_reserve_cache(14u*1024u*1024u);
        if(sprite<12u*1024u*1024u && bs_ready()){
            /* Retry the established budget without retaining the first
             * attempt's directory allocation alongside the second one. */
            (void)bs_reserve_cache(0);
            sprite=bs_reserve_cache(12u*1024u*1024u);
        }
        if(!sprite || !bs_ready())asset_error=bs_last_error()?bs_last_error():14;
        else {
            /* Preload only the initial Radiant fountain.
             * No pinning, extra heap or phase changes. A skipped low-memory
             * warmup retains the ordinary renderer; real I/O errors surface. */
            (void)bs_map_sidecar_open_default();
            if(!vision_assets_open_default()){asset_error=15;goto assets_done;}
            (void)bs_prewarm_fountain(0,prewarm_cancel_poll);
            if(closing)goto finish;
            if(bs_last_error())asset_error=bs_last_error();
            h1_dota_cache.magic=0x31435048u;h1_dota_cache.version=3;
            h1_dota_cache.rgb_bytes=rgb565_table?65536u*sizeof(uint32_t):0;
            h1_dota_cache.sprite_bytes=bs_sprite_cache_bytes();
            h1_dota_cache.resident_frames=bs_resident_frame_count();
            h1_dota_cache.scene_heap_bytes=bs_dynamic_storage_bytes();
            h1_dota_cache.io_bytes=baked_io_cache_bytes();
            h1_dota_cache.io_heap_bytes=baked_io_dynamic_storage_bytes();
            h1_dota_cache.rules_bytes=item_assets_rules_cache_bytes();
            h1_dota_cache.icon_slots=item_assets_icon_cache_slots();h1_dota_cache.icon_heap_bytes=item_assets_icon_cache_bytes();
            h1_dota_cache.vision_heap_bytes=vision_assets_stats()->heap_bytes;
            h1_dota_cache.total_heap_bytes=h1_dota_cache.rgb_bytes+h1_dota_cache.scene_heap_bytes+h1_dota_cache.io_heap_bytes+h1_dota_cache.rules_bytes+h1_dota_cache.icon_heap_bytes+h1_dota_cache.vision_heap_bytes;
            h1_dota_cache.fallback_flags=(!rgb565_table&&!mxu_ready?1u:0u)|(sprite<12u*1024u*1024u?2u:0u)|(!bs_resident_frame_count()?4u:0u)|(baked_io_cache_bytes()<256u*1024u?8u:0u)|(!item_assets_rules_cache_bytes()?16u:0u)|(h1_dota_cache.icon_slots<64?32u:0u)|(!vision_assets_stats()->ready?64u:0u);
            shop_catalog_reset();shop_catalog_bind(&dota_game);
        }
    }
assets_done:
    if(asset_error){
        h1_dota_debug.error=20u+(unsigned)asset_error;
        dota_render_asset_error(asset_error);h1_dota_present(dota_pixels);
        while(!closing){pump();if(pressed[H1_KEY_ENTER]||pressed[H1_KEY_CONFIRM]||pressed[H1_KEY_ESCAPE])closing=1;}
        goto finish;
    }}
#endif
    dota_audio_open();dota_audio_preload_hero(0);
    h1_dota_cache.audio_heap_bytes=dota_audio_stats()->heap_bytes;h1_dota_cache.total_heap_bytes+=h1_dota_cache.audio_heap_bytes;
    h1_timer_start();timer_started=1;last=frame_clock_ms()-50;
    while(!closing) {
        uint32_t now,elapsed;int old_screen;pump();if(closing)break;
        dota_audio_update(&dota_game,view.screen==DOTA_SCREEN_SKILLS?(view.skills_return_screen==DOTA_SCREEN_PAUSE?DOTA_SCREEN_PAUSE:DOTA_SCREEN_PLAY):view.screen,view.hero);
        now=frame_clock_ms();elapsed=now-last;
        /* Audio positions advance in blocks. Keep the 50ms frame phase rather
         * than discarding its overshoot, which otherwise caps 31.25ms audio
         * blocks at 16 frames/s. Simulation still receives actual elapsed time. */
        if(elapsed<50-frame_phase)continue;
        frame_phase=elapsed-(50u-frame_phase);if(frame_phase>49u)frame_phase=49u;
        last=now;if(elapsed>250)elapsed=250;
        old_screen=view.screen;process_ui();
        if(old_screen!=view.screen)dota_audio_ui(DOTA_AUDIO_CONFIRM);
        if(view.screen==DOTA_SCREEN_PLAY || (view.screen==DOTA_SCREEN_SKILLS&&view.skills_return_screen!=DOTA_SCREEN_PAUSE)) {
            DotaInput input;
            play_input(&input);
            if(old_screen==DOTA_SCREEN_PLAY || (old_screen==DOTA_SCREEN_SKILLS&&view.skills_return_screen!=DOTA_SCREEN_PAUSE))accumulator+=elapsed;
            else accumulator=0; /* Menus and pause do not accrue game time. */
            for(i=0;i<5 && accumulator>=50;++i) {
                dota_tick(&dota_game,&input);input.actions=0;accumulator-=50;
            }
            /* A phase-corrected short visual frame can run zero logic ticks.
             * Retain its input until a tick actually consumes it. */
            if(i)pending_actions=0;
            if(view.follow){view.camera_x=dota_game.units[0].x;view.camera_y=dota_game.units[0].y;}
            if(dota_game.state!=DOTA_PLAYING)screen(DOTA_SCREEN_RESULT);
        } else accumulator=0;
        view.camera_x=clamp(view.camera_x,0,4095);view.camera_y=clamp(view.camera_y,0,4095);
        h1_dota_debug.screen=view.screen;h1_dota_debug.hero=view.hero;
        h1_dota_debug.shop_item=view.shop_item;h1_dota_debug.ticks=dota_game.tick;
        view.frames=h1_dota_debug.frames;dota_render(&dota_game,&view);h1_dota_present(dota_pixels);
    }
finish:
    dota_audio_close();
#ifdef DOTA_BAKED
    vision_assets_close();item_assets_close();bs_close();
#endif
    cache_memory_free(rgb565_table);rgb565_table=0;
    h1_dota_cache.total_heap_bytes=0;h1_dota_cache.released=1;
    if(timer_started)h1_timer_stop();
    if(opened && gui(0x850)){typedef int (*Close)(void);((Close)gui(0x850))();}
    lcd=0;opened=timer_started=0;return h1_dota_debug.error?1:0;
}
