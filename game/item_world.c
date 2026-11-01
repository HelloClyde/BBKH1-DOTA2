#include "item_world.h"
#include "items.h"
#include "items_ids.h"
#include "items_params.h"
#include "items_actions.h"
#include "navigation.h"
#include "turbo.h"
#include "neutral.h"
#include "items_internal.h"
#include "vision.h"
#include "roster_heroes.h"
#include "jungle.h"
#include "item_npc_data.h"
#include "jungle_npc_data.h"
#include "lane_creep_data.h"
#include "sight.h"
#include "ward_placement_data.h"
enum { NPC_EFFECT_PURGE=1,NPC_EFFECT_ICEFIRE,NPC_EFFECT_DESECRATE,NPC_EFFECT_PETRIFY,NPC_EFFECT_SLAM,NPC_EFFECT_FRENZY };
static void npc_tick(DotaGame *g,int index);
static void npc_effect_tick(DotaGame *g);
static int npc_begin(DotaGame *g,int index,int target);
static void smoke_tick(DotaGame *g){
    int i,j,radius=0;
    for(i=0;i<DOTA_ITEMS_EFFECTS;++i){const DotaItemEffect *e=&g->items.effects[i];int target=e->target;
        if(!e->ticks || e->kind!=IE_INVISIBLE || e->value!=2 || (e->tag&32767)!=DOTA_ITEM_SMOKE_OF_DECEIT || (unsigned)target>=DOTA_MAX_UNITS || !g->units[target].active || e->generation!=g->units[target].generation)continue;
        if(!radius)radius=dota_items_parameter(DOTA_ITEM_SMOKE_OF_DECEIT,IP_VISIBILITY_RADIUS,-1,102500)/100;
        for(j=0;j<DOTA_MAX_UNITS;++j){const DotaUnit *u=&g->units[j];long long dx,dy;
            if(!u->active || u->hp<=0 || u->team==g->units[target].team || (unsigned)u->team>=2 || (u->type!=DOTA_HERO && u->type!=DOTA_TOWER))continue;
            dx=u->x-g->units[target].x;dy=u->y-g->units[target].y;
            if((dx*dx+dy*dy)*16<=(long long)radius*radius){dota_items_remove_effect(g,i);break;}
        }
    }
}
static void clear(void*p,int n){
    unsigned char*b=p;
    while(n--)*b++=0;
}
static int min_i(int a,int b){
    return a<b?a:b;
}
static int max_i(int a,int b){
    return a>b?a:b;
}
static int live(const DotaGame*g,int i){
    return i>=0&&i<DOTA_MAX_UNITS&&g->units[i].active&&g->units[i].hp>0;
}
static int sq(int x,int y,int xx,int yy){
    int dx=x-xx,dy=y-yy;
    return dx*dx+dy*dy;
}
static int range(int x,int y,int xx,int yy,int source){
    int w=(source+3)/4;
    return sq(x,y,xx,yy)<=w*w;
}
int dota_item_world_ward_placeable(const DotaGame*g,int owner,int x,int y){
    int polygon,px,py;
    if(!g || (unsigned)owner>=DOTA_HERO_COUNT || !live(g,owner) || g->units[owner].type!=DOTA_HERO || (unsigned)g->units[owner].team>=2 || (unsigned)x>=DOTA_WORLD || (unsigned)y>=DOTA_WORLD)return 0;
    px=x*65536;py=y*65536;
    for(polygon=0;polygon<DOTA_WARD_FORBIDDEN_HULLS;++polygon){
        const int *bounds=dota_ward_forbidden_bounds_q16[polygon];
        int first=dota_ward_forbidden_offsets[polygon],end=dota_ward_forbidden_offsets[polygon+1],i,inside=1;
        if(px<bounds[0] || px>bounds[1] || py<bounds[2] || py>bounds[3])continue;
        for(i=first;i<end;++i){
            int next=i+1<end?i+1:first;
            long long dx=dota_ward_forbidden_q16[next][0]-dota_ward_forbidden_q16[i][0];
            long long dy=dota_ward_forbidden_q16[next][1]-dota_ward_forbidden_q16[i][1];
            if(dx*(py-dota_ward_forbidden_q16[i][1])-dy*(px-dota_ward_forbidden_q16[i][0])<0){inside=0;break;}
        }
        if(inside)return 0;
    }
    return 1;
}
static int param(int id,int key,int fallback){
    return dota_items_parameter(id,key,-1,fallback*100)/100;
}
static int ticks(int id,int key,int fallback){
    return dota_items_parameter(id,key,-1,fallback*5)/5;
}
static int teleport_level(const DotaGame*g,int h){
    int i,level=0;
    for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS;i++){
        int id=dota_items_equipped_item(g,h,i);
        if(id==DOTA_ITEM_TRAVEL_BOOTS_2)return 2;
        if(id==DOTA_ITEM_TRAVEL_BOOTS)level=1;
    }
    return level;
}
static int teleport_anchor(const DotaGame*g,int h,int level,int t){
    const DotaUnit*u;
    if(!g||(unsigned)h>=10u||!live(g,h)||g->units[h].type!=DOTA_HERO||(unsigned)g->units[h].team>=2u||t==h)return 0;
    if(t==DOTA_TELEPORT_FOUNTAIN_TARGET(g->units[h].team))return 1;
    if(!live(g,t))return 0;
    u=&g->units[t];
    if(u->team!=g->units[h].team)return 0;
    if(u->type==DOTA_TOWER||u->type==DOTA_BASE||u->type==DOTA_BUILDING)return 1;
    if(!level)return 0;
    if(u->type==DOTA_CREEP||u->type==DOTA_NEUTRAL||u->type==DOTA_ANCIENT)return 1;
    if(u->type==DOTA_HERO)return level>=2;
    if(u->type==DOTA_SUMMON&&t>=DOTA_ITEM_UNIT_FIRST){
        int kind=g->item_world.unit[t-DOTA_ITEM_UNIT_FIRST].kind;
        return kind==ITEM_UNIT_SUMMON||kind==ITEM_UNIT_DOMINATED||(level>=2&&kind==ITEM_UNIT_ILLUSION);
    }
    return 0; /* Observer/sentry/healing wards, fish, anchors and bosses. */
}
int dota_items_is_teleport(int item){return item==DOTA_ITEM_TPSCROLL;}
int dota_items_teleport_target_valid(const DotaGame*g,int h,int item,int t){
    return dota_items_is_teleport(item)&&teleport_anchor(g,h,teleport_level(g,h),t);
}
int dota_items_teleport_target(const DotaGame*g,int h,int item,int x,int y,int target){
    int i,best=0x7fffffff,nearest=-1,level;
    if(!g||!dota_items_is_teleport(item)||(unsigned)h>=10u||!live(g,h))return -1;
    level=teleport_level(g,h);
    if(target>=0)return teleport_anchor(g,h,level,target)?target:-1;
    if(x<0||x>=DOTA_WORLD||y<0||y>=DOTA_WORLD)return -1;
    for(i=0;i<DOTA_MAX_UNITS;i++)if(teleport_anchor(g,h,level,i)){
        int d=sq(x,y,g->units[i].x,g->units[i].y);
        if(d<best){best=d;nearest=i;}
    }
    if((unsigned)g->units[h].team<2u){
        DotaPoint p=dota_fountains[g->units[h].team];int d=sq(x,y,p.x,p.y);
        if(d<best){best=d;nearest=DOTA_TELEPORT_FOUNTAIN_TARGET(g->units[h].team);}
    }
    return nearest>=0&&best<=200*200?nearest:-1; /* maximum_distance800Source. */
}
int dota_items_teleport_cooldown(const DotaGame*g,int h){
    return g&&(unsigned)h<10u?max_i(0,g->item_world.teleport.ready_tick[h]-g->tick):0;
}
static int teleport_mobile(const DotaUnit*u){
    return u->type!=DOTA_TOWER&&u->type!=DOTA_BASE&&u->type!=DOTA_BUILDING;
}
static DotaPoint teleport_anchor_point(const DotaGame*g,int target){
    DotaPoint p;
    if(target>=DOTA_MAX_UNITS)return dota_fountains[target-DOTA_MAX_UNITS];
    p.x=g->units[target].x;p.y=g->units[target].y;return p;
}
static int teleport_mobile_target(const DotaGame*g,int target){
    return target<DOTA_MAX_UNITS&&teleport_mobile(&g->units[target]);
}
static int teleport_disabled(const DotaGame*g,int h){
    int i;const DotaUnit*u=&g->units[h];const DotaRosterTarget*r=&g->roster.target[h];
    if(u->stun_ticks||u->taunt_ticks||dota_roster_stun(g,h)||dota_items_rooted(g,h))return 1;
    if(!dota_items_debuff_immune(g,h)){
        if(g->extra.hero[h].leash_until>g->tick)return 1;
        if(r->generation==u->generation&&r->tether_ticks&&live(g,r->tether_anchor))return 1;
    }
    /* Hex is stored as silence/disarm/slow by the existing item engine; an
     * ordinary silence must not be mistaken for its channel interruption. */
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        const DotaItemEffect*e=&g->items.effects[i];
        if(e->ticks&&e->target==h&&e->generation==u->generation&&e->kind==IE_SILENCE&&(e->tag&32767)==DOTA_ITEM_SHEEPSTICK)return 1;
    }
    return 0;
}
static int teleport_valid_channel(const DotaGame*g,int h,const DotaItemChannel*c){
    const DotaUnit*u=&g->units[h];int level=c->item==DOTA_ITEM_TRAVEL_BOOTS_2?2:(c->item==DOTA_ITEM_TRAVEL_BOOTS?1:0);
    return c->until>0&&c->kind==ITEM_ACTION_TELEPORT&&live(g,h)&&u->generation==c->generation
        &&!teleport_disabled(g,h)
        &&u->x==c->origin_x&&u->y==c->origin_y&&(g->tick>=c->until||u->channel_ticks>0)
        &&teleport_anchor(g,h,level,c->target)&&(c->target>=DOTA_MAX_UNITS?c->target_generation==0:g->units[c->target].generation==c->target_generation);
}
int dota_items_teleport_channel(const DotaGame*g,int h,int*x,int*y,int*remaining,int*total){
    const DotaItemChannel*c;DotaPoint p;int mobile;
    if(!g||(unsigned)h>=10u)return 0;
    c=&g->item_world.channel[h];
    if(!teleport_valid_channel(g,h,c))return 0;
    p=teleport_anchor_point(g,c->target);mobile=teleport_mobile_target(g,c->target);
    if(x)*x=c->x+(mobile?p.x:0);
    if(y)*y=c->y+(mobile?p.y:0);
    if(remaining)*remaining=max_i(0,c->until-g->tick);
    if(total)*total=c->until-c->started_tick;
    return 1;
}
void dota_items_cancel_teleport(DotaGame*g,int h){
    DotaItemChannel*c;if(!g||(unsigned)h>=10u)return;c=&g->item_world.channel[h];
    if(c->kind==ITEM_ACTION_TELEPORT&&c->until){c->until=0;g->units[h].channel_ticks=0;}
}
static int teleport_sqrt(int value){
    unsigned n=(unsigned)value,res=0,bit=1u<<30;
    while(bit>n)bit>>=2;
    while(bit){if(n>=res+bit){n-=res+bit;res=(res>>1)+bit;}else res>>=1;bit>>=2;}
    return (int)res;
}
static int teleport_landing(const DotaGame*g,int h,int target,int x,int y,int*nx,int*ny){
    DotaPoint p=teleport_anchor_point(g,target);int dx=x-p.x,dy=y-p.y,len,center=!dx&&!dy;
    if(!dx&&!dy){dx=g->units[h].x-p.x;dy=g->units[h].y-p.y;if(!dx&&!dy)dx=1;}
    len=max_i(1,teleport_sqrt(dx*dx+dy*dy));
    /* Respect Source's70..800 landing annulus, then find legal H1 ground.
     * The normal structure collision masks remain in force at both ends. */
    if(center||len<18||len>200){int radius=center||len<18?18:200;dx=dx*radius/len;dy=dy*radius/len;}
    if(!dota_nav_nearest(p.x+dx,p.y+dy,128,nx,ny))return 0;
    return sq(*nx,*ny,p.x,p.y)<=200*200;
}
static DotaItemWorldUnit*world_unit(DotaGame*g,int i){
    return i>=DOTA_ITEM_UNIT_FIRST&&i<DOTA_MAX_UNITS?&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST]:0;
}
static int available(const DotaGame*g){
    int i,n=0;
    for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;i++)if(!live(g,i))n++;
    return n;
}
static int new_unit(DotaGame*g,int owner,int kind,int item,int duration,int x,int y){
    int i,nx,ny;
    DotaUnit*u;
    DotaItemWorldUnit*s;
    if(kind==ITEM_UNIT_OBSERVER||kind==ITEM_UNIT_SENTRY){
        if(!dota_item_world_ward_placeable(g,owner,x,y))return -1;
        nx=x;ny=y;
    }else if(!dota_nav_nearest(x,y,160,&nx,&ny))return -1;
    for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;i++)if(!live(g,i))break;
    if(i==DOTA_MAX_UNITS)return -1;
    u=&g->units[i];
    {
        int generation=u->generation+1;
        clear(u,sizeof(*u));
        u->generation=generation;
    }
    u->active=1;
    u->type=DOTA_SUMMON;
    u->team=g->units[owner].team;
    u->hero_id=g->units[owner].hero_id;
    u->x=nx;
    u->y=ny;
    u->target=-1;
    u->hp=u->max_hp=200;
    u->level=1;
    {
        int j;
        for(j=0;j<6;j++)u->inventory[j]=-1;
    }
    s=world_unit(g,i);
    clear(s,sizeof(*s));
    s->owner=owner;
    s->generation=g->units[owner].generation;
    s->kind=kind;
    s->until=duration?g->tick+duration:0;
    s->damage_percent=100;
    s->incoming_percent=100;
    s->source_item=item;
 s->source_type=0;s->source_team=g->units[owner].team;
    dota_sight_invalidate();
    return i;
}
/* Original rune tooltip: two75s illusions,35% outgoing,200% melee/300%
 * ranged incoming. Keep the existing bounded owned-unit simulation. */
