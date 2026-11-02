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
#include "match_time.h"
#include "trees.h"

/* Terrain sight uses a 64-Source-unit grid, independent of navigation.
 * Cached integer shadowcasting visits cells, never pixel x unit rays. Masks
 * are rebuilt only when source cell/height/radius/pit or trees change. All
 * large storage is borrowed heap. Nearest-128-Source-unit ground levels are
 * an H1 LOS approximation, not Valve's engine implementation. */
#define GRID DOTA_SIGHT_GRID_SIDE
#define CELL DOTA_SIGHT_CELL_WORLD
#define GRID_WORDS (GRID*GRID/32)
#define MAX_RADIUS 64 /* 4096 Source; installed sight peaks below this. */
#define LOCAL_SIDE (MAX_RADIUS*2+1)
#define LOCAL_WORDS ((LOCAL_SIDE*LOCAL_SIDE+31)/32)
#define SLOPE_COUNT (MAX_RADIUS*(MAX_RADIUS+3)/2)
#define SHARED_ENTRIES 128
#define REACH_ENTRIES 16
#define REACH_VALUES (MAX_RADIUS*CELL+CELL+1)
#define REACH_WORDS ((REACH_VALUES+31)/32)
typedef struct { unsigned budget,age;uint32_t valid[REACH_WORDS];uint16_t value[REACH_VALUES]; } SightReach;
#define WATCHER_FIRST (DOTA_MAX_UNITS+2)
#define FIELD_FIRST (WATCHER_FIRST+DOTA_WATCHERS)
#define GEM_FIRST (FIELD_FIRST+DOTA_ROSTER_FIELDS)
#define SOURCE_COUNT (GEM_FIRST+8)
typedef struct {
    int x,y,cx,cy,radius,cell_radius,height,team,region,bonus,heading_x,heading_y,flying;
    int key_cx,key_cy,key_radius,key_height,key_region,key_flying;
    unsigned tree_stamp;
    int active,valid;
    uint32_t mask[LOCAL_WORDS];
} SightSource;

/* Diagnostic exact-key direct-mapped memoization. Each source retains its own
 * mask; no pointer is retained into an evictable shared entry. */
typedef struct {
    int cx,cy,radius,height,region,flying;
    unsigned tree_stamp,valid;
    uint32_t mask[LOCAL_WORDS];
} SightShared;
volatile unsigned h1_shared_shadow_profile[5]; /* own_hits,lookups,hits,casts,evictions */
typedef struct {
    const DotaGame *owner;
    uint32_t current[2][GRID_WORDS],explored[2][GRID_WORDS],dynamic_trees[GRID_WORDS];
    unsigned char regions[GRID*GRID];
    SightSource source[SOURCE_COUNT];
    unsigned short active[SOURCE_COUNT];
    int active_count;
    unsigned tree_signature,tree_stamp;
    const DotaGame *tree_owner;
    unsigned static_revision,geometry_revision,trees_valid,static_trees_ready;
    int slope_left[SLOPE_COUNT],slope_right[SLOPE_COUNT];
    SightReach reach[REACH_ENTRIES];unsigned reach_age;
    SightShared shared[SHARED_ENTRIES];
} SightWork;
static const DotaGame *cached_game;
static int cached_tick=-1;
static unsigned char visible[2][DOTA_MAX_UNITS];
static SightWork *work;
static const int16_t *terrain_height;
static const uint8_t *terrain_trees;

