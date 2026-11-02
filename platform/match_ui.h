#ifndef H1_DOTA_MATCH_UI_H
#define H1_DOTA_MATCH_UI_H
/* Include in render.c after its text/box/art/button/backdrop static helpers. */
#include "../game/match_history.h"
static void match_ui_u32(int x,int y,uint32_t value,uint16_t color){
 char b[11],r[10];unsigned n=0,i=0;do{r[n++]=(char)('0'+value%10u);value/=10u;}while(value);while(n)b[i++]=r[--n];b[i]=0;text(x,y,b,color);
}
static void match_ui_short(int x,int y,uint32_t value,uint16_t color){
 if(value<1000u)match_ui_u32(x,y,value,color);
 else if(value<1000000u){match_ui_u32(x,y,value/1000u,color);text(x+number_width((int)(value/1000u)),y,"k",color);}
 else{match_ui_u32(x,y,value/1000000u,color);text(x+number_width((int)(value/1000000u)),y,"m",color);}
}
static void match_ui_time(int x,int y,uint32_t seconds,uint16_t color){
 uint32_t mins=seconds/60u;int width;if(mins>9999u)mins=9999u;
 match_ui_u32(x,y,mins,color);width=number_width((int)mins);text(x+width,y,":",color);
 text(x+width+7,y,seconds%60u<10u?"0":"",color);match_ui_u32(x+width+(seconds%60u<10u?14:7),y,seconds%60u,color);
}
static void match_ui_save_error(int code){if(code){text(326,222,"SAVE ERROR S",RED);number(420,222,code,RED);}}
static void match_ui_item(int x,int y,int item,unsigned charges){
 frame(x-1,y-1,22,22,EDGE);if(item>=0)art_small(x,y,item_assets_item_art(item));
 if(charges>1){box(x+9,y+10,12,10,PANEL);tiny_number(x+10,y+11,(int)(charges>999?999:charges),GOLD);}
}
static void match_ui_board(const DotaMatchRecord*r,int selected){
 unsigned i;if(!r){center(0,100,480,"MATCH RECORD UNAVAILABLE",DIM);return;}
 text(8,7,r->winner_team==0?"RADIANT WIN":"DIRE WIN",r->winner_team==0?GREEN:RED);
 text(191,7,"TIME",DIM);match_ui_time(229,7,r->duration_seconds,INK);text(329,7,"MATCH #",DIM);match_ui_u32(381,7,r->match_id,GOLD);
 text(26,30,"HERO",DIM);text(124,30,"LV",DIM);text(155,30,"K",DIM);text(188,30,"D",DIM);text(221,30,"A",DIM);text(254,30,"LH / DN",DIM);text(346,30,"NW",DIM);text(433,30,"MVP",GOLD);
 for(i=0;i<10;i++){
  const DotaMatchHeroRecord*h=&r->heroes[i];int y=50+(int)i*16;uint16_t team=h->team==0?GREEN:RED;
  box(8,y-1,464,16,(int)i==selected?RGB(43,53,47):i&1u?RGB(20,35,34):PANEL);box(8,y,3,14,team);
  if(i==r->player)text(13,y,"*",GOLD);
  text_fit(26,y,91,h->hero_id<10?hero_names[h->hero_id]:"?",team);match_ui_short(124,y,h->level,INK);
  match_ui_short(155,y,h->kills,INK);match_ui_short(188,y,h->deaths,INK);match_ui_short(221,y,h->assists,INK);
  match_ui_short(254,y,h->last_hits,INK);text(282,y,"/",DIM);match_ui_short(294,y,h->denies,DIM);match_ui_short(346,y,h->net_worth,GOLD);
  if(i==r->mvp_hero)text(433,y,"MVP",GOLD);
 }
}
static void match_ui_details(const DotaMatchRecord*r,int selected){
 const DotaMatchHeroRecord*h;unsigned i;if(!r){center(0,100,480,"MATCH RECORD UNAVAILABLE",DIM);return;}
 selected=clampi(selected,0,9);h=&r->heroes[selected];text_fit(12,7,160,h->hero_id<10?hero_names[h->hero_id]:"?",h->team==0?GREEN:RED);
 if((unsigned)selected==r->mvp_hero){text(180,7,"MVP",GOLD);}text(244,7,"MATCH #",DIM);match_ui_u32(300,7,r->match_id,GOLD);
 art_small(12,34,hero_art((int)h->hero_id));text(43,36,"LV",DIM);match_ui_u32(65,36,h->level,INK);
 text(92,36,"K/D/A",DIM);match_ui_short(137,36,h->kills,INK);text(163,36,"/",DIM);match_ui_short(175,36,h->deaths,INK);text(201,36,"/",DIM);match_ui_short(213,36,h->assists,INK);
 text(12,62,"HERO DMG",DIM);match_ui_u32(100,62,h->hero_damage,INK);text(12,82,"BUILD DMG",DIM);match_ui_u32(100,82,h->building_damage,INK);
 text(12,102,"ALLY HEAL",DIM);match_ui_u32(100,102,h->ally_healing,GREEN);text(12,122,"NET WORTH",DIM);match_ui_u32(100,122,h->net_worth,GOLD);
 text(12,142,"MVP SCORE",DIM);match_ui_u32(100,142,h->mvp_score,GOLD);
 text(247,36,"FINAL EQUIPMENT",DIM);for(i=0;i<6;i++)match_ui_item(247+(int)i*36,58,h->items[i],h->charges[i]);
 text(247,87,"BAG",DIM);for(i=0;i<3;i++)match_ui_item(247+(int)i*36,105,h->items[6+i],h->charges[6+i]);
 text(367,87,"N",DIM);match_ui_item(367,105,h->items[9],h->charges[9]);text(413,87,"TP",DIM);match_ui_item(413,105,h->items[10],h->charges[10]);
 text(247,142,"NW: gold + owned items/cargo",DIM);
 text(12,170,"LOCAL MVP (not Valve): winning team first",GOLD);
 text(12,187,"K*300 + A*200 + heroDmg/20 + buildDmg/10",DIM);
 text(12,204,"+ allyHeal/15 + participation*500 - D*75",DIM);
}
static void match_result_panel(const DotaMatchRecord*r,int selected,int detail,int save_error){
 backdrop();if(detail)match_ui_details(r,selected);else match_ui_board(r,selected);
 text(12,222,detail?"ESC: scoreboard  S retry":"UP/DOWN hero  D details",DIM);match_ui_save_error(save_error);
 button(8,240,140,28,"AGAIN ENTER",1);button(164,240,140,28,"HISTORY R",0);button(328,240,140,28,"BACK ESC",0);
}
static void match_history_panel(const DotaMatchHistory*h,int page,int selected,int mode,int hero,int save_error){
 unsigned i,count=h?h->count:0,pages=(count+7u)/8u;backdrop();
 if(mode){
  const DotaMatchRecord*r=selected>=0?dota_match_history_get(h,(unsigned)selected):0;
  if(mode>=2)match_ui_details(r,hero);else match_ui_board(r,hero);
  text(12,222,mode>=2?"ESC: scoreboard  S retry":"A/D hero  ENTER details",DIM);match_ui_save_error(save_error);
  button(8,240,140,28,"HERO - A",0);button(164,240,140,28,"HERO + D",0);button(328,240,140,28,"BACK ESC",0);return;
 }
 page=clampi(page,0,pages?(int)pages-1:0);text(12,7,"MATCH HISTORY",GOLD);text(337,7,"PAGE",DIM);number(373,7,page+1,INK);text(390,7,"/",DIM);number(401,7,pages?(int)pages:1,INK);
 text(12,29,"MATCH #",DIM);text(104,29,"HERO",DIM);text(199,29,"RESULT",DIM);text(262,29,"TIME",DIM);text(348,29,"MVP",GOLD);
 if(!count)center(0,111,480,"NO COMPLETED MATCHES",DIM);
 for(i=0;i<8;i++){
  unsigned index=(unsigned)page*8u+i;const DotaMatchRecord*r=dota_match_history_get(h,index);int y=50+(int)i*22,won;
  if(!r)break;
  won=r->heroes[r->player].team==r->winner_team;
  box(8,y-2,464,21,(int)index==selected?RGB(43,53,47):i&1u?RGB(20,35,34):PANEL);
  match_ui_u32(12,y,r->match_id,INK);text_fit(104,y,88,hero_names[r->heroes[r->player].hero_id],INK);text(199,y,won?"WIN":"LOSS",won?GREEN:RED);match_ui_time(262,y,r->duration_seconds,INK);
  text_fit(348,y,118,hero_names[r->heroes[r->mvp_hero].hero_id],GOLD);
 }
 match_ui_save_error(save_error);button(8,240,140,28,"PREV A",0);button(164,240,140,28,"NEXT D",0);button(328,240,140,28,"BACK ESC",0);
}
#endif