int dota_item_world_rune_illusions(DotaGame*g,int h,int duration){
    int i,j,indices[2],nx[2],ny[2];DotaUnit *u;DotaItemStats stats;
    if(!g || h<0 || h>=10 || !live(g,h) || available(g)<2 || duration<=0)return 0;
    u=&g->units[h];
    for(j=0;j<2;++j)if(!dota_nav_nearest(u->x+24*(j?1:-1),u->y+18,160,&nx[j],&ny[j]))return 0;
    dota_items_stats(g,h,&stats);
    for(j=0;j<2;++j){
        DotaItemWorldUnit *s;i=new_unit(g,h,ITEM_UNIT_ILLUSION,-1,duration,nx[j],ny[j]);
        if(i<0){while(j-->0)dota_item_world_on_death(g,indices[j],-1);return 0;}
        indices[j]=i;s=world_unit(g,i);s->damage_percent=35;s->incoming_percent=dota_attack_style(u)==DOTA_ATTACK_MELEE?200:300;
        g->units[i].damage=max_i(0,u->damage-stats.value[ITEM_STAT_DAMAGE])*35/100;
        g->units[i].armor=u->armor;g->units[i].range=u->range;g->units[i].speed=u->speed;
        g->units[i].max_hp=u->max_hp;g->units[i].hp=u->hp;
        g->units[i].max_mana=u->max_mana;g->units[i].mana=u->mana;
    }
    return 1;
}
void dota_item_world_init(DotaGame*g){
    int i;
    clear(&g->item_world,sizeof(g->item_world));
    g->item_world.last_tick=-1;
    for(i=0;i<DOTA_ITEM_UNIT_COUNT;i++)g->item_world.unit[i].owner=-1;
    dota_nav_set_item_trees(g->item_world.tree,DOTA_ITEM_TREES);
}
int dota_item_world_drop_mode(DotaGame*g,int owner,int item,int x,int y,int mode){int i;for(i=0;i<16;i++)if(!g->item_world.drop[i].active){DotaItemDrop*d=&g->item_world.drop[i];d->active=1;d->owner=owner;d->item=item;d->x=x;d->y=y;d->ready_tick=g->tick+10;d->spell_mode=mode;dota_rules_item_effect(g,DOTA_FX_GOLD,owner>=0&&owner<10?g->units[owner].team:2,x,y,x,y,8,40);return 1;}return 0;}
int dota_item_world_owner(const DotaGame*g,int unit){
    const DotaItemWorldUnit*s;
    if(unit<DOTA_ITEM_UNIT_FIRST||unit>=DOTA_MAX_UNITS)return -1;
    s=&g->item_world.unit[unit-DOTA_ITEM_UNIT_FIRST];
    /* Hero slots are stable across lives. Owned wards/summons survive owner
     * death and respawn; only their own duration/death removes them. */
    return s->owner>=0&&s->owner<10&&g->units[s->owner].active?s->owner:-1;
}
int dota_item_world_on_death(DotaGame*g,int victim,int source){
    DotaItemWorldUnit*s=world_unit(g,victim);
    if(!s)return 0;
    g->item_world.native_mask&=~(1u<<(victim-DOTA_ITEM_UNIT_FIRST));
    if(s->kind==ITEM_UNIT_OBSERVER&&source>=0&&source<10){
        g->units[source].gold+=dota_turbo_gold_reward(100);
        dota_rules_item_xp(g,source,100);
    }
    if(s->kind==ITEM_UNIT_DOMINATED&&source>=0&&source<10&&g->units[source].team!=g->units[victim].team){
        int h,n=0,xp=npc_definition(s->native_id)->xp;
        g->units[source].gold+=dota_turbo_gold_reward(param(s->source_item,IP_BOUNTY_GOLD,100));
        for(h=0;h<10;h++)if(live(g,h)&&g->units[h].team==g->units[source].team&&range(g->units[h].x,g->units[h].y,g->units[victim].x,g->units[victim].y,1500))n++;
        if(n){int rem=xp%n;for(h=0;h<10;h++)if(live(g,h)&&g->units[h].team==g->units[source].team&&range(g->units[h].x,g->units[h].y,g->units[victim].x,g->units[victim].y,1500)){dota_rules_item_xp(g,h,xp/n+(rem>0));if(rem)rem--;}}
    }
    g->units[victim].hp=0;
    g->units[victim].active=0;
    /* Keep the source owner for an already applied native DOT/projectile.
     * Reuse is generation guarded and new_unit clears this record. */
    s->until=0;
    dota_sight_invalidate();
    return 1;
}
int dota_item_world_incoming_from(DotaGame*g,int victim,int source,int amount,int magical,int attack){
    DotaItemWorldUnit*s=world_unit(g,victim);
    if(s && (s->kind==ITEM_UNIT_OBSERVER || s->kind==ITEM_UNIT_SENTRY)){
        /* The core already checks the shooter's generation. An in-flight
         * attack remains valid after its original shooter dies. */
        if(amount<=0 || magical || !attack || (unsigned)source>=DOTA_MAX_UNITS)return 0;
        return g->units[source].type==DOTA_HERO?100:25;
    }
    return dota_item_world_incoming(g,victim,amount);
}
int dota_item_world_incoming(DotaGame*g,int victim,int amount){
    DotaItemWorldUnit*s=world_unit(g,victim);
    if(!s)return amount;
    if(s->kind==ITEM_UNIT_OBSERVER||s->kind==ITEM_UNIT_SENTRY)return amount>0?100:0;
    return amount*s->incoming_percent/100;
}
int dota_item_world_smoked(const DotaGame*g,int target){
    int word;if(!g || (unsigned)target>=DOTA_HERO_COUNT)return 0;
    for(word=0;word<DOTA_ITEMS_EFFECT_WORDS;++word){unsigned bits=g->items.effect_active[word];int index=word*32;
        while(bits){if(bits&1u){const DotaItemEffect *e=&g->items.effects[index];
            if(e->ticks && e->target==target && e->generation==g->units[target].generation && e->kind==IE_INVISIBLE && e->value==2 && (e->tag&32767)==DOTA_ITEM_SMOKE_OF_DECEIT)return 1;
        }bits>>=1;++index;}
    }
    return 0;
}
int dota_item_world_visible(const DotaGame*g,int observer,int target){
    int i,team;
    if(!live(g,observer)||!live(g,target))return -1;
    team=g->units[observer].team;
    if(team==g->units[target].team)return -1;
    /* Detection never grants ground vision or pierces Smoke of Deceit. */
    if(!dota_sight_team_position_visible(g,team,g->units[target].x,g->units[target].y))return 0;
    if(dota_item_world_smoked(g,target))return 0;
    for(i=0;i<8;i++){const DotaItemTrap*t=&g->item_world.trap[i];if(t->until>g->tick&&t->item==DOTA_ITEM_GEM&&t->owner>=0&&t->owner<10&&g->units[t->owner].team==team&&range(t->x,t->y,g->units[target].x,g->units[target].y,300)&&dota_pit_same_region(t->x,t->y,g->units[target].x,g->units[target].y))return 1;}
    for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&g->units[i].team==team){
        const DotaItemWorldUnit*s=&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST];
        if(s->kind==ITEM_UNIT_SENTRY&&range(g->units[i].x,g->units[i].y,g->units[target].x,g->units[target].y,1050)&&dota_pit_same_region(g->units[i].x,g->units[i].y,g->units[target].x,g->units[target].y))return 1;
    }
    for(i=0;i<10;i++)if(live(g,i)&&g->units[i].team==team){
        int j;
        for(j=0;j<6;j++)if(dota_items_equipped_item(g,i,j)==DOTA_ITEM_GEM&&range(g->units[i].x,g->units[i].y,g->units[target].x,g->units[target].y,900)&&dota_pit_same_region(g->units[i].x,g->units[i].y,g->units[target].x,g->units[target].y))return 1;
    }
    if(target>=DOTA_ITEM_UNIT_FIRST){
        const DotaItemWorldUnit*s=&g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST];
        if(s->kind==ITEM_UNIT_OBSERVER||s->kind==ITEM_UNIT_SENTRY)return 0;
    }
    if(dota_items_invisible(g,target))return 0;
    return -1;
}
int dota_item_world_shared_vision(const DotaGame*g,int observer,int target){
    if(!live(g,observer)||!live(g,target))return 0;
    return dota_sight_team_position_visible(g,g->units[observer].team,g->units[target].x,g->units[target].y);
}
void dota_item_world_update_unit(DotaGame*g,int i){
    DotaUnit*u;
    DotaItemWorldUnit*s;
    int owner,t,j,best=0x7fffffff;
    if(!live(g,i)||(s=world_unit(g,i))==0)return;
    u=&g->units[i];
    owner=dota_item_world_owner(g,i);
    if(owner<0||(s->until&&g->tick>=s->until)){
        dota_item_world_on_death(g,i,-1);
        return;
    }
    if(s->kind==ITEM_UNIT_OBSERVER||s->kind==ITEM_UNIT_SENTRY)return;
    if(s->kind==ITEM_UNIT_DOMINATED){npc_tick(g,i);if(!live(g,i))return;}
    if(u->stun_ticks>0||u->channel_ticks>0)return;
    t=u->target;
    if(!live(g,t)||g->units[t].team==u->team||!dota_unit_visible(g,i,t)||!range(u->x,u->y,g->units[t].x,g->units[t].y,1200)){
        t=-1;
        for(j=0;j<DOTA_MAX_UNITS;j++)if(live(g,j)&&g->units[j].team!=u->team&&g->units[j].type!=DOTA_BASE&&dota_unit_visible(g,i,j)){
            int d=sq(u->x,u->y,g->units[j].x,g->units[j].y);
            if(d<best&&range(u->x,u->y,g->units[j].x,g->units[j].y,700)){
                best=d;
                t=j;
            }
        }
    }
    if(t>=0){
        u->target=t;
        if(s->kind==ITEM_UNIT_DOMINATED && npc_begin(g,i,t))return;
        if(range(u->x,u->y,g->units[t].x,g->units[t].y,u->range))dota_rules_item_attack_once(g,i,t);
        else dota_rules_item_move_order(g,i,g->units[t].x,g->units[t].y);
    }
    else if(range(u->x,u->y,g->units[owner].x,g->units[owner].y,300)==0)dota_rules_item_move_order(g,i,g->units[owner].x+24,g->units[owner].y+24);
}
int dota_rules_item_primitive(DotaGame*g,int encoded,int h,int target,int x,int y,int amount,int duration){
    int action=encoded&255,id=(unsigned)encoded>>8,i,j,n,old;
    DotaUnit*u;
    DotaItemWorldUnit*s;
    if(!live(g,h)||h>=10)return 0;
    u=&g->units[h];
    switch(action){
        case ITEM_ACTION_TREE_PLANT:if(!range(u->x,u->y,x,y,400)||!dota_nav_walkable(x,y))return 0;
        for(i=0;i<DOTA_ITEM_TREES;i++)if(!g->item_world.tree[i].until||g->item_world.tree[i].until<=g->tick)break;
        if(i==DOTA_ITEM_TREES)return 0;
        g->item_world.tree[i].x=x;
        g->item_world.tree[i].y=y;
        g->item_world.tree[i].until=g->tick+duration;
        g->item_world.tree[i].removed=0;
        g->item_world.tree[i].source_item=id;
        dota_sight_invalidate();
        return 1;
        case ITEM_ACTION_TREE:if(!range(u->x,u->y,x,y,id==DOTA_ITEM_TANGO?165:350))return 0;
        for(i=0;i<DOTA_ITEM_TREES;i++){
            DotaItemTree*t=&g->item_world.tree[i];
            if(t->until>g->tick&&!t->removed&&range(x,y,t->x,t->y,96)){
                t->removed=1;
                dota_sight_invalidate();
                return t->source_item==DOTA_ITEM_BRANCHES?2:1;
            }
        }
        /* The map's baked source trees cannot be removed visually. Reject the cast. */return 0;
        case ITEM_ACTION_WARD:if(!range(u->x,u->y,x,y,500+dota_items_range_bonus(g,h,0)))return 0;
        i=new_unit(g,h,amount?ITEM_UNIT_SENTRY:ITEM_UNIT_OBSERVER,id,duration,x,y);
        if(i<0)return 0;
        g->units[i].hp=g->units[i].max_hp=200;
        g->units[i].damage=g->units[i].range=g->units[i].speed=0;
        return 1;
        case ITEM_ACTION_REVEAL:
        for(i=0;i<8;i++)if(g->item_world.trap[i].until<=g->tick)break;
        if(i==8)return 0;
        g->item_world.trap[i].until=g->tick+duration;g->item_world.trap[i].owner=h;g->item_world.trap[i].generation=u->generation;g->item_world.trap[i].item=id;g->item_world.trap[i].x=id==DOTA_ITEM_DUST?u->x:x;g->item_world.trap[i].y=id==DOTA_ITEM_DUST?u->y:y;
        dota_sight_invalidate();
        return 1;
        case ITEM_ACTION_TELEPORT:{
            DotaItemChannel*c=&g->item_world.channel[h];
            int nearest,nx,ny,count=0,mobile;DotaPoint p;
            if(c->until>g->tick||u->channel_ticks||teleport_disabled(g,h)||dota_items_teleport_cooldown(g,h))return 0;
            nearest=dota_items_teleport_target(g,h,DOTA_ITEM_TPSCROLL,x,y,target);
            if(nearest<0)return 0;
            p=teleport_anchor_point(g,nearest);
            if(target>=0){x=p.x;y=p.y;}
            if(!teleport_landing(g,h,nearest,x,y,&nx,&ny))return 0;
            mobile=teleport_mobile_target(g,nearest);
            if(id==DOTA_ITEM_TPSCROLL){
                /* Native timer manager: same-team destinations within1600
                 * Source; first prior TP adds2s, each additional adds0.5s. */
                for(i=0;i<10;i++){
                    for(j=0;j<DOTA_TELEPORT_HISTORY;j++){
                        DotaTeleportRecent*r=&g->item_world.teleport.recent[i][j];
                        if(r->until>g->tick&&g->units[i].team==u->team&&range(nx,ny,r->x,r->y,1600))count++;
                    }
                }
                if(count)duration+=40+(count-1)*10;
                if(!range(nx,ny,dota_fountains[u->team].x,dota_fountains[u->team].y,2200)){
                    int oldest=0;DotaTeleportRecent*r;
                    for(i=1;i<DOTA_TELEPORT_HISTORY;i++)if(g->item_world.teleport.recent[h][i].until<g->item_world.teleport.recent[h][oldest].until)oldest=i;
                    r=&g->item_world.teleport.recent[h][oldest];
                    r->until=g->tick+25*DOTA_HZ;r->x=nx;r->y=ny;
                }
            }
            c->x=nx-(mobile?p.x:0);
            c->y=ny-(mobile?p.y:0);
            c->until=g->tick+duration;
            c->started_tick=g->tick;
            c->generation=u->generation;
            c->item=id;
            c->kind=action;
            c->origin_x=u->x;
            c->origin_y=u->y;
            c->target=nearest;c->target_generation=nearest<DOTA_MAX_UNITS?g->units[nearest].generation:0;
            u->channel_ticks=duration;
            u->target=-1;u->move_order=0;
            if(h==g->player)g->player_path_count=g->player_path_index=0;
            return 1;
        }
        case ITEM_ACTION_ILLUSION:n=min_i(2,max_i(1,amount));
        if(available(g)<n)return 0;
        for(j=0;j<n;j++){
            i=new_unit(g,h,ITEM_UNIT_ILLUSION,id,duration,u->x+24*(j?1:-1),u->y+18);
            if(i<0)return 0;
            s=world_unit(g,i);
            s->damage_percent=id==DOTA_ITEM_RIFTSHADOW_PRISM?param(id,IP_OUTGOING_DAMAGE,60):param(id,u->range>200?IP_TOOLTIP_DAMAGE_OUTGOING_RANGED:IP_TOOLTIP_DAMAGE_OUTGOING_MELEE,33);
            s->incoming_percent=id==DOTA_ITEM_RIFTSHADOW_PRISM?param(id,IP_INCOMING_DAMAGE,200):param(id,IP_TOOLTIP_DAMAGE_INCOMING_TOTAL_PCT,300);
        {DotaItemStats stats;dota_items_stats(g,h,&stats);g->units[i].damage=max_i(0,u->damage-stats.value[ITEM_STAT_DAMAGE])*s->damage_percent/100;}
            g->units[i].armor=u->armor;
            g->units[i].range=u->range;
            g->units[i].speed=u->speed;
            g->units[i].max_hp=u->max_hp;
            g->units[i].hp=u->hp;
            g->units[i].max_mana=u->max_mana;
            g->units[i].mana=u->mana;
        }
        if(id==DOTA_ITEM_RIFTSHADOW_PRISM)u->hp=max_i(1,u->hp-u->max_hp*param(id,IP_HEALTH_COST,8)/100);
        return 1;
        case ITEM_ACTION_SUMMON:if(available(g)<2)return 0;
        for(j=0;j<2;j++){
            i=new_unit(g,h,ITEM_UNIT_SUMMON,id,duration,u->x+24*(j?1:-1),u->y+20);
            if(i<0)return 0;
            g->units[i].tier=j;
            /* Installed npc_units: level3 Necronomicon warrior/archer. */
            g->units[i].hp=g->units[i].max_hp=j?650:800;
            g->units[i].damage=j?75:77;
            g->units[i].armor=4;
            g->units[i].range=j?550:100;
            g->units[i].speed=j?425:380;
        }
        return 1;
        case ITEM_ACTION_DOMINATE:if(!live(g,target)||g->units[target].team==u->team||(g->units[target].type!=DOTA_NEUTRAL&&g->units[target].type!=DOTA_CREEP&&g->units[target].type!=DOTA_ANCIENT)||dota_item_world_dominate_cooldown(g,h))return 0;
        if((g->units[target].type==DOTA_ANCIENT&&(unsigned)g->units[target].tier>=10)||(g->units[target].type==DOTA_NEUTRAL&&(unsigned)g->units[target].tier>=DOTA_JUNGLE_SPECIES)||(g->units[target].type==DOTA_CREEP&&(unsigned)g->units[target].tier>=2))return 0;
        if(available(g)<1){int own=0;for(j=DOTA_ITEM_UNIT_FIRST;j<DOTA_MAX_UNITS;j++)if(live(g,j)&&g->item_world.unit[j-DOTA_ITEM_UNIT_FIRST].owner==h&&dota_item_world_dominated(g,j))own=1;if(!own)return 0;}
        for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;i++)if(live(g,i)){
            s=world_unit(g,i);
            if(s->owner==h&&s->kind==ITEM_UNIT_DOMINATED)dota_item_world_on_death(g,i,-1);
        }
        i=new_unit(g,h,ITEM_UNIT_DOMINATED,id,0,g->units[target].x,g->units[target].y);
        if(i<0)return 0;
        old=g->units[i].generation;
        s=world_unit(g,i);s->source_type=g->units[target].type;s->source_team=g->units[target].team;
        s->native_id=(unsigned char)(s->source_type==DOTA_NEUTRAL?(g->units[target].tier==2?1:g->units[target].tier==26?2:17+g->units[target].tier):(s->source_type==DOTA_ANCIENT?3+g->units[target].tier:13+2*(s->source_team==1)+(g->units[target].tier!=0)));
        if(s->native_id>46){dota_item_world_on_death(g,i,-1);return 0;}
        s->ability_level=(unsigned char)(1+param(id,IP_CREEP_ABILITY_LEVEL_INCREASE,0));
        g->item_world.native_mask|=1u<<(i-DOTA_ITEM_UNIT_FIRST);
        g->units[i]=g->units[target];
        g->units[i].generation=old;
        g->units[i].type=DOTA_SUMMON;
        g->units[i].team=u->team;
        g->units[i].hp=g->units[i].max_hp=max_i(amount,g->units[i].max_hp);
        g->units[i].damage+=param(id,IP_CREEP_BONUS_DAMAGE,25);
        g->units[i].armor+=param(id,IP_CREEP_BONUS_ARMOR,4);
        g->units[i].speed=param(id,IP_SPEED_BASE,370);
        g->units[i].level=npc_definition(s->native_id)->level;
        g->units[i].max_mana=npc_definition(s->native_id)->mana;
        g->units[i].mana=min_i(g->units[i].max_mana,g->units[target].max_mana>0?g->units[target].mana:g->units[i].max_mana);
        g->units[i].target=-1;
        g->units[i].stun_ticks=g->units[i].taunt_ticks=g->units[i].channel_ticks=0;
        {const DotaNpcSourceDef *d=npc_definition(s->native_id);int percent=param(id,IP_BOUNTY_PCT,50),gold;
            g->seed=g->seed*1664525u+1013904223u;gold=d->gold_min+(int)((g->seed>>8)%(unsigned)(d->gold_max-d->gold_min+1));
            u->gold+=dota_turbo_gold_reward(gold*percent/100);dota_rules_item_xp(g,h,d->xp*percent/100);
        }
        g->units[target].active=0;
        if(s->source_type==DOTA_ANCIENT){int c=g->units[target].lane,alive=0;if(c>=0&&c<DOTA_ANCIENT_CAMPS){for(j=DOTA_ANCIENT_FIRST+c*3;j<DOTA_ANCIENT_FIRST+c*3+3;j++)if(live(g,j))alive=1;if(!alive)g->bosses.ancient_spawn_tick[c]=(g->tick/(60*DOTA_HZ)+1)*60*DOTA_HZ;}}
        return 1;
        case ITEM_ACTION_MIDAS:if(!live(g,target)||dota_item_world_dominated(g,target))return 0;
        old=u->gold;
        dota_neutral_midas(g,target,h);
        dota_rules_item_kill(g,target,h);
        u->gold=old+dota_turbo_gold_reward(amount);
        return 1;
        case ITEM_ACTION_METEOR:{
            DotaItemChannel*c=&g->item_world.channel[h];
            if(c->until>g->tick)return 0;
            c->until=g->tick+duration+ticks(id,IP_LAND_TIME,10);
            c->generation=u->generation;
            c->item=id;
            c->kind=action;
            c->target=-1;c->target_generation=0;
            c->x=x;
            c->y=y;
            c->origin_x=u->x;
            c->origin_y=u->y;
            u->channel_ticks=duration;
            return 1;
        }
        case ITEM_ACTION_SOUL:for(i=0;i<8;i++)if(g->item_world.trap[i].until<=g->tick)break;
        if(i==8)return 0;
        g->item_world.trap[i].until=g->tick+duration;
        g->item_world.trap[i].generation=u->generation;
        g->item_world.trap[i].owner=h;
        g->item_world.trap[i].item=id;
        g->item_world.trap[i].x=x;
        g->item_world.trap[i].y=y;
        return 1;
        case ITEM_ACTION_NEUTRAL:/* Core movement consults dota_items_free_path. */return id==DOTA_ITEM_SPIDER_LEGS;
        default:return 0;
    }
}
void dota_item_world_tick(DotaGame*g){
    int h,i,j;
    if(g->item_world.last_tick==g->tick)return;
    g->item_world.last_tick=g->tick;
    smoke_tick(g);
    npc_effect_tick(g);
    for(i=0;i<16;i++)if(g->item_world.drop[i].active&&g->tick>=g->item_world.drop[i].ready_tick){DotaItemDrop*d=&g->item_world.drop[i];for(h=0;h<10;h++)if(live(g,h)&&range(d->x,d->y,g->units[h].x,g->units[h].y,100)){
      int binding=d->owner,received;if(binding>=0&&g->units[binding].team!=g->units[h].team)binding=-1;
      received=d->item==DOTA_ITEM_RAPIER?dota_items_receive_rapier(g,h,binding,d->spell_mode):dota_items_receive(g,h,d->item,201);if(received){d->active=0;break;}}
}
    for(h=0;h<10;h++){
        DotaItemChannel*c=&g->item_world.channel[h];
        DotaUnit*u=&g->units[h];
        int land,casting;
        if(!c->until)continue;
        if(c->kind==ITEM_ACTION_TELEPORT){
            int x,y,nx,ny;DotaPoint p;
            if(!dota_items_teleport_channel(g,h,&x,&y,0,0)){dota_items_cancel_teleport(g,h);continue;}
            if(g->tick<c->until)continue;
            c->until=0;u->channel_ticks=0;
            p=teleport_anchor_point(g,c->target);
            /* Recheck a moving anchor and changed collisions on the final
             * tick. Never land at an unchecked building centre or corpse. */
            if(dota_nav_nearest(x,y,128,&nx,&ny)&&range(nx,ny,p.x,p.y,800)
               &&dota_rules_item_move(g,h,nx,ny,1)){
                dota_rules_item_effect(g,DOTA_FX_BURST,u->team,c->origin_x,c->origin_y,c->origin_x,c->origin_y,20,10);
                dota_rules_item_effect(g,DOTA_FX_BURST,u->team,u->x,u->y,u->x,u->y,20,10);
            }
            continue;
        }
        land=c->kind==ITEM_ACTION_METEOR?ticks(c->item,IP_LAND_TIME,10):0;casting=g->tick<c->until-land;
        if(!live(g,h)||u->generation!=c->generation||(casting&&(u->stun_ticks||u->x!=c->origin_x||u->y!=c->origin_y||u->channel_ticks<=0))){
            c->until=0;
            u->channel_ticks=0;
            continue;
        }
        if(g->tick<c->until)continue;
        c->until=0;
        u->channel_ticks=0;
        if(c->kind==ITEM_ACTION_METEOR){
            int radius=param(c->item,IP_IMPACT_RADIUS,400);
            for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&g->units[i].team!=u->team&&range(c->x,c->y,g->units[i].x,g->units[i].y,radius)){
                g->units[i].stun_ticks=max_i(g->units[i].stun_ticks,ticks(c->item,IP_STUN_DURATION,15));
                dota_rules_item_damage(g,h,i,param(c->item,IP_IMPACT_DAMAGE_UNITS,130),1);
                g->units[i].dot_ticks=ticks(c->item,IP_BURN_DURATION,120);
                g->units[i].dot_source=h;
                g->units[i].dot_damage=param(c->item,IP_BURN_DPS_UNITS,50);
            }
        }
    }
    for(i=0;i<DOTA_ITEM_TREES;i++)if(g->item_world.tree[i].until&&g->item_world.tree[i].until<=g->tick)g->item_world.tree[i].until=0;
    for(i=0;i<8;i++){
        DotaItemTrap*t=&g->item_world.trap[i];
        if(t->until<=g->tick)continue;
        if(t->owner<0||t->owner>=10||!g->units[t->owner].active){
            t->until=0;
            continue;
        }
        if(t->item==DOTA_ITEM_GEM)continue;
        if(t->item==DOTA_ITEM_DUST){
            for(j=0;j<DOTA_MAX_UNITS;j++)if(live(g,j)&&g->units[j].team!=g->units[t->owner].team&&range(t->x,t->y,g->units[j].x,g->units[j].y,1050)){
                if(dota_items_effect_value(g,j,IE_INVISIBLE)&&!dota_items_effect_value(g,j,IE_REVEAL))dota_rules_item_damage(g,t->owner,j,param(t->item,IP_DAMAGE,25),1);
                if(dota_items_effect_value(g,j,IE_INVISIBLE))dota_items_apply_effect(g,t->owner,j,IE_SLOW,160,20,t->item);
                dota_items_apply_effect(g,t->owner,j,IE_REVEAL,160,1,t->item);
            }
            continue;
        }
        for(j=0;j<DOTA_MAX_UNITS;j++)if(live(g,j)&&g->units[j].team!=g->units[t->owner].team&&range(t->x,t->y,g->units[j].x,g->units[j].y,450)){
            dota_items_apply_effect(g,t->owner,j,IE_DOT,ticks(t->item,IP_DURATION,160),param(t->item,IP_SOUL_DAMAGE_AMOUNT,25)*100,t->item);
            dota_items_apply_effect(g,t->owner,j,IE_REVEAL,ticks(t->item,IP_DURATION,160),1,t->item);
            t->until=0;
            break;
        }
    }
}

