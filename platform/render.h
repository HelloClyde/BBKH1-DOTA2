#ifndef H1_DOTA_RENDER_H
#define H1_DOTA_RENDER_H

#include <stdint.h>
#include "../game/game.h"
#include "hud_layout.h"
#include "status_effects.h"
#include "../game/match_history.h"

enum {
    DOTA_SCREEN_MENU=0, DOTA_SCREEN_PLAY=1, DOTA_SCREEN_PAUSE=2,
    DOTA_SCREEN_SHOP=3, DOTA_SCREEN_RESULT=4, DOTA_SCREEN_HELP=5,
    DOTA_SCREEN_SKILLS=6, DOTA_SCREEN_HISTORY=7
};
enum { DOTA_STATUS_INFO_EVENT=14 };

typedef struct DotaView {
    int screen,hero,shop_item,camera_x,camera_y,target_x,target_y,aim_skill,follow;
    unsigned frames;
    int scoreboard; /* Append-only; opening the table never pauses rules. */
    int shop_category,shop_details,shop_scroll,aim_item;
    int skills_return_screen,skills_selected,skills_details,skills_scroll;
    int inventory_source,inventory_hover,inventory_mode;
    int drag_x,drag_y;
    int ward_mode; /* Append-only: 0 observer, 1 sentry while aiming a ward. */
    int help_return_screen,help_page,pause_selected;
    const DotaMatchHistory *history;
    const DotaMatchRecord *result_record;
    int history_page,history_selected,history_mode;
    int result_selected,result_detail,save_error,history_return_screen;
} DotaView;

extern uint16_t dota_pixels[480*272];
void dota_render(const DotaGame *game,const DotaView *view);
void dota_render_asset_error(int code);
/* Shared pause-button hit areas match the renderer's three-column grid. */
int dota_pause_button_at(int x,int y);
/* Pick actual 3D terrain with the battlefield's oblique camera and height grid. */
int dota_screen_to_world(const DotaView *view,int sx,int sy,int *wx,int *wy);
/* Hit visible enemy model bounds, resolving overlapping models by nearest depth.
 * units_only excludes structures for unit-targeted skills. Returns unit slot. */
int dota_hit_enemy(const DotaGame *game,const DotaView *view,int sx,int sy,int units_only);
enum { DOTA_HIT_ENEMY=0,DOTA_HIT_FRIENDLY=1,DOTA_HIT_ANY=2 };
/* Same visible model/depth picking, including friendly teleport anchors. */
int dota_hit_unit(const DotaGame *game,const DotaView *view,int sx,int sy,int relation,int units_only);
int dota_hit_owned_ward(const DotaGame *game,int sx,int sy);
int dota_hit_shop(int sx,int sy);
int dota_hit_objective(const DotaGame *game,const DotaView *view,int sx,int sy);
int dota_hit_rune(const DotaGame *game,const DotaView *view,int sx,int sy);
const DotaStatusEffect *dota_status_snapshot(int *count);
int dota_status_info_visible(void);

#endif
