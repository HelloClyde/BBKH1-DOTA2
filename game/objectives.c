#include "objectives.h"
#include "bosses.h"
#include "vision.h"
#include "items.h"
#include "items_params.h"
#include "runes.h"
#include "match_time.h"
#include "sight.h"

const DotaPoint dota_objective_positions[DOTA_OBJECTIVES]={
    {26,1856},{4090,2334},{161,996},{3924,3149},
    {4027,2429},{432,384},{2727,2212},{1177,2039},{2306,3113},
    {3680,3696},{2240,3960},{1880,266},{1666,1067},{96,1784}
};
const short dota_objective_heights[DOTA_OBJECTIVES]={10,9,25,25,5,38,64,64,64,34,32,32,64,4};

static int live_hero(const DotaUnit *u){return u->active && u->type==DOTA_HERO && u->hp>0 && (unsigned)u->team<2;}
static int near(int x,int y,int xx,int yy,int radius_source){
    int dx=x-xx,dy=y-yy;return (dx*dx+dy*dy)*16<=radius_source*radius_source;
}
static void event(DotaGame *g,int kind,int amount){g->last_event=kind;g->last_event_value=amount;g->last_event_ticks=50;}
static unsigned draw(DotaGame *g){g->seed=g->seed*1664525u+1013904223u;return g->seed;}
static int pick(DotaGame *g,unsigned mask){
    int i,count=0,choice;for(i=0;i<DOTA_HERO_COUNT;++i)if(mask&(1u<<i))++count;
    if(!count)return -1;
    choice=(int)(draw(g)%(unsigned)count);
    for(i=0;i<DOTA_HERO_COUNT;++i)if(mask&(1u<<i)){if(!choice)return i;--choice;}
    return -1;
}
static unsigned nearby(const DotaGame *g,int objective,int radius,int lotus_item){
    const DotaPoint *p=&dota_objective_positions[objective];unsigned mask=0;int i;
    for(i=0;i<DOTA_HERO_COUNT;++i){const DotaUnit *u=&g->units[i];
        if(live_hero(u) && near(u->x,u->y,p->x,p->y,radius) && dota_pit_same_region(u->x,u->y,p->x,p->y)
           && (!lotus_item || dota_items_can_receive(g,i,lotus_item)))mask|=1u<<i;
    }
    return mask;
}
static int only_team(const DotaGame *g,unsigned mask){
    int i,teams=0;for(i=0;i<DOTA_HERO_COUNT;++i)if(mask&(1u<<i))teams|=1<<g->units[i].team;
    return teams==1?0:teams==2?1:teams==3?-2:-1;
}
void dota_objectives_init(DotaGame *g){
    unsigned i;unsigned char *b=(unsigned char*)&g->objectives;
    for(i=0;i<sizeof(g->objectives);++i)b[i]=0;
    for(i=0;i<2;++i){g->objectives.wisdom_team[i]=-1;g->objectives.lotus_team[i]=-1;g->objectives.lotus_pickup_ticks[i]=30;}
    for(i=0;i<DOTA_WATCHERS;++i)g->objectives.watcher_owner[i]=-1;
    g->objectives.lotus_grade=1;
}
int dota_objective_nearest(const DotaGame *g,int x,int y,int radius){
    int i,best=-1,best_d=radius*radius;if(!g || radius<0)return -1;
    for(i=0;i<DOTA_OBJECTIVES;++i){int dx=x-dota_objective_positions[i].x,dy=y-dota_objective_positions[i].y,d=dx*dx+dy*dy;
        if(d<=best_d && dota_pit_same_region(g->units[g->player].x,g->units[g->player].y,dota_objective_positions[i].x,dota_objective_positions[i].y)){best=i;best_d=d;}
    }return best;
}
void dota_objective_cancel(DotaGame *g){if(g){g->objectives.order=0;g->objectives.channel_ticks=0;}}
int dota_objective_order(DotaGame *g,int id){
    DotaUnit *u;int w;if(!g || (unsigned)id>=DOTA_OBJECTIVES || g->paused || g->state!=DOTA_PLAYING)return 0;
    u=&g->units[g->player];if(live_hero(u))dota_items_cancel_teleport(g,g->player);
    if(!live_hero(u) || u->stun_ticks || u->taunt_ticks || u->channel_ticks || dota_omnislash_phase(g,g->player))return 0;
    if(!dota_pit_same_region(u->x,u->y,dota_objective_positions[id].x,dota_objective_positions[id].y))return 0;
    dota_objective_cancel(g);
    dota_rune_cancel(g);
    if(id<DOTA_OBJECTIVE_WATCHER_FIRST){dota_move_to(g,dota_objective_positions[id].x,dota_objective_positions[id].y);return 1;}
    w=id-DOTA_OBJECTIVE_WATCHER_FIRST;
    if(g->objectives.watcher_owner[w]==u->team || (g->objectives.watcher_owner[w]==-2 && g->objectives.watcher_until[w]>g->tick)){
        event(g,DOTA_OBJECTIVE_EVENT_UNAVAILABLE,0);return 0;
    }
    u->target=-1;u->move_order=0;g->player_path_count=g->player_path_index=0;
    g->objectives.order=id+1;g->objectives.order_generation=u->generation;return 1;
}
int dota_objective_player(DotaGame *g,const DotaInput *input){
    DotaObjectiveState *s=&g->objectives;DotaUnit *u=&g->units[g->player];int id,w;
    if(g->paused || g->state!=DOTA_PLAYING)return 0;
    if(!s->order)return 0;
    if(!live_hero(u) || u->generation!=s->order_generation || u->stun_ticks || u->taunt_ticks || u->channel_ticks
       || dota_omnislash_phase(g,g->player) || (input && (input->move_x || input->move_y || input->actions))){dota_objective_cancel(g);return 0;}
    id=s->order-1;w=id-DOTA_OBJECTIVE_WATCHER_FIRST;
    if((unsigned)w>=DOTA_WATCHERS || s->watcher_owner[w]==u->team
       || (s->watcher_owner[w]==-2 && s->watcher_until[w]>g->tick)){dota_objective_cancel(g);return 0;}
    u->target=-1;u->move_order=0;
    if(!near(u->x,u->y,dota_objective_positions[id].x,dota_objective_positions[id].y,200)){
        s->channel_ticks=0;dota_rules_move(g,g->player,dota_objective_positions[id].x,dota_objective_positions[id].y);return 1;
    }
    if(++s->channel_ticks>=DOTA_HZ){
        if(s->watcher_owner[w]>=0){s->watcher_owner[w]=-2;s->watcher_until[w]=g->tick+120*DOTA_HZ;event(g,DOTA_OBJECTIVE_EVENT_WATCHER_OFF,0);}
        else{s->watcher_owner[w]=(signed char)u->team;s->watcher_until[w]=g->tick+420*DOTA_HZ;event(g,DOTA_OBJECTIVE_EVENT_WATCHER_ON,0);}
        dota_sight_invalidate();
        dota_objective_cancel(g);
    }
    return 1;
}
static void wisdom_tick(DotaGame *g,int j){
    DotaObjectiveState *s=&g->objectives;unsigned mask;int team,recipient,lowest=-1,i,amount;
    if(!s->wisdom_cycle[j])return;
    mask=nearby(g,j,300,0);team=only_team(g,mask);
    if(team<0){if(team==-2 && s->wisdom_progress[j]>0)--s->wisdom_progress[j];else if(team==-1)s->wisdom_progress[j]=0;return;}
    if(s->wisdom_team[j]!=team){s->wisdom_team[j]=team;s->wisdom_progress[j]=0;}
    if(++s->wisdom_progress[j]<3*DOTA_HZ)return;
    recipient=pick(g,mask);if(recipient<0)return;
    /* Lowest total experience is ordered by level first, then current-level XP.
     * The recipient cannot also receive the distinct teammate award. */
    for(i=0;i<DOTA_HERO_COUNT;++i){const DotaUnit *u=&g->units[i];
        if(i!=recipient && u->active && u->type==DOTA_HERO && u->team==team
           && (lowest<0 || u->level<g->units[lowest].level || (u->level==g->units[lowest].level && u->xp<g->units[lowest].xp)))lowest=i;
    }
    amount=200+300*(s->wisdom_cycle[j]-1);
    dota_rules_grant_xp(g,recipient,amount);if(lowest>=0)dota_rules_grant_xp(g,lowest,amount);
    if(team==g->units[g->player].team)event(g,DOTA_OBJECTIVE_EVENT_WISDOM,amount);
    s->wisdom_cycle[j]=s->wisdom_progress[j]=0;s->wisdom_team[j]=-1;
}
static void lotus_tick(DotaGame *g,int j){
    DotaObjectiveState *s=&g->objectives;unsigned mask;int team,recipient,threshold,item=s->lotus_grade==3?DOTA_ITEM_GREATER_FAMANGO:s->lotus_grade==2?DOTA_ITEM_GREAT_FAMANGO:DOTA_ITEM_FAMANGO;
    if(!s->lotus_count[j]){s->lotus_progress[j]=0;s->lotus_pickup_ticks[j]=30;return;}
    /* A full inventory still contests the opposing team's pool. */
    mask=nearby(g,j+DOTA_OBJECTIVE_LOTUS_FIRST,350,0);team=only_team(g,mask);
    if(team<0){if(team==-2 && s->lotus_progress[j]>0)--s->lotus_progress[j];
        else if(team==-1){s->lotus_progress[j]=0;s->lotus_pickup_ticks[j]=30;s->lotus_team[j]=-1;}
        return;
    }
    if(s->lotus_team[j]!=team){s->lotus_team[j]=team;s->lotus_progress[j]=0;s->lotus_pickup_ticks[j]=30;}
    mask=nearby(g,j+DOTA_OBJECTIVE_LOTUS_FIRST,350,item);
    if(!mask){s->lotus_progress[j]=0;s->lotus_pickup_ticks[j]=30;return;}
    threshold=s->lotus_pickup_ticks[j];if(++s->lotus_progress[j]<threshold)return;
    recipient=pick(g,mask);if(recipient<0)return;
    if(!dota_items_receive(g,recipient,item,201)){s->lotus_progress[j]=0;return;}
    --s->lotus_count[j];s->lotus_progress[j]=0;
    s->lotus_pickup_ticks[j]=(threshold*70+99)/100;if(s->lotus_pickup_ticks[j]<6)s->lotus_pickup_ticks[j]=6;
    if(recipient==g->player)event(g,DOTA_OBJECTIVE_EVENT_LOTUS,dota_objective_lotus_amount(g,recipient));
}
void dota_objectives_tick(DotaGame *g){
    DotaObjectiveState *s;int i,cycle,delta,match;if(!g || g->paused || g->state!=DOTA_PLAYING)return;s=&g->objectives;match=dota_match_game_ticks(g);
    for(i=0;i<DOTA_HERO_COUNT;++i)if(s->lotus_cooldown[i])--s->lotus_cooldown[i];
    for(i=0;i<DOTA_WATCHERS;++i)if(s->watcher_owner[i]!=-1 && s->watcher_until[i]<=g->tick){s->watcher_owner[i]=-1;s->watcher_until[i]=0;}
    cycle=match/(420*DOTA_HZ);
    if(cycle>s->wisdom_spawn_cycle){s->wisdom_spawn_cycle=cycle;for(i=0;i<2;++i){s->wisdom_cycle[i]=cycle;s->wisdom_progress[i]=0;s->wisdom_team[i]=-1;}}
    if(s->lotus_grade<2 && match>=35*60*DOTA_HZ){
        for(i=0;i<2;++i)s->lotus_count[i]=(s->lotus_count[i]+2)/3;
        s->lotus_grade=2;
    }
    if(s->lotus_grade<3 && match>=60*60*DOTA_HZ){
        for(i=0;i<2;++i)s->lotus_count[i]=(s->lotus_count[i]+1)/2;
        s->lotus_grade=3;
    }
    cycle=match/(DOTA_TURBO_LOTUS_SECONDS*DOTA_HZ);delta=cycle-s->lotus_spawn_cycle;
    if(delta>0){s->lotus_spawn_cycle=cycle;for(i=0;i<2;++i){s->lotus_count[i]+=delta;if(s->lotus_count[i]>6)s->lotus_count[i]=6;}}
    for(i=0;i<2;++i){wisdom_tick(g,i);lotus_tick(g,i);}
}
int dota_objective_owner(const DotaGame *g,int id){
    if(!g || (unsigned)id>=DOTA_OBJECTIVES)return -1;
    if(id<2)return g->objectives.wisdom_team[id];
    if(id<4)return g->objectives.lotus_team[id-2];
    return g->objectives.watcher_owner[id-4];
}
int dota_objective_amount(const DotaGame *g,int id){
    if(!g || (unsigned)id>=DOTA_OBJECTIVES)return 0;
    if(id<2)return g->objectives.wisdom_cycle[id]?200+300*(g->objectives.wisdom_cycle[id]-1):0;
    if(id<4)return g->objectives.lotus_count[id-2];
    return g->objectives.watcher_until[id-4]>g->tick?(g->objectives.watcher_until[id-4]-g->tick+DOTA_HZ-1)/DOTA_HZ:0;
}
int dota_objective_progress(const DotaGame *g,int id){
    if(!g || (unsigned)id>=DOTA_OBJECTIVES)return 0;
    if(id<2)return g->objectives.wisdom_progress[id]*1000/(3*DOTA_HZ);
    if(id<4){int threshold=g->objectives.lotus_pickup_ticks[id-2];return threshold?g->objectives.lotus_progress[id-2]*1000/threshold:0;}
    return g->objectives.order==id+1?g->objectives.channel_ticks*1000/DOTA_HZ:0;
}
int dota_objective_vision(const DotaGame *g,int team,int x,int y){
    int i,radius;if(!g || (unsigned)team>1)return 0;radius=dota_match_is_night(g)?450:800;
    for(i=0;i<DOTA_WATCHERS;++i)if(g->objectives.watcher_owner[i]==team && g->objectives.watcher_until[i]>g->tick
        && near(x,y,dota_objective_positions[i+4].x,dota_objective_positions[i+4].y,radius)
        && dota_pit_same_region(x,y,dota_objective_positions[i+4].x,dota_objective_positions[i+4].y))return 1;
    return 0;
}
int dota_objective_lotus_amount(const DotaGame *g,int hero){
    int i,total=0;if(!g||(unsigned)hero>=DOTA_HERO_COUNT)return 0;
    for(i=0;i<DOTA_INVENTORY_SLOTS;i++)total+=dota_items_lotus_value(dota_inventory_item(g,hero,i))*dota_inventory_charges(g,hero,i);
    return total;
}
static int lotus_slot(const DotaGame*g,int hero){
    int i,best=-1,value=0;
    for(i=0;i<DOTA_ITEM_SLOTS;i++){
        int id=dota_items_equipped_item(g,hero,i),weight=dota_items_lotus_value(id);
        if(weight>value&&dota_items_charges(g,hero,i)>0&&!dota_items_cooldown(g,hero,i)){best=i;value=weight;}
    }
    return best;
}
int dota_objective_lotus_heal(const DotaGame *g,int hero){
    int slot;if(!g||(unsigned)hero>=DOTA_HERO_COUNT||(slot=lotus_slot(g,hero))<0)return 0;
    return dota_items_parameter(dota_inventory_item(g,hero,slot),IP_REPLENISH_AMOUNT,-1,12500)/100;
}
int dota_objective_pool_grade(const DotaGame *g,int id){return g && id>=2 && id<4?g->objectives.lotus_grade:0;}
int dota_objective_pool_heal(const DotaGame *g,int id){int grade=dota_objective_pool_grade(g,id);return grade==3?900:grade==2?400:grade==1?125:0;}
int dota_objective_use_lotus(DotaGame *g,int hero){
    DotaUnit *u;int slot;if(!g || (unsigned)hero>=DOTA_HERO_COUNT || g->paused || g->state!=DOTA_PLAYING)return 0;
    u=&g->units[hero];if(!live_hero(u))return 0;
    slot=lotus_slot(g,hero);
    if(slot<0){if(!dota_objective_lotus_amount(g,hero)&&hero==g->player)event(g,DOTA_OBJECTIVE_EVENT_NO_LOTUS,0);return 0;}
    return dota_items_use(g,hero,slot,u->x,u->y,hero);
}