int dota_item_world_drop(DotaGame*g,int owner,int item,int x,int y){return dota_item_world_drop_mode(g,owner,item,x,y,0);}

int dota_item_world_dominated(const DotaGame *g,int i){return g&&i>=DOTA_ITEM_UNIT_FIRST&&i<DOTA_MAX_UNITS&&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_DOMINATED;}
int dota_item_world_dominate_cooldown(const DotaGame *g,int h){return g&&h>=0&&h<10?max_i(0,g->item_world.dominate_lock[h]-g->tick):0;}
static int npc_rank(const DotaItemWorldUnit *s){return min_i(3,max_i(0,s->ability_level-1));}
static int npc_effect_valid(const DotaGame *g,const DotaItemNPCEffect *e){return e->until>g->tick&&live(g,e->target)&&g->units[e->target].generation==e->target_generation&&e->source>=DOTA_ITEM_UNIT_FIRST&&e->source<DOTA_MAX_UNITS&&g->units[e->source].generation==e->source_generation;}
static int mobile(const DotaUnit *u){return u->type!=DOTA_TOWER&&u->type!=DOTA_BASE&&u->type!=DOTA_BUILDING&&u->type!=DOTA_TORMENTOR;}
static int npc_aura(const DotaGame *g,int target,int kind,int second){
    int i,best=0;if(!g||!g->item_world.native_mask||!live(g,target)||!mobile(&g->units[target]))return 0;
    for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&dota_item_world_dominated(g,i)){
        const DotaItemWorldUnit *s=&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST];const DotaNpcAuraDef *a;int value;
        if(!s->native_id||s->native_id>12||dota_items_broken(g,i))continue;
        a=&npc_auras[s->native_id];
        if(a->kind!=kind||(g->units[i].team==g->units[target].team)==(kind==NPC_AURA_WEAK)||!range(g->units[i].x,g->units[i].y,g->units[target].x,g->units[target].y,a->radius))continue;
        value=(second?a->second:a->first)[npc_rank(s)];if(kind==NPC_AURA_ARMOR)best+=value;else best=max_i(best,value);
    }return best;
}
int dota_item_world_accuracy(const DotaGame *g,int i){return npc_aura(g,i,NPC_AURA_DRUMS,1);}
int dota_item_world_attack_damage(const DotaGame *g,int source,int target,int amount){
    int hero;if(!g||(unsigned)source>=DOTA_MAX_UNITS||(unsigned)target>=DOTA_MAX_UNITS)return amount;
    /* Irresolute/Piercing belong to ordinary lane creeps too. Resolve them
     * once in the shared landed-attack path, including dominated copies. */
    if(dota_item_world_dominated(g,source)){
        if(g->item_world.unit[source-DOTA_ITEM_UNIT_FIRST].source_type!=DOTA_CREEP)return amount;
    }else if(g->units[source].type!=DOTA_CREEP)return amount;
    hero=g->units[target].type==DOTA_HERO||(target>=DOTA_ITEM_UNIT_FIRST&&g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION);
    if(!g->units[source].tier)return hero?amount*DOTA_LANE_MELEE_HERO_DAMAGE_PERCENT/100:amount;
    if(hero)return amount*DOTA_LANE_RANGED_HERO_DAMAGE_PERCENT/100;
    if(g->units[target].type==DOTA_TOWER||g->units[target].type==DOTA_BASE||g->units[target].type==DOTA_BUILDING)return amount*DOTA_LANE_RANGED_BUILDING_DAMAGE_PERCENT/100;
    return amount*DOTA_LANE_RANGED_BASIC_DAMAGE_PERCENT/100;
}
int dota_item_world_max_health_percent(const DotaGame *g,int i){return 100+npc_aura(g,i,NPC_AURA_HEALTH,0);}
int dota_item_world_magic_resist(const DotaGame *g,int i,int base){
    int second,aura;if(!g||i<0||i>=DOTA_MAX_UNITS)return base;
    if(g->units[i].type==DOTA_CREEP)base=dota_lane_creep_source[g->units[i].tier!=0].magic_resist;
    if(dota_item_world_dominated(g,i)){int n=g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST].native_id;if(n>0&&n<47)base=npc_definition(n)->mr;}
    second=g->units[i].type!=DOTA_HERO;
    if(i>=DOTA_ITEM_UNIT_FIRST&&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION)second=0;
    aura=npc_aura(g,i,NPC_AURA_MAGIC,second);return 100-(100-base)*(100-aura)/100;
}
int dota_item_world_armor_delta(const DotaGame *g,int i){
    int armor,j,half=0,base;if(!live(g,i))return 0;
    armor=npc_aura(g,i,NPC_AURA_ARMOR,0)-npc_aura(g,i,NPC_AURA_WEAK,0);
    for(j=0;j<32;j++){const DotaItemNPCEffect *e=&g->item_world.npc_effect[j];if(e->target==i&&e->kind==NPC_EFFECT_DESECRATE&&npc_effect_valid(g,e)&&!dota_items_debuff_immune(g,i))half=50;}
    if(!half)return armor;
    base=g->units[i].armor;
    if(g->units[i].type==DOTA_HERO){DotaItemStats st;dota_items_stats((DotaGame*)g,i,&st);base-=st.value[ITEM_STAT_ARMOR]/100;base+=st.value[ITEM_STAT_AGILITY]/600;}
    else if(dota_item_world_dominated(g,i))base-=param(g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST].source_item,IP_CREEP_BONUS_ARMOR,4);
    return armor-max_i(0,base)*half/100;
}
void dota_item_world_apply_stats(const DotaGame *g,int i,DotaItemStats *out){
    int n;if(!g||!out||i<0||i>=DOTA_MAX_UNITS)return;
    out->value[ITEM_STAT_ATTACK_SPEED]+=npc_aura(g,i,NPC_AURA_DRUMS,0);
    out->value[ITEM_STAT_SPELL_AMP]+=100*npc_aura(g,i,NPC_AURA_SPELL,0);
    n=100*npc_aura(g,i,NPC_AURA_TIME,0);out->value[ITEM_STAT_COOLDOWN_REDUCTION]=10000-(10000-out->value[ITEM_STAT_COOLDOWN_REDUCTION])*(10000-n)/10000;
    out->value[ITEM_STAT_LIFESTEAL]+=100*npc_aura(g,i,NPC_AURA_PROWLER,0);
    out->value[ITEM_STAT_HEALTH_REGEN]+=100*npc_aura(g,i,NPC_AURA_PROWLER,1);
    for(n=0;n<32;n++){const DotaItemNPCEffect *e=&g->item_world.npc_effect[n];int left=e->until-g->tick;
        if(e->target!=i||!npc_effect_valid(g,e))continue;
        if(e->kind==NPC_EFFECT_FRENZY)out->value[ITEM_STAT_ATTACK_SPEED]+=75;
        else if(!dota_items_debuff_immune(g,i)){
            if(e->kind==NPC_EFFECT_SLAM)out->value[ITEM_STAT_ATTACK_SPEED]-=60;
            if(e->kind==NPC_EFFECT_SLAM)out->value[ITEM_STAT_MOVE_PERCENT]-=6000;
            if(e->kind==NPC_EFFECT_PURGE)out->value[ITEM_STAT_MOVE_PERCENT]-=min_i(100,100*left/max_i(1,e->until-e->start))*100;
        }
    }
}
int dota_item_world_attack_traits(const DotaGame *g,int i,DotaItemWorldAttackTraits *out){
    const DotaItemWorldUnit *s;const DotaNpcSourceDef *d;DotaItemStats st;
    if(!out||!dota_item_world_dominated(g,i))return 0;
    s=&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST];
    if(!s->native_id||s->native_id>46)return 0;
    d=npc_definition(s->native_id);
    dota_items_stats((DotaGame*)g,i,&st);out->bat_ticks=d->bat_ticks;out->point_ms=d->point_ms;out->projectile_speed=d->projectile;
    out->attack_speed_x100=max_i(2000,min_i(70000,100*(d->base_as+st.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,i))));return 1;
}
void dota_item_world_on_damage(DotaGame *g,int victim,int source,int dealt){
    DotaItemWorldUnit *s;if(!g||dealt<=0||!dota_item_world_dominated(g,victim)||(unsigned)source>=DOTA_MAX_UNITS)return;
    s=&g->item_world.unit[victim-DOTA_ITEM_UNIT_FIRST];if(s->owner<0||s->owner>=10||g->units[source].team==g->units[victim].team)return;
    if(g->units[source].type==DOTA_HERO||g->units[source].type==DOTA_ROSHAN)g->item_world.dominate_lock[s->owner]=g->tick+ticks(s->source_item,IP_CREEP_DAMAGE_TAKEN_COOLDOWN,60);
}
int dota_item_world_rooted(const DotaGame *g,int i){
    int n;if(!g||i<0||i>=DOTA_MAX_UNITS)return 0;for(n=0;n<32;n++){const DotaItemNPCEffect *e=&g->item_world.npc_effect[n];if(e->kind==NPC_EFFECT_PETRIFY&&e->target==i&&npc_effect_valid(g,e))return 1;}return 0;
}
void dota_item_world_purge(DotaGame *g,int i,int positive){
    int n;if(!g)return;for(n=0;n<32;n++){DotaItemNPCEffect *e=&g->item_world.npc_effect[n];if(e->target==i&&(e->kind==NPC_EFFECT_FRENZY)==!!positive)e->until=0;}
}
int dota_item_world_statuses(const DotaGame *g,int i,DotaItemStatus *out,int capacity){
    int n,count=0;if(!g||!out||capacity<=0)return 0;for(n=0;n<32&&count<capacity;n++){
        const DotaItemNPCEffect *e=&g->item_world.npc_effect[n];int src=e->source-DOTA_ITEM_UNIT_FIRST;
        if(e->target!=i||!npc_effect_valid(g,e)||src<0||src>=DOTA_ITEM_UNIT_COUNT)continue;
        if(e->kind!=NPC_EFFECT_FRENZY&&e->kind!=NPC_EFFECT_PETRIFY&&dota_items_debuff_immune(g,i))continue;
        out[count].item=g->item_world.unit[src].source_item;out[count].harmful=e->kind!=NPC_EFFECT_FRENZY;out[count].ticks=e->until-g->tick;out[count].stacks=1;count++;
    }return count;
}
static void npc_effect(DotaGame *g,int source,int target,int kind,int duration,int rank){
    int n;if(!live(g,target)||duration<=0)return;
    if(kind!=NPC_EFFECT_FRENZY){int sr=dota_items_status_resist_percent(g,target);duration=max_i(1,(duration*(100-sr)+99)/100);}
    for(n=0;n<32;n++)if(g->item_world.npc_effect[n].until<=g->tick)break;
    if(n==32)return;
    {DotaItemNPCEffect *e=&g->item_world.npc_effect[n];clear(e,sizeof(*e));e->start=g->tick;e->until=g->tick+duration;e->source=(unsigned char)source;e->target=(unsigned char)target;e->kind=(unsigned char)kind;e->rank=(unsigned char)rank;e->source_generation=(unsigned short)g->units[source].generation;e->target_generation=(unsigned short)g->units[target].generation;}
}
static void npc_effect_tick(DotaGame *g){
    int n,i;for(n=0;n<32;n++){DotaItemNPCEffect *e=&g->item_world.npc_effect[n];int age=g->tick-e->start,total,damage=0;
        if(!e->until)continue;
        if(e->source<DOTA_ITEM_UNIT_FIRST||e->source>=DOTA_MAX_UNITS||g->units[e->source].generation!=e->source_generation||!live(g,e->target)||g->units[e->target].generation!=e->target_generation){e->until=0;continue;}
        if(e->kind==NPC_EFFECT_ICEFIRE&&age%20==0){int pct=mobile(&g->units[e->target])?100:25;total=50*min_i(age,e->until-e->start)*pct/(20*100);damage=total-e->done;e->done=(unsigned short)total;}
        else if(e->kind==NPC_EFFECT_PETRIFY&&age%2==0){total=100*min_i(age,e->until-e->start)/20;damage=total-e->done;e->done=(unsigned short)total;}
        if(damage>0)dota_rules_item_damage(g,e->source,e->target,damage,e->kind==NPC_EFFECT_PETRIFY?0:1);
        if(g->tick>=e->until)e->until=0;
    }
    for(i=DOTA_ITEM_UNIT_FIRST;i<DOTA_MAX_UNITS;i++){DotaItemWorldUnit *s=&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST];
        int age;if(s->kind!=ITEM_UNIT_DOMINATED||s->ground_until<g->tick||!s->ground_until)continue;
        age=g->tick-(s->ground_until-npc_abilities[NPC_FIREBALL].duration[npc_rank(s)]);if(age<=0||age%10)continue;
        for(n=0;n<DOTA_MAX_UNITS;n++)if(live(g,n)&&g->units[n].team!=g->units[i].team&&mobile(&g->units[n])&&range(s->ground_x,s->ground_y,g->units[n].x,g->units[n].y,300))dota_rules_item_damage(g,i,n,85*age/20-85*(age-10)/20,1);
        dota_rules_item_effect(g,DOTA_FX_BURST,g->units[i].team,s->ground_x,s->ground_y,s->ground_x,s->ground_y,75,10);
    }
    for(i=10;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&mobile(&g->units[i])){
        DotaUnit *u=&g->units[i];int base,bonus,regen;
        if(g->item_world.aura_hp_generation[i]!=(unsigned short)u->generation){g->item_world.aura_hp_generation[i]=(unsigned short)u->generation;g->item_world.aura_hp_bonus[i]=0;g->item_world.npc_hp_fraction[i]=0;}
        base=max_i(1,u->max_hp-g->item_world.aura_hp_bonus[i]);bonus=base*(dota_item_world_max_health_percent(g,i)-100)/100;u->max_hp=base+bonus;g->item_world.aura_hp_bonus[i]=(short)bonus;u->hp=min_i(u->hp,u->max_hp);
        if(dota_item_world_dominated(g,i))continue;
        regen=100*npc_aura(g,i,NPC_AURA_PROWLER,1);g->item_world.npc_hp_fraction[i]=(unsigned short)(g->item_world.npc_hp_fraction[i]+regen);
        u->hp=min_i(u->max_hp,u->hp+g->item_world.npc_hp_fraction[i]/2000);g->item_world.npc_hp_fraction[i]%=2000;
    }
}
static int owned_helm(const DotaGame *g,int h){int i;for(i=0;i<9;i++)if(dota_inventory_item(g,h,i)==DOTA_ITEM_HELM_OF_THE_DOMINATOR||dota_inventory_item(g,h,i)==DOTA_ITEM_HELM_OF_THE_OVERLORD)return 1;return 0;}
static void npc_resolve(DotaGame *g,int index){
    DotaItemWorldUnit *s=&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];DotaUnit *u=&g->units[index];int skill=s->cast_skill,r=npc_rank(s),t=s->cast_target,j,life;
    const DotaNpcAbilityDef *a=&npc_abilities[skill];s->cast_at=0;u->channel_ticks=0;
    if(skill==NPC_FIREBALL){s->ground_until=g->tick+a->duration[r];return;}
    if(skill==NPC_SLAM||skill==NPC_DESECRATE){for(j=0;j<DOTA_MAX_UNITS;j++)if(live(g,j)&&g->units[j].team!=u->team&&mobile(&g->units[j])&&range(u->x,u->y,g->units[j].x,g->units[j].y,a->radius[r])){
        dota_rules_item_damage(g,index,j,a->value[r],skill==NPC_SLAM?1:0);life=skill==NPC_SLAM&&g->units[j].type!=DOTA_HERO?a->second[r]:a->duration[r];npc_effect(g,index,j,skill==NPC_SLAM?NPC_EFFECT_SLAM:NPC_EFFECT_DESECRATE,life,r);
    }}else if(live(g,t)&&g->units[t].generation==s->cast_generation){
        if(skill==NPC_PURGE){dota_items_dispel(g,t,g->units[t].team!=u->team);if(g->units[t].team!=u->team){npc_effect(g,index,t,NPC_EFFECT_PURGE,a->duration[r],r);if(g->units[t].type==DOTA_SUMMON&&!dota_item_world_dominated(g,t))dota_rules_item_damage(g,index,t,a->value[r],1);}}
        else if(skill==NPC_FRENZY)npc_effect(g,index,t,NPC_EFFECT_FRENZY,a->duration[r],r);
        else if(skill==NPC_ICEFIRE)npc_effect(g,index,t,NPC_EFFECT_ICEFIRE,a->duration[r],r);
    }
    dota_rules_item_effect(g,DOTA_FX_BURST,u->team,u->x,u->y,u->x,u->y,a->radius[r]/4,12);
}
static int npc_start(DotaGame *g,int index,int skill,int target,int slot){
    DotaItemWorldUnit *s=&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];DotaUnit *u=&g->units[index];int r=npc_rank(s);const DotaNpcAbilityDef *a=&npc_abilities[skill];
    if(!live(g,target)||(!mobile(&g->units[target])&&skill!=NPC_ICEFIRE)||s->cast_at||s->ability_ready[slot]>g->tick||u->mana<a->mana[r]||u->stun_ticks||u->channel_ticks||dota_items_silenced(g,index))return 0;
    if(a->range[r]&&!range(u->x,u->y,g->units[target].x,g->units[target].y,a->range[r]))return 0;
    if(!a->range[r]&&a->radius[r]&&!range(u->x,u->y,g->units[target].x,g->units[target].y,a->radius[r]))return 0;
    u->mana-=a->mana[r];s->ability_ready[slot]=g->tick+a->cooldown[r];s->cast_at=g->tick+a->cast[r];s->cast_skill=(unsigned char)skill;s->cast_target=target;s->cast_generation=g->units[target].generation;u->channel_ticks=a->cast[r];
    if(skill==NPC_FIREBALL){s->ground_x=g->units[target].x;s->ground_y=g->units[target].y;}
    if(!a->cast[r])npc_resolve(g,index);
    return 1;
}
static int npc_begin(DotaGame *g,int i,int target){
    DotaItemWorldUnit *s=&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST];int id=s->native_id;
    if(id==2){int h;for(h=0;h<DOTA_MAX_UNITS;h++)if(live(g,h)&&g->units[h].team==g->units[i].team&&(g->units[h].slow_ticks||dota_items_rooted(g,h)||dota_items_silenced(g,h))&&npc_start(g,i,NPC_PURGE,h,0))return 1;return npc_start(g,i,NPC_PURGE,target,0);}
    if(id==3)return npc_start(g,i,NPC_FIREBALL,target,0);
    if(id==7){if(npc_start(g,i,NPC_SLAM,target,0))return 1;return npc_start(g,i,NPC_FRENZY,live(g,s->owner)?s->owner:i,1);}
    if(id==9)return npc_start(g,i,NPC_ICEFIRE,target,0);
    if(id==11)return npc_start(g,i,NPC_DESECRATE,target,0);
    return 0;
}
static void npc_tick(DotaGame *g,int i){
    DotaItemWorldUnit *s=&g->item_world.unit[i-DOTA_ITEM_UNIT_FIRST];DotaUnit *u=&g->units[i];const DotaNpcSourceDef *d;
    int hp,mp,add;if(!s->native_id||s->native_id>46)return;
    if(!owned_helm(g,s->owner)){dota_item_world_on_death(g,i,-1);return;}d=npc_definition(s->native_id);
    hp=d->hp_regen100+100*param(s->source_item,IP_CREEP_BONUS_HP_REGEN,12)+100*npc_aura(g,i,NPC_AURA_PROWLER,1);mp=d->mp_regen100+100*param(s->source_item,IP_CREEP_BONUS_MP_REGEN,4);
    hp=dota_items_heal_amount(g,i,i,hp);s->hp_fraction=(unsigned short)(s->hp_fraction+max_i(0,hp));add=s->hp_fraction/2000;s->hp_fraction%=2000;if(add)u->hp=min_i(u->max_hp,u->hp+add);
    s->mp_fraction=(unsigned short)(s->mp_fraction+mp);u->mana=min_i(u->max_mana,u->mana+s->mp_fraction/2000);s->mp_fraction%=2000;
    if(s->cast_at){int targeted=s->cast_skill==NPC_PURGE||s->cast_skill==NPC_FRENZY||s->cast_skill==NPC_ICEFIRE;
        if(u->stun_ticks||dota_items_silenced(g,i)||(targeted&&(!live(g,s->cast_target)||g->units[s->cast_target].generation!=s->cast_generation))){s->cast_at=0;u->channel_ticks=0;}else if(g->tick>=s->cast_at)npc_resolve(g,i);}
}
void dota_item_world_on_attack_hit(DotaGame *g,int source,int target,int dealt){
    int ls,i;DotaItemWorldUnit *s;if(!g||dealt<=0||!live(g,source)||!live(g,target))return;
    ls=npc_aura(g,source,NPC_AURA_PROWLER,0);if(g->units[source].type!=DOTA_HERO&&ls&&mobile(&g->units[target]))g->units[source].hp=min_i(g->units[source].max_hp,g->units[source].hp+dota_items_heal_amount(g,source,source,dealt*ls/100));
    if(!dota_item_world_dominated(g,source)||dota_items_broken(g,source))return;
    s=&g->item_world.unit[source-DOTA_ITEM_UNIT_FIRST];
    if(s->native_id==3)for(i=0;i<DOTA_MAX_UNITS;i++)if(i!=target&&live(g,i)&&g->units[i].team!=g->units[source].team&&mobile(&g->units[i])&&range(g->units[target].x,g->units[target].y,g->units[i].x,g->units[i].y,250))dota_rules_item_damage(g,source,i,g->units[source].damage,0);
    if(s->native_id==11&&s->ability_ready[1]<=g->tick&&g->units[target].hp*100<=g->units[target].max_hp*90){s->ability_ready[1]=g->tick+300;npc_effect(g,source,target,NPC_EFFECT_PETRIFY,npc_abilities[NPC_PETRIFY].duration[npc_rank(s)],npc_rank(s));}
}
