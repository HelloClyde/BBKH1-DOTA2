#ifndef DOTA_EXTRA_ABILITY_H
#define DOTA_EXTRA_ABILITY_H
#include "extra_heroes.h"
#include "roster_heroes.h"
static inline int extra_available(const DotaGame *g,int h){int id;if(!g||(unsigned)h>=DOTA_HERO_COUNT||g->units[h].type!=DOTA_HERO)return 0;id=g->units[h].hero_id;return id==DOTA_HERO_PUDGE?dota_roster_swallowed(g,h)>=0:id==DOTA_HERO_CRYSTAL_MAIDEN||dota_extra_is_hero(&g->units[h])?dota_hero_has_shard(g,h):(id==7||id==9)?dota_hero_has_shard(g,h):id==8?dota_hero_has_scepter(g,h):0;}
static inline int extra_range(const DotaGame *g,int h){return dota_roster_is_hero(&g->units[h])?dota_roster_extra_range(g,h):dota_extra_shard_range(g,h);}
static inline int extra_radius(const DotaGame *g,int h){return dota_roster_is_hero(&g->units[h])?(g->units[h].hero_id==7?375:0):dota_extra_shard_radius(g,h);}
static inline int extra_cooldown(const DotaGame *g,int h){int cd=g->roster.hero[h].extra_cooldown-g->tick;return dota_roster_is_hero(&g->units[h])?(cd>0?cd:0):dota_extra_shard_cooldown(g,h);}
static inline int extra_cast(DotaGame *g,int h,int x,int y){return dota_roster_is_hero(&g->units[h])?dota_roster_cast_extra(g,h,x,y):dota_extra_cast_shard(g,h,x,y);}
static inline const char *extra_name(int id){return id==3?"冰晶分身":id==4?"深海护罩":id==5?"死亡搜寻":id==6?"喷吐":id==7?"震荡手雷":id==8?"风暴之拳模式":"锚定";}
#endif
