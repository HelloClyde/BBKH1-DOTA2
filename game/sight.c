#include "sight.h"
#include "barracks.h"
#include "vision.h"
#include "items.h"
#include "items_ids.h"
#include "items_params.h"
#include "items_special.h"
#include "item_world.h"
#include "neutral.h"
#include "objectives.h"
#include "unit_vision_data.h"
#include "jungle.h"
#include "roster_heroes.h"
/* Radial shared sight, refreshed once per simulation tick. The baked terrain
 * remains visible; hill/tree line-of-sight is outside this H1 renderer. Strict
 * Roshan-region separation is still applied by the caller for each observer. */
static const DotaGame *cached_game;
static int cached_tick=-1;
static unsigned char visible[2][DOTA_MAX_UNITS];
void dota_sight_reset(void){cached_game=0;cached_tick=-1;}
int dota_sight_is_current(const DotaGame *g){return cached_game==g && cached_tick==g->tick;}
static int live(const DotaUnit *u){return u->active && u->hp>0 && u->respawn_ticks<=0;}
static int in_range(int x,int y,int xx,int yy,int source){long long dx=x-xx,dy=y-yy;return (dx*dx+dy*dy)*16<=(long long)source*source;}
int dota_rules_unit_vision_source(const DotaGame *g,int index){
    const DotaUnit *u;const unsigned short *table=unit_default_vision;int night,range,owner=-1,type,hero_id,tier;
    if(!g || (unsigned)index>=DOTA_MAX_UNITS)return 0;
    u=&g->units[index];type=u->type;hero_id=u->hero_id;tier=u->tier;night=(g->tick/(5*60*DOTA_HZ))&1;
    if(index>=DOTA_ITEM_UNIT_FIRST){const DotaItemWorldUnit *s=&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];
        if(s->kind==ITEM_UNIT_ILLUSION){type=DOTA_HERO;owner=dota_item_world_owner(g,index);}
        else if(s->kind==ITEM_UNIT_DOMINATED)type=s->source_type;
        else if(s->kind==ITEM_UNIT_OBSERVER)table=unit_observer_vision;
        else if(s->kind==ITEM_UNIT_SENTRY)table=unit_sentry_vision;
        else if(s->kind==ITEM_UNIT_SUMMON)table=tier?unit_necro_archer_vision:unit_necro_warrior_vision;
    }
    if(type==DOTA_HERO && (unsigned)hero_id<DOTA_HERO_TYPES){table=unit_hero_vision[hero_id];if(index<DOTA_HERO_COUNT)owner=index;}
    else if(type==DOTA_TOWER)table=unit_tower_vision;
    else if(type==DOTA_BASE)table=unit_base_vision;
    else if(dota_barracks_kind(u))table=tier==DOTA_BARRACKS_MELEE?unit_melee_barracks_vision:unit_ranged_barracks_vision;
    else if(type==DOTA_CREEP)table=tier?unit_ranged_creep_vision:unit_melee_creep_vision;
    else if(type==DOTA_NEUTRAL){const DotaJungleDefinition *d=(unsigned)tier<DOTA_JUNGLE_SPECIES?&dota_jungle_definitions[tier]:0;if(d)return night?d->vision_night:d->vision_day;table=unit_default_vision;}
    else if(type==DOTA_ROSHAN)table=unit_roshan_vision;
    range=table[night];
    if(owner>=0){int i,bonus=0;
        if(night){range+=dota_items_special_night_vision_bonus(g,owner);
            for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS;++i){int item=dota_items_equipped_item(g,owner,i);if(item>=0){int n=dota_items_parameter(item,IP_BONUS_NIGHT_VISION,-1,0)/100;if(n>bonus)bonus=n;}}
            if(g->items.permanent[owner]&1u){int n=dota_items_parameter(DOTA_ITEM_MOON_SHARD,IP_CONSUMED_BONUS_NIGHT_VISION,-1,0)/100;if(n>bonus)bonus=n;}
            range+=bonus;
        }
        range=range*dota_items_special_vision_percent(g,owner)/100;
    }
    return range>0?range:0;
}
static void refresh(const DotaGame *g){
    int i,j,k,team,night=(g->tick/(5*60*DOTA_HZ))&1,sight[DOTA_MAX_UNITS];
    int observer_count[2]={0,0},bonus[DOTA_HERO_COUNT],heading_x[DOTA_HERO_COUNT],heading_y[DOTA_HERO_COUNT];
    unsigned char observers[2][DOTA_MAX_UNITS],region[DOTA_MAX_UNITS],fountain_region[2],watcher_region[DOTA_WATCHERS];
    /* These query-only values belong to this exact shared-sight snapshot.
     * Each team's eligible observers retain the original ascending unit order;
     * no result survives a refresh or changes its first triggering query. */
    for(j=0;j<DOTA_MAX_UNITS;++j){const DotaUnit *u=&g->units[j];
        int living=live(u);
        sight[j]=living?dota_rules_unit_vision_source(g,j):0;
        region[j]=(unsigned char)(living?dota_roshan_region_at(u->x,u->y):0);
        if((unsigned)u->team<2 && sight[j]>0){
            observers[u->team][observer_count[u->team]++]=(unsigned char)j;
            if(j<DOTA_HERO_COUNT){bonus[j]=dota_roster_vision_bonus(g,j);heading_x[j]=g->roster.hero[j].vision_dx;heading_y[j]=g->roster.hero[j].vision_dy;}
        }
    }
    for(team=0;team<2;++team)fountain_region[team]=(unsigned char)dota_roshan_region_at(dota_fountains[team].x,dota_fountains[team].y);
    for(j=0;j<DOTA_WATCHERS;++j){const DotaPoint *p=&dota_objective_positions[DOTA_OBJECTIVE_WATCHER_FIRST+j];watcher_region[j]=(unsigned char)dota_roshan_region_at(p->x,p->y);}
    for(team=0;team<2;++team)for(i=0;i<DOTA_MAX_UNITS;++i){const DotaUnit *target=&g->units[i];visible[team][i]=0;
        if(target->team==team){visible[team][i]=1;continue;}
        if(!live(target))continue;
        for(k=0;k<observer_count[team];++k){const DotaUnit *eye;
            j=observers[team][k];eye=&g->units[j];
            if(j<DOTA_HERO_COUNT && bonus[j]>0){
                long long dx=target->x-eye->x,dy=target->y-eye->y,dot,cross;
                if(!in_range(eye->x,eye->y,target->x,target->y,sight[j]+bonus[j]))continue;
                if(dx || dy){
                    dot=dx*heading_x[j]+dy*heading_y[j];if(dot<=0)continue;
                    cross=dx*heading_y[j]-dy*heading_x[j];if(cross<0)cross=-cross;
                    if(cross*1000000>dot*2747477)continue;
                }
            }else if(!in_range(eye->x,eye->y,target->x,target->y,sight[j]))continue;
            /* dota_pit_same_region is exactly equality of these pure regions. */
            if(region[j]==region[i]){visible[team][i]=1;break;}
        }
        if(!visible[team][i]&&dota_roster_area_vision(g,team,target->x,target->y))visible[team][i]=1;
        if(!visible[team][i] && in_range(dota_fountains[team].x,dota_fountains[team].y,target->x,target->y,unit_fountain_vision[0]) && fountain_region[team]==region[i])visible[team][i]=1;
        for(j=0;j<DOTA_WATCHERS && !visible[team][i];++j){const DotaPoint *p=&dota_objective_positions[DOTA_OBJECTIVE_WATCHER_FIRST+j];if(g->objectives.watcher_owner[j]==team && g->objectives.watcher_until[j]>g->tick && in_range(p->x,p->y,target->x,target->y,unit_watcher_vision[night]) && watcher_region[j]==region[i])visible[team][i]=1;}
    }
    cached_game=g;cached_tick=g->tick;
}
int dota_sight_position_visible(const DotaGame *g,int observer,int x,int y){
    int j,team,night;if(!g || (unsigned)observer>=DOTA_MAX_UNITS)return 0;
    if(!dota_pit_same_region(g->units[observer].x,g->units[observer].y,x,y))return 0;
    team=g->units[observer].team;night=(g->tick/(5*60*DOTA_HZ))&1;
    for(j=0;j<DOTA_MAX_UNITS;++j)if(g->units[j].team==team && live(&g->units[j]) && (j<10&&dota_roster_vision_bonus(g,j)>0?dota_roster_vision_point(g,j,x,y,dota_rules_unit_vision_source(g,j)):in_range(g->units[j].x,g->units[j].y,x,y,dota_rules_unit_vision_source(g,j))) && dota_pit_same_region(g->units[j].x,g->units[j].y,x,y))return 1;
    if(dota_roster_area_vision(g,team,x,y))return 1;
    for(j=0;j<DOTA_WATCHERS;++j){const DotaPoint *p=&dota_objective_positions[DOTA_OBJECTIVE_WATCHER_FIRST+j];if(g->objectives.watcher_owner[j]==team && g->objectives.watcher_until[j]>g->tick && in_range(p->x,p->y,x,y,unit_watcher_vision[night]) && dota_pit_same_region(p->x,p->y,x,y))return 1;}
    return (unsigned)team<2 && in_range(dota_fountains[team].x,dota_fountains[team].y,x,y,unit_fountain_vision[0]) && dota_pit_same_region(dota_fountains[team].x,dota_fountains[team].y,x,y);
}
int dota_sight_team_visible(const DotaGame *g,int observer,int target){
    int team;if(!g || (unsigned)observer>=DOTA_MAX_UNITS || (unsigned)target>=DOTA_MAX_UNITS)return 0;
    team=g->units[observer].team;
    if((unsigned)team>=2)return in_range(g->units[observer].x,g->units[observer].y,g->units[target].x,g->units[target].y,dota_rules_unit_vision_source(g,observer));
    if(cached_game!=g || cached_tick!=g->tick)refresh(g);
    return visible[team][target];
}
