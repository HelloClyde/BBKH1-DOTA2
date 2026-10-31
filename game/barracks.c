#include "barracks.h"
#include "../private/generated/dota_barracks_data.h"

static int living(const DotaUnit *u){return u->active && u->hp>0;}
int dota_barracks_ordinal(int team,int lane,int kind){
    return (unsigned)team<2 && (unsigned)lane<3 && (kind==DOTA_BARRACKS_MELEE || kind==DOTA_BARRACKS_RANGED)?(team*3+lane)*2+kind-1:-1;
}
int dota_barracks_index(int team,int lane,int kind){
    if(dota_barracks_ordinal(team,lane,kind)<0)return -1;
    return kind==DOTA_BARRACKS_MELEE?dota_high_tower_index(team,lane):DOTA_BARRACKS_RANGED_FIRST+team*3+lane;
}
int dota_barracks_kind(const DotaUnit *u){
    int ordinal;
    if(!u || u->type!=DOTA_BUILDING)return 0;
    ordinal=dota_barracks_ordinal(u->team,u->lane,u->tier);
    return ordinal>=0 && u->hero_id==DOTA_FILLER_COUNT+ordinal?u->tier:0;
}
int dota_high_tower_alive(const DotaGame *g,int team,int slot){
    int index=dota_high_tower_index(team,slot);
    return g && index>=0 && g->units[index].type==DOTA_TOWER && living(&g->units[index]);
}
unsigned dota_barracks_alive_mask(const DotaGame *g){
    unsigned mask=0;int team,lane,kind;
    if(!g)return 0;
    for(team=0;team<2;++team)for(lane=0;lane<3;++lane)for(kind=1;kind<=2;++kind){
        const DotaUnit *u=&g->units[dota_barracks_index(team,lane,kind)];
        /* The original map already shows the protected melee barracks.
         * It receives its combat slot only after the matching T3 dies. */
        if((kind==DOTA_BARRACKS_MELEE && u->type==DOTA_TOWER) ||
           (dota_barracks_kind(u)==kind && living(u)))mask|=1u<<dota_barracks_ordinal(team,lane,kind);
    }
    return mask;
}
void dota_barracks_create(DotaGame *g,int team,int lane,int kind){
    int ordinal=dota_barracks_ordinal(team,lane,kind),index,i,generation;unsigned n;unsigned char *p;DotaUnit *u;
    if(!g || ordinal<0)return;
    index=dota_barracks_index(team,lane,kind);u=&g->units[index];generation=u->generation+1;
    p=(unsigned char *)u;n=(unsigned)sizeof(*u);while(n--)*p++=0;
    u->active=1;u->type=DOTA_BUILDING;u->team=team;u->lane=lane;u->tier=kind;
    u->hero_id=DOTA_FILLER_COUNT+ordinal;u->generation=generation;
    u->x=dota_barracks_positions[ordinal][0];u->y=dota_barracks_positions[ordinal][1];
    u->hp=u->max_hp=kind==DOTA_BARRACKS_MELEE?2200:1300;
    u->armor=kind==DOTA_BARRACKS_MELEE?15:9;
    u->target=u->taunt_source=u->dot_source=-1;
    for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
}
void dota_barracks_commit_tower_deaths(DotaGame *g){
    int team,lane;
    for(team=0;team<2;++team)for(lane=0;lane<3;++lane){
        DotaUnit *u=&g->units[dota_high_tower_index(team,lane)];
        if(u->active && u->type==DOTA_TOWER && u->tier==2 && u->hp==0 && u->respawn_ticks==-1)
            dota_barracks_create(g,team,lane,DOTA_BARRACKS_MELEE);
    }
}
