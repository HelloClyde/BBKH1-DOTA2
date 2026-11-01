/* Initial states only; all acceptance outcomes execute the final BDA. */
#include "game.h"
#include "match_time.h"
#include "extra_heroes.h"
#include "progression.h"
#include "baked_io.h"
#include "item_assets.h"
#include "../platform/render.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
static DotaGame g;static const char *outdir;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"fixture fail %d %s\n",__LINE__,#x);exit(2);}}while(0)
static void setup(int match_tick){int i;dota_init(&g,DOTA_HERO_AXE,20261101);for(i=1;i<DOTA_MAX_UNITS;i++)g.units[i].active=0;g.units[0].target=-1;g.units[0].attack_wait=100000;dota_rules_set_position(&g,0,1800,2500);g.tick=dota_match_elapsed_tick(match_tick);}
static void save(const char *name){char path[2048];FILE *file;snprintf(path,sizeof(path),"%s/%s",outdir,name);file=fopen(path,"wb");CHECK(file);CHECK(fwrite(&g,1,sizeof(g),file)==sizeof(g));CHECK(!fclose(file));}
int main(int argc,char **argv){
    CHECK(sizeof(DotaGame)==178044 && sizeof(DotaUnit)==240 && sizeof(DotaView)==112);
    CHECK(argc==3);outdir=argv[1];CHECK(baked_io_open(argv[2])&&item_assets_open());
    setup(-60*DOTA_HZ);CHECK(dota_progression_learn(&g,0,0));save("pregame.bin");
    setup(-1);save("horn.bin");setup(30*DOTA_HZ-1);g.wave=1;save("wave.bin");
    setup(60*DOTA_HZ-1);save("camps.bin");setup(90*DOTA_HZ-1);save("lotus.bin");
    setup(120*DOTA_HZ-1);save("water.bin");setup(300*DOTA_HZ-1);save("night.bin");
    printf("{\"game_size\":%u,\"unit_size\":%u,\"input_size\":%u,",(unsigned)sizeof(g),(unsigned)sizeof(DotaUnit),(unsigned)sizeof(DotaInput));
#define GAME(n) printf("\"" #n "\":%u,",(unsigned)offsetof(DotaGame,n))
    GAME(units);GAME(tick);GAME(paused);GAME(wave);GAME(spawn_wait);
    printf("\"lotus_count\":%u,\"rune_kind\":%u,\"roshan_index\":%u,\"unit_count\":%u,\"unit\":{",(unsigned)(offsetof(DotaGame,objectives)+offsetof(DotaObjectiveState,lotus_count)),(unsigned)(offsetof(DotaGame,runes)+offsetof(DotaRuneState,kind)),(unsigned)DOTA_ROSHAN_INDEX,(unsigned)DOTA_MAX_UNITS);
#define UNIT(n) printf("\"" #n "\":%u,",(unsigned)offsetof(DotaUnit,n))
    UNIT(active);UNIT(type);UNIT(x);UNIT(y);UNIT(hp);UNIT(max_hp);UNIT(mana);UNIT(gold);
    printf("\"cooldown\":%u},\"types\":{\"creep\":%d,\"neutral\":%d,\"ancient\":%d}}\n",(unsigned)offsetof(DotaUnit,cooldown),DOTA_CREEP,DOTA_NEUTRAL,DOTA_ANCIENT);
    item_assets_close();baked_io_close();return 0;
}
