/* Deterministic completed-match fixtures, actual renderer and native UI.
 * Saves are redirected into the supplied local test output directory. */
#define main old_vision_fixture_main
#include "vision_ui.c"
#undef main
#include "resource_pack.h"
/* The shared host fixture remaps this declaration around native.c. */
int vision_assets_open_default(void);
static void *local_open(const char *name,const char *mode){
    char path[2048];const char *leaf=0;
    if(!strcmp(name,"B:\\DOTA2\\MATCH0.H1S"))leaf="B-MATCH0.H1S";
    else if(!strcmp(name,"B:\\DOTA2\\MATCH1.H1S"))leaf="B-MATCH1.H1S";
    else if(!strcmp(name,"A:\\DOTA2.HS0"))leaf="A-DOTA2.HS0";
    else if(!strcmp(name,"A:\\DOTA2.HS1"))leaf="A-DOTA2.HS1";
    CHECK(leaf);snprintf(path,sizeof(path),"%s/%s",output_dir,leaf);return fopen(path,mode);
}
static int local_close(void *f){return fclose(f);}
static unsigned local_read(void *f,void *p,unsigned n){return (unsigned)fread(p,1,n,f);}
static unsigned local_write(void *f,const void *p,unsigned n){return (unsigned)fwrite(p,1,n,f);}
static int local_seek(void *f,int n,int w){return fseek(f,n,w)?-1:(int)ftell(f);}
int main(int argc,char **argv){
    DotaMatchStorageHost fs={local_open,local_close,local_read,local_write,local_seek,0,0};
    static const int kills[10]={13,9,7,5,2,8,5,3,2,1},deaths[10]={3,4,5,3,4,8,9,7,6,6};
    static const int roster[10]={0,1,2,3,5,4,6,7,8,9};
    DotaMatchRecord record;int i,j;unsigned allocation_count;
    CHECK(argc==3);output_dir=argv[2];
    CHECK(resource_mount(argv[1]));CHECK(baked_io_open_resource(0));CHECK(bs_initialize()&&item_assets_open());
    CHECK(item_assets_reserve_icon_cache(64)==64);shop_catalog_bind(&dota_game);shop_catalog_reset();
    match_storage_host_services(&fs);CHECK(match_storage_open());
    dota_init(&dota_game,DOTA_HERO_AXE,1101);dota_game.match_stats.match_id=match_storage_begin_match();
    for(i=0;i<10;i++){
        DotaUnit *u=&dota_game.units[i];u->hero_id=roster[i];u->level=15+i%5;u->kills=kills[i];u->deaths=deaths[i];
        u->gold=1000+i*170;dota_game.assists[i]=i==4?30:10+i%4;
        dota_game.match_stats.last_hits[i]=80+i*19;dota_game.match_stats.denies[i]=2+i;
        dota_game.match_stats.hero_damage[i]=26000-i*2000;
        dota_game.match_stats.building_damage[i]=i==0?4500:900;
        dota_game.match_stats.ally_healing[i]=i==4?80000:0;
        CHECK(dota_items_receive(&dota_game,i,DOTA_ITEM_TPSCROLL,201));
    }
    CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_QUELLING_BLADE,201));
    dota_game.kills[0]=36;dota_game.kills[1]=19;dota_game.tick=25200;dota_game.state=DOTA_WON;
    CHECK(dota_match_capture(&dota_game,&record));CHECK(record.mvp_hero==4);
    match_completed=0;complete_match();CHECK(view.screen==DOTA_SCREEN_RESULT&&view.result_record&&view.history->count==1&&!view.save_error);
    shot("result-ten-heroes");ui_tap(70,50+4*16+2);CHECK(view.result_detail&&view.result_selected==4);shot("result-mvp-details");
    key_press(H1_KEY_ESCAPE);CHECK(!view.result_detail);key_press(H1_KEY_R);CHECK(view.screen==DOTA_SCREEN_HISTORY&&!view.history_mode);shot("history-one-match");
    key_press(H1_KEY_ENTER);CHECK(view.history_mode==1);key_press(H1_KEY_ENTER);CHECK(view.history_mode==2);key_press(H1_KEY_ESCAPE);CHECK(view.history_mode==1);key_press(H1_KEY_ESCAPE);CHECK(view.history_mode==0);
    for(i=1;i<12;i++){
        dota_game.match_stats.match_id=match_storage_begin_match();dota_game.tick+=1200;
        dota_game.state=i%2?DOTA_LOST:DOTA_WON;CHECK(match_storage_complete(&dota_game)==1);
    }
    view.history=match_storage_history();view.history_page=view.history_selected=view.history_mode=0;
    shot("history-twelve-matches");key_press(H1_KEY_D);CHECK(view.history_page==1&&view.history_selected==8);shot("history-second-page");
    ui_tap(90,52);CHECK(view.history_mode==1&&view.history_selected==8);shot("history-selected-match");
    allocation_count=live_allocations;match_storage_close();CHECK(live_allocations+1==allocation_count);CHECK(match_storage_open());CHECK(match_storage_history()->count==12);
    for(i=0;i<12;i++){const DotaMatchRecord*r=dota_match_history_get(match_storage_history(),(unsigned)i);CHECK(r&&r->match_id==(unsigned)(12-i));}
    view.screen=DOTA_SCREEN_MENU;view.history=match_storage_history();shot("menu-history-button");key_press(H1_KEY_R);CHECK(view.screen==DOTA_SCREEN_HISTORY&&!view.history_mode);
    for(j=0;j<3;j++){key_press(H1_KEY_ENTER);key_press(H1_KEY_ESCAPE);}CHECK(match_storage_history()->count==12);
    /* TP uses legacy active slot7 while Unit.inventory has exactly six cells.
     * Render the actual aimed screen as well as testing its command handler. */
    dota_init(&dota_game,DOTA_HERO_AXE,1101);CHECK(dota_items_receive(&dota_game,0,DOTA_ITEM_TPSCROLL,201));
    CHECK(vision_assets_open_default());CHECK(tree_visual_open_default());CHECK(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);
    memset(&view,0,sizeof(view));view.screen=DOTA_SCREEN_PLAY;view.aim_skill=-1;view.aim_item=DOTA_ITEMS_TP_SLOT;
    view.camera_x=view.target_x=dota_game.units[0].x;view.camera_y=view.target_y=dota_game.units[0].y;
    CHECK(dota_items_cast_item(&dota_game,0,view.aim_item)==DOTA_ITEM_TPSCROLL);CHECK(dota_inventory_item(&dota_game,0,DOTA_INVENTORY_TP_SLOT)==DOTA_ITEM_TPSCROLL);
    for(j=0;j<6;j++)CHECK(dota_game.units[0].inventory[j]!=DOTA_ITEM_TPSCROLL);
    shot("tp-independent-aim");CHECK(!resource_stats()->last_error);CHECK(!tree_visual_error());
    match_storage_close();tree_visual_close();vision_assets_close();item_assets_close();bs_close();CHECK(resource_unmount());CHECK(!live_allocations);
    printf("RESULT {\"passed\":true,\"checks\":%u,\"history_records\":12,\"fixture_only\":true,\"physical_h1_tested\":false}\n",checks);return 0;
}
