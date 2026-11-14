/* Actual native input and renderer; only host timer/key/audio services modeled. */
#define main autocast_unused_vision_main
#include "vision_ui.c"
#undef main
#include "resource_pack.h"
int main(int argc,char**argv){
 int mana,i,green;DotaGame frozen;
 CHECK(argc==3);output_dir=argv[2];CHECK(resource_mount(argv[1]));CHECK(baked_io_open_resource(0));CHECK(bs_initialize()&&item_assets_open());
 flat_workspace=malloc(dota_sight_workspace_bytes());CHECK(flat_workspace);setup(DOTA_HERO_SLARK);CHECK(vision_assets_open(0));
 mana=dota_game.units[0].mana;mock_ms=1000;key_press(H1_KEY_E);CHECK(view.aim_skill==2&&!dota_skill_autocast_enabled(&dota_game,0,2));
 mock_ms=1300;key_press(H1_KEY_E);CHECK(view.aim_skill==-1&&dota_skill_autocast_enabled(&dota_game,0,2));CHECK(dota_game.units[0].mana==mana&&!dota_game.units[0].cooldown[2]);
 frozen=dota_game;shot("autocast-on");CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));
 /* The A crossbar must actually exist in the tiny-font output. */
 for(i=0;i<5;i++)CHECK(dota_pixels[206*480+279+i]==0x6630u);
 mock_ms=2000;ui_tap(269,216);CHECK(view.aim_skill==2&&dota_skill_autocast_enabled(&dota_game,0,2));
 mock_ms=2300;ui_tap(269,216);CHECK(view.aim_skill==-1&&!dota_skill_autocast_enabled(&dota_game,0,2));CHECK(dota_game.units[0].mana==mana&&!dota_game.units[0].cooldown[2]);
 frozen=dota_game;shot("autocast-off");CHECK(!memcmp(&frozen,&dota_game,sizeof(frozen)));green=0;for(i=0;i<5;i++)green+=dota_pixels[206*480+279+i]==0x6630u;CHECK(green<5);
 mock_ms=3000;key_press(H1_KEY_E);mock_ms=3600;key_press(H1_KEY_E);CHECK(view.aim_skill==2&&!dota_skill_autocast_enabled(&dota_game,0,2));
 screen(DOTA_SCREEN_PAUSE);screen(DOTA_SCREEN_PLAY);mock_ms=4000;key_press(H1_KEY_E);held[H1_KEY_E]=1;mock_ms=4300;process_ui();held[H1_KEY_E]=0;CHECK(!dota_skill_autocast_enabled(&dota_game,0,2));
 CHECK(!resource_stats()->last_error&&!bs_last_error());vision_assets_close();dota_sight_detach();free(flat_workspace);item_assets_close();bs_close();CHECK(resource_unmount());CHECK(!live_allocations);
 printf("RESULT {\"passed\":true,\"checks\":%u,\"keyboard_and_touch\":true,\"A_marker_pixels_checked\":5,\"renderer_read_only\":true,\"physical_h1_tested\":false}\n",checks);return 0;
}
