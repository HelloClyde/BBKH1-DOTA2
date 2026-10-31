#ifndef H1_DOTA_HUD_LAYOUT_H
#define H1_DOTA_HUD_LAYOUT_H

/* Shared by the renderer, touch dispatch and acceptance controllers. The
 * baked camera remains480x192; the bottom HUD covers its last16 scanlines. */
#define DOTA_HUD_TOP 200
#define DOTA_HUD_MAP_X 0
#define DOTA_HUD_MAP_Y 200
#define DOTA_HUD_MAP_W 72
#define DOTA_HUD_MAP_H 72
#define DOTA_HUD_ATTR_X 72
#define DOTA_HUD_ATTR_W 112
#define DOTA_HUD_SKILLS_X 184
#define DOTA_HUD_SKILLS_Y 200
#define DOTA_HUD_SKILLS_W 136
#define DOTA_HUD_SKILLS_H 40
#define DOTA_HUD_SKILL_W 34
#define DOTA_HUD_SKILL_KEY_X_OFFSET 3
#define DOTA_HUD_SKILL_KEY_Y 203
#define DOTA_HUD_SKILL_LEVEL_Y 230
#define DOTA_HUD_SKILL_READY_X_OFFSET 27
#define DOTA_HUD_SKILL_READY_Y 221
#define DOTA_HUD_SKILL_READY_W 6
#define DOTA_HUD_SKILL_READY_H 3
#define DOTA_HUD_HEALTH_Y 242
#define DOTA_HUD_MANA_Y 258
#define DOTA_HUD_RESOURCE_H 14
#define DOTA_HUD_ITEMS_X 320
#define DOTA_HUD_ITEMS_Y 200
#define DOTA_HUD_ITEMS_W 84
#define DOTA_HUD_ITEMS_H 72
#define DOTA_HUD_ITEM_W 28
#define DOTA_HUD_ITEM_H 24
#define DOTA_HUD_BACKPACK_Y 248
#define DOTA_HUD_ITEM_LONG_PRESS_MS 450u
#define DOTA_HUD_LEVEL_X 100
#define DOTA_HUD_LEVEL_Y 216
#define DOTA_HUD_LEVEL_W 80
#define DOTA_HUD_LEVEL_H 14
#define DOTA_HUD_PROGRESS_X 160
#define DOTA_HUD_PROGRESS_Y 201
#define DOTA_HUD_PROGRESS_W 22
#define DOTA_HUD_PROGRESS_H 15
#define DOTA_HUD_GOLD_X 404
#define DOTA_HUD_GOLD_W 76
#define DOTA_HUD_SHOP_X 408
#define DOTA_HUD_SHOP_Y 240
#define DOTA_HUD_SHOP_W 68
#define DOTA_HUD_SHOP_H 30
#define DOTA_HUD_KDA_X 0
#define DOTA_HUD_KDA_Y 0
#define DOTA_HUD_KDA_W 128
#define DOTA_HUD_KDA_H 24
#define DOTA_HUD_PAUSE_X 440
#define DOTA_HUD_PAUSE_Y 0
#define DOTA_HUD_PAUSE_W 40
#define DOTA_HUD_PAUSE_H 24
#define DOTA_HUD_LOTUS_X 344
#define DOTA_HUD_LOTUS_Y 1
#define DOTA_HUD_LOTUS_W 92
#define DOTA_HUD_LOTUS_H 22
#define DOTA_HUD_SCORE_X 6
#define DOTA_HUD_SCORE_Y 28
#define DOTA_HUD_SCORE_W 468
#define DOTA_HUD_SCORE_H 238
#define DOTA_HUD_SCORE_CLOSE_X 440
#define DOTA_HUD_SCORE_CLOSE_Y 28
#define DOTA_HUD_SCORE_CLOSE_W 34
#define DOTA_HUD_SCORE_CLOSE_H 22
#define DOTA_HUD_SCORE_ROWS_Y 72
#define DOTA_HUD_SCORE_ROW_H 18
#define DOTA_HUD_SCORE_LEVEL_X 200
#define DOTA_HUD_SCORE_KILLS_X 270
#define DOTA_HUD_SCORE_DEATHS_X 334
#define DOTA_HUD_SCORE_ASSISTS_X 398
#define DOTA_HUD_STATUS_Y 178
#define DOTA_HUD_STATUS_H 22
#define DOTA_HUD_BUFF_X 76
#define DOTA_HUD_DEBUFF_X 252
#define DOTA_HUD_STATUS_LABEL_W 28
#define DOTA_HUD_STATUS_SLOT_W 24
#define DOTA_HUD_STATUS_ICON_W 22
#define DOTA_HUD_STATUS_SLOTS 6
#define DOTA_HUD_STATUS_INFO_X 112
#define DOTA_HUD_STATUS_INFO_Y 154
#define DOTA_HUD_STATUS_INFO_W 256
#define DOTA_HUD_STATUS_INFO_H 20
#define DOTA_HUD_SHARD_X 450
#define DOTA_HUD_SHARD_Y 178
#define DOTA_HUD_SHARD_W 28
#define DOTA_HUD_SHARD_H 22

