/* Host execution of actual native input, renderer, item provider and rules.
 * SDK keyboard/timer and audio UI services are modeled, never gameplay. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "h1_types.h"
#define H1_RUNTIME_H
#define H1_RUNTIME_GUI_TABLE_SLOT 0x83c00004u
#define H1_RUNTIME_FS_TABLE_SLOT 0x83c00008u
#define H1_RUNTIME_SYS_TABLE_SLOT 0x83c0000cu
#define H1_RUNTIME_MEM_TABLE_SLOT 0x83c00010u
#define H1_RUNTIME_RES_TABLE_SLOT 0x83c00014u
static unsigned mock_ms=1000;
static int entry_mode,entry_event_sent,audio_open_calls;
static int fail_alloc,entry_force_heap_failure;
static const char *entry_vision_path;
static uint32_t modeled_lcd[480*272];
static int entry_event(int *code,int *value);
static int entry_video_open(void){return 1;}
static int entry_video_close(void){return 0;}
static void entry_timer(void){}
static volatile uint32_t *entry_buffer(h1_u16 *info,void *unused){(void)unused;info[1]=480;info[2]=272;info[3]=1920;info[5]=32;return modeled_lcd;}
static unsigned mock_timer(void){return mock_ms;}
static void *h1_runtime_table(h1_u32 slot){(void)slot;return 0;}
static void *h1_runtime_entry(void *table,h1_u32 offset){(void)table;
    if(offset==0x71cu)return (void *)mock_timer;
    if(entry_mode){if(offset==0x84cu)return (void *)entry_video_open;if(offset==0x850u)return (void *)entry_video_close;if(offset==0x8f4u)return (void *)entry_buffer;if(offset==0x750u)return (void *)entry_event;if(offset==0x714u||offset==0x718u)return (void *)entry_timer;}
    return 0;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#include "h1_sdk.h"
#pragma GCC diagnostic pop
#define memcpy native_test_memcpy
#define memset native_test_memset
#define memcmp native_test_memcmp
#define bs_open_default entry_scene_open
#define vision_assets_open_default entry_vision_open
#include "../platform/native.c"
#undef memcpy
#undef memset
#undef memcmp
#undef bs_open_default
#undef vision_assets_open_default
#include "sight.h"
#include "match_time.h"
#include "item_world.h"
#include "vision.h"
#include "roshan_data.h"
#include "vision_asset_identity.h"
#include "../private/generated/dota_overview.h"
void dota_audio_ui(unsigned event){(void)event;}
void dota_audio_purchase(int succeeded){(void)succeeded;}
unsigned dota_audio_preload_hero(unsigned hero){(void)hero;return 0;}
int dota_audio_clock_ms(uint32_t *milliseconds){(void)milliseconds;return 0;}
void dota_audio_close(void){}
int dota_audio_open(void){++audio_open_calls;return 0;}
void dota_audio_update(const DotaGame *g,int screen,int hero){(void)g;(void)screen;(void)hero;}
const DotaAudioStats *dota_audio_stats(void){static DotaAudioStats s;return &s;}
static int entry_event(int *code,int *value){if(!entry_event_sent++){*code=H1_EVENT_KEY_DOWN;*value=H1_KEY_BACK;}else{*code=*value=-1;}return 0;}
static const char *entry_pack;
int entry_scene_open(void){return baked_io_open(entry_pack)&&bs_initialize();}
int entry_vision_open(void){int result;fail_alloc=entry_force_heap_failure;result=vision_assets_open(entry_vision_path?entry_vision_path:"missing-required-vision-fixture.H1V");fail_alloc=0;return result;}
static unsigned checks,cases,live_allocations;
static const char *output_dir;
void *cache_memory_alloc(unsigned bytes){void *p=fail_alloc?0:malloc(bytes);if(p)++live_allocations;return p;}
void cache_memory_free(void *p){if(p){--live_allocations;free(p);}}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *flat_workspace;
static void flat_sight(void){dota_sight_detach();CHECK(dota_sight_attach(0,0,flat_workspace,dota_sight_workspace_bytes()));}
static void key_press(int code){pressed[code]=held[code]=1;process_ui();held[code]=0;}
static void ui_tap(int x,int y){touch_x=x;touch_y=y;touch_pending=1;process_ui();}
static void setup(int hero){
    int i,s;flat_sight();dota_init(&dota_game,hero,161);dota_game.units[0].level=30;dota_progression_level_changed(&dota_game,0);
    for(s=0;s<4;++s)for(i=0;i<dota_progression_skill_max(hero,s);++i)CHECK(dota_progression_learn(&dota_game,0,s));
    for(i=1;i<DOTA_MAX_UNITS;++i)dota_game.units[i].active=0;
    dota_rules_set_position(&dota_game,0,2048,2048);dota_game.units[0].mana=dota_game.units[0].max_mana;
    memset(&view,0,sizeof(view));view.screen=DOTA_SCREEN_PLAY;view.hero=hero;view.follow=1;view.camera_x=view.camera_y=2048;view.aim_skill=view.aim_item=-1;view.target_x=2100;view.target_y=2048;
    memset(held,0,sizeof(held));memset(pressed,0,sizeof(pressed));pending_actions=0;touch_pending=touch_down=target_controls_frame=0;closing=0;
    dota_inventory_gesture_cancel(&inventory_gesture);dota_battlefield_gesture_cancel(&battlefield_gesture);inventory_action.kind=battlefield_action.kind=0;
    dota_sight_invalidate();
}
static void write_frame(const char *name){
    char path[2048];FILE *f;unsigned char row[480*3];int x,y;snprintf(path,sizeof(path),"%s/%s.ppm",output_dir,name);f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n480 272\n255\n");
    for(y=0;y<272;++y){for(x=0;x<480;++x){unsigned p=dota_pixels[y*480+x];row[x*3]=(unsigned char)((p>>11)*255/31);row[x*3+1]=(unsigned char)(((p>>5)&63)*255/63);row[x*3+2]=(unsigned char)((p&31)*255/31);}CHECK(fwrite(row,1,sizeof(row),f)==sizeof(row));}CHECK(!fclose(f));
}
static void shot(const char *name){dota_render(&dota_game,&view);write_frame(name);}
static void enemy(int at,int x,int y){DotaUnit *u=&dota_game.units[at];u->active=1;u->hp=u->max_hp=2000;u->type=DOTA_HERO;u->team=!dota_game.units[0].team;u->hero_id=DOTA_HERO_NECROPHOS;u->x=x;u->y=y;u->generation=11;u->stun_ticks=u->channel_ticks=u->taunt_ticks=0;dota_sight_invalidate();}
static void loader_tests(const char *valid,const char *bad_header,const char *bad_crc,const char *truncated){
    unsigned before,closes;CHECK(bs_map_sidecar_open("assets/runtime/MAP12.H1C"));CHECK(baked_io_aux_is_open());before=baked_io_aux_stats()->read_requests;
    CHECK(vision_assets_open(valid));CHECK(vision_assets_stats()->ready&&vision_assets_stats()->heap_bytes==139264u+dota_sight_workspace_bytes());CHECK(vision_assets_stats()->unknown_height_cells==1270u&&vision_assets_stats()->payload_crc32==VISION_EXPECTED_PAYLOAD_CRC32);CHECK(vision_assets_stats()->read_requests==4u&&vision_assets_stats()->bytes_read==139328u&&vision_assets_stats()->closes==1u);
    CHECK(baked_io_aux_is_open()&&baked_io_aux_stats()->read_requests==before);
    dota_init(&dota_game,DOTA_HERO_AXE,161);memset(&view,0,sizeof(view));view.screen=DOTA_SCREEN_PLAY;view.hero=DOTA_HERO_AXE;view.camera_x=dota_game.units[0].x;view.camera_y=dota_game.units[0].y;view.aim_skill=view.aim_item=-1;view.target_x=view.target_y=-1;shot("host-source-terrain-pregame");CHECK(vision_assets_stats()->read_requests==4u);closes=vision_assets_stats()->closes;
    CHECK(!vision_assets_open(bad_header));CHECK(!vision_assets_stats()->ready&&!vision_assets_stats()->heap_bytes&&vision_assets_stats()->last_reason==2u&&vision_assets_stats()->closes==closes+1);closes++;
    CHECK(!vision_assets_open(bad_crc));CHECK(!vision_assets_stats()->ready&&!vision_assets_stats()->heap_bytes&&vision_assets_stats()->last_reason==3u&&vision_assets_stats()->closes==closes+1);closes++;
    CHECK(!vision_assets_open(truncated));CHECK(!vision_assets_stats()->ready&&!vision_assets_stats()->heap_bytes&&vision_assets_stats()->last_reason==2u&&vision_assets_stats()->closes==closes+1);closes++;
    fail_alloc=1;CHECK(!vision_assets_open(valid));fail_alloc=0;CHECK(!vision_assets_stats()->heap_bytes&&vision_assets_stats()->last_reason==4u&&vision_assets_stats()->closes==closes+1);
    CHECK(!vision_assets_open("missing-vision-fixture.H1V"));CHECK(vision_assets_stats()->last_reason==1u&&vision_assets_stats()->released);
    CHECK(vision_assets_open(valid));vision_assets_close();CHECK(!vision_assets_stats()->ready&&!vision_assets_stats()->heap_bytes&&vision_assets_stats()->released);CHECK(baked_io_aux_is_open());++cases;
}
static void fog_and_leak_tests(void){
    uint16_t before[480*272];int bounds[4];setup(DOTA_HERO_LINA);view.camera_x=2500;view.follow=0;
    enemy(5,2670,2048);dota_game.extra.hero[5].ghost_until=1000;CHECK(!dota_unit_visible(&dota_game,0,5));shot("host-fog-hidden-enemy");CHECK(!bs_unit_screen_bounds(5,bounds,0));CHECK(bs_fog_stats()->cells==1701u&&bs_fog_stats()->queries<=1701u&&bs_fog_stats()->dimmed_pixels>0&&bs_fog_stats()->soft_cells>0);
    memcpy(before,dota_pixels,sizeof(before));dota_game.units[5].active=0;dota_sight_invalidate();dota_render(&dota_game,&view);CHECK(!memcmp(before,dota_pixels,sizeof(before)));++cases;
    {int x=60,y=27,wx=x*DOTA_WORLD/72,wy=y*DOTA_WORLD/72;unsigned p=(DOTA_HUD_MAP_Y+y)*480+DOTA_HUD_MAP_X+x;uint16_t dim=vision_assets_fog_color(dota_overview[y*72+x],3);
        CHECK(dota_sight_fog_state(&dota_game,0,wx,wy)==0);CHECK(dota_pixels[p]==dim);
        dota_game.units[0].x=wx;dota_game.units[0].y=wy;dota_sight_invalidate();CHECK(dota_sight_fog_state(&dota_game,0,wx,wy)==2);
        dota_game.units[0].x=dota_game.units[0].y=2048;dota_sight_invalidate();CHECK(dota_sight_fog_state(&dota_game,0,wx,wy)==1);dota_render(&dota_game,&view);CHECK(dota_pixels[p]==dim);
    }++cases;
}
static int ward_at(int kind,int owner){int i;for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;++i)if(dota_game.units[i].active&&dota_game.item_world.unit[i-DOTA_ITEM_UNIT_FIRST].kind==kind&&dota_game.item_world.unit[i-DOTA_ITEM_UNIT_FIRST].owner==owner)return i;return -1;}
static void ward_ui_tests(void){
    int ward,enemy_ward,bounds[4],before;DotaGame frozen;
    setup(DOTA_HERO_LINA);key_press(H1_KEY_M);key_press(H1_KEY_L);CHECK(view.shop_category==3&&view.shop_item==DOTA_ITEM_WARD_OBSERVER&&dota_game.paused);shot("host-ward-shop");key_press(H1_KEY_L);CHECK(view.shop_item==DOTA_ITEM_WARD_SENTRY);key_press(H1_KEY_BACK);CHECK(view.screen==DOTA_SCREEN_PLAY);
    CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_OBSERVER,201));before=dota_items_charges(&dota_game,0,0);key_press(H1_KEY_Z);CHECK(view.aim_item==0);view.target_x=2100;view.target_y=2048;frozen=dota_game;shot("host-ward-preview");CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));key_press(H1_KEY_ENTER);CHECK(view.aim_item<0);ward=ward_at(ITEM_UNIT_OBSERVER,0);CHECK(ward>=0);CHECK(dota_game.units[0].inventory[0]<0||dota_items_charges(&dota_game,0,0)==before-1);dota_render(&dota_game,&view);CHECK(bs_unit_screen_bounds(ward,bounds,0)&&bs_unit_animation(ward)->model==88);++cases;
    enemy(5,2170,2048);CHECK(dota_rules_item_primitive(&dota_game,ITEM_ACTION_WARD|(DOTA_ITEM_WARD_OBSERVER<<8),5,-1,2120,2048,0,7200));enemy_ward=ward_at(ITEM_UNIT_OBSERVER,5);CHECK(enemy_ward>=0);dota_sight_invalidate();CHECK(!dota_unit_visible(&dota_game,0,enemy_ward));dota_render(&dota_game,&view);CHECK(!bs_unit_screen_bounds(enemy_ward,bounds,0));
    CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_SENTRY,201));key_press(H1_KEY_Z);CHECK(view.aim_item==0);view.target_x=2090;view.target_y=2048;key_press(H1_KEY_ENTER);CHECK(view.aim_item<0&&ward_at(ITEM_UNIT_SENTRY,0)>=0);dota_sight_invalidate();CHECK(dota_unit_visible(&dota_game,0,enemy_ward));shot("host-ward-detection");CHECK(bs_unit_screen_bounds(enemy_ward,bounds,0)&&bs_unit_animation(enemy_ward)->model==88);++cases;
}
static void combined_ward_tests(void){
    int packed,i,sentry;DotaGame before;DotaInput input={0};
    setup(DOTA_HERO_LINA);CHECK(sizeof(DotaView)==112);CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_OBSERVER,201));CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_SENTRY,201));CHECK(dota_game.units[0].inventory[0]==DOTA_ITEM_WARD_DISPENSER);packed=dota_items_charges(&dota_game,0,0);CHECK(dota_items_ward_available(&dota_game,0,0,0)==1&&dota_items_ward_available(&dota_game,0,0,1)==1);
    key_press(H1_KEY_Z);CHECK(view.aim_item==0&&!view.ward_mode);view.target_x=2100;view.target_y=2048;key_press(H1_KEY_Z);CHECK(view.ward_mode==1&&view.target_x==2100&&view.target_y==2048&&dota_items_charges(&dota_game,0,0)==packed);shot("host-combined-sentry-preview");
    inventory_choose(0);CHECK(!view.ward_mode&&view.target_x==2100);key_press(H1_KEY_Z);CHECK(view.ward_mode==1);key_press(H1_KEY_ENTER);sentry=ward_at(ITEM_UNIT_SENTRY,0);CHECK(sentry>=0&&dota_items_ward_available(&dota_game,0,0,0)==1&&!dota_items_ward_available(&dota_game,0,0,1));
    for(i=0;i<200&&dota_items_cooldown(&dota_game,0,0)>0;++i){dota_tick(&dota_game,&input);}
    CHECK(!dota_items_cooldown(&dota_game,0,0));key_press(H1_KEY_Z);CHECK(view.aim_item==0&&!view.ward_mode);view.target_x=2100;view.target_y=2048;key_press(H1_KEY_Z);CHECK(!view.ward_mode);view.ward_mode=1;before=dota_game;confirm_item(2100,2048,-1);CHECK(view.aim_item==0&&!memcmp(&before,&dota_game,sizeof(before)));CHECK(!dota_use_item_at(&dota_game,0,2100,2048,DOTA_WARD_TARGET_SENTRY));CHECK(!memcmp(&before,&dota_game,sizeof(before)));
    view.ward_mode=0;key_press(H1_KEY_ENTER);CHECK(view.aim_item<0&&ward_at(ITEM_UNIT_OBSERVER,0)>=0&&dota_game.units[0].inventory[0]<0);++cases;
}
static void digit_mask(unsigned char out[35],int x,int y,uint16_t color){int xx,yy;for(yy=0;yy<7;++yy)for(xx=0;xx<5;++xx)out[yy*5+xx]=(unsigned char)(dota_pixels[(y+yy)*480+x+xx]==color);}
static void ward_hud_count_tests(void){
    unsigned char one[35],two[35],left[35],right[35];int i,count=0,x=DOTA_HUD_ITEMS_X,y=DOTA_HUD_ITEMS_Y;
    setup(DOTA_HERO_LINA);CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_OBSERVER,201));dota_render(&dota_game,&view);digit_mask(one,x+16,y+13,0xe75cu);for(i=0;i<35;++i)count+=one[i];CHECK(count>0);
    CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_OBSERVER,201));CHECK(dota_inventory_charges(&dota_game,0,0)==2);dota_render(&dota_game,&view);digit_mask(two,x+16,y+13,0xe75cu);CHECK(memcmp(one,two,35));
    setup(DOTA_HERO_LINA);CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_OBSERVER,201));CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_SENTRY,201));CHECK(dota_game.items.charges[0][0]==257&&dota_inventory_charges(&dota_game,0,0)==2);shot("host-combined-ward-one-one-hud");digit_mask(left,x+4,y+13,0xe5edu);digit_mask(right,x+16,y+13,0x753du);CHECK(!memcmp(left,one,35)&&!memcmp(right,one,35)&&memcmp(left,two,35));
    CHECK(dota_inventory_move(&dota_game,0,0,6));CHECK(dota_game.items.backpack[0][0].charges==257&&dota_inventory_charges(&dota_game,0,6)==2);shot("host-backpack-ward-one-one-hud");y+=DOTA_HUD_ITEM_H*2;digit_mask(left,x+4,y+13,0x8d33u);digit_mask(right,x+16,y+13,0x8d33u);CHECK(!memcmp(left,one,35)&&!memcmp(right,one,35)&&memcmp(left,two,35));++cases;
}
static void forbidden_ward_ui_tests(void){
    int i,mode;DotaGame before;
    for(i=0;i<4;++i){int x=i<2?dota_fountains[i].x:dota_roshan_pit_positions[i-2][0],y=i<2?dota_fountains[i].y:dota_roshan_pit_positions[i-2][1];
        setup(DOTA_HERO_LINA);dota_game.units[0].x=x;dota_game.units[0].y=y;view.camera_x=x;view.camera_y=y;
        CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_OBSERVER,201)&&dota_items_receive(&dota_game,0,DOTA_ITEM_WARD_SENTRY,201));key_press(H1_KEY_Z);CHECK(view.aim_item==0&&!dota_item_world_ward_placeable(&dota_game,0,x,y));
        for(mode=0;mode<2;++mode){int sx,sy;BSCamera camera={x,y,bs_ground_height(x,y)};view.ward_mode=mode;before=dota_game;confirm_item(x,y,-1);CHECK(view.aim_item==0&&view.ward_mode==mode&&view.target_x==x&&view.target_y==y&&!memcmp(&before,&dota_game,sizeof(before)));CHECK(dota_game.items.charges[0][0]==257&&!dota_items_cooldown(&dota_game,0,0));dota_render(&dota_game,&view);CHECK(bs_project(&camera,x,y,bs_ground_height(x,y)+2,&sx,&sy,0));CHECK(dota_pixels[(sy-8)*480+sx]==0xe34bu);}
        if(i==0){shot("host-fountain-ward-forbidden");}
        if(i==2){shot("host-roshan-ward-forbidden");}++cases;
    }
}
static void time_and_slark_ui_tests(void){
    int i,x=dota_hud_skill_x(3,4)+6+6;
    setup(DOTA_HERO_LINA);CHECK(dota_match_seconds(&dota_game)==-60&&!dota_match_is_night(&dota_game));shot("host-pregame-minus-60");dota_game.tick=1200;dota_sight_invalidate();CHECK(dota_match_seconds(&dota_game)==0);shot("host-horn-zero");dota_game.tick=7200;dota_sight_invalidate();CHECK(dota_match_is_night(&dota_game));shot("host-night");++cases;
    setup(DOTA_HERO_SLARK);dota_game.tick=100;dota_game.extra.hero[0].unseen_since=1;CHECK(dota_extra_shadow_dance_passive_active(&dota_game,0));shot("host-slark-passive-on");CHECK(dota_pixels[225*480+x]==0x6630u);
    enemy(5,2100,2048);CHECK(!dota_extra_shadow_dance_passive_active(&dota_game,0));shot("host-slark-passive-off");CHECK(dota_pixels[225*480+x]==0x8d33u);
    dota_game.extra.hero[0].shadow_until=200;CHECK(dota_extra_shadow_dance_passive_active(&dota_game,0));CHECK(!dota_unit_visible(&dota_game,5,0));
    {unsigned effects;int bounds[4];dota_game.player=5;dota_game.selected_hero=DOTA_HERO_NECROPHOS;view.camera_x=view.camera_y=2048;view.target_x=view.target_y=-1;dota_sight_invalidate();shot("host-enemy-sees-slark-cloud-not-body");CHECK(!bs_unit_screen_bounds(0,bounds,0));effects=bs_stats()->effects_drawn;CHECK(effects>0);dota_game.units[0].active=0;dota_sight_invalidate();dota_render(&dota_game,&view);CHECK(bs_stats()->effects_drawn<effects);}
    ++cases;
    for(i=0;i<65536;++i){unsigned c=(unsigned)i,dim=vision_assets_fog_color((uint16_t)c,3);CHECK((dim>>11)<=(c>>11)&&((dim>>5)&63)<=((c>>5)&63)&&(dim&31)<=(c&31));CHECK(vision_assets_fog_color((uint16_t)c,8)==c);}++cases;
}
static void pause_help_tests(void){
    DotaGame frozen;DotaInput input;int i;
    static const int destination[6]={DOTA_SCREEN_PLAY,DOTA_SCREEN_SHOP,DOTA_SCREEN_SKILLS,DOTA_SCREEN_HELP,DOTA_SCREEN_MENU,DOTA_SCREEN_PAUSE};
    static const unsigned shortcut[6]={H1_KEY_P,H1_KEY_M,H1_KEY_U,H1_KEY_H,H1_KEY_C,H1_KEY_X};
    memset(&input,0,sizeof(input));setup(DOTA_HERO_NECROPHOS);
    key_press(H1_KEY_BACK);CHECK(view.screen==DOTA_SCREEN_PAUSE&&dota_game.paused&&!closing&&view.pause_selected==0);frozen=dota_game;
    shot("host-pause-buttons");ui_tap(165,100);CHECK(view.screen==DOTA_SCREEN_PAUSE&&view.pause_selected==0);
    key_press(H1_KEY_DOWN);CHECK(view.pause_selected==3);key_press(H1_KEY_ENTER);
    CHECK(view.screen==DOTA_SCREEN_HELP&&view.help_return_screen==DOTA_SCREEN_PAUSE&&!view.help_page&&dota_game.paused);
    shot("host-help-controls");key_press(H1_KEY_RIGHT);CHECK(view.help_page==1);shot("host-help-copyright");
    ui_tap(240,170);CHECK(view.screen==DOTA_SCREEN_HELP&&view.help_page==1);
    for(i=0;i<20;++i)dota_tick(&dota_game,&input);
    CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen))&&!closing);
    ui_tap(50,46);CHECK(!view.help_page);ui_tap(450,12);CHECK(view.screen==DOTA_SCREEN_PAUSE&&dota_game.paused);
    CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));++cases;
    key_press(H1_KEY_H);key_press(H1_KEY_D);CHECK(view.screen==DOTA_SCREEN_HELP&&view.help_page==1);
    key_press(H1_KEY_A);CHECK(!view.help_page);key_press(H1_KEY_BACK);CHECK(view.screen==DOTA_SCREEN_PAUSE&&dota_game.paused&&!closing);
    key_press(H1_KEY_ESCAPE);CHECK(view.screen==DOTA_SCREEN_PLAY&&!dota_game.paused&&!closing);++cases;
    key_press(H1_KEY_P);key_press(H1_KEY_C);CHECK(view.screen==DOTA_SCREEN_MENU&&dota_game.paused);
    frozen=dota_game;ui_tap(470,250);CHECK(view.screen==DOTA_SCREEN_MENU);key_press(H1_KEY_H);CHECK(view.screen==DOTA_SCREEN_HELP&&view.help_return_screen==DOTA_SCREEN_MENU);
    key_press(H1_KEY_ENTER);CHECK(view.screen==DOTA_SCREEN_MENU&&dota_game.paused&&!memcmp(&frozen,&dota_game,sizeof(frozen)));
    ui_tap(410,250);CHECK(view.screen==DOTA_SCREEN_HELP&&view.help_return_screen==DOTA_SCREEN_MENU);
    ui_tap(230,45);CHECK(view.help_page==1);key_press(H1_KEY_BACK);CHECK(view.screen==DOTA_SCREEN_MENU&&dota_game.paused&&!closing);
    CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));++cases;
    for(i=0;i<6;++i){
        setup(DOTA_HERO_NECROPHOS);key_press(H1_KEY_BACK);
        ui_tap(20+(i%3)*150+70,76+(i/3)*70+28);
        CHECK(view.screen==destination[i]&&view.pause_selected==i&&closing==(i==5));
        CHECK(dota_game.paused==(i!=0));
        if(i==1||i==2||i==3){key_press(H1_KEY_BACK);CHECK(view.screen==DOTA_SCREEN_PAUSE&&dota_game.paused&&!closing);}
        setup(DOTA_HERO_NECROPHOS);key_press(H1_KEY_BACK);key_press(shortcut[i]);
        CHECK(view.screen==destination[i]&&closing==(i==5)&&dota_game.paused==(i!=0));++cases;
    }
    setup(DOTA_HERO_NECROPHOS);key_press(H1_KEY_BACK);key_press(H1_KEY_RIGHT);CHECK(view.pause_selected==1);
    key_press(H1_KEY_DOWN);CHECK(view.pause_selected==4);key_press(H1_KEY_LEFT);CHECK(view.pause_selected==3);
    key_press(H1_KEY_UP);CHECK(view.pause_selected==0);key_press(H1_KEY_CONFIRM);CHECK(view.screen==DOTA_SCREEN_PLAY&&!dota_game.paused&&!closing);++cases;
}
static void required_resource_entry_test(const char *pack,const char *vision){
    unsigned i,mode;entry_pack=pack;
    for(mode=0;mode<3;++mode){entry_mode=1;entry_event_sent=audio_open_calls=0;entry_force_heap_failure=mode==1;entry_vision_path=mode?vision:0;closing=opened=timer_started=0;memset(pressed,0,sizeof(pressed));memset(held,0,sizeof(held));
        CHECK(h1_app_main()==1);CHECK(h1_dota_debug.error==(mode==2?36u:35u)&&closing&&!opened&&!timer_started);CHECK(!audio_open_calls&&h1_dota_cache.released&&!h1_dota_cache.total_heap_bytes&&!vision_assets_stats()->ready&&vision_assets_stats()->released);CHECK(view.screen==DOTA_SCREEN_MENU&&dota_game.paused&&!dota_game.tick);if(mode<2)CHECK(vision_assets_stats()->last_reason==(mode?4u:1u));else CHECK(tree_visual_error()&&!tree_visual_ready()&&!tree_visual_dynamic_bytes());for(i=0;i<480u*272u;++i)CHECK(modeled_lcd[i]==color32(dota_pixels[i]));write_frame(mode==2?"host-required-tree-missing-error":mode?"host-required-vision-memory-error":"host-required-vision-missing-error");entry_mode=0;CHECK(!live_allocations);++cases;
    }
}
static void runtime_resource_back_test(void){
    int pages[2]={DOTA_SCREEN_PLAY,DOTA_SCREEN_SHOP},i;
    for(i=0;i<2;++i){setup(DOTA_HERO_AXE);screen(pages[i]);entry_mode=1;entry_event_sent=0;closing=0;
        resource_error_wait(16);CHECK(closing&&view.screen==pages[i]);entry_mode=0;closing=0;++cases;}
}
int main(int argc,char **argv){
    CHECK(argc==7);output_dir=argv[6];CHECK(baked_io_open(argv[1])&&bs_initialize()&&item_assets_open());CHECK(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);CHECK(item_assets_reserve_icon_cache(64)==64);shop_catalog_bind(&dota_game);shop_catalog_reset();
    loader_tests(argv[2],argv[3],argv[4],argv[5]);flat_workspace=malloc(dota_sight_workspace_bytes());CHECK(flat_workspace);fog_and_leak_tests();ward_ui_tests();combined_ward_tests();ward_hud_count_tests();forbidden_ward_ui_tests();time_and_slark_ui_tests();pause_help_tests();
    runtime_resource_back_test();dota_sight_detach();free(flat_workspace);vision_assets_close();item_assets_close();bs_close();CHECK(!live_allocations&&!bs_dynamic_storage_bytes()&&!baked_io_dynamic_storage_bytes());required_resource_entry_test(argv[1],argv[2]);
    printf("RESULT {\"passed\":true,\"checks\":%u,\"cases\":%u,\"workspace_bytes\":%u,\"actual_native_handlers\":true,\"actual_core\":true,\"actual_mips32\":false,\"physical_h1_tested\":false,\"host_fog_fixture_uses_flat_ground\":true,\"source_sidecar_loader_tested_separately\":true}\n",checks,cases,dota_sight_workspace_bytes());return 0;
}