static void zero(void *p,unsigned bytes){unsigned char *b=p;while(bytes--)*b++=0;}
/* SUMMON stores its birth tick in respawn_ticks; only HP/active mean alive. */
static int live(const DotaUnit *u){return u->active && u->hp>0;}
static int in_range(int x,int y,int xx,int yy,int source){
    int dx,dy;
    /* Coordinates are bounded before arithmetic: max squared distance is
     * 2*4095^2, and floor(radius^2/16) gives the exact integer comparison. */
    if(source<=0 || (unsigned)(x|y|xx|yy)>=DOTA_WORLD)return 0;
    if(source>=23165)return 1;
    dx=x-xx;dy=y-yy;
    return (unsigned)(dx*dx+dy*dy)<=((unsigned)source*(unsigned)source>>4);
}
static int bit(const uint32_t *bits,int i){return (int)((bits[(unsigned)i>>5]>>(i&31))&1u);}
static void set(uint32_t *bits,int i){bits[(unsigned)i>>5]|=1u<<(i&31);}
static int height_level(int cell){
    int h;if(!terrain_height)return 0;h=terrain_height[cell];
    if(h==DOTA_SIGHT_HEIGHT_UNKNOWN)return 32767;
    return h>=0?(h+16)/32:-((-h+16)/32);
}
unsigned dota_sight_workspace_bytes(void){return (unsigned)sizeof(SightWork);}
void dota_sight_invalidate(void){cached_game=0;cached_tick=-1;}
void dota_sight_reset(void){
    dota_sight_invalidate();
    if(work){int i;work->owner=0;work->trees_valid=0;zero(work->current,sizeof(work->current));zero(work->explored,sizeof(work->explored));
        for(i=0;i<SOURCE_COUNT;++i)work->source[i].valid=0;}
}
void dota_sight_detach(void){work=0;terrain_height=0;terrain_trees=0;dota_sight_invalidate();}
int dota_sight_attach(const int16_t *height,const uint8_t *trees,void *memory,unsigned bytes){
    int x,y;
    dota_sight_detach();
    if(!memory || ((uintptr_t)memory&3u) || bytes<sizeof(SightWork) || (!!height!=!!trees))return 0;
    terrain_height=height;terrain_trees=trees;work=memory;zero(work,sizeof(*work));
    for(y=1;y<=MAX_RADIUS;++y)for(x=-y;x<=0;++x){int index=(y-1)*(y+2)/2+x+y;
        work->slope_left[index]=(2*x-1)*65536/(-2*y+1);
        work->slope_right[index]=(2*x+1)*65536/(-2*y-1);
    }
    for(y=0;y<GRID;++y)for(x=0;x<GRID;++x)work->regions[y*GRID+x]=(unsigned char)dota_roshan_region_at(x*CELL+CELL/2,y*CELL+CELL/2);
    work->tree_stamp=1;dota_sight_reset();return 1;
}
int dota_sight_is_current(const DotaGame *g){return g && cached_game==g && cached_tick==g->tick;}
int dota_rules_unit_vision_source(const DotaGame *g,int index){
    const DotaUnit *u;const unsigned short *table=unit_default_vision;int night,range,owner=-1,type,hero_id,tier;
    if(!g || (unsigned)index>=DOTA_MAX_UNITS)return 0;
    u=&g->units[index];type=u->type;hero_id=u->hero_id;tier=u->tier;night=dota_match_is_night(g);
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
static unsigned mix(unsigned hash,unsigned value){return (hash^value)*16777619u;}
static void dynamic_trees(const DotaGame *g){
    unsigned hash=2166136261u;int i;
    for(i=0;i<DOTA_ITEM_TREES;++i){const DotaItemTree *t=&g->item_world.tree[i];if(t->until>g->tick && !t->removed){hash=mix(hash,(unsigned)t->x);hash=mix(hash,(unsigned)t->y);hash=mix(hash,(unsigned)i+1u);}}
    if(work->trees_valid && work->tree_owner==g && work->static_revision==dota_trees_revision(g) && work->geometry_revision==dota_trees_geometry_revision() && work->tree_signature==hash)return;
    work->trees_valid=1;work->tree_owner=g;work->static_revision=dota_trees_revision(g);work->geometry_revision=dota_trees_geometry_revision();
    work->tree_signature=hash;++work->tree_stamp;zero(work->dynamic_trees,sizeof(work->dynamic_trees));
    /* Re-rasterise standing IDs, rather than clearing cut circles from the
     * original union: overlapping living trees must keep their own shadow. */
    work->static_trees_ready=(unsigned)dota_trees_ready();
    if(work->static_trees_ready)for(i=0;i<DOTA_STATIC_TREE_COUNT;++i)if(!dota_trees_is_cut(g,(unsigned)i)){
        int x,y,xx,yy;dota_trees_position((unsigned)i,&x,&y);
        for(yy=y/CELL-1;yy<=y/CELL+1;++yy)for(xx=x/CELL-1;xx<=x/CELL+1;++xx){
            int dx=xx*CELL+CELL/2-x,dy=yy*CELL+CELL/2-y;
            if((unsigned)xx<GRID && (unsigned)yy<GRID && dx*dx+dy*dy<=DOTA_TREE_RADIUS_WORLD*DOTA_TREE_RADIUS_WORLD)set(work->dynamic_trees,yy*GRID+xx);
        }
    }
    for(i=0;i<DOTA_ITEM_TREES;++i){const DotaItemTree *t=&g->item_world.tree[i];if(t->until>g->tick && !t->removed && (unsigned)t->x<DOTA_WORLD && (unsigned)t->y<DOTA_WORLD)set(work->dynamic_trees,(t->y/CELL)*GRID+t->x/CELL);}
}
/* 0 clear, 1 tree (its cell may be seen), 2 higher ground/unknown/pit edge. */
static int obstruction(const SightSource *s,int x,int y){
    int c;
    if((unsigned)x>=GRID || (unsigned)y>=GRID)return 2;
    c=y*GRID+x;
    if(work->regions[c]!=s->region)return 2;
    if(s->flying)return 0;
    if(s->height==32767)return 2;
    if(height_level(c)>s->height)return 2;
    if((!work->static_trees_ready && terrain_trees && ((terrain_trees[(unsigned)c>>3]>>(c&7))&1u)) || bit(work->dynamic_trees,c))return 1;
    return 0;
}
static void plot(SightSource *s,int x,int y,int wall){
    int dx=x-s->cx,dy=y-s->cy;
    if(dx*dx+dy*dy<=s->cell_radius*s->cell_radius && wall!=2)
        set(s->mask,(dy+MAX_RADIUS)*LOCAL_SIDE+dx+MAX_RADIUS);
}
static void cast(SightSource *s,int row,int start,int end,int xx,int xy,int yx,int yy){
    int distance,new_start=start,blocked=0;
    if(start<end)return;
    for(distance=row;distance<=s->cell_radius && !blocked;++distance){
        int dx,dy=-distance;
        for(dx=-distance;dx<=0;++dx){int x,y,left,right,wall;
            x=s->cx+dx*xx+dy*xy;y=s->cy+dx*yx+dy*yy;
            {int index=(distance-1)*(distance+2)/2+dx+distance;
                left=work->slope_left[index];right=work->slope_right[index];}
            if(start<right)continue;
            if(end>left)break;
            wall=obstruction(s,x,y);
            if((unsigned)x<GRID && (unsigned)y<GRID)plot(s,x,y,wall);
            if(blocked){if(wall)new_start=right;else{blocked=0;start=new_start;}}
            else if(wall && distance<s->cell_radius){blocked=1;cast(s,distance+1,start,left,xx,xy,yx,yy);new_start=right;}
        }
    }
}
static void shadow(SightSource *s){
    static const signed char octants[8][4]={{1,0,0,1},{0,1,1,0},{0,-1,1,0},{-1,0,0,1},{-1,0,0,-1},{0,-1,-1,0},{0,1,-1,0},{1,0,0,-1}};
    SightShared *entry;unsigned slot;int i;
    if(s->valid && s->key_cx==s->cx && s->key_cy==s->cy && s->key_radius==s->cell_radius && s->key_height==s->height && s->key_region==s->region && s->key_flying==s->flying && s->tree_stamp==work->tree_stamp){++h1_shared_shadow_profile[0];return;}
    ++h1_shared_shadow_profile[1];
    slot=((unsigned)s->cx*17u^(unsigned)s->cy*131u^(unsigned)s->cell_radius*23u^(unsigned)s->height*7u^(unsigned)s->region*31u^(unsigned)s->flying*61u^work->tree_stamp)&(SHARED_ENTRIES-1u);
    entry=&work->shared[slot];
    if(entry->valid && entry->cx==s->cx && entry->cy==s->cy && entry->radius==s->cell_radius && entry->height==s->height && entry->region==s->region && entry->flying==s->flying && entry->tree_stamp==work->tree_stamp){
        ++h1_shared_shadow_profile[2];
        for(i=0;i<LOCAL_WORDS;++i)s->mask[i]=entry->mask[i];
    }else{
        ++h1_shared_shadow_profile[3];if(entry->valid)++h1_shared_shadow_profile[4];
        zero(s->mask,sizeof(s->mask));set(s->mask,MAX_RADIUS*LOCAL_SIDE+MAX_RADIUS);
        for(i=0;i<8;++i)cast(s,1,65536,0,octants[i][0],octants[i][1],octants[i][2],octants[i][3]);
        entry->cx=s->cx;entry->cy=s->cy;entry->radius=s->cell_radius;entry->height=s->height;entry->region=s->region;entry->flying=s->flying;entry->tree_stamp=work->tree_stamp;entry->valid=1;
        for(i=0;i<LOCAL_WORDS;++i)entry->mask[i]=s->mask[i];
    }
    s->key_cx=s->cx;s->key_cy=s->cy;s->key_radius=s->cell_radius;s->key_height=s->height;s->key_region=s->region;s->key_flying=s->flying;s->tree_stamp=work->tree_stamp;s->valid=1;
}

static int source_point(const SightSource *s,int x,int y){
    int dx=x/CELL-s->cx,dy=y/CELL-s->cy;
    if(dx < -s->cell_radius || dx>s->cell_radius || dy < -s->cell_radius || dy>s->cell_radius || !bit(s->mask,(dy+MAX_RADIUS)*LOCAL_SIDE+dx+MAX_RADIUS))return 0;
    if(s->bonus>0 && in_range(s->x,s->y,x,y,s->radius+s->bonus)){
        long long vx=x-s->x,vy=y-s->y,dot=vx*s->heading_x+vy*s->heading_y,cross=vx*s->heading_y-vy*s->heading_x;
        if(!vx && !vy)return 1;
        if(cross<0)cross=-cross;
        return dot>0 && cross*1000000<=dot*2747477;
    }
    return !s->bonus && in_range(s->x,s->y,x,y,s->radius);
}
static unsigned root32(unsigned value){
    unsigned result=0,part=1u<<30;
    while(part>value)part>>=2;
    while(part){if(value>=result+part){value-=result+part;result=(result>>1)+part;}else result>>=1;part>>=2;}
    return result;
}
static void merge_span(uint32_t *dst,const uint32_t *src,int dst_bit,int src_bit,int width){
    while(width>0){unsigned shift=(unsigned)src_bit&31u,to=(unsigned)dst_bit&31u,value;int take=32-(int)to;
        if(take>width)take=width;
        value=src[(unsigned)src_bit>>5]>>shift;
        if(shift && take>32-(int)shift)value|=src[((unsigned)src_bit>>5)+1]<<(32-shift);
        if(take<32)value&=(1u<<take)-1u;
        dst[(unsigned)dst_bit>>5]|=value<<to;
        src_bit+=take;dst_bit+=take;width-=take;
    }
}
static SightReach *reach_entry(unsigned budget){
    SightReach *oldest=&work->reach[0];int i;
    if(++work->reach_age==0){for(i=0;i<REACH_ENTRIES;++i)work->reach[i].age=0;work->reach_age=1;}
    for(i=0;i<REACH_ENTRIES;++i){SightReach *r=&work->reach[i];
        if(r->age && r->budget==budget){r->age=work->reach_age;return r;}
        if(r->age<oldest->age)oldest=r;
    }
    oldest->budget=budget;oldest->age=work->reach_age;zero(oldest->valid,sizeof(oldest->valid));return oldest;
}
static unsigned reach_value(SightReach *r,unsigned dy,unsigned square){
    unsigned value;
    if(dy>=REACH_VALUES)return root32(r->budget-square);
    if(bit(r->valid,(int)dy))return r->value[dy];
    value=root32(r->budget-square);r->value[dy]=(uint16_t)value;set(r->valid,(int)dy);return value;
}
static void merge_circle(const SightSource *s,int miny,int maxy){
    unsigned budget=s->radius>=23165?2u*4095u*4095u:(unsigned)s->radius*(unsigned)s->radius>>4;
    SightReach *cached=reach_entry(budget);int y;
    /* This is exactly the former per-cell radius test at cell centers. The
     * span endpoints use integer sqrt and outward ceil/inward floor, then OR
     * the cached LOS mask in word fragments. No circular radius is widened. */
    for(y=miny;y<=maxy;++y){int dy=y*CELL+CELL/2-s->y,reach,left,right;
        unsigned vertical=(unsigned)(dy*dy);
        if(vertical>budget)continue;
        reach=(int)reach_value(cached,(unsigned)(dy<0?-dy:dy),vertical);
        left=s->x-reach-CELL/2;right=s->x+reach-CELL/2;
        left=left>0?(left+CELL-1)/CELL:0;
        if(right<0)continue;
        right/=CELL;if(right>=GRID)right=GRID-1;
        if(left<s->cx-s->cell_radius)left=s->cx-s->cell_radius;
        if(right>s->cx+s->cell_radius)right=s->cx+s->cell_radius;
        if(left<=right)merge_span(work->current[s->team],s->mask,y*GRID+left,(y-s->cy+MAX_RADIUS)*LOCAL_SIDE+left-s->cx+MAX_RADIUS,right-left+1);
    }
}
static void source_add(int index,int team,int x,int y,int radius,int bonus,int hx,int hy,int flying){
    SightSource *s=&work->source[index];int xx,yy,minx,maxx,miny,maxy;
    if((unsigned)team>=2 || (unsigned)x>=DOTA_WORLD || (unsigned)y>=DOTA_WORLD || radius<=0)return;
    s->active=1;s->team=team;s->x=x;s->y=y;s->cx=x/CELL;s->cy=y/CELL;s->radius=radius;s->bonus=bonus;s->heading_x=hx;s->heading_y=hy;s->flying=flying;
    s->height=height_level(s->cy*GRID+s->cx);s->region=dota_roshan_region_at(x,y);
    s->cell_radius=(radius+bonus+4*CELL-1)/(4*CELL)+1;if(s->cell_radius>MAX_RADIUS)s->cell_radius=MAX_RADIUS;
    shadow(s);work->active[work->active_count++]=(unsigned short)index;
    minx=s->cx-s->cell_radius;maxx=s->cx+s->cell_radius;miny=s->cy-s->cell_radius;maxy=s->cy+s->cell_radius;
    if(minx<0)minx=0;
    if(miny<0)miny=0;
    if(maxx>=GRID)maxx=GRID-1;
    if(maxy>=GRID)maxy=GRID-1;
    if(!bonus)merge_circle(s,miny,maxy);
    else for(yy=miny;yy<=maxy;++yy)for(xx=minx;xx<=maxx;++xx)if(source_point(s,xx*CELL+CELL/2,yy*CELL+CELL/2))set(work->current[team],yy*GRID+xx);
    set(work->current[team],s->cy*GRID+s->cx);
}
static int team_point(int team,int x,int y){
    int k,region;
    if(!work || (unsigned)team>=2 || (unsigned)x>=DOTA_WORLD || (unsigned)y>=DOTA_WORLD)return 0;
    region=dota_roshan_region_at(x,y);
    for(k=0;k<work->active_count;++k){const SightSource *s=&work->source[work->active[k]];
        if(s->team==team && s->region==region && source_point(s,x,y))return 1;}
    return 0;
}
static void refresh(const DotaGame *g){
    int i,team,night=dota_match_is_night(g);
    if(!work){zero(visible,sizeof(visible));cached_game=g;cached_tick=g->tick;return;}
    if(work->owner!=g){dota_sight_reset();work->owner=g;}
    zero(work->current,sizeof(work->current));work->active_count=0;dynamic_trees(g);
    for(i=0;i<SOURCE_COUNT;++i)work->source[i].active=0;
    for(i=0;i<DOTA_MAX_UNITS;++i){const DotaUnit *u=&g->units[i];int radius,bonus=0,hx=0,hy=0;
        if(!live(u) || (unsigned)u->team>=2 || !(radius=dota_rules_unit_vision_source(g,i)))continue;
        if(i<DOTA_HERO_COUNT){bonus=dota_roster_vision_bonus(g,i);hx=g->roster.hero[i].vision_dx;hy=g->roster.hero[i].vision_dy;}
        source_add(i,u->team,u->x,u->y,radius,bonus,hx,hy,0);
    }
    for(team=0;team<2;++team)source_add(DOTA_MAX_UNITS+team,team,dota_fountains[team].x,dota_fountains[team].y,unit_fountain_vision[0],0,0,0,0);
    for(i=0;i<DOTA_WATCHERS;++i){const DotaPoint *p=&dota_objective_positions[DOTA_OBJECTIVE_WATCHER_FIRST+i];if(g->objectives.watcher_until[i]>g->tick)source_add(WATCHER_FIRST+i,g->objectives.watcher_owner[i],p->x,p->y,unit_watcher_vision[night],0,0,0,0);}
    for(i=0;i<DOTA_ROSTER_FIELDS;++i){const DotaRosterField *f=&g->roster.field[i];if(f->until>=g->tick && f->starts_at<=g->tick && (g->roster.field_mask&(1u<<i)) && (unsigned)f->source<DOTA_MAX_UNITS)source_add(FIELD_FIRST+i,g->units[f->source].team,f->x,f->y,f->radius,0,0,0,1);}
    for(i=0;i<8;++i){const DotaItemTrap *t=&g->item_world.trap[i];if(t->until>g->tick && t->item==DOTA_ITEM_GEM && (unsigned)t->owner<DOTA_HERO_COUNT)source_add(GEM_FIRST+i,g->units[t->owner].team,t->x,t->y,300,0,0,0,0);}
    for(team=0;team<2;++team){for(i=0;i<GRID_WORDS;++i)work->explored[team][i]|=work->current[team][i];
        for(i=0;i<DOTA_MAX_UNITS;++i){const DotaUnit *u=&g->units[i];visible[team][i]=(unsigned char)(live(u) && (u->team==team || team_point(team,u->x,u->y)));}}
    cached_game=g;cached_tick=g->tick;
}
static void ensure(const DotaGame *g){if(g && !dota_sight_is_current(g))refresh(g);}
int dota_sight_team_position_visible(const DotaGame *g,int team,int x,int y){if(!g)return 0;ensure(g);return team_point(team,x,y);}
int dota_sight_position_visible(const DotaGame *g,int observer,int x,int y){if(!g || (unsigned)observer>=DOTA_MAX_UNITS)return 0;return dota_sight_team_position_visible(g,g->units[observer].team,x,y);}
int dota_sight_team_visible(const DotaGame *g,int observer,int target){
    int team;if(!g || (unsigned)observer>=DOTA_MAX_UNITS || (unsigned)target>=DOTA_MAX_UNITS)return 0;
    team=g->units[observer].team;ensure(g);
    if(team==g->units[target].team)return live(&g->units[target]);
    if((unsigned)team>=2){const DotaUnit *a=&g->units[observer],*b=&g->units[target];return live(a) && live(b) && in_range(a->x,a->y,b->x,b->y,dota_rules_unit_vision_source(g,observer)) && dota_pit_same_region(a->x,a->y,b->x,b->y);}
    return visible[team][target];
}
int dota_sight_fog_state(const DotaGame *g,int team,int x,int y){
    int cell;if(!g || !work || (unsigned)team>=2 || (unsigned)x>=DOTA_WORLD || (unsigned)y>=DOTA_WORLD)return 0;
    ensure(g);cell=(y/CELL)*GRID+x/CELL;return bit(work->current[team],cell)?2:bit(work->explored[team],cell)?1:0;
}