static inline int dota_hud_contains(int x,int y,int left,int top,int width,int height){
    return x>=left && y>=top && x-left<width && y-top<height;
}
static inline int dota_hud_skill_at(int x,int y){
    return dota_hud_contains(x,y,DOTA_HUD_SKILLS_X,DOTA_HUD_SKILLS_Y,DOTA_HUD_SKILLS_W,DOTA_HUD_SKILLS_H)?(x-DOTA_HUD_SKILLS_X)/DOTA_HUD_SKILL_W:-1;
}
/* Use the same integer boundaries for the compact fifth skill and touch. */
static inline int dota_hud_skill_x(int skill,int count){return DOTA_HUD_SKILLS_X+skill*DOTA_HUD_SKILLS_W/count;}
static inline int dota_hud_skill_width(int skill,int count){return dota_hud_skill_x(skill+1,count)-dota_hud_skill_x(skill,count);}
static inline int dota_hud_skill_at_count(int x,int y,int count){
    int i;if(count!=5)return dota_hud_skill_at(x,y);
    if(!dota_hud_contains(x,y,DOTA_HUD_SKILLS_X,DOTA_HUD_SKILLS_Y,DOTA_HUD_SKILLS_W,DOTA_HUD_SKILLS_H))return -1;
    for(i=0;i<5;++i)if(x<dota_hud_skill_x(i+1,5))return i;
    return -1;
}
static inline int dota_hud_item_at(int x,int y){
    return dota_hud_contains(x,y,DOTA_HUD_ITEMS_X,DOTA_HUD_ITEMS_Y,DOTA_HUD_ITEMS_W,DOTA_HUD_ITEMS_H)?
        (y-DOTA_HUD_ITEMS_Y)/DOTA_HUD_ITEM_H*3+(x-DOTA_HUD_ITEMS_X)/DOTA_HUD_ITEM_W:-1;
}
static inline int dota_hud_skill_upgrade_at(int x,int y){
    return y>=228&&y<240?dota_hud_skill_at(x,y):-1;
}
static inline int dota_hud_skill_upgrade_at_count(int x,int y,int count){
    int i=y>=228&&y<240?dota_hud_skill_at_count(x,y,count):-1;return i>=0&&i<4?i:-1;
}
static inline int dota_hud_progress_at(int x,int y){
    if(dota_hud_contains(x,y,DOTA_HUD_PROGRESS_X,DOTA_HUD_PROGRESS_Y,DOTA_HUD_PROGRESS_W,DOTA_HUD_PROGRESS_H))return 1;
    return dota_hud_contains(x,y,DOTA_HUD_LEVEL_X,DOTA_HUD_LEVEL_Y,DOTA_HUD_LEVEL_W,DOTA_HUD_LEVEL_H);
}
/* Skill-page selectable cells0..3 abilities,4..11 talents,12 attributes,
 *13 innate information. Exactly the same geometry is used for hit testing. */
static inline int dota_hud_progression_cell(int x,int y){
    int i;
    if(y>=30&&y<77){for(i=0;i<4;++i)if(x>=8+i*94&&x<96+i*94)return i;if(x>=384&&x<472)return 13;}
    if(y>=86&&y<214){int row=(y-86)/32;if(y<86+row*32+30){if(x>=8&&x<226)return 4+row*2;if(x>=254&&x<472)return 5+row*2;}}
    if(dota_hud_contains(x,y,8,219,160,22))return 12;
    return -1;
}
#endif
