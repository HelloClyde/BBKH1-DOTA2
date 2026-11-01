/* Initial boundary fixtures plus real public gameplay/tick APIs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "match_time.h"
#include "turbo.h"
#include "extra_heroes.h"
#include "roster_heroes.h"
#include "items.h"
#include "progression.h"
#include "objectives.h"
#include "runes.h"
#include "bosses.h"
#include "item_assets.h"
#include "baked_io.h"
#include "roshan_data.h"
static DotaGame g,paused;
static int checks,cases;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d %s elapsed=%d match=%d\n",__LINE__,#x,g.tick,dota_match_ticks(&g));exit(1);}}while(0)
static void setup(void){int i;dota_init(&g,DOTA_HERO_AXE,20261101);for(i=1;i<DOTA_MAX_UNITS;i++)g.units[i].active=0;g.units[0].target=-1;g.units[0].attack_wait=100000;dota_rules_set_position(&g,0,1800,2500);}
static void step(int n){DotaInput input={0,0,0,0,0};while(n-->0)dota_tick(&g,&input);}
static int count(int type){int i,n=0;for(i=0;i<DOTA_MAX_UNITS;i++)if(g.units[i].active&&g.units[i].type==type)n++;return n;}
static void pass(const char *name){++cases;printf("CASE {\"name\":\"%s\",\"passed\":true}\n",name);}
static void clock_contract(void){
    setup();CHECK(dota_match_pregame_ticks()==1200&&g.tick==0&&dota_match_ticks(&g)==-1200&&dota_match_seconds(&g)==-60);
    g.tick=1199;CHECK(dota_match_seconds(&g)==-1&&!dota_match_started(&g)&&!dota_match_is_night(&g));
    g.tick=1200;CHECK(dota_match_seconds(&g)==0&&dota_match_started(&g));
    g.tick=7199;CHECK(dota_match_seconds(&g)==299&&!dota_match_is_night(&g));
    g.tick=7200;CHECK(dota_match_seconds(&g)==300&&dota_match_is_night(&g));
    g.tick=13199;CHECK(dota_match_is_night(&g));g.tick=13200;CHECK(!dota_match_is_night(&g));
    CHECK(dota_match_elapsed_tick(0)==1200&&dota_match_elapsed_tick(1200)==2400&&dota_match_elapsed_tick(-2000)==0);
    pass("signed-clock-countdown-and-five-minute-day-night-boundaries");
}
static void pregame(void){
    int gold,mana,cooldown,x;DotaInput input={1,0,0,0,0};setup();gold=g.units[0].gold;
    CHECK(!dota_extra_invulnerable(&g,0)&&!dota_roster_invulnerable(&g,0));
    CHECK(dota_progression_learn(&g,0,0));mana=g.units[0].mana;CHECK(dota_cast(&g,0,1800,2500));
    CHECK(g.units[0].mana<mana);cooldown=g.units[0].cooldown[0];x=g.units[0].x;dota_tick(&g,&input);CHECK(g.units[0].x>x);
    step(19);CHECK(g.tick==20&&g.units[0].cooldown[0]==cooldown-20&&g.units[0].gold==gold);
    CHECK(!g.wave&&!count(DOTA_CREEP)&&!count(DOTA_NEUTRAL)&&!count(DOTA_ANCIENT)&&dota_roshan_index(&g)<0);
    CHECK(dota_rune_kind(&g,0)<0&&dota_rune_kind(&g,1)<0&&dota_rune_kind(&g,2)<0&&dota_rune_kind(&g,3)<0);
    dota_set_paused(&g,1);paused=g;step(5);CHECK(!memcmp(&paused,&g,sizeof(g)));dota_set_paused(&g,0);step(1);CHECK(g.tick==21);
    pass("playable-pregame-relative-cooldowns-no-gold-or-spawns-and-exact-pause");
}
static void horn(void){
    int i;setup();g.tick=dota_match_elapsed_tick(0)-2;step(1);CHECK(dota_match_ticks(&g)==-1&&!g.wave&&!count(DOTA_CREEP));
    CHECK(g.units[0].gold==DOTA_TURBO_STARTING_GOLD&&dota_roshan_index(&g)<0);step(1);
    CHECK(dota_match_ticks(&g)==0&&g.wave==1&&count(DOTA_CREEP)==18&&g.spawn_wait==600);
    for(i=0;i<4;i++)CHECK(dota_rune_kind(&g,i)==DOTA_RUNE_BOUNTY&&g.runes.spawn_tick[i]==0);
    CHECK(dota_roshan_index(&g)>=0&&g.units[DOTA_ROSHAN_INDEX].max_hp==6000);
    CHECK(!count(DOTA_NEUTRAL)&&!count(DOTA_ANCIENT)&&g.units[0].gold==DOTA_TURBO_STARTING_GOLD);
    pass("horn-zero-first-balanced-wave-four-bounty-and-roshan-zero-growth");
    setup();g.tick=dota_match_elapsed_tick(30*DOTA_HZ)-1;g.wave=1;dota_set_paused(&g,1);step(8);CHECK(g.wave==1);dota_set_paused(&g,0);step(1);CHECK(g.wave==2&&count(DOTA_CREEP)==18&&dota_match_seconds(&g)==30);
    pass("next-wave-thirty-seconds-and-pause-does-not-accrue-match-time");
}
static void gold(void){int initial;setup();g.tick=dota_match_elapsed_tick(0);initial=g.units[0].gold;step(45);CHECK(g.units[0].gold==initial+8);CHECK(dota_turbo_passive_gold_at_tick(0)==0);pass("turbo-passive-gold-starts-after-horn-with-original-fractional-rate");}
static void camps(void){
    setup();CHECK(g.neutral_respawn[0]==dota_match_elapsed_tick(60*DOTA_HZ));g.tick=dota_match_elapsed_tick(60*DOTA_HZ)-2;step(1);CHECK(!count(DOTA_NEUTRAL)&&!count(DOTA_ANCIENT));step(1);
    CHECK(count(DOTA_NEUTRAL)>0&&count(DOTA_ANCIENT)==6);CHECK(g.units[DOTA_ROSHAN_INDEX].max_hp==6000+DOTA_ROSHAN_HEALTH_PER_MINUTE);
    CHECK(g.neutral_group[0]>=0&&g.neutral_respawn[0]==0);pass("normal-and-ancient-camps-at-game-one-minute-not-elapsed-one-minute");
}
static void objectives(void){
    setup();g.tick=dota_match_elapsed_tick(90*DOTA_HZ)-2;step(1);CHECK(!g.objectives.lotus_count[0]&&!g.objectives.lotus_count[1]);step(1);CHECK(g.objectives.lotus_count[0]==1&&g.objectives.lotus_count[1]==1&&!g.objectives.wisdom_cycle[0]);
    setup();g.tick=dota_match_elapsed_tick(420*DOTA_HZ)-2;step(1);CHECK(!g.objectives.wisdom_cycle[0]);step(1);CHECK(g.objectives.wisdom_cycle[0]==1&&g.objectives.wisdom_cycle[1]==1);
    setup();CHECK(g.tormentor.spawn_tick==dota_match_elapsed_tick(DOTA_TORMENTOR_FIRST_SPAWN));g.tick=g.tormentor.spawn_tick-1;step(1);CHECK(g.units[DOTA_TORMENTOR_INDEX].active&&g.tormentor.growth_minutes==20&&g.units[DOTA_TORMENTOR_INDEX].max_hp==4001);
    pass("lotus-wisdom-and-tormentor-use-horn-relative-schedules");
}
static void runes(void){
    int spot,power=0;setup();g.tick=120*DOTA_HZ-1;step(1);CHECK(dota_match_seconds(&g)==60&&dota_rune_kind(&g,0)==DOTA_RUNE_BOUNTY);
    setup();g.tick=dota_match_elapsed_tick(120*DOTA_HZ)-2;step(1);CHECK(dota_rune_kind(&g,0)==DOTA_RUNE_BOUNTY);step(1);CHECK(dota_rune_kind(&g,0)==DOTA_RUNE_WATER&&dota_rune_kind(&g,1)==DOTA_RUNE_WATER);
    setup();g.tick=dota_match_elapsed_tick(360*DOTA_HZ)-1;step(1);for(spot=0;spot<2;spot++)if(dota_rune_kind(&g,spot)>=0){CHECK(dota_rune_kind(&g,spot)!=DOTA_RUNE_WATER&&dota_rune_kind(&g,spot)!=DOTA_RUNE_BOUNTY);power++;}CHECK(power==1);
    CHECK(g.runes.spawn_tick[2]==240*DOTA_HZ&&g.runes.spawn_tick[3]==240*DOTA_HZ);
    pass("water-power-and-jungle-bounty-source-match-time-and-birth-time");
}
int main(int argc,char **argv){CHECK(argc==2&&baked_io_open(argv[1])&&item_assets_open());CHECK(item_assets_reserve_rules_cache(65536)>0);clock_contract();pregame();horn();gold();camps();objectives();runes();item_assets_close();baked_io_close();printf("RESULT {\"passed\":true,\"cases\":%d,\"checks\":%d,\"game_bytes\":%u,\"unit_bytes\":%u,\"actual_core\":true,\"actual_mips32\":false,\"physical_h1_tested\":false}\n",cases,checks,(unsigned)sizeof(g),(unsigned)sizeof(DotaUnit));return 0;}
