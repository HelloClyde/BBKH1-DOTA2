#include "courier.h"
#include "items.h"

static int valid_hero(const DotaGame *g,int hero){
    return g && (unsigned)hero<DOTA_COURIER_HEROES && g->units[hero].active &&
        g->units[hero].type==DOTA_HERO && (unsigned)g->units[hero].team<2;
}
static int live(const DotaUnit *u){return u->hp>0 && u->respawn_ticks<=0;}
static int count(const int16_t *items,int slots){int i,n=0;for(i=0;i<slots;++i)if(items[i]>=0)++n;return n;}
static int free_slot(const int16_t *items,int slots){int i;for(i=0;i<slots;++i)if(items[i]<0)return i;return -1;}
static int x_at(const DotaCourier *c){return c->x_q16/DOTA_MOVE_ONE;}
static int y_at(const DotaCourier *c){return c->y_q16/DOTA_MOVE_ONE;}
static int distance2(int x,int y,int xx,int yy){int dx=x-xx,dy=y-yy;return dx*dx+dy*dy;}
static int length(int dx,int dy){
    unsigned long long n=(unsigned long long)((long long)dx*dx)+(unsigned long long)((long long)dy*dy);
    unsigned long long bit=1ull<<62,result=0;
    while(bit>n)bit>>=2;
    while(bit){if(n>=result+bit){n-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return (int)result;
}
static void event(DotaGame *g,int hero,int kind,int amount){
    if(hero==g->player){g->last_event=kind;g->last_event_value=amount;g->last_event_ticks=50;}
}
static int at_home(const DotaCourier *c,const DotaPoint *home){return c->x_q16==home->x*DOTA_MOVE_ONE && c->y_q16==home->y*DOTA_MOVE_ONE;}
static int close_to_hero(const DotaCourier *c,const DotaUnit *u){
    long long dx=(long long)u->x*DOTA_MOVE_ONE-c->x_q16,dy=(long long)u->y*DOTA_MOVE_ONE-c->y_q16;
    long long radius=(long long)DOTA_COURIER_HANDOFF_SOURCE*DOTA_MOVE_ONE/DOTA_SOURCE_UNITS_PER_WORLD;
    /* Original transfer modifier compares2D origin distance strictly<400. */
    return dx*dx+dy*dy<radius*radius;
}
static int move(DotaCourier *c,int x,int y){
    int dx=x*DOTA_MOVE_ONE-c->x_q16,dy=y*DOTA_MOVE_ONE-c->y_q16,d=length(dx,dy);
    int step=DOTA_COURIER_SPEED_SOURCE*DOTA_MOVE_ONE/(DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ);
    c->target_x=x;c->target_y=y;
    if(d<=step){c->x_q16=x*DOTA_MOVE_ONE;c->y_q16=y*DOTA_MOVE_ONE;return 1;}
    c->x_q16+=(int)((long long)dx*step/d);c->y_q16+=(int)((long long)dy*step/d);
    return 0;
}
static void pickup(DotaCourier *c){
    int i,slot;for(i=0;i<DOTA_COURIER_STASH_SLOTS;++i)if(c->stash[i]>=0){
        slot=free_slot(c->cargo,DOTA_COURIER_SLOTS);if(slot<0)return;c->cargo[slot]=c->stash[i];c->cargo_age[slot]=c->stash_age[i];c->stash[i]=-1;
    }
}
static int handoff(DotaGame *g,int hero){
    DotaUnit *u=&g->units[hero];DotaCourier *c=&g->couriers.hero[hero];int i,n=0,counted=0;int16_t items[DOTA_COURIER_SLOTS];uint16_t ages[DOTA_COURIER_SLOTS];
    if(!live(u) || !close_to_hero(c,u))return 0;
    for(i=0;i<DOTA_COURIER_SLOTS;++i)if(c->cargo[i]>=0){items[counted]=c->cargo[i];ages[counted++]=c->cargo_age[i];}
    if(counted && dota_items_receive_batch(g,hero,items,ages,counted)){
        for(i=0;i<DOTA_COURIER_SLOTS;++i)c->cargo[i]=-1;
        n=counted;
    }
    for(i=0;i<DOTA_COURIER_SLOTS;++i)if(c->cargo[i]>=0){
        if(dota_items_receive(g,hero,c->cargo[i],c->cargo_age[i])){c->cargo[i]=-1;++n;}
    }
    if(n){dota_rules_recalc(g,hero);event(g,hero,DOTA_COURIER_EVENT_DELIVERED,n);}
    if(!count(c->cargo,DOTA_COURIER_SLOTS))c->phase=DOTA_COURIER_RETURN;
    return n;
}
void dota_couriers_init(DotaGame *g){
    int hero,i;if(!g)return;
    for(hero=0;hero<DOTA_COURIER_HEROES;++hero){
        DotaCourier *c=&g->couriers.hero[hero];const DotaPoint *home=&dota_hero_spawns[g->units[hero].team&1][hero%5];
        c->x_q16=home->x*DOTA_MOVE_ONE;c->y_q16=home->y*DOTA_MOVE_ONE;
        c->target_x=c->previous_x=home->x;c->target_y=c->previous_y=home->y;c->phase=DOTA_COURIER_HOME;
        for(i=0;i<DOTA_COURIER_SLOTS;++i){c->cargo[i]=-1;c->cargo_age[i]=0;}
        for(i=0;i<DOTA_COURIER_STASH_SLOTS;++i){c->stash[i]=-1;c->stash_age[i]=0;}
    }
    g->couriers.last_tick=-1;
}
static int stash_free(const DotaCourier *c){return DOTA_COURIER_STASH_SLOTS-count(c->stash,DOTA_COURIER_STASH_SLOTS);}
int dota_courier_can_buy(const DotaGame *g,int hero,int item){
    DotaItemPurchasePlan plan;
    if(!valid_hero(g,hero) || g->state!=DOTA_PLAYING || !dota_items_purchase_plan(g,hero,item,&plan) || !plan.count)return 0;
    return g->units[hero].gold>=plan.cost && stash_free(&g->couriers.hero[hero])>=plan.count;
}
int dota_courier_buy(DotaGame *g,int hero,int item){
    DotaItemPurchasePlan plan;DotaUnit *u;DotaCourier *c;int i,slot;
    if(!valid_hero(g,hero) || g->state!=DOTA_PLAYING || !dota_items_purchase_plan(g,hero,item,&plan) || !plan.count)return 0;
    u=&g->units[hero];c=&g->couriers.hero[hero];
    if(u->gold<plan.cost){event(g,hero,5,item);return 0;}
    if(stash_free(c)<plan.count){event(g,hero,DOTA_COURIER_EVENT_STASH_FULL,item);return 0;}
    if(!dota_items_purchase_commit(g,hero,&plan))return 0;
    u->gold-=plan.cost;
    for(i=0;i<plan.count;++i){
        int delivered=live(u) && distance2(u->x,u->y,dota_fountains[u->team].x,dota_fountains[u->team].y)<=DOTA_SHOP_RANGE*DOTA_SHOP_RANGE && dota_items_receive(g,hero,plan.missing[i],0);
        if(delivered)continue;
        slot=free_slot(c->stash,DOTA_COURIER_STASH_SLOTS);c->stash[slot]=plan.missing[i];c->stash_age[slot]=0;
    }
    event(g,hero,dota_courier_pending_count(g,hero)?DOTA_COURIER_EVENT_ORDERED:8,item);return 1;
}
void dota_couriers_tick(DotaGame *g){
    int hero;if(!g || g->paused || g->state!=DOTA_PLAYING || g->couriers.last_tick==g->tick)return;
    g->couriers.last_tick=g->tick;
    for(hero=0;hero<DOTA_COURIER_HEROES;++hero){
        DotaUnit *u=&g->units[hero];DotaCourier *c=&g->couriers.hero[hero];const DotaPoint *home;int i;
        if(!valid_hero(g,hero))continue;
        for(i=0;i<DOTA_COURIER_SLOTS;++i)if(c->cargo[i]>=0 && c->cargo_age[i]<201)++c->cargo_age[i];
        for(i=0;i<DOTA_COURIER_STASH_SLOTS;++i)if(c->stash[i]>=0 && c->stash_age[i]<201)++c->stash_age[i];
        home=&dota_hero_spawns[u->team][hero%5];
        c->previous_x=x_at(c);c->previous_y=y_at(c);
        if(!live(u) && c->phase==DOTA_COURIER_DELIVER)c->phase=DOTA_COURIER_RETURN;
        if(c->phase==DOTA_COURIER_RETURN){
            if(move(c,home->x,home->y))c->phase=live(u)?DOTA_COURIER_HOME:DOTA_COURIER_WAIT_DEAD;
            continue;
        }
        if(c->phase==DOTA_COURIER_WAIT_DEAD && live(u))c->phase=DOTA_COURIER_HOME;
        if(c->phase==DOTA_COURIER_HOME || c->phase==DOTA_COURIER_WAIT_DEAD){
            c->target_x=home->x;c->target_y=home->y;
            if(!at_home(c,home)){c->phase=DOTA_COURIER_RETURN;move(c,home->x,home->y);continue;}
            if(!live(u)){c->phase=DOTA_COURIER_WAIT_DEAD;continue;}
            if(g->tick%(DOTA_COURIER_AUTO_SECONDS*DOTA_HZ)==0)pickup(c);
            if(count(c->cargo,DOTA_COURIER_SLOTS))c->phase=DOTA_COURIER_DELIVER;else continue;
        }
        if(c->phase==DOTA_COURIER_DELIVER){
            if(!count(c->cargo,DOTA_COURIER_SLOTS)){c->phase=DOTA_COURIER_RETURN;continue;}
            c->target_x=u->x;c->target_y=u->y;
            if(!close_to_hero(c,u))move(c,u->x,u->y);
            if(close_to_hero(c,u))handoff(g,hero);
        }
    }
}
int dota_courier_pending_count(const DotaGame *g,int hero){
    if(!valid_hero(g,hero))return 0;
    return count(g->couriers.hero[hero].cargo,DOTA_COURIER_SLOTS)+count(g->couriers.hero[hero].stash,DOTA_COURIER_STASH_SLOTS);
}
int dota_courier_pending_item(const DotaGame *g,int hero,int nth){
    const DotaCourier *c;int i;if(!valid_hero(g,hero) || nth<0)return -1;c=&g->couriers.hero[hero];
    for(i=0;i<DOTA_COURIER_SLOTS;++i)if(c->cargo[i]>=0){if(!nth--)return c->cargo[i];}
    for(i=0;i<DOTA_COURIER_STASH_SLOTS;++i)if(c->stash[i]>=0){if(!nth--)return c->stash[i];}
    return -1;
}
static int travel_ticks(int x,int y,int xx,int yy,int radius){
    int distance=length((x-xx)*DOTA_MOVE_ONE,(y-yy)*DOTA_MOVE_ONE)-radius*DOTA_MOVE_ONE;
    int step=DOTA_COURIER_SPEED_SOURCE*DOTA_MOVE_ONE/(DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ);
    return distance<=0?0:(distance+step-1)/step;
}
int dota_courier_eta_ticks(const DotaGame *g,int hero){
    const DotaCourier *c;const DotaUnit *u;const DotaPoint *home;int back,pending,period=DOTA_COURIER_AUTO_SECONDS*DOTA_HZ;
    if(!valid_hero(g,hero))return 0;
    c=&g->couriers.hero[hero];u=&g->units[hero];pending=dota_courier_pending_count(g,hero);
    if(pending && !live(u))return -1;
    if(c->phase==DOTA_COURIER_DELIVER){
        int i,n=0,can=0;int16_t incoming[DOTA_COURIER_SLOTS];
        for(i=0;i<DOTA_COURIER_SLOTS;++i)if(c->cargo[i]>=0)incoming[n++]=c->cargo[i];
        if(dota_items_can_receive_batch(g,hero,incoming,n))can=1;
        else for(i=0;i<n;++i)if(dota_items_can_receive((DotaGame*)g,hero,incoming[i]))can=1;
        if(!can)return -1;
        return travel_ticks(x_at(c),y_at(c),u->x,u->y,DOTA_COURIER_HANDOFF_SOURCE/4);
    }
    home=&dota_hero_spawns[u->team][hero%5];back=travel_ticks(x_at(c),y_at(c),home->x,home->y,0);
    if(!pending)return c->phase==DOTA_COURIER_RETURN?back:0;
    if(count(c->cargo,DOTA_COURIER_SLOTS))return back+travel_ticks(home->x,home->y,u->x,u->y,DOTA_COURIER_HANDOFF_SOURCE/4);
    return back+period-(g->tick+back)%period+travel_ticks(home->x,home->y,u->x,u->y,DOTA_COURIER_HANDOFF_SOURCE/4);
}
int dota_courier_view(const DotaGame *g,int hero,DotaCourierView *v){
    const DotaCourier *c;if(!v || !valid_hero(g,hero))return 0;c=&g->couriers.hero[hero];
    v->x=x_at(c);v->y=y_at(c);v->target_x=c->target_x;v->target_y=c->target_y;v->phase=c->phase;
    v->moving=v->x!=c->previous_x || v->y!=c->previous_y;v->cargo=count(c->cargo,DOTA_COURIER_SLOTS);v->stash=count(c->stash,DOTA_COURIER_STASH_SLOTS);v->eta_ticks=dota_courier_eta_ticks(g,hero);return 1;
}
int dota_courier_speed_source(void){return DOTA_COURIER_SPEED_SOURCE;}
