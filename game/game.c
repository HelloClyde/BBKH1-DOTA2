#include "game.h"
#include "navigation.h"
#include "bosses.h"
#include "vision.h"
#include "highground_data.h"
#include "objectives.h"
#include "courier.h"
#include "turbo.h"
#include "extra_heroes.h"
#include "items.h"
#include "items_internal.h"
#include "items_actions.h"
#include "item_world.h"
#include "neutral.h"
#include "items_special.h"
#include "sight.h"
#include "water.h"
#include "jungle.h"
#include "roster_heroes.h"
#include "progression.h"
#include "lane_creep_data.h"
#include "barracks.h"
#include "runes.h"
#include "match_time.h"
#include "trees.h"

static int effective_health_regen_with_stats(const DotaGame *,int,const DotaItemStats *);
static int mana_regen_with_stats(const DotaGame *,int,const DotaItemStats *);
static void crystal_clone_shatter(DotaGame *,int);
static int fiery_soul_limit(const DotaGame *g,int h){const DotaUnit *u=&g->units[h];return g->extra.hero[h].shadow_until>g->tick?12:dota_progression_value100(u,2,"fiery_soul_max_stacks",700)/100;}

const DotaHeroDef dota_hero_defs[DOTA_HERO_TYPES] = {
    {"Axe", {"Berserker Call","Battle Hunger","Counter Helix","Culling Blade"},
     670,291,58,3,150,315,{360,400,0,1600},{90,50,0,100}},
    {"Lina", {"Dragon Slave","Light Strike","Fiery Soul","Laguna Blade"},
     560,411,53,3,670,290,{220,260,0,1400},{90,100,0,150}},
    {"Juggernaut", {"Blade Fury","Healing Ward","Blade Dance","Omnislash"},
     560,243,55,5,150,305,{600,1200,0,2400},{110,120,0,200}},
    {"Crystal Maiden", {"Crystal Nova","Frostbite","Arcane Aura","Freezing Field"},
     494,315,51,2,600,280,{220,180,0,2000},{115,125,0,200}},
    {"Slark", {"Dark Pact","Pounce","Saltwater Shiv","Shadow Dance"},
     560,267,61,3,150,300,{180,480,240,1000},{65,75,25,100}},
    {"Necrophos", {"Death Pulse","Ghost Shroud","Heartstopper Aura","Reaper Scythe"},
     516,351,51,4,500,280,{160,560,0,2000},{115,75,0,250}},
    {"Pudge",{"Meat Hook","Rot","Flesh Heap","Dismember"},670,267,73,2,175,280,{0},{0}},
    {"Sniper",{"Shrapnel","Headshot","Take Aim","Assassinate"},538,255,43,4,550,285,{0},{0}},
    {"Sven",{"Storm Hammer","Great Cleave","Warcry","Gods Strength"},648,267,62,3,150,325,{0},{0}},
    {"Tidehunter",{"Gush","Kraken Shell","Anchor Smash","Ravage"},670,315,53,2,150,300,{0},{0}}
};

const DotaItemDef dota_item_defs[DOTA_LEGACY_ITEM_TYPES] = {
    {"Boots",500},{"Blades of Attack",450},{"Chainmail",500},
    {"Vitality Booster",1000},{"Energy Booster",800},{"Healing Salve",100}
};

/* Steam maps/dota/entities/default_ents.vents_c, original entity origins and
 * NPCFirstWaypoint -> path_corner target chains. x=2048+SourceX/4,
 * y=2048-SourceY/4, rounded to the nearest world unit (half upward).
 * The two teams have different chains; neither is a reversed approximation.
 * Source identity and every point are checked against reports/layout.json.
 * These anchors do not implement the original terrain navigation mesh. */
const DotaPoint dota_lanes[2][3][DOTA_LANE_POINTS] = {
    {
      {{402,3104},{528,1952},{384,1648},{496,992},{498,720},{600,682},{758,620},{926,592},{1102,566},{1350,536},{1584,522},{3012,602},{3379,835}},
      {{834,3142},{1261,2722},{1804,2254},{1958,2112},{2092,2018},{2276,1892},{2694,1622},{3166,1062},{3379,851}},
      {{1042,3576},{2168,3662},{2560,3632},{2878,3594},{3055,3583},{3216,3573},{3484,3493},{3555,3419},{3581,3225},{3612,2861},{3632,1216},{3395,851}}
    },
    {
      {{2974,606},{1580,512},{736,624},{513,679},{489,830},{464,1074},{404,2978},{650,3304}},
      {{3162,1058},{2678,1616},{2264,1880},{2084,2010},{1950,2104},{1796,2238},{830,3138},{666,3304}},
      {{3628,1088},{3568,1960},{3728,2576},{3623,2878},{3627,3062},{3570,3371},{3476,3494},{3376,3583},{2307,3655},{992,3572},{666,3320}}
    }
};
const int dota_lane_point_counts[2][3]={{13,9,12},{8,8,11}};
const DotaPoint dota_creep_spawns[2][3]={
    {{396,3064},{796,3170},{1136,3576}},
    {{2840,600},{3072,1152},{3616,1136}}
};
const DotaPoint dota_hero_spawns[2][5]={
    {{373,3723},{361,3686},{323,3654},{311,3617},{273,3586}},
    {{3757,410},{3774,443},{3807,470},{3840,496},{3877,527}}
};
const DotaPoint dota_fountains[2]={{184,3783},{3900,336}};
const DotaPoint dota_ancients[2]={{568,3386},{3430,798}};
const DotaPoint dota_tower_positions[2][3][2]={
    {{{464,1584},{423,2266}},{{1662,2400},{1250,2780}},{{3263,3643},{1958,3612}}},
    {{{729,539},{2016,544}},{{2179,1885},{2672,1520}},{{3615,2608},{3648,1952}}}
};
/* All26 original ordinary-neutral spawners. The two Ancient camps have their
 * separate native category3 pool and retain the existing boss service. */
const DotaPoint dota_neutral_camps[DOTA_NEUTRAL_CAMPS]={
    DOTA_JUNGLE_CAMP_POSITIONS
};
/* Original ent_dota_shop shoptype2 and miniboss_location origins. The300-world
 * interaction radius is an H1 convenience; it is not a recovered source value. */
const DotaPoint dota_secret_shops[2]={{778,1561},{3270,2350}};
const DotaPoint dota_tormentor_locations[2]={{128,464},{3984,3600}};
static void sync_navigation(const DotaGame *g);
/* Legacy obstacle API stays empty. Real movement uses the separately baked
 * source navigation graph and its blocked edges, rather than rectangles. */
const DotaObstacle dota_obstacles[1]={{0,0,0,0}};
const int dota_obstacle_count=0;

static int clamp(int x,int low,int high) { return x<low?low:(x>high?high:x); }
static int dist2(int x,int y,int xx,int yy) { int dx=x-xx,dy=y-yy;return dx*dx+dy*dy; }
static int alive(const DotaUnit *u) { return u->active && u->hp>0; }
static int roster_busy(const DotaGame *g,int index){
    const DotaRosterHero *s;if((unsigned)index>=DOTA_HERO_COUNT || !dota_roster_is_hero(&g->units[index]))return 0;
    s=&g->roster.hero[index];return s->generation==g->units[index].generation && (s->cast_at>g->tick || s->channel_until>g->tick);
}
static int structure(const DotaUnit *u) { return u->type==DOTA_TOWER || u->type==DOTA_BASE || u->type==DOTA_BUILDING; }
static int high_tower_slot(int team,int slot){
    int index=102+team*DOTA_HIGH_TOWERS_PER_TEAM+slot;
    return index>=DOTA_ROSHAN_INDEX?index+1:index;
}
int dota_high_tower_index(int team,int slot){
    return (unsigned)team<2 && (unsigned)slot<DOTA_HIGH_TOWERS_PER_TEAM?high_tower_slot(team,slot):-1;
}
static int source_range(int squared,int source_units){return (long long)squared*16<=(long long)source_units*source_units;}
int dota_unit_visible(const DotaGame *g,int observer,int target){
    if(!g || (unsigned)observer>=DOTA_MAX_UNITS || (unsigned)target>=DOTA_MAX_UNITS)return 0;
    if(dota_roster_swallowed_by(g,target)>=0)return 0;
    if(!dota_sight_team_visible(g,observer,target))return 0;
    if(dota_extra_target_hidden(g,observer,target))return 0;
    if(g->units[observer].team!=g->units[target].team&&dota_item_world_smoked(g,target))return 0;
    /* True sight can only change hidden wards or item invisibility here.
     * Keep the shared-sight refresh and hero concealment gates above in their
     * original order; ordinary visible units need no detector search. */
    if((target<DOTA_ITEM_UNIT_FIRST ||
        (g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST].kind!=ITEM_UNIT_OBSERVER &&
         g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST].kind!=ITEM_UNIT_SENTRY)) &&
       !dota_items_invisible(g,target))return 1;
    if(dota_item_world_visible(g,observer,target)==0)return 0;
    return 1;
}
int dota_roshan_index(const DotaGame *g){return g && g->units[DOTA_ROSHAN_INDEX].active && g->units[DOTA_ROSHAN_INDEX].hp>0 && g->units[DOTA_ROSHAN_INDEX].type==DOTA_ROSHAN?DOTA_ROSHAN_INDEX:-1;}
int dota_ancient_index(int camp,int member){return (unsigned)camp<DOTA_ANCIENT_CAMPS && (unsigned)member<3?DOTA_ANCIENT_FIRST+camp*3+member:-1;}
static int skill_rank(const DotaUnit *u,int skill){return dota_progression_rank(u,skill);}
static void hero_stat_delta100(const DotaGame *g,int h,int *str,int *agi,int *intel){int s=0,a=0,i=0;dota_extra_stat_delta(g,h,&s,&a,&i);dota_roster_stat_delta(g,h,&s,&a,&i);*str+=s*100;*agi+=a*100;*intel+=i*100;dota_progression_stat_delta100(g,h,str,agi,intel);}
static void hero_stat_delta(const DotaGame *g,int h,int *str,int *agi,int *intel){int s=0,a=0,i=0;hero_stat_delta100(g,h,&s,&a,&i);*str+=s/100;*agi+=a/100;*intel+=i/100;}
static int skill_value(const DotaUnit *u,int skill,const char *field,int fallback){return dota_progression_value100(u,skill,field,fallback*100)/100;}
static int skill_time(const DotaUnit *u,int skill,const char *field,int fallback_ticks){return dota_progression_value100(u,skill,field,fallback_ticks*5)/5;}
static int resisted_duration(DotaGame *g,int index,int ticks){return (ticks*(100-dota_boss_status_resist(&g->units[index]))*(100-dota_items_status_resist_percent(g,index))+9999)/10000;}
/* Values are source seconds*20 (cooldowns), mana, and source distances.
 * Only existing H1 skill mechanics are represented; no talent/scepter trees. */
static const unsigned short skill_mana[4][3][4]={
 {{90,100,110,120},{50,60,70,80},{100,125,150,150}},
 {{90,100,110,120},{100,110,120,130},{150,300,450,450}},
 {{110,110,110,110},{120,120,120,120},{200,275,350,350}},
 {{115,135,155,175},{125,135,145,155},{200,400,600,600}}};
static const unsigned short skill_cd[4][3][4]={
 {{360,320,280,240},{400,300,200,100},{1600,1500,1400,1400}},
 {{220,200,180,160},{260,220,180,140},{1400,1200,1000,1000}},
 {{600,520,440,360},{1200,1200,1200,1200},{2400,2400,2400,2400}},
 {{220,200,180,160},{180,160,140,120},{2000,1900,1800,1800}}};
int dota_unit_hull_source(const DotaUnit *u){
    if(!u)return 0;
    if(u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN)return dota_boss_hull_source(u);
    if(u->type==DOTA_HERO || u->type==DOTA_TORMENTOR)return 24;
    if(u->type==DOTA_TOWER)return 144;
    if(u->type==DOTA_BUILDING)return dota_barracks_kind(u)?144:96;
    if(u->type==DOTA_CREEP)return u->tier?8:16;
    if(u->type==DOTA_NEUTRAL){const DotaJungleDefinition *d=dota_jungle_definition(u);return d?d->hull:24;}
    if(u->type==DOTA_SUMMON)return 8;
    return 0;
}
int dota_attack_reach_source(const DotaUnit *u,const DotaUnit *v){return u?u->range+dota_unit_hull_source(v):0;}
static int attack_reach_for(const DotaGame *g,const DotaUnit *u,const DotaUnit *v){
    int range=dota_items_attack_range_for(g,(int)(u-g->units),(int)(v-g->units),u->range);
    if(range>=DOTA_WORLD*4)range=DOTA_WORLD*6;
    return range+dota_unit_hull_source(v);
}
int dota_skill_cast_range_source(const DotaGame *g,int index,int skill){
    static const unsigned short ranges[4][4]={{0,600,0,175},{1075,700,0,750},{0,350,0,450},{700,600,0,0}};
    const DotaUnit *u;if(!g || (unsigned)index>=10 || (unsigned)skill>=4)return 0;u=&g->units[index];if(!dota_progression_rank(u,skill))return 0;
    if(dota_roster_is_hero(u))return dota_roster_range(g,index,skill);
    if(dota_extra_is_hero(u)){int base=dota_extra_range(u,skill);return base?base+dota_items_range_bonus((DotaGame*)g,index,0):0;}
    if(u->type!=DOTA_HERO || (unsigned)u->hero_id>=4)return 0;
    {int range=skill_value(u,skill,"AbilityCastRange",ranges[u->hero_id][skill]);return range?range+dota_items_range_bonus((DotaGame*)g,index,0):0;}
}
int dota_skill_effect_radius_source(const DotaGame *g,int index,int skill){
    static const unsigned short radii[4][4]={{315,0,275,0},{275,250,0,0},{260,400,0,425},{425,0,1200,810}};
    const DotaUnit *u;if(!g || (unsigned)index>=10 || (unsigned)skill>=4)return 0;u=&g->units[index];if(!dota_progression_rank(u,skill))return 0;
    if(dota_roster_is_hero(u))return dota_roster_radius(g,index,skill);
    if(dota_extra_is_hero(u))return dota_extra_effect_radius(g,index,skill);
    if(u->type!=DOTA_HERO || (unsigned)u->hero_id>=4)return 0;
    {static const char *const fields[4][4]={{"radius","","radius",""},{"dragon_slave_width_initial","light_strike_array_aoe","",""},{"blade_fury_radius","healing_ward_aura_radius","","omni_slash_radius"},{"radius","","proximity_bonus_radius","radius"}};int radius=*fields[u->hero_id][skill]?skill_value(u,skill,fields[u->hero_id][skill],radii[u->hero_id][skill]):radii[u->hero_id][skill];return u->hero_id==DOTA_HERO_JUGGERNAUT&&skill==3?radius:dota_rules_aoe_radius(g,index,radius);}
}
int dota_skill_cooldown_ticks(const DotaGame *g,int index,int skill){
    const DotaUnit *u;if(!g || (unsigned)index>=10 || (unsigned)skill>=4)return 0;u=&g->units[index];if(!dota_progression_rank(u,skill))return 0;
    if(dota_roster_is_hero(u))return dota_roster_cooldown(g,index,skill);
    if(dota_extra_is_hero(u))return dota_extra_cooldown(u,skill)*(100-dota_items_cooldown_percent((DotaGame*)g,index))/100;
    if(skill==2)return 0;
    if(u->type!=DOTA_HERO || (unsigned)u->hero_id>=4)return 0;
    return skill_time(u,skill,"AbilityCooldown",skill_cd[u->hero_id][skill==3?2:skill][skill_rank(u,skill)-1])*(100-dota_items_cooldown_percent((DotaGame*)g,index))/100;
}
int dota_rules_armor100(const DotaGame *g,int index){
    const DotaUnit *u;int armor;if(!g||(unsigned)index>=DOTA_MAX_UNITS)return 0;u=&g->units[index];armor=u->armor*100;
    if((u->type==DOTA_HERO||(index>=DOTA_ITEM_UNIT_FIRST&&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION))&&u->armor==u->calculated_armor100/100)armor=u->calculated_armor100;
    armor+=(dota_item_world_armor_delta(g,index)+dota_roster_armor(g,index))*100;
    if(u->type==DOTA_HERO&&u->hero_id==DOTA_HERO_AXE&&u->skill_ticks[0]>0)armor+=dota_progression_value100(u,0,"bonus_armor",1200);
    if(index<DOTA_HERO_COUNT){armor+=g->culling[index].permanent_armor100;if(g->culling[index].until>g->tick)armor+=g->culling[index].armor100;}
    return armor;
}
int dota_effective_armor(const DotaGame *g,int index){return dota_rules_armor100(g,index)/100;}

int dota_skill_is_passive(int hero,int skill){return hero>=DOTA_HERO_PUDGE?dota_roster_passive(hero,skill):((unsigned)hero<6 && hero!=DOTA_HERO_SLARK && skill==2);}
int dota_passive_rank(const DotaUnit *u){return dota_progression_rank(u,2);}
int dota_omnislash_rank(const DotaUnit *u){
    return u && u->type==DOTA_HERO && u->hero_id==DOTA_HERO_JUGGERNAUT?dota_progression_rank(u,3):0;
}
int dota_omnislash_phase(const DotaGame *g,int index){
    const DotaUnit *u;const DotaOmnislashState *s;
    if(!g || (unsigned)index>=DOTA_HERO_COUNT)return DOTA_OMNISLASH_OFF;
    u=&g->units[index];s=&g->omnislash[index];
    return alive(u) && u->type==DOTA_HERO && u->hero_id==DOTA_HERO_JUGGERNAUT && s->generation==u->generation?s->phase:DOTA_OMNISLASH_OFF;
}
int dota_unit_invulnerable(const DotaGame *g,int index){return dota_omnislash_phase(g,index)==DOTA_OMNISLASH_ACTIVE || dota_extra_invulnerable(g,index) || dota_roster_invulnerable(g,index) || dota_items_invulnerable(g,index);}
int dota_skill_mana_cost(const DotaGame *g,int index,int skill){
    const DotaUnit *u;
    if(!g || (unsigned)index>=DOTA_HERO_COUNT || (unsigned)skill>=DOTA_SKILLS)return 0;
    u=&g->units[index];if(!dota_progression_rank(u,skill))return 0;if(u->type!=DOTA_HERO || (unsigned)u->hero_id>=DOTA_HERO_TYPES)return 0;
    if(dota_roster_is_hero(u))return dota_roster_mana(u,skill)*dota_items_mana_cost_percent((DotaGame*)g,index)/100;
    if(dota_extra_is_hero(u))return dota_extra_mana(u,skill)*dota_items_mana_cost_percent((DotaGame*)g,index)/100;
    return skill==2?0:skill_value(u,skill,"AbilityManaCost",skill_mana[u->hero_id][skill==3?2:skill][skill_rank(u,skill)-1])*dota_items_mana_cost_percent((DotaGame*)g,index)/100;
}
int dota_hero_assists(const DotaGame *g,int index){
    return g && (unsigned)index<DOTA_HERO_COUNT && g->units[index].type==DOTA_HERO?g->assists[index]:0;
}
typedef DotaItemWorldAttackTraits ItemAttackTraits;
static int item_attack_traits(const DotaGame *g,int index,ItemAttackTraits *out){
    const DotaItemWorldUnit *s;const DotaUnit *u;
    if((unsigned)index<DOTA_MAX_UNITS){
        const DotaJungleDefinition *d=dota_jungle_definition(&g->units[index]);
        if(d){DotaItemStats stats;dota_items_stats((DotaGame*)g,index,&stats);out->bat_ticks=d->bat_ticks;out->point_ms=d->point_ms;out->projectile_speed=d->projectile_speed;out->attack_speed_x100=clamp(d->attack_speed*100+(stats.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)+dota_extra_attack_speed_delta(g,index))*100,2000,70000);return 1;}
        if(g->units[index].type==DOTA_CREEP){const DotaLaneCreepSource *lane=&dota_lane_creep_source[g->units[index].tier!=0];DotaItemStats stats;dota_items_stats((DotaGame*)g,index,&stats);out->bat_ticks=lane->bat_ticks;out->point_ms=lane->point_ms;out->projectile_speed=lane->projectile_speed;out->attack_speed_x100=clamp(lane->attack_speed100+100*(stats.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)),2000,70000);return 1;}
        if(g->units[index].type==DOTA_HERO){const DotaUnit *actor=&g->units[index];const DotaProgressionBase *base=dota_progression_base(actor->hero_id);DotaItemStats stats;int str=0,agi=0,intel=0,bs,ba,bi;if(!base)return 0;dota_items_stats((DotaGame*)g,index,&stats);dota_progression_base_attributes100(actor,&bs,&ba,&bi);hero_stat_delta100(g,index,&str,&agi,&intel);out->bat_ticks=base->bat_ticks;out->point_ms=base->point_ms;out->projectile_speed=base->projectile_speed;out->attack_speed_x100=clamp(base->attack_speed100+ba+agi+(stats.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)+dota_extra_attack_speed_delta(g,index))*100,2000,70000);if(actor->hero_id==DOTA_HERO_LINA&&actor->skill_ticks[2]>0)out->attack_speed_x100=clamp(out->attack_speed_x100+skill_value(actor,2,"fiery_soul_attack_speed_bonus",7*dota_passive_rank(actor))*100*clamp(g->passive_stacks[index],0,fiery_soul_limit(g,index)),2000,70000);return 1;}
    }
    if(dota_item_world_attack_traits(g,index,out))return 1;
    if((unsigned)index>=DOTA_MAX_UNITS || index<DOTA_ITEM_UNIT_FIRST)return 0;
    s=&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];u=&g->units[index];
    if(s->kind!=ITEM_UNIT_SUMMON || s->source_item!=DOTA_ITEM_DEMONICON)return 0;
    {DotaItemStats st;dota_items_stats((DotaGame*)g,index,&st);
     out->bat_ticks=u->tier?20:15;out->attack_speed_x100=clamp(10900+100*(st.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)),2000,70000);}
    out->point_ms=u->tier?700:560;out->projectile_speed=u->tier?900:0;return 1;
}
static int attack_speed_x100(const DotaGame *g,int index){
    static const unsigned short agility[4]={1800,2100,3200,1600},gain[4]={170,240,280,180};
    const DotaUnit *u;int speed;DotaItemStats st;ItemAttackTraits traits;
    if(!g || index<0 || index>=DOTA_MAX_UNITS)return 10000;
    u=&g->units[index];
    if(item_attack_traits(g,index,&traits))return traits.attack_speed_x100;
    if(index>=DOTA_ITEM_UNIT_FIRST && g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION){int owner=dota_item_world_owner(g,index);if(owner>=0)return attack_speed_x100(g,owner);}
    if(u->type!=DOTA_HERO){
        dota_items_stats((DotaGame*)g,index,&st);
        speed=u->type==DOTA_TOWER?11000:
          (u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN)?dota_boss_attack_speed_percent(u)*100:
          u->type==DOTA_NEUTRAL?(u->tier?12500:15000):10000;
        return clamp(speed+100*(st.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)),2000,70000);
    }
    if(dota_extra_is_hero(u)){dota_items_stats((DotaGame*)g,index,&st);return clamp(dota_extra_attack_speed(g,index)+100*(st.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)),2000,70000);}
    if(u->type!=DOTA_HERO || (unsigned)u->hero_id>=4)return 10000;
    speed=(u->hero_id==2?11000:10000)+agility[u->hero_id]+gain[u->hero_id]*(u->level-1);
    if(index<DOTA_HERO_COUNT && u->type==DOTA_HERO && u->hero_id==DOTA_HERO_LINA && alive(u) && u->skill_ticks[2]>0)
        speed+=700*dota_passive_rank(u)*clamp(g->passive_stacks[index],0,fiery_soul_limit(g,index));
    dota_items_stats((DotaGame*)g,index,&st);
    {int str=0,agi=0,intel=0;dota_extra_stat_delta(g,index,&str,&agi,&intel);speed+=agi*100;}
    return clamp(speed+100*(st.value[ITEM_STAT_ATTACK_SPEED]+dota_roster_attack_speed_delta(g,index)),2000,70000);
}
int dota_attack_speed_percent(const DotaGame *g,int index){return attack_speed_x100(g,index)/100;}
static int move_speed_q16(const DotaGame *g,int index){
    const DotaUnit *u;long long speed;DotaItemStats st;int slow_res,extra,percent;
    if(!g || index<0 || index>=DOTA_MAX_UNITS)return 0;
    {int wind=dota_items_wind_move_source(g,index);if(wind>0)return wind*DOTA_MOVE_ONE/(DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ);}
    u=&g->units[index];if(u->speed<=0)return 0;
    speed=(long long)clamp(u->speed+dota_extra_move_flat(g,index),1,10000)*DOTA_MOVE_ONE;
    slow_res=clamp(dota_items_slow_resistance((DotaGame*)g,index)+dota_roster_slow_resist(g,index),0,100);extra=dota_extra_move_percent(g,index)*dota_roster_move_percent(g,index)/100;
    if(extra<100)extra=100-(100-extra)*(100-slow_res)/100;
    speed=speed*extra/100;
    dota_items_stats((DotaGame*)g,index,&st);percent=st.value[ITEM_STAT_MOVE_PERCENT]/100+dota_items_special_move_bonus(g,index);
    if(percent<0)percent=percent*(100-slow_res)/100;
    speed=speed*clamp(100+percent,0,1000)/100;
    if(dota_items_rooted(g,index))return 0;
    if(index<DOTA_HERO_COUNT && u->type==DOTA_HERO && u->hero_id==DOTA_HERO_LINA && u->skill_ticks[2]>0)
        speed=speed*(10000+dota_progression_value100(u,2,"fiery_soul_move_speed_bonus",(5+5*dota_passive_rank(u))*10)*clamp(g->passive_stacks[index],0,fiery_soul_limit(g,index)))/10000;
    if(u->type==DOTA_HERO){int extra=dota_progression_move_percent(g,index);speed=speed*(100+extra)/100;if(u->hero_id==DOTA_HERO_JUGGERNAUT&&u->skill_ticks[0]>0)speed+=(long long)skill_value(u,0,"bonus_movespeed",0)*DOTA_MOVE_ONE;}
    if(index<DOTA_HERO_COUNT&&u->type==DOTA_HERO){
        if(g->culling[index].until>g->tick)speed=speed*(100+g->culling[index].speed_percent)/100;
        if(u->hero_id==DOTA_HERO_AXE&&skill_rank(u,1)>0){int bonus=skill_value(u,1,"speed_bonus",0),count=0,i;if(bonus)for(i=0;i<DOTA_MAX_UNITS;++i)if(alive(&g->units[i])&&g->units[i].team!=u->team&&g->units[i].dot_ticks>0&&g->units[i].dot_source==index)++count;speed=speed*(100+bonus*count)/100;}
    }
    /* Source strength for applied slows; legacy fixtures without strength
     * retain the edition's generic50% slow. One existing slow slot is shared. */
    if(u->slow_ticks>0)speed=speed*(100-(u->buff_ticks>0?clamp(u->buff_ticks,0,99):50)*(100-slow_res)/100)/100;
    {int cap_source=dota_roster_move_cap_source(g,index);if(cap_source>0 && speed>(long long)cap_source*DOTA_MOVE_ONE)speed=(long long)cap_source*DOTA_MOVE_ONE;}
    {int haste=dota_rune_haste_speed(g,index);if(haste>0)speed=(long long)haste*DOTA_MOVE_ONE;}
    if(speed>(long long)DOTA_WORLD*DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ*DOTA_MOVE_ONE)
        speed=(long long)DOTA_WORLD*DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ*DOTA_MOVE_ONE;
    return (int)(speed/(DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ));
}
int dota_move_speed_source(const DotaGame *g,int index){
    return (int)((long long)move_speed_q16(g,index)*DOTA_SOURCE_UNITS_PER_WORLD*DOTA_HZ/DOTA_MOVE_ONE);
}
static void zero_bytes(void *p,unsigned bytes) { unsigned char *b=p;while(bytes--)*b++=0; }
static void omnislash_clear(DotaGame *g,int caster){
    if((unsigned)caster>=DOTA_HERO_COUNT)return;
    zero_bytes(&g->omnislash[caster],(unsigned)sizeof(g->omnislash[caster]));
    if(g->units[caster].type==DOTA_HERO && g->units[caster].hero_id==DOTA_HERO_JUGGERNAUT)g->units[caster].skill_ticks[3]=0;
}
static void omnislash_cancel_cast(DotaGame *g,int caster){
    if(dota_omnislash_phase(g,caster)==DOTA_OMNISLASH_CASTING)omnislash_clear(g,caster);
}
static void assist_sync(DotaGame *g,int index){
    int i;if((unsigned)index>=DOTA_HERO_COUNT || g->assist_generation[index]==g->units[index].generation)return;
    for(i=0;i<DOTA_HERO_COUNT;++i){g->assist_tick[index][i]=0;g->assist_tick[i][index]=0;}
    g->assist_generation[index]=g->units[index].generation;
    g->hero_dot_source_generation[index]=0;
}
static int root_i(unsigned n) {
    unsigned bit=1u<<30,result=0;
    while(bit>n)bit>>=2;
    while(bit) { if(n>=result+bit){n-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2; }
    return (int)result;
}
static int move_length(int dx_q16,int dy_q16){
    unsigned long long n=(unsigned long long)((long long)dx_q16*dx_q16)+
                         (unsigned long long)((long long)dy_q16*dy_q16);
    unsigned long long bit=1ull<<62,result=0;
    while(bit>n)bit>>=2;
    while(bit){if(n>=result+bit){n-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return (int)result;
}
static void event(DotaGame *g,int kind,int value) { g->last_event=kind;g->last_event_value=value;g->last_event_ticks=50; }
static int effect(DotaGame *g,int kind,int team,int x,int y,int xx,int yy,int radius,int ticks) {
    int i,best=0;
    for(i=0;i<DOTA_MAX_EFFECTS;++i) { if(!g->effects[i].active){best=i;break;}if(g->effects[i].ticks<g->effects[best].ticks)best=i; }
    g->effects[best].active=1;g->effects[best].kind=kind;g->effects[best].team=team;
    g->effects[best].x=x;g->effects[best].y=y;g->effects[best].to_x=xx;g->effects[best].to_y=yy;
    g->effects[best].radius=radius;g->effects[best].ticks=ticks;
    g->skill_effect_tags[best]=0;return best;
}

static void skill_effect(DotaGame *g,int caster,int skill,int kind,int x,int y,int xx,int yy,int radius,int ticks) {
    int slot=effect(g,kind,g->units[caster].team,x,y,xx,yy,radius,ticks);
    g->skill_effect_tags[slot]=1u+(unsigned)g->units[caster].hero_id*DOTA_SKILLS+(unsigned)skill;
}
static DotaSkillEvent *skill_event(DotaGame *,int,int,int,int,int,int,int);
static void passive_event(DotaGame *g,int caster,int target,int ticks,int step){
    DotaUnit *u=&g->units[caster];DotaSkillEvent *e;
    e=skill_event(g,DOTA_SKILL_EVENT_PASSIVE,caster,2,target,
                 target>=0?g->units[target].x:u->x,target>=0?g->units[target].y:u->y,step);
    e->duration_ticks=ticks;
}
static void passive_clear(DotaGame *g,int index){
    if(index<0 || index>=DOTA_HERO_COUNT)return;
    if(!dota_skill_is_passive(g->units[index].hero_id,2))return;
    g->passive_stacks[index]=g->passive_wait[index]=g->passive_mana_fraction[index]=g->passive_move_fraction[index]=0;
    g->units[index].skill_ticks[2]=g->units[index].cooldown[2]=0;
}
static void passive_sync(DotaGame *g,int index){
    DotaUnit *u=&g->units[index];
    if(index<0 || index>=DOTA_HERO_COUNT || u->type!=DOTA_HERO)return;
    if(!dota_skill_is_passive(u->hero_id,2))return;
    if(g->passive_generation[index]!=u->generation){passive_clear(g,index);g->passive_generation[index]=u->generation;}
    u->cooldown[2]=0;
    if(u->hero_id==DOTA_HERO_LINA && g->extra.hero[index].shadow_until>0 && g->extra.hero[index].shadow_until<=g->tick){DotaExtraHero *sh=&g->extra.hero[index];g->passive_stacks[index]=sh->shroud_until>g->tick?sh->shroud_rank:0;u->skill_ticks[2]=sh->shroud_until>g->tick?sh->shroud_until-g->tick:0;sh->shadow_until=0;}
    if(!alive(u)||!dota_passive_rank(u)){passive_clear(g,index);return;}
    if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && !u->skill_ticks[2]){
        u->skill_ticks[2]=1;passive_event(g,index,-1,1,dota_passive_rank(u));
    }
}
static void fiery_soul_hit(DotaGame *g,int caster,int target){
    DotaUnit *u;
    if(caster<0 || caster>=DOTA_HERO_COUNT)return;
    u=&g->units[caster];
    if(!alive(u) || u->type!=DOTA_HERO || u->hero_id!=DOTA_HERO_LINA || !dota_passive_rank(u) || dota_items_broken(g,caster) || structure(&g->units[target]))return;
    passive_sync(g,caster);
    if(g->extra.hero[caster].shadow_until>g->tick){DotaExtraHero *sh=&g->extra.hero[caster];sh->shroud_rank=clamp(sh->shroud_rank+1,0,skill_value(u,2,"fiery_soul_max_stacks",7));sh->shroud_until=g->tick+skill_time(u,2,"fiery_soul_stack_duration",16*DOTA_HZ);g->passive_stacks[caster]=12;}
    else g->passive_stacks[caster]=clamp(g->passive_stacks[caster]+1,0,skill_value(u,2,"fiery_soul_max_stacks",7));
    u->skill_ticks[2]=skill_time(u,2,"fiery_soul_stack_duration",16*DOTA_HZ);
    passive_event(g,caster,target,u->skill_ticks[2],g->passive_stacks[caster]);
}
static int blade_dance(DotaGame *g,int caster,int target,int spin){
    DotaUnit *u;int multiplier;
    if(caster<0 || caster>=DOTA_HERO_COUNT)return 100;
    u=&g->units[caster];
    if(!alive(u) || u->type!=DOTA_HERO ||
       u->hero_id!=DOTA_HERO_JUGGERNAUT || !dota_passive_rank(u) || dota_items_broken(g,caster) || structure(&g->units[target]) || g->units[target].type==DOTA_SUMMON)return 100;
    g->seed=g->seed*1664525u+1013904223u;
    if((int)((g->seed>>8)%100u)>=skill_value(u,2,"blade_dance_crit_chance",35))return 100;
    multiplier=skill_value(u,2,"blade_dance_crit_mult",120+20*dota_passive_rank(u));u->skill_ticks[2]=8;
    passive_event(g,caster,target,8,spin?-multiplier:multiplier);
    return multiplier;
}

int dota_rules_attack_critical(DotaGame *g,int source,int target){
    int own=100,other;if(!g||(unsigned)source>=DOTA_HERO_COUNT||(unsigned)target>=DOTA_MAX_UNITS)return 100;
    if(g->units[source].hero_id==DOTA_HERO_JUGGERNAUT)own=blade_dance(g,source,target,0);
    g->attack_crit_percent[source]=own;other=dota_progression_attack_critical(g,source,target);return own>other?own:other;
}
static DotaSkillEvent *skill_event(DotaGame *g,int kind,int caster,int skill,int target,int x,int y,int step) {
    const DotaUnit *u=&g->units[caster];DotaSkillEvent *e;
    unsigned serial=++g->skill_event_serial;
    if(g->skill_event_count<DOTA_MAX_SKILL_EVENTS)++g->skill_event_count;
    e=&g->skill_events[serial%DOTA_MAX_SKILL_EVENTS];
    e->serial=serial;e->kind=kind;e->caster=caster;e->caster_generation=u->generation;
    e->hero_id=u->hero_id;e->skill=skill;e->target=target;
    e->target_generation=target>=0?g->units[target].generation:0;
    e->origin_x=u->x;e->origin_y=u->y;
    e->target_x=target>=0?g->units[target].x:x;e->target_y=target>=0?g->units[target].y:y;
    e->started_tick=g->tick;e->duration_ticks=16;e->step=step;
    return e;
}

const DotaSkillEvent *dota_get_skill_event(const DotaGame *g,unsigned serial) {
    const DotaSkillEvent *e;
    if(!g || !g->skill_event_count || g->skill_event_serial-serial>=g->skill_event_count)return 0;
    e=&g->skill_events[serial%DOTA_MAX_SKILL_EVENTS];
    return e->serial==serial?e:0;
}

int dota_attack_style(const DotaUnit *u) {
    if(!u)return DOTA_ATTACK_MELEE;
    if(u->type==DOTA_HERO) {
        if(u->hero_id==DOTA_HERO_LINA)return DOTA_ATTACK_LINA;
        if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN)return DOTA_ATTACK_CM;
        if(u->hero_id==DOTA_HERO_NECROPHOS)return DOTA_ATTACK_NECROPHOS;
        if(u->hero_id==DOTA_HERO_SNIPER)return DOTA_ATTACK_SNIPER;
    }
    if(u->type==DOTA_CREEP && u->tier==1)
        return u->team==DOTA_RADIANT?DOTA_ATTACK_RADIANT_CREEP:DOTA_ATTACK_DIRE_CREEP;
    if(u->type==DOTA_NEUTRAL){const DotaJungleDefinition *d=dota_jungle_definition(u);if(d && d->projectile_speed>0)return DOTA_ATTACK_NEUTRAL;}
    if(u->type==DOTA_ANCIENT && dota_boss_projectile_speed(u)>0)return DOTA_ATTACK_NEUTRAL;
    if(u->type==DOTA_TOWER)return u->team==DOTA_RADIANT?DOTA_ATTACK_RADIANT_TOWER:DOTA_ATTACK_DIRE_TOWER;
    return DOTA_ATTACK_MELEE;
}
static int attack_style_for(const DotaGame *g,int index){
    const DotaUnit *u=&g->units[index];DotaUnit copy;
    if(index<DOTA_ITEM_UNIT_FIRST)return dota_attack_style(u);
    copy=*u;
    {const DotaItemWorldUnit *s=&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];
        if(s->kind==ITEM_UNIT_ILLUSION)copy.type=DOTA_HERO;
        else if(s->kind==ITEM_UNIT_DOMINATED){copy.type=s->source_type;copy.team=s->source_team;}
        else if(s->kind==ITEM_UNIT_SUMMON && copy.tier==1)return DOTA_ATTACK_NEUTRAL;
    }
    return dota_attack_style(&copy);
}

static void attack_event(DotaGame *g,int kind,const DotaProjectile *p,int duration) {
    unsigned serial=++g->attack_event_serial;DotaAttackEvent *e=&g->attack_events[serial%DOTA_MAX_ATTACK_EVENTS];
    if(g->attack_event_count<DOTA_MAX_ATTACK_EVENTS)++g->attack_event_count;
    e->serial=serial;e->kind=kind;e->style=p->style;e->source=p->source;
    e->source_generation=p->source_generation;e->source_type=p->source_type;e->team=p->team;
    e->target=p->target;e->target_generation=p->target_generation;
    e->origin_x=p->origin_x;e->origin_y=p->origin_y;e->target_x=p->target_x;e->target_y=p->target_y;
    e->started_tick=g->tick;e->duration_ticks=duration;e->launch_z=p->launch_z;e->aim_z=p->aim_z;
}

const DotaAttackEvent *dota_get_attack_event(const DotaGame *g,unsigned serial) {
    const DotaAttackEvent *e;
    if(!g || !g->attack_event_count || g->attack_event_serial-serial>=g->attack_event_count)return 0;
    e=&g->attack_events[serial%DOTA_MAX_ATTACK_EVENTS];
    return e->serial==serial?e:0;
}

static int summon_valid(const DotaGame *g,int summon) {
    int owner=g->summon_owner[summon];const DotaUnit *w=&g->units[DOTA_SUMMON_FIRST+summon];
    return owner>=0 && owner<DOTA_HERO_COUNT && g->units[owner].active
        && g->units[owner].type==DOTA_HERO && (g->units[owner].hero_id==DOTA_HERO_JUGGERNAUT || (g->units[owner].hero_id==DOTA_HERO_CRYSTAL_MAIDEN && w->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && w->tier==-1))
        && g->units[owner].generation==g->summon_owner_generation[summon]
        && w->team==g->units[owner].team && w->type==DOTA_SUMMON;
}
static int assist_hero_source(const DotaGame *g,int source){
    if((unsigned)source>=DOTA_MAX_UNITS)return -1;
    if(source<DOTA_HERO_COUNT && g->units[source].type==DOTA_HERO)return source;
    if(source>=DOTA_SUMMON_FIRST && source<DOTA_SUMMON_FIRST+DOTA_MAX_SUMMONS
       && summon_valid(g,source-DOTA_SUMMON_FIRST))return g->summon_owner[source-DOTA_SUMMON_FIRST];
    return -1;
}
static void assist_record(DotaGame *g,int victim,int source){
    int hero=assist_hero_source(g,source);
    if((unsigned)victim>=DOTA_HERO_COUNT || hero<0 || hero==victim || g->units[victim].type!=DOTA_HERO
       || (unsigned)g->units[hero].team>=2 || g->units[hero].team==g->units[victim].team)return;
    assist_sync(g,victim);assist_sync(g,hero);
    g->assist_tick[victim][hero]=g->tick+1;
}
static void assist_award(DotaGame *g,int victim,int team,int killer){
    int i;for(i=0;i<DOTA_HERO_COUNT;++i)assist_sync(g,i);
    if((unsigned)victim>=DOTA_HERO_COUNT)return;
    if((unsigned)team<2 && team!=g->units[victim].team)for(i=0;i<DOTA_HERO_COUNT;++i){
        int stamp=g->assist_tick[victim][i];
        if(i!=victim && i!=killer && g->units[i].type==DOTA_HERO && g->units[i].team==team && stamp>0
           && (unsigned)g->tick-(unsigned)(stamp-1)<=DOTA_ASSIST_WINDOW_TICKS)++g->assists[i];
    }
    for(i=0;i<DOTA_HERO_COUNT;++i)g->assist_tick[victim][i]=0;
    g->hero_dot_source_generation[victim]=0;
}

int dota_healing_ward(const DotaGame *g,int owner) {
    int i;if(!g || owner<0 || owner>=DOTA_HERO_COUNT)return -1;
    for(i=0;i<DOTA_MAX_SUMMONS;++i)if(g->summon_owner[i]==owner && g->summon_life[i]>0
        && alive(&g->units[DOTA_SUMMON_FIRST+i]) && summon_valid(g,i))return DOTA_SUMMON_FIRST+i;
    return -1;
}
int dota_healing_ward_radius_source(void){return 400;}
int dota_healing_ward_lifetime(const DotaGame *g,int owner){
    int ward;if(!g || (unsigned)owner>=10 || g->units[owner].hero_id!=DOTA_HERO_JUGGERNAUT)return 0;
    ward=dota_healing_ward(g,owner);
    return ward>=0?skill_time(&g->units[owner],1,"AbilityDuration",(16+2*clamp(g->units[ward].tier,1,4))*DOTA_HZ):skill_time(&g->units[owner],1,"AbilityDuration",(16+2*skill_rank(&g->units[owner],1))*DOTA_HZ);
}

int dota_move_healing_ward(DotaGame *g,int owner,int x,int y) {
    DotaUnit *w;int ward;
    if(!g || g->paused || g->state!=DOTA_PLAYING || owner<0 || owner>=DOTA_HERO_COUNT)return 0;
    sync_navigation(g);ward=dota_healing_ward(g,owner);
    if(ward<0)return 0;
    x=clamp(x,0,DOTA_WORLD-1);y=clamp(y,0,DOTA_WORLD-1);
    if(!dota_nav_nearest(x,y,256,&x,&y))return 0;
    w=&g->units[ward];w->move_order=1;w->move_x=x;w->move_y=y;w->target=-1;return 1;
}

static void summon_remove(DotaGame *g,int index) {
    int summon=index-DOTA_SUMMON_FIRST,owner;
    if(summon<0 || summon>=DOTA_MAX_SUMMONS)return;
    owner=g->summon_owner[summon];
    if(owner>=0 && owner<DOTA_HERO_COUNT && g->units[owner].generation==g->summon_owner_generation[summon]
       && g->units[owner].type==DOTA_HERO && g->units[owner].hero_id==DOTA_HERO_JUGGERNAUT)
        g->units[owner].skill_ticks[1]=0;
    g->units[index].active=0;g->units[index].hp=0;g->units[index].move_order=0;g->units[index].target=-1;
    g->summon_owner[summon]=-1;g->summon_owner_generation[summon]=0;g->summon_life[summon]=0;
}

static int summon_slot(DotaGame *g) {
    int i;
    for(i=0;i<DOTA_MAX_SUMMONS;++i) {
        int index=DOTA_SUMMON_FIRST+i;
        if(g->units[index].active && g->units[index].type==DOTA_SUMMON
           && (!alive(&g->units[index]) || !summon_valid(g,i) || g->summon_life[i]<=0))summon_remove(g,index);
        if(!g->units[index].active)return index;
    }
    return -1;
}

static void summon_create(DotaGame *g,int index,int owner,int x,int y) {
    DotaUnit *u=&g->units[index],*hero=&g->units[owner];int i,summon=index-DOTA_SUMMON_FIRST,generation=u->generation+1;
    zero_bytes(u,(unsigned)sizeof(*u));u->generation=generation;u->active=1;u->type=DOTA_SUMMON;
    u->team=hero->team;u->hero_id=-1;u->lane=hero->lane;u->hp=u->max_hp=1+dota_hero_has_shard(g,owner);u->speed=325;
    u->tier=skill_rank(hero,1);
    u->x=x;u->y=y;u->target=u->taunt_source=u->dot_source=-1;
    u->respawn_ticks=g->tick; /* SUMMON uses this otherwise-unused field as birth tick. */
    for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
    g->summon_owner[summon]=owner;g->summon_owner_generation[summon]=hero->generation;
    g->summon_life[summon]=skill_time(hero,1,"AbilityDuration",(16+2*u->tier)*DOTA_HZ);hero->skill_ticks[1]=g->summon_life[summon];
    hero->skill_x=x;hero->skill_y=y;
}

int dota_rules_crystal_clone(DotaGame *g,int owner){
    DotaUnit *u,*hero;int index=summon_slot(g),summon,i,generation;if(index<0)return -1;
    hero=&g->units[owner];u=&g->units[index];generation=u->generation+1;summon=index-DOTA_SUMMON_FIRST;
    zero_bytes(u,sizeof(*u));u->active=1;u->generation=generation;u->type=DOTA_SUMMON;u->hero_id=DOTA_HERO_CRYSTAL_MAIDEN;u->tier=-1;u->team=hero->team;u->lane=hero->lane;u->hp=u->max_hp=150;u->x=hero->x;u->y=hero->y;u->target=u->taunt_source=u->dot_source=-1;u->respawn_ticks=g->tick;
    for(i=0;i<DOTA_ITEM_SLOTS;i++)u->inventory[i]=-1;
    g->summon_owner[summon]=owner;g->summon_owner_generation[summon]=hero->generation;g->summon_life[summon]=100;
    for(i=0;i<DOTA_MAX_PROJECTILES;i++)if(g->projectiles[i].state&&g->projectiles[i].target==owner)g->projectiles[i].state=0;
    return index;
}
static void legal_position(DotaUnit *u,int x,int y){
    int px=x,py=y;
    if(!dota_nav_nearest(x,y,256,&px,&py))dota_nav_nearest(x,y,DOTA_WORLD,&px,&py);
    u->x=px;u->y=py;
}
static void sync_navigation(const DotaGame *g){
    unsigned mask=0,fillers=0;int team,lane,tier,index;
    dota_nav_set_item_trees(g->item_world.tree,DOTA_ITEM_TREES);
    dota_nav_set_static_trees(g);
    for(team=0;team<2;++team)for(lane=0;lane<3;++lane)for(tier=0;tier<2;++tier){
        index=g->towers[team][lane][tier];
        if((unsigned)index<DOTA_MAX_UNITS && g->units[index].type==DOTA_TOWER && alive(&g->units[index]))mask|=1u<<((team*3+lane)*2+tier);
    }
    for(team=0;team<2;++team)for(tier=0;tier<DOTA_HIGH_TOWERS_PER_TEAM;++tier){
        index=high_tower_slot(team,tier);
        if(g->units[index].type==DOTA_TOWER && alive(&g->units[index]))mask|=1u<<(12+team*DOTA_HIGH_TOWERS_PER_TEAM+tier);
    }
    for(index=0;index<DOTA_FILLER_COUNT;++index)if(g->units[DOTA_FILLER_FIRST+index].type==DOTA_BUILDING && alive(&g->units[DOTA_FILLER_FIRST+index]))fillers|=1u<<index;
    dota_nav_set_all_structures(mask,fillers,dota_barracks_alive_mask(g));
}
static void neutral_home(int camp,int role,int *x,int *y){
    static const signed char offset[5][2]={{0,0},{-16,12},{16,12},{-18,-12},{18,-12}};
    role=clamp(role,0,4);*x=dota_neutral_camps[camp].x+offset[role][0];*y=dota_neutral_camps[camp].y+offset[role][1];
    dota_nav_nearest(*x,*y,256,x,y);
}

int dota_walkable(int x,int y) {
    return dota_nav_walkable(x,y);
}

/* Every ordinary movement step tests all crossed cells and graph edges.
 * Axis sliding only uses an axis the caller actually requested. */
static int move_commit(DotaGame *g,DotaUnit *u,DotaMovementState *m,int sx,int sy){
    int x_q16=clamp(m->x_q16+sx,0,(DOTA_WORLD-1)*DOTA_MOVE_ONE);
    int y_q16=clamp(m->y_q16+sy,0,(DOTA_WORLD-1)*DOTA_MOVE_ONE);
    int nx=x_q16/DOTA_MOVE_ONE,ny=y_q16/DOTA_MOVE_ONE,spent;
    if(!dota_extra_allow_move(g,(int)(u-g->units),nx,ny) || !dota_roster_allow_move(g,(int)(u-g->units),nx,ny))return 0;
    if(!dota_items_wind_move_source(g,(int)(u-g->units)) && !dota_items_free_path(g,(int)(u-g->units)) && !dota_nav_segment(u->x,u->y,nx,ny))return 0;
    spent=move_length(x_q16-m->x_q16,y_q16-m->y_q16);
    if(spent>m->remaining_q16)return 0;
    m->remaining_q16-=spent;m->x_q16=x_q16;m->y_q16=y_q16;u->x=nx;u->y=ny;return 1;
}
static void move_step_direction(DotaGame *g,DotaUnit *u,int x,int y,int direct) {
    int index=(int)(u-g->units),dx,dy,d,sx,sy;DotaMovementState *m;
    if(index<0 || index>=DOTA_MAX_UNITS || u->stun_ticks || u->channel_ticks || u->speed<=0)return;
    m=&g->movement[index];
    if(m->generation!=u->generation || m->x_q16/DOTA_MOVE_ONE!=u->x || m->y_q16/DOTA_MOVE_ONE!=u->y){
        m->x_q16=u->x*DOTA_MOVE_ONE;m->y_q16=u->y*DOTA_MOVE_ONE;m->generation=u->generation;m->tick=-1;
    }
    if(m->tick!=g->tick){m->tick=g->tick;m->remaining_q16=move_speed_q16(g,index);}
    if(m->remaining_q16<=0)return;
    x=clamp(x,0,DOTA_WORLD-1);y=clamp(y,0,DOTA_WORLD-1);
    dx=direct?(x-u->x)*DOTA_MOVE_ONE:x*DOTA_MOVE_ONE-m->x_q16;
    dy=direct?(y-u->y)*DOTA_MOVE_ONE:y*DOTA_MOVE_ONE-m->y_q16;
    d=move_length(dx,dy);
    if(!d)return;
    if(d<=m->remaining_q16){sx=dx;sy=dy;}
    else{sx=(int)((long long)dx*m->remaining_q16/d);sy=(int)((long long)dy*m->remaining_q16/d);}
    if(move_commit(g,u,m,sx,sy))return;
    if(sx && move_commit(g,u,m,sx,0))return;
    if(sy && move_commit(g,u,m,0,sy))return;
    /* Never bank a blocked tick or its fractional position for a later burst. */
    m->remaining_q16=0;m->x_q16=u->x*DOTA_MOVE_ONE;m->y_q16=u->y*DOTA_MOVE_ONE;
}
static void move_step(DotaGame *g,DotaUnit *u,int x,int y){move_step_direction(g,u,x,y,0);}
static void navigation_budget(DotaGame *g){
    if(g->navigation_tick!=g->tick){g->navigation_tick=g->tick;g->navigation_searches=g->navigation_nodes=0;}
}
static void move_towards(DotaGame *g,DotaUnit *u,int x,int y){
    DotaUnitRoute *route=&g->unit_routes[u-g->units];int target_x,target_y,reset;
    if(u->stun_ticks || u->channel_ticks || u->speed<=0)return;
    if(dota_items_wind_move_source(g,(int)(u-g->units)) || dota_items_free_path(g,(int)(u-g->units))){route->count=route->index=0;move_step(g,u,x,y);return;}
    if(!dota_nav_nearest(x,y,256,&target_x,&target_y))return;
    if(dota_nav_segment(u->x,u->y,target_x,target_y)){route->count=route->index=0;move_step(g,u,target_x,target_y);return;}
    reset=route->generation!=u->generation || dist2(route->goal_x,route->goal_y,target_x,target_y)>64*64;
    if(reset){route->count=route->index=0;route->repath_tick=0;route->generation=u->generation;}
    while(route->index<route->count){
        int px,py;
        if(route->complete && route->index+1==route->count){px=route->goal_x;py=route->goal_y;}
        else dota_nav_cell_center(route->cells[route->index],&px,&py);
        if(u->x==px && u->y==py){++route->index;continue;}
        if(!dota_nav_segment(u->x,u->y,px,py)){route->count=route->index=0;route->repath_tick=0;break;}
        move_step(g,u,px,py);
        if(u->x==px && u->y==py && g->movement[u-g->units].remaining_q16>0){++route->index;continue;}
        return;
    }
    navigation_budget(g);
    if(g->tick<route->repath_tick || g->navigation_searches>=4 || g->navigation_nodes>=4096)return;
    {DotaPoint points[DOTA_ROUTE_POINTS];unsigned before=dota_nav_stats()->expanded_nodes;int i,complete=0;
     int count=dota_nav_find_path(u->x,u->y,target_x,target_y,points,DOTA_ROUTE_POINTS,(unsigned)(4096-g->navigation_nodes),&complete);
     ++g->navigation_searches;g->navigation_nodes+=(int)(dota_nav_stats()->expanded_nodes-before);
     route->goal_x=(unsigned short)target_x;route->goal_y=(unsigned short)target_y;route->generation=u->generation;
     route->count=(unsigned short)count;route->index=0;route->complete=complete;route->repath_tick=g->tick+(count?4:20);
     for(i=0;i<count;++i)route->cells[i]=(unsigned short)dota_nav_cell_at(points[i].x,points[i].y);
     if(count){int px,py;
        if(complete && count==1){px=target_x;py=target_y;}else dota_nav_cell_center(route->cells[0],&px,&py);
        move_step(g,u,px,py);}}
}
static void move_for_attack(DotaGame *g,DotaUnit *u,const DotaUnit *v){
    DotaUnitRoute *route=&g->unit_routes[u-g->units];
    int reach=attack_reach_for(g,u,v),dx=u->x-v->x,dy=u->y-v->y;
    int d=root_i((unsigned)dist2(u->x,u->y,v->x,v->y)),x,y,oldx=u->x,oldy=u->y;
    if(dota_items_free_path(g,(int)(u-g->units))){move_towards(g,u,v->x,v->y);return;}
    if(route->generation==u->generation && route->complete && source_range(dist2(route->goal_x,route->goal_y,v->x,v->y),reach)){
        move_towards(g,u,route->goal_x,route->goal_y);return;
    }
    /* Fine legal movement near a target reaches cell edges: an unreachable
     * model center must not strand a melee unit at a coarser cell center. */
    if(source_range(d*d,reach+256)){
        move_step(g,u,v->x,v->y);if(u->x!=oldx || u->y!=oldy)return;
        {int xs[16],ys[16],ds[16],count=0,a,b,i,j,best,complete;
         DotaPoint points[DOTA_ROUTE_POINTS];
         for(a=-2;a<=2;++a)for(b=-2;b<=2;++b)if(a==-2 || a==2 || b==-2 || b==2){
             /* The perimeter has lengths2, sqrt(5), sqrt(8) in Q16.
              * These are the exact floors from move_length, not rounded. */
             int other=a==-2 || a==2?b:a;
             int len=other==0?131072:(other==-1 || other==1?146542:185363);
             x=v->x+(int)((long long)a*(reach>4?reach-4:0)*DOTA_MOVE_ONE/(4ll*len));
             y=v->y+(int)((long long)b*(reach>4?reach-4:0)*DOTA_MOVE_ONE/(4ll*len));
             if(dota_nav_walkable(x,y)){xs[count]=x;ys[count]=y;ds[count++]=dist2(u->x,u->y,x,y);}
         }
         for(i=0;i<count;++i){
             best=-1;for(j=0;j<count;++j)if(ds[j]>=0 && (best<0 || ds[j]<ds[best]))best=j;
             if(best<0)break;
             x=xs[best];y=ys[best];ds[best]=-1;
             if(dota_nav_segment(u->x,u->y,x,y)){move_step(g,u,x,y);return;}
             navigation_budget(g);if(g->navigation_searches>=4 || g->navigation_nodes>=3072)break;
             {unsigned before=dota_nav_stats()->expanded_nodes;int n=dota_nav_find_path(u->x,u->y,x,y,points,DOTA_ROUTE_POINTS,768,&complete);
              ++g->navigation_searches;g->navigation_nodes+=(int)(dota_nav_stats()->expanded_nodes-before);
              if(complete && n){
                  route->generation=u->generation;route->goal_x=(unsigned short)x;route->goal_y=(unsigned short)y;
                  route->count=(unsigned short)n;route->index=0;route->complete=1;route->repath_tick=g->tick+4;
                  for(j=0;j<n;++j)route->cells[j]=(unsigned short)dota_nav_cell_at(points[j].x,points[j].y);
                  move_towards(g,u,x,y);return;
              }
             }
         }
        }
    }
    x=v->x+(d?dx*(reach>4?reach-4:0)/(4*d):0);
    y=v->y+(d?dy*(reach>4?reach-4:0)/(4*d):0);
    move_towards(g,u,x,y);
}
/* Player orders use the same fine graph, retaining up to64 smoothed corners.
 * Long routes can continue with another bounded search after the last corner. */
static void click_path(DotaGame *g,int x,int y) {
    DotaUnit *u=&g->units[g->player];
    unsigned before=dota_nav_stats()->expanded_nodes;
    g->player_path_count=g->player_path_index=0;
    if(dota_items_wind_move_source(g,g->player) || dota_items_free_path(g,g->player)){g->player_path[0].x=x;g->player_path[0].y=y;g->player_path_count=1;g->player_path_complete=1;return;}
    g->player_path_count=dota_nav_find_path(u->x,u->y,x,y,g->player_path,DOTA_MAX_PATH,4096,&g->player_path_complete);
    navigation_budget(g);++g->navigation_searches;g->navigation_nodes+=(int)(dota_nav_stats()->expanded_nodes-before);
}

static int targetable(const DotaGame *g,int index) {
    const DotaUnit *u;int lane,team;
    if(index<0 || index>=DOTA_MAX_UNITS || !alive(&g->units[index]) || dota_unit_invulnerable(g,index))return 0;
    u=&g->units[index];team=u->team;
    if(structure(u) && (unsigned)team>=2)return 0;
    if(u->type==DOTA_TOWER && u->tier==1 && alive(&g->units[g->towers[team][u->lane][0]]))return 0;
    /* Explicit H1 dependency policy, separate from backdoor/fortification. */
    if(u->type==DOTA_TOWER && u->tier==2 && (alive(&g->units[g->towers[team][u->lane][0]]) || alive(&g->units[g->towers[team][u->lane][1]])))return 0;
    if(dota_barracks_kind(u))return !dota_high_tower_alive(g,team,u->lane);
    if((u->type==DOTA_TOWER && u->tier==3) || u->type==DOTA_BUILDING){
        for(lane=0;lane<3;++lane)if(!dota_high_tower_alive(g,team,lane))return 1;
        return 0;
    }
    if(u->type==DOTA_BASE)return !dota_high_tower_alive(g,team,3) && !dota_high_tower_alive(g,team,4);
    return 1;
}

int dota_nearest_enemy(const DotaGame *g,int team,int x,int y,int radius) {
    int i,best=-1,best_d=radius*radius+1,observer=-1,observer_d=0x7fffffff;
    for(i=0;i<DOTA_MAX_UNITS;++i)if(alive(&g->units[i]) && g->units[i].team==team){int d=dist2(x,y,g->units[i].x,g->units[i].y);if(d<observer_d){observer=i;observer_d=d;}}
    for(i=0;i<DOTA_MAX_UNITS;++i)if(g->units[i].team!=team && targetable(g,i)) {
        const DotaUnit *eye=&g->units[g->player];
        if(observer>=0 && !dota_unit_visible(g,observer,i))continue;
        if(!dota_pit_same_region(eye->team==team?eye->x:x,eye->team==team?eye->y:y,g->units[i].x,g->units[i].y))continue;
        int d=dist2(x,y,g->units[i].x,g->units[i].y);
        if(d<best_d){best_d=d;best=i;}
    }
    return best;
}
/* Query-local field constants; source-defined NPC/boss hulls retain the
 * original getter. No active-list, team or generation cache survives a call. */
static int search_target_hull_source(const DotaUnit *u){
    switch(u->type){
    case DOTA_HERO:case DOTA_TORMENTOR:return 24;
    case DOTA_TOWER:return 144;
    case DOTA_BUILDING:return dota_barracks_kind(u)?144:96;
    case DOTA_CREEP:return u->tier?8:16;
    case DOTA_SUMMON:return 8;
    default:return dota_unit_hull_source(u);
    }
}
static int enemy_source(const DotaGame *g,int observer,int radius,int ignore_tormentor){
    const DotaUnit *eye=&g->units[observer];int team=eye->team,x=eye->x,y=eye->y;
    int i,best=-1,best_d=0x7fffffff;
    int box=radius>=0 && radius<=DOTA_WORLD*8 && (unsigned)x<DOTA_WORLD && (unsigned)y<DOTA_WORLD;
    int half=box?(radius+144)/4:0,left=x-half,right=x+half,top=y-half,bottom=y+half;
    for(i=0;i<DOTA_MAX_UNITS;++i){
        const DotaUnit *v=&g->units[i];int d;
        if(v->team==team || !alive(v))continue;
        int outside=box && (v->x<left || v->x>right || v->y<top || v->y>bottom);
        if(outside || (d=dist2(x,y,v->x,v->y))>=best_d || !source_range(d,radius+search_target_hull_source(v))){
            /* This is the exact original far prefix, including ignored
             * visibility calls before the first shared-sight refresh. */
            if(!dota_sight_is_current(g) && targetable(g,i) &&
               (!ignore_tormentor || (v->type!=DOTA_TORMENTOR && v->type!=DOTA_ROSHAN)))
                (void)dota_unit_visible(g,observer,i);
            continue;
        }
        if(targetable(g,i) && (!ignore_tormentor ||
           (v->type!=DOTA_TORMENTOR && v->type!=DOTA_ROSHAN)) && dota_unit_visible(g,observer,i)){
            best_d=d;best=i;
        }
    }
    return best;
}

int dota_next_level_xp(int level) { return dota_turbo_level_xp(level); }

static void recalc_with_stats(DotaGame *g,int index,const DotaItemStats *st) {
    DotaUnit *u=&g->units[index];const DotaProgressionBase *base;
    int str=0,agi=0,intel=0,bs=0,ba=0,bi=0,primary;
    if(u->type!=DOTA_HERO || !(base=dota_progression_base(u->hero_id)))return;
    dota_progression_base_attributes100(u,&bs,&ba,&bi);hero_stat_delta100(g,index,&str,&agi,&intel);
    str=clamp(str,100-bs,1000000);agi=clamp(agi,100-ba,1000000);intel=clamp(intel,100-bi,1000000);
    primary=ITEM_STAT_STRENGTH+base->primary;
    u->max_hp=base->health+bs*22/100+str*22/100+st->value[ITEM_STAT_HEALTH]+dota_roster_max_health_bonus(g,index);
    u->max_mana=base->mana+bi*12/100+intel*12/100+st->value[ITEM_STAT_MANA];
    u->damage=base->weapon_damage100/100+(base->primary==0?bs:base->primary==1?ba:bi)/100;
    u->damage+=st->value[ITEM_STAT_DAMAGE]+st->value[primary]/100+(base->primary==0?str:base->primary==1?agi:intel)/100;
    {int percent=100+dota_rune_base_damage_percent(g,index),bonus=0;
     if(dota_roster_is_hero(u)){percent+=dota_roster_base_damage_percent(g,index)-100;bonus=dota_roster_bonus_damage(g,index);}
     u->damage=(u->damage-st->value[ITEM_STAT_DAMAGE])*percent/100+st->value[ITEM_STAT_DAMAGE]+bonus;}
    u->calculated_armor100=base->armor100+ba/6+agi/6+st->value[ITEM_STAT_ARMOR];u->armor=u->calculated_armor100/100;
    u->range=base->range+st->value[ITEM_STAT_ATTACK_RANGE]+dota_roster_attack_range_bonus(g,index);u->speed=base->speed+st->value[ITEM_STAT_MOVE_FLAT];
    u->max_hp=u->max_hp*(dota_items_special_max_hp_percent(g,index)+dota_item_world_max_health_percent(g,index)-100)/100;
    u->max_mana=u->max_mana*dota_items_special_max_mana_percent(g,index)/100;
    u->max_hp=clamp(u->max_hp,1,100000);u->max_mana=clamp(u->max_mana,0,100000);
    u->hp=clamp(u->hp,0,u->max_hp);u->mana=clamp(u->mana,0,u->max_mana);
}
static void recalc(DotaGame *g,int index) {
    DotaItemStats st;
    if(g->units[index].type!=DOTA_HERO || !dota_progression_base(g->units[index].hero_id))return;
    dota_items_stats(g,index,&st);recalc_with_stats(g,index,&st);
}

static void add_xp(DotaGame *g,int index,int amount) {
    DotaUnit *u=&g->units[index];int living=alive(u);u->xp+=dota_turbo_xp_award(g,index,amount);
    while(u->level<DOTA_LEVEL_CAP && u->xp>=dota_next_level_xp(u->level)) {
        int old_hp=u->max_hp,old_mana=u->max_mana;
        u->xp-=dota_next_level_xp(u->level);++u->level;dota_progression_level_changed(g,index);if(index!=g->player)dota_progression_bot(g,index);recalc(g,index);
        if(living){
            u->hp+=u->max_hp-old_hp;u->mana+=u->max_mana-old_mana;
            effect(g,DOTA_FX_HEAL,u->team,u->x,u->y,u->x,u->y,80,16);
        }
    }
    if(u->level==DOTA_LEVEL_CAP)u->xp=0;
}

static void finish(DotaGame *g,int losing_team) {
    int i;
    if(g->state!=DOTA_PLAYING)return;
    g->state=losing_team==DOTA_RADIANT?DOTA_LOST:DOTA_WON;
    /* A finished match no longer advances: retire flights now, never freeze
     * an active pool forever or apply another hit after the deciding death. */
    for(i=0;i<DOTA_MAX_PROJECTILES;++i)g->projectiles[i].state=0;
    for(i=0;i<DOTA_HERO_COUNT;++i)omnislash_clear(g,i);
    event(g,10,g->state);
}

int dota_tormentor_index(const DotaGame *g) {
    return g && g->units[DOTA_TORMENTOR_INDEX].type==DOTA_TORMENTOR && alive(&g->units[DOTA_TORMENTOR_INDEX])?DOTA_TORMENTOR_INDEX:-1;
}
int dota_hero_has_shard(const DotaGame *g,int index) {
    return g && (unsigned)index<DOTA_HERO_COUNT && g->units[index].type==DOTA_HERO && ((g->tormentor.shard_mask&(1u<<index))!=0 || dota_items_has_upgrade(g,index,1));
}
static void tormentor_killed(DotaGame *g,int team) {
    DotaUnit *u=&g->units[DOTA_TORMENTOR_INDEX];int i,j,recipient=-1,lowest=0x7fffffff;
    u->active=0;u->hp=0;u->target=-1;u->respawn_ticks=DOTA_TORMENTOR_RESPAWN;
    g->tormentor.spawn_tick=g->tick+DOTA_TORMENTOR_RESPAWN;
    if((unsigned)team>=2)return;
    /* Deterministic H1 choice among unupgraded heroes by current net worth.
     * The award is a real permanent Q/W/R cooldown bonus in this replica,
     * not a claim that all four original Aghanim upgrades are implemented. */
    for(i=0;i<DOTA_HERO_COUNT;++i)if(g->units[i].type==DOTA_HERO && g->units[i].team==team && !dota_hero_has_shard(g,i)) {
        int worth=g->units[i].gold;
        for(j=0;j<DOTA_ITEM_SLOTS;++j)if((unsigned)g->units[i].inventory[j]<DOTA_ITEM_TYPES)worth+=dota_items_cost(g->units[i].inventory[j]);
        /* Paid cargo/stash remains the owner's net worth while in transit. */
        for(j=0;j<dota_courier_pending_count(g,i);++j){int item=dota_courier_pending_item(g,i,j);
            if((unsigned)item<DOTA_ITEM_TYPES)worth+=dota_items_cost(item);}
        if(worth<lowest){lowest=worth;recipient=i;}
    }
    if(recipient>=0){g->tormentor.shard_mask|=1u<<recipient;event(g,12,recipient);}
    for(i=0;i<DOTA_HERO_COUNT;++i)if(g->units[i].type==DOTA_HERO && g->units[i].team==team && i!=recipient)
        g->units[i].gold+=dota_turbo_gold_reward(recipient>=0?175:415);
    if(recipient<0)event(g,13,team);
}

static void kill_unit(DotaGame *g,int victim,int killer,int source_team) {
    DotaUnit *v=&g->units[victim];int i,eligible=0,share,remainder,team=source_team,assist_team=(unsigned)source_team<2?source_team:-1;
    int reincarnate=v->type==DOTA_HERO && dota_bosses_aegis_on_death(g,victim);
    int assist_killer=assist_hero_source(g,killer);
    int gold=v->type==DOTA_HERO?110:(v->type==DOTA_CREEP?(v->tier?48:37):(v->type==DOTA_NEUTRAL?(v->tier?13:17):(v->type==DOTA_BUILDING?68:100)));
    int xp=v->type==DOTA_HERO?100:(v->type==DOTA_CREEP?(v->tier?69:57):(v->type==DOTA_NEUTRAL?(v->tier?24:32):0));
    /* Original npc_units barracks carry zero NPC kill bounty. */
    if(dota_barracks_kind(v))gold=0;
    if(v->type==DOTA_NEUTRAL){const DotaJungleDefinition *d=dota_jungle_definition(v);if(d){gold=(d->gold_min+d->gold_max)/2;xp=d->xp;}}
    {int owner=dota_item_world_owner(g,killer);if(owner>=0){killer=owner;source_team=g->units[owner].team;team=source_team;assist_team=team;assist_killer=owner;}}
    if((unsigned)killer<DOTA_HERO_COUNT && g->units[killer].type==DOTA_HERO){
        if(v->type==DOTA_CREEP)gold=dota_items_special_bounty_roll(g,killer,v->tier?43:34,v->tier?52:39);
        else if(v->type==DOTA_NEUTRAL){const DotaJungleDefinition *d=dota_jungle_definition(v);if(d)gold=dota_items_special_bounty_roll(g,killer,d->gold_min,d->gold_max);}
    }
    if(v->active&&v->hp<=0)dota_sight_invalidate();
    if(dota_item_world_on_death(g,victim,killer))return;
    dota_neutral_on_kill(g,victim,killer);
    dota_extra_on_kill(g,victim,killer);dota_roster_on_kill(g,victim,killer);
    if(!reincarnate)dota_items_on_death(g,victim,killer);
    if(reincarnate)gold=xp=0;
    if(team<0)team=v->team<2?1-v->team:-1;
    if((v->type==DOTA_ANCIENT || v->type==DOTA_ROSHAN) && dota_bosses_on_killed(g,victim,killer,source_team))return;
    effect(g,DOTA_FX_DEATH,v->team,v->x,v->y,v->x,v->y,structure(v)?150:60,16);
    if((unsigned)killer<DOTA_MAX_UNITS)dota_items_clear_hunger(g,killer);
    if(v->type==DOTA_SUMMON){if(v->hero_id==DOTA_HERO_CRYSTAL_MAIDEN&&v->tier==-1)crystal_clone_shatter(g,victim);else summon_remove(g,victim);return;} /* no unit/hero bounty */
    if(v->type==DOTA_TORMENTOR){tormentor_killed(g,team);return;}
    if(killer>=0 && killer<DOTA_MAX_UNITS && g->units[killer].team!=v->team) {
        DotaUnit *k=&g->units[killer];team=k->team;
        assist_team=(unsigned)team<2?team:-1;
        if(k->type==DOTA_HERO){k->gold+=dota_turbo_gold_reward(gold);if(v->type==DOTA_HERO && !reincarnate)++k->kills;}
    }
    for(i=0;i<DOTA_HERO_COUNT;++i)if(g->units[i].team==team && alive(&g->units[i]) && source_range(dist2(g->units[i].x,g->units[i].y,v->x,v->y),DOTA_XP_RADIUS_SOURCE))++eligible;
    share=eligible?xp/eligible:0;remainder=eligible?xp%eligible:0;
    for(i=0;i<DOTA_HERO_COUNT;++i) {
        DotaUnit *h=&g->units[i];
        if(h->team==team && alive(h) && source_range(dist2(h->x,h->y,v->x,v->y),DOTA_XP_RADIUS_SOURCE)){add_xp(g,i,share+(remainder>0));if(remainder>0)--remainder;}
        if(v->type==DOTA_TOWER && h->team==team)h->gold+=dota_turbo_gold_reward(60);
    }
    if(v->type==DOTA_HERO) {
        g->culling[victim].until=0;
        if(!reincarnate)assist_award(g,victim,assist_team,assist_killer);
        omnislash_clear(g,victim);
        passive_clear(g,victim);
        if(!reincarnate){if(team>=0 && team<2)++g->kills[team];++v->deaths;}
        v->respawn_ticks=reincarnate?100:dota_turbo_respawn_ticks(v->level);v->move_order=0;v->target=-1;
        if(victim==g->player)g->player_path_count=g->player_path_index=0;
        v->stun_ticks=v->slow_ticks=v->taunt_ticks=v->dot_ticks=0;
        v->buff_ticks=v->channel_ticks=v->heal_ticks=0;
        for(i=0;i<DOTA_SKILLS;++i)v->skill_ticks[i]=0;
    }else if(v->type==DOTA_CREEP || v->type==DOTA_NEUTRAL)v->active=0;
    else if(v->type==DOTA_BASE)finish(g,v->team);
    /* Reuse only a genuinely killed lane T3. Defer replacement until the
     * whole causing attack/status callback stack has completed. */
    if(v->type==DOTA_TOWER && v->tier==2)v->respawn_ticks=-1;
    if(v->type==DOTA_TOWER || v->type==DOTA_BUILDING)sync_navigation(g);
}

static void damage_snapshot_ex(DotaGame *g,int victim,int amount,int source,int generation,int source_team,int magical,int attack) {
    DotaUnit *v;int armor,dealt,raw_damage=amount,is_magic=magical==1 || magical==3 || magical==5 || magical==7 || magical==9,deferred=magical==4,aura=magical==3 || magical==5;
    if(g->state!=DOTA_PLAYING || amount<=0 || victim<0 || victim>=DOTA_MAX_UNITS || !targetable(g,victim))return;
    v=&g->units[victim];
    if(victim>=DOTA_ITEM_UNIT_FIRST){int kind=g->item_world.unit[victim-DOTA_ITEM_UNIT_FIRST].kind;
        if((kind==ITEM_UNIT_OBSERVER||kind==ITEM_UNIT_SENTRY)&&(!attack||magical))return;}
    if(v->type==DOTA_SUMMON && v->hero_id<0){if(magical)return;amount=1;}
    if(!magical && dota_extra_physical_immune(g,victim))return;
    if(v->type==DOTA_ROSHAN && (source<0 || (unsigned)source>=DOTA_MAX_UNITS || !dota_unit_visible(g,source,victim)))return;
    /* Resolve the original shooter before applying its item modifiers. */
    if(source<0 || source>=DOTA_MAX_UNITS || g->units[source].generation!=generation || g->units[source].team!=source_team)source=-1;
    if(!deferred && (unsigned)source<DOTA_MAX_UNITS && g->units[source].generation==generation)
        amount=dota_turbo_tower_damage(&g->units[source],v,amount);
    if(!deferred){
        if(!g->items.reflection_guard && magical!=9){
            if(magical && source>=0)amount=amount*(100+dota_items_spell_amp_percent(g,source))*dota_items_outgoing_spell_percent(g,source)/10000;
            amount=dota_items_special_outgoing_damage(g,source,victim,amount,magical);
        }
        dota_items_capture_incoming(g,source,victim,amount,magical);
    }
    if(!deferred)amount=amount*dota_bosses_incoming_damage_percent(g,victim)/100;
    if(structure(v)) {if(is_magic)amount=amount*60/100;}
    raw_damage=amount;armor=dota_rules_armor100(g,victim);
    if(is_magic){const DotaJungleDefinition *jungle=dota_jungle_definition(v);int resist=jungle?jungle->magic_resist:((v->type==DOTA_CREEP || dota_barracks_kind(v))?0:(v->type==DOTA_TORMENTOR?55:((v->type==DOTA_ANCIENT || v->type==DOTA_ROSHAN)?dota_boss_magic_resist(v):25)));resist=dota_item_world_magic_resist(g,victim,resist)+dota_extra_magic_resist_delta(g,victim);amount=amount*clamp(100-resist,0,300)/100;if(v->type==DOTA_HERO&&v->hero_id==DOTA_HERO_LINA&&v->skill_ticks[2]>0&&!dota_items_broken(g,victim)){int soul=skill_value(v,2,"fiery_soul_magic_resist",0)*clamp(g->passive_stacks[victim],0,fiery_soul_limit(g,victim));amount=amount*(100-clamp(soul,0,95))/100;}}
    else if(!magical){int absolute=armor<0?-armor:armor;amount=(int)((long long)amount*(10000+6*absolute-(armor*6))/(10000+6*absolute));}
    if(v->type==DOTA_HERO && v->hero_id==DOTA_HERO_JUGGERNAUT && v->skill_ticks[0] && is_magic)amount/=2;
    if(amount<1)amount=1;
    if(!deferred){amount=dota_item_world_incoming_from(g,victim,source,amount,magical,attack);amount=dota_items_before_damage(g,source,victim,amount,magical);amount=dota_roster_before_damage(g,source,victim,amount,magical);amount=dota_progression_before_damage(g,source,victim,amount,magical);}
    if(!deferred)amount=dota_rune_before_damage(g,victim,amount);
    if(amount<=0)return;
    assist_record(g,victim,source);
    v->last_damage_team=source_team;v->last_damage_source=source;
    dota_bosses_on_damage(g,victim,source);dota_extra_on_damage(g,victim,source);
    if(source>=0 && !aura && !deferred) {
        if(g->units[source].type==DOTA_HERO && g->units[source].team!=v->team)v->heal_ticks=0;
    }
    if(!aura && !deferred && v->type==DOTA_NEUTRAL && source>=0 && g->units[source].team!=DOTA_NEUTRAL_TEAM
       && v->lane>=0 && v->lane<DOTA_NEUTRAL_CAMPS && g->neutral_state[v->lane]!=DOTA_NEUTRAL_RETURN) {
        int i;g->neutral_state[v->lane]=DOTA_NEUTRAL_AGGRO;
        for(i=dota_jungle_first(v->lane);i<dota_jungle_end(v->lane);++i)
            if(alive(&g->units[i]))g->units[i].target=source;
    }
    /* Lane creeps may be pulled by a real hit, but never acquire a neutral
     * camp merely because it is behind them inside the general bot radius.
     * Their unused skill fields hold a short leash, without another array. */
    if(!aura && !deferred && v->type==DOTA_CREEP && source>=0 && g->units[source].team!=v->team
       && (g->units[source].type==DOTA_HERO || g->units[source].type==DOTA_NEUTRAL)) {
        if(v->skill_ticks[0]<=g->tick || v->target!=source){v->skill_x=v->x;v->skill_y=v->y;}
        if(g->tick>=v->skill_ticks[1]){v->target=source;v->skill_ticks[0]=g->tick+46;v->skill_ticks[1]=g->tick+60;}
    }
    if(is_magic && magical!=9 && source>=0 && source_team!=v->team && g->progression.skill_pending && g->progression.skill_source==source && g->progression.skill_target==victim)fiery_soul_hit(g,source,victim);
    if(v->type==DOTA_TORMENTOR) {
        int dealt=amount>v->hp?v->hp:amount;
        g->tormentor.last_damage_tick=g->tick;
        g->tormentor.reflect_damage_q200+=dealt*(60+3*g->tormentor.growth_minutes);
        if(!g->tormentor.reflect_wait)g->tormentor.reflect_wait=16;
    }
    if(v->type==DOTA_SUMMON && v->hero_id<0)amount=1;
    dealt=amount>v->hp?v->hp:amount;v->hp-=amount;
    if(!deferred)dota_rune_on_damage(g,victim,source,dealt);
    dota_progression_on_damage(g,source,victim,raw_damage,dealt,magical);
    if(!deferred&&magical!=9)dota_items_after_damage(g,source,victim,dealt,magical);
    if(!deferred)dota_roster_after_damage(g,source,victim,dealt,magical);
    if(v->hp<=0){int credited=dota_extra_kill_source(g,victim,source);if(credited!=source && (unsigned)credited<DOTA_MAX_UNITS){source=credited;source_team=g->units[source].team;}v->hp=0;kill_unit(g,victim,source,source_team);}
}

static void damage_snapshot(DotaGame *g,int victim,int amount,int source,int generation,int source_team,int magical){damage_snapshot_ex(g,victim,amount,source,generation,source_team,magical,0);}

/* Only landed basic attacks call this hook. Magical pulses and reflected
 * Helix damage do not recursively count as incoming attacks. */
static void received_attack(DotaGame *g,int victim,int source,int source_team){
    DotaUnit *u;int threshold,i;
    dota_bosses_on_attack_hit(g,source,victim);
    dota_extra_on_attack_hit(g,source,victim);
    if(victim<0 || victim>=DOTA_HERO_COUNT || g->state!=DOTA_PLAYING)return;
    u=&g->units[victim];
    if(!alive(u) || u->type!=DOTA_HERO || u->hero_id!=DOTA_HERO_AXE || !dota_passive_rank(u) || dota_items_broken(g,victim) || source_team==u->team)return;
    passive_sync(g,victim);threshold=skill_value(u,2,"trigger_attacks",8-dota_passive_rank(u));
    g->passive_stacks[victim]=clamp(g->passive_stacks[victim]+1,0,threshold);
    if(g->passive_stacks[victim]<threshold || g->passive_wait[victim])return;
    g->passive_stacks[victim]=0;g->passive_wait[victim]=skill_time(u,2,"AbilityCooldown",6);u->skill_ticks[2]=12;
    passive_event(g,victim,source>=0 && source<DOTA_MAX_UNITS?source:-1,12,threshold);
    /* Installed275Source radius, source damage/counter/CD; damage is pure. */
    for(i=0;i<DOTA_MAX_UNITS;++i)if(targetable(g,i) && g->units[i].team!=u->team && !structure(&g->units[i]) &&
        source_range(dist2(u->x,u->y,g->units[i].x,g->units[i].y),dota_skill_effect_radius_source(g,victim,2)))
        damage_snapshot(g,i,skill_value(u,2,"damage",80+20*dota_passive_rank(u)),victim,u->generation,u->team,2);
}

static void damage(DotaGame *g,int victim,int amount,int source,int magical) {
    int valid=source>=0 && source<DOTA_MAX_UNITS;
    damage_snapshot(g,victim,amount,source,valid?g->units[source].generation:0,valid?g->units[source].team:-1,magical);
}

static void area_damage(DotaGame *g,int caster,int x,int y,int radius,int amount,int stun,int slow) {
    int i;DotaUnit *c=&g->units[caster];
    radius=dota_rules_aoe_radius(g,caster,radius);
    if(c->hero_id==DOTA_HERO_CRYSTAL_MAIDEN)for(i=0;i<DOTA_MAX_SUMMONS;i++){
        int at=DOTA_SUMMON_FIRST+i;const DotaUnit *clone=&g->units[at];
        if(clone->active&&clone->hero_id==DOTA_HERO_CRYSTAL_MAIDEN&&clone->tier==-1&&g->summon_owner[i]==caster&&source_range(dist2(x,y,clone->x,clone->y),radius))crystal_clone_shatter(g,at);
    }
    for(i=0;i<DOTA_MAX_UNITS;++i)if(g->units[i].team!=c->team && targetable(g,i) && !structure(&g->units[i])
        && (g->units[i].type!=DOTA_ROSHAN || dota_unit_visible(g,caster,i))
        && source_range(dist2(x,y,g->units[i].x,g->units[i].y),radius)) {
        if(c->hero_id==DOTA_HERO_LINA)dota_progression_ability_damage(g,caster,i,amount,1);else damage(g,i,amount,caster,1);
        if(alive(&g->units[i]) && !structure(&g->units[i]) && !dota_items_debuff_immune(g,i)) {
            int actual_stun=resisted_duration(g,i,stun),actual_slow=resisted_duration(g,i,slow);
            if(g->units[i].stun_ticks<actual_stun)g->units[i].stun_ticks=actual_stun;
            if(stun>0 && g->units[i].type==DOTA_HERO && g->units[i].hero_id==DOTA_HERO_CRYSTAL_MAIDEN) {
                g->units[i].channel_ticks=0;g->units[i].skill_ticks[3]=0;
            }
            if(g->units[i].slow_ticks<actual_slow)g->units[i].slow_ticks=actual_slow;
            if(slow>0 && c->hero_id==DOTA_HERO_CRYSTAL_MAIDEN)g->units[i].buff_ticks=c->channel_ticks>0?40:-skill_value(c,0,"movespeed_slow",-(10+10*skill_rank(c,0)));
        }
    }
}

static void crystal_clone_shatter(DotaGame *g,int index){
    int n=index-DOTA_SUMMON_FIRST,owner,x,y,i,r,life,radius;DotaUnit *u;
    if((unsigned)n>=DOTA_MAX_SUMMONS)return;
    owner=g->summon_owner[n];
    if((unsigned)owner>=DOTA_HERO_COUNT)return;
    u=&g->units[owner];x=g->units[index].x;y=g->units[index].y;
    summon_remove(g,index);r=skill_rank(u,1);radius=dota_rules_aoe_radius(g,owner,450);
    skill_effect(g,owner,1,DOTA_FX_BURST,x,y,x,y,radius,12);
    if(r<=0)return;
    life=skill_time(u,1,"duration",20+10*r);
    for(i=0;i<DOTA_MAX_UNITS;i++)if(targetable(g,i)&&g->units[i].team!=u->team&&!structure(&g->units[i])&&source_range(dist2(x,y,g->units[i].x,g->units[i].y),radius)&&!dota_items_debuff_immune(g,i)){
        int duration=life,dps=dota_progression_value100(u,1,"damage_per_second",10000);
        if(g->units[i].type==DOTA_CREEP||g->units[i].type==DOTA_NEUTRAL)dps=dps*skill_value(u,1,"creep_multiplier",4);
        dota_items_apply_effect(g,owner,i,IE_ROOT,duration,1,DOTA_ITEM_AGHANIMS_SHARD);
        dota_items_apply_effect(g,owner,i,IE_DISARM,duration,1,DOTA_ITEM_AGHANIMS_SHARD);
        dota_items_apply_effect(g,owner,i,IE_DOT,duration,dps,DOTA_ITEM_AGHANIMS_SHARD);
        skill_event(g,DOTA_SKILL_EVENT_STRIKE,owner,1,i,g->units[i].x,g->units[i].y,duration);
    }
}

static int omnislash_target(const DotaGame *g,int caster,int target){
    const DotaUnit *u;
    if(!targetable(g,target) || g->units[target].team==g->units[caster].team || !dota_unit_visible(g,caster,target))return 0;
    u=&g->units[target];
    return u->type==DOTA_HERO || u->type==DOTA_CREEP || u->type==DOTA_NEUTRAL || u->type==DOTA_TORMENTOR || u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN;
}
static int omnislash_landing(const DotaGame *g,int caster,int target,int step,int *x,int *y){
    static const int dx[4]={24,-24,-24,24},dy[4]={24,24,-24,-24};
    const DotaUnit *v=&g->units[target];int i,p,range=dota_hero_defs[DOTA_HERO_JUGGERNAUT].range+dota_unit_hull_source(v);
    (void)caster;
    for(i=0;i<4;++i){
        p=(step+i)&3;
        if(dota_nav_nearest(v->x+dx[p],v->y+dy[p],96,x,y) && source_range(dist2(*x,*y,v->x,v->y),range))return 1;
    }
    return 0;
}
static int omnislash_cast_target(const DotaGame *g,int caster,int x,int y){
    const DotaUnit *u=&g->units[caster];int i,target=-1,best=250*250+1,px,py;
    for(i=0;i<DOTA_MAX_UNITS;++i)if(dota_rules_cast_target_matches(g,caster,i) && omnislash_target(g,caster,i)
       && source_range(dist2(u->x,u->y,g->units[i].x,g->units[i].y),DOTA_OMNISLASH_CAST_RANGE_SOURCE)){
        int d=dist2(x,y,g->units[i].x,g->units[i].y);
        if(d<best && omnislash_landing(g,caster,i,0,&px,&py)){best=d;target=i;}
    }
    return target;
}
static int omnislash_queue(DotaGame *g,int caster,int target){
    DotaUnit *u=&g->units[caster];DotaOmnislashState *s=&g->omnislash[caster];
    zero_bytes(s,(unsigned)sizeof(*s));s->generation=u->generation;s->phase=DOTA_OMNISLASH_CASTING;
    s->phase_ticks=(DOTA_OMNISLASH_CAST_TICKS*100+dota_items_special_cast_speed_percent(g,caster)-1)/dota_items_special_cast_speed_percent(g,caster);s->target=target;s->target_generation=g->units[target].generation;
    s->remaining_ticks=skill_time(u,3,"duration",55+5*dota_omnislash_rank(u));u->skill_ticks[3]=0;u->move_order=0;u->target=-1;
    if(caster==g->player)g->player_path_count=g->player_path_index=0;
    return 1;
}

static void culling_bonus(DotaGame *g,int caster){
    const DotaUnit *u;int i,duration,radius;if((unsigned)caster>=DOTA_HERO_COUNT)return;u=&g->units[caster];duration=skill_time(u,3,"speed_duration",120);radius=skill_value(u,3,"speed_aoe",900);
    if(u->hero_id!=DOTA_HERO_AXE)return;
    g->culling[caster].permanent_armor100+=dota_progression_value100(u,3,"armor_per_stack",100);
    for(i=0;i<DOTA_HERO_COUNT;++i)if(alive(&g->units[i])&&g->units[i].team==u->team&&source_range(dist2(u->x,u->y,g->units[i].x,g->units[i].y),radius)){
        g->culling[i].until=g->tick+duration;g->culling[i].speed_percent=skill_value(u,3,"speed_bonus",20);g->culling[i].armor100=dota_progression_value100(u,3,"armor_bonus",1000);
    }
}
static int legacy_target_skill(int hero,int skill){return (hero==DOTA_HERO_AXE && (skill==1 || skill==3)) || (hero==DOTA_HERO_LINA && skill==3) || (hero==DOTA_HERO_CRYSTAL_MAIDEN && skill==1);}
static int apply_target_skill(DotaGame *g,int caster,int target,int hero,int skill,int rank){
    DotaUnit *u=&g->units[caster],*v=&g->units[target];
    if(hero==DOTA_HERO_AXE && skill==1){
        if(dota_items_debuff_immune(g,target))return 0;
        if(u->hero_id==DOTA_HERO_AXE && dota_hero_has_shard(g,caster)){
            dota_items_battle_hunger(g,caster,target,skill_time(u,1,"duration",240),dota_progression_value100(u,1,"damage_per_second",(8+4*rank)*100),skill_value(u,1,"slow",14+4*rank),1);
            return skill_time(u,1,"duration",240);
        }
        assist_record(g,target,caster);v->dot_ticks=resisted_duration(g,target,skill_time(u,1,"duration",240));v->dot_damage=skill_value(u,1,"damage_per_second",8+4*rank);v->dot_source=caster;v->slow_ticks=v->dot_ticks;v->buff_ticks=skill_value(u,1,"slow",14+4*rank);
        if((unsigned)target<DOTA_HERO_COUNT && v->type==DOTA_HERO)g->hero_dot_source_generation[target]=u->generation;
        return 240;
    }
    if(hero==DOTA_HERO_AXE){int amount=skill_value(u,3,"damage",175+100*rank);
        if(v->type!=DOTA_TORMENTOR && v->type!=DOTA_ROSHAN && v->hp<=amount){int killed=v->type==DOTA_HERO;v->hp=0;kill_unit(g,target,caster,u->team);if(killed && u->hero_id==DOTA_HERO_AXE){u->cooldown[3]=0;culling_bonus(g,caster);}}
        else damage(g,target,amount,caster,2);
        skill_effect(g,caster,skill,DOTA_FX_LINE,u->x,u->y,v->x,v->y,25,16);return 16;
    }
    if(hero==DOTA_HERO_LINA){dota_progression_ability_damage(g,caster,target,skill_value(u,3,"damage",220+180*rank),1);skill_effect(g,caster,skill,DOTA_FX_LINE,u->x,u->y,v->x,v->y,25,16);return 16;}
    damage(g,target,skill_value(u,1,"damage_per_second",100)*skill_time(u,1,"duration",20+10*rank)/DOTA_HZ,caster,1);
    if(alive(v) && !dota_items_debuff_immune(g,target)){v->stun_ticks=resisted_duration(g,target,skill_time(u,1,"duration",20+10*rank));if(v->type==DOTA_HERO && v->hero_id==DOTA_HERO_CRYSTAL_MAIDEN){v->channel_ticks=0;v->skill_ticks[3]=0;}}
    return 20+10*rank;
}
static int cast_for_impl(DotaGame *g,int caster,int skill,int x,int y) {
    DotaUnit *u=&g->units[caster];int target=-1,range,i,ward=-1,mana_cost,rank;
    DotaSkillEvent *visual;
    int player=caster==g->player;
    if(!g->paused && g->state==DOTA_PLAYING && u->type==DOTA_HERO && u->hero_id==DOTA_HERO_JUGGERNAUT && skill==1 && dota_healing_ward(g,caster)>=0)return dota_move_healing_ward(g,caster,x,y);
    if(dota_skill_is_passive(u->hero_id,skill))return 0;
    if(dota_roster_is_hero(u))return dota_roster_cast(g,caster,skill,x,y);
    if(g->paused || g->state!=DOTA_PLAYING || !alive(u) || u->type!=DOTA_HERO || skill<0 || skill>=4 || !dota_progression_rank(u,skill) || u->stun_ticks || dota_roster_stun(g,caster) || u->channel_ticks || u->taunt_ticks || dota_omnislash_phase(g,caster))return 0;
    if(dota_items_silenced(g,caster))return 0;
    if(dota_extra_is_hero(u))return dota_extra_cast(g,caster,skill,x,y);
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==1) {
        if(dota_healing_ward(g,caster)>=0)return dota_move_healing_ward(g,caster,x,y);
        ward=summon_slot(g);if(ward<0){if(player)event(g,4,skill);return 0;}
    }
    if(dota_items_silenced(g,caster))return 0;
    rank=skill_rank(u,skill);range=dota_skill_cast_range_source(g,caster,skill);
    if(!rank){if(player)event(g,3,skill);return 0;}
    if(u->cooldown[skill]){if(player)event(g,2,skill);return 0;}
    mana_cost=dota_skill_mana_cost(g,caster,skill);
    if(dota_items_available_mana(g,caster)<mana_cost){if(player)event(g,1,skill);return 0;}
    x=clamp(x,0,DOTA_WORLD-1);y=clamp(y,0,DOTA_WORLD-1);
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==3){
        target=omnislash_cast_target(g,caster,x,y);
        if(target<0){int candidate=dota_nearest_enemy(g,u->team,x,y,250);if(player)event(g,candidate>=0 && omnislash_target(g,caster,candidate)?15:4,skill);return 0;}
        return omnislash_queue(g,caster,target);
    }
    if((u->hero_id==DOTA_HERO_AXE && (skill==1 || skill==3)) ||
       (u->hero_id==DOTA_HERO_LINA && skill==3) ||
       (u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && skill==1)) {
        int best=250*250+1;
        for(i=0;i<DOTA_MAX_UNITS;++i)if(dota_rules_cast_target_matches(g,caster,i) && targetable(g,i) && g->units[i].team!=u->team && !structure(&g->units[i]) && dota_unit_visible(g,caster,i)){
            int d=dist2(x,y,g->units[i].x,g->units[i].y);if(d<best){target=i;best=d;}
        }
        if(target<0) {
            if(player)event(g,4,skill);
            return 0;
        }
        if(structure(&g->units[target])){if(player)event(g,4,skill);return 0;}
        if(!source_range(dist2(u->x,u->y,g->units[target].x,g->units[target].y),range)){if(player)event(g,15,skill);return 0;}
    }else if(!range){x=u->x;y=u->y;}
    else if(!source_range(dist2(u->x,u->y,x,y),range)){
        if(player)event(g,15,skill);
        return 0;
    }
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==1 && !dota_nav_nearest(x,y,256,&x,&y)){
        if(player)event(g,4,skill);
        return 0;
    }
    if(u->hero_id==DOTA_HERO_LINA && skill==0){
        int dx=x-u->x,dy=y-u->y,d;
        if(!dx && !dy)dx=u->team==DOTA_RADIANT?1:-1;
        d=move_length(dx*DOTA_MOVE_ONE,dy*DOTA_MOVE_ONE);
        x=clamp(u->x+(int)((long long)dx*1075*DOTA_MOVE_ONE/(4ll*d)),0,DOTA_WORLD-1);
        y=clamp(u->y+(int)((long long)dy*1075*DOTA_MOVE_ONE/(4ll*d)),0,DOTA_WORLD-1);
    }
    g->item_world.last_mana_spent[caster]=mana_cost;dota_items_spend_mana(g,caster,mana_cost);dota_progression_on_cast(g,caster,mana_cost);u->cooldown[skill]=dota_skill_cooldown_ticks(g,caster,skill);dota_items_on_cast(g,caster);
    visual=skill_event(g,DOTA_SKILL_EVENT_CAST,caster,skill,target,x,y,0);
    if(u->hero_id==DOTA_HERO_LINA && skill==3 && dota_hero_has_shard(g,caster) && dota_passive_rank(u)>0){DotaExtraHero *sh=&g->extra.hero[caster];if(sh->shadow_until<=g->tick){sh->shroud_rank=clamp(g->passive_stacks[caster],0,7);sh->shroud_until=g->tick+u->skill_ticks[2];}sh->shadow_until=g->tick+100;g->passive_stacks[caster]=12;if(u->skill_ticks[2]<100)u->skill_ticks[2]=100;}
    if(target>=0 && (dota_bosses_spell_block(g,target) || dota_items_blocks_spell(g,caster,target)))return 1;
    if(target>=0 && legacy_target_skill(u->hero_id,skill)){
        int reflected=dota_items_reflects_spell(g,target),hero=u->hero_id;
        dota_items_on_target_spell(g,caster,target);
        if(!alive(&g->units[target]))return 1;
        visual->duration_ticks=apply_target_skill(g,caster,target,hero,skill,rank);
        if(reflected && alive(&g->units[target]) && alive(u)){
            DotaSkillEvent *r=skill_event(g,DOTA_SKILL_EVENT_CAST,target,skill,caster,u->x,u->y,0);r->hero_id=hero;
            r->duration_ticks=apply_target_skill(g,target,caster,hero,skill,rank);
        }
        return 1;
    }
    skill_effect(g,caster,skill,DOTA_FX_BURST,x,y,x,y,160,14);
    switch(u->hero_id) {
        case DOTA_HERO_AXE:
            if(skill==0) {
                u->skill_ticks[0]=skill_time(u,0,"duration",36+6*rank);
                for(i=0;i<DOTA_MAX_UNITS;++i)if(targetable(g,i) && g->units[i].team!=u->team && !structure(&g->units[i]) && source_range(dist2(u->x,u->y,g->units[i].x,g->units[i].y),dota_skill_effect_radius_source(g,caster,0))) {
                    if(g->units[i].type==DOTA_ROSHAN && !dota_unit_visible(g,caster,i))continue;
                    assist_record(g,i,caster);g->units[i].taunt_ticks=resisted_duration(g,i,u->skill_ticks[0]);g->units[i].taunt_source=caster;
                    if(dota_hero_has_shard(g,caster) && skill_rank(u,1)>0)dota_items_battle_hunger(g,caster,i,skill_time(u,1,"duration",240),dota_progression_value100(u,1,"damage_per_second",1200),skill_value(u,1,"slow",18),1);
                    if(g->units[i].type==DOTA_HERO && g->units[i].hero_id==DOTA_HERO_CRYSTAL_MAIDEN) {
                        g->units[i].channel_ticks=0;g->units[i].skill_ticks[3]=0;
                    }
                }
            }else if(skill==1){
                assist_record(g,target,caster); /* A real newly applied slow counts before the first DoT pulse. */
                if(dota_hero_has_shard(g,caster)){dota_items_battle_hunger(g,caster,target,skill_time(u,1,"duration",240),dota_progression_value100(u,1,"damage_per_second",(8+4*rank)*100),skill_value(u,1,"slow",14+4*rank),1);break;}
                g->units[target].dot_ticks=resisted_duration(g,target,240);g->units[target].dot_damage=skill_value(u,1,"damage_per_second",8+4*rank);g->units[target].dot_source=caster;g->units[target].slow_ticks=g->units[target].dot_ticks;
                g->units[target].buff_ticks=14+4*rank; /* existing unused buff field now stores actual slow percent */
                if((unsigned)target<DOTA_HERO_COUNT && g->units[target].type==DOTA_HERO)g->hero_dot_source_generation[target]=u->generation;
            }
            else {
                DotaUnit *v=&g->units[target];int amount=skill_value(u,3,"damage",175+100*rank);
                if(v->type!=DOTA_TORMENTOR && v->type!=DOTA_ROSHAN && v->hp<=amount) {int hero_kill=v->type==DOTA_HERO;v->hp=0;kill_unit(g,target,caster,u->team);if(hero_kill){u->cooldown[3]=0;culling_bonus(g,caster);}}
                else damage(g,target,amount,caster,2);
                skill_effect(g,caster,skill,DOTA_FX_LINE,u->x,u->y,v->x,v->y,25,16);
            }break;
        case DOTA_HERO_LINA:
            if(skill==0) {
                int dx=x-u->x,dy=y-u->y,len=dx*dx+dy*dy;
                for(i=0;i<DOTA_MAX_UNITS;++i)if(targetable(g,i) && !structure(&g->units[i]) && g->units[i].team!=u->team) {
                    int t=len?(int)(((long long)(g->units[i].x-u->x)*dx+(long long)(g->units[i].y-u->y)*dy)*256/len):0;
                    int xx=u->x+dx*clamp(t,0,256)/256,yy=u->y+dy*clamp(t,0,256)/256;
                    if(source_range(dist2(xx,yy,g->units[i].x,g->units[i].y),dota_rules_aoe_radius(g,caster,275-75*clamp(t,0,256)/256)))dota_progression_ability_damage(g,caster,i,skill_value(u,0,"dragon_slave_damage",5+60*rank),1);
                }
                skill_effect(g,caster,skill,DOTA_FX_LINE,u->x,u->y,x,y,69,18);
            }else if(skill==1){dota_trees_cut_radius(g,x,y,dota_rules_aoe_radius(g,caster,skill_value(u,1,"light_strike_array_aoe",250)));area_damage(g,caster,x,y,skill_value(u,1,"light_strike_array_aoe",250),skill_value(u,1,"light_strike_array_damage",35+45*rank),skill_time(u,1,"light_strike_array_stun_duration",16+8*rank),0);}
            else {dota_progression_ability_damage(g,caster,target,skill_value(u,3,"damage",220+180*rank),1);skill_effect(g,caster,skill,DOTA_FX_LINE,u->x,u->y,g->units[target].x,g->units[target].y,25,16);}
            break;
        case DOTA_HERO_JUGGERNAUT:
            if(skill==0)u->skill_ticks[0]=skill_time(u,0,"duration",100);
            else if(skill==1)summon_create(g,ward,caster,x,y);
            break;
        default:
            if(skill==0)area_damage(g,caster,x,y,skill_value(u,0,"radius",425),skill_value(u,0,"nova_damage",60+50*rank),0,skill_time(u,0,"duration",80));
            else if(skill==1) {
                damage(g,target,skill_value(u,1,"damage_per_second",100)*skill_time(u,1,"duration",20+10*rank)/DOTA_HZ,caster,1); /* H1 frontloads the source total Frostbite damage. */
                if(alive(&g->units[target])) {
                    g->units[target].stun_ticks=resisted_duration(g,target,20+10*rank);
                    if(g->units[target].type==DOTA_HERO && g->units[target].hero_id==DOTA_HERO_CRYSTAL_MAIDEN) {
                        g->units[target].channel_ticks=0;g->units[target].skill_ticks[3]=0;
                    }
                }
            }
            else {u->skill_ticks[3]=200;u->channel_ticks=200;}
            break;
    }
    if(u->skill_ticks[skill]>0)visual->duration_ticks=u->skill_ticks[skill];
    else if(u->hero_id==DOTA_HERO_AXE && skill==1)visual->duration_ticks=240;
    else if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && skill==1)visual->duration_ticks=20+10*rank;
    return 1;
}

static int cast_for(DotaGame *g,int caster,int skill,int x,int y){int result=cast_for_impl(g,caster,skill,x,y);if(result)dota_sight_invalidate();return result;}

int dota_cast(DotaGame *g,int skill,int x,int y) {
    int result;
    if(!g || (unsigned)skill>DOTA_SKILLS || (unsigned)g->player>=DOTA_HERO_COUNT)return 0;
    if(!g->paused && g->state==DOTA_PLAYING){dota_objective_cancel(g);dota_rune_cancel(g);}
    if(!g->paused && g->state==DOTA_PLAYING && (skill==4?g->units[g->player].hero_id!=DOTA_HERO_SVEN:!dota_skill_is_passive(g->units[g->player].hero_id,skill) && dota_progression_rank(&g->units[g->player],skill)))dota_items_cancel_teleport(g,g->player);
    sync_navigation(g);if(skill==4){result=dota_roster_is_hero(&g->units[g->player])?dota_roster_cast_extra(g,g->player,x,y):dota_extra_cast_shard(g,g->player,x,y);if(result)dota_sight_invalidate();return result;}
    return cast_for(g,g->player,skill,x,y);
}
int dota_rules_cast_target_matches(const DotaGame *g,int caster,int candidate){
    if(!g || (unsigned)candidate>=DOTA_MAX_UNITS)return 0;
    return caster!=g->player || !g->cast_target_plus_one ||
        (g->cast_target_plus_one==candidate+1 && g->units[candidate].generation==g->cast_target_generation);
}
int dota_cast_on_unit(DotaGame *g,int skill,int target,int generation){
    int result,old_target,old_generation;DotaUnit *u;
    if(!g || (unsigned)skill>4 || (unsigned)target>=DOTA_MAX_UNITS || (unsigned)g->player>=DOTA_HERO_COUNT)return 0;
    u=&g->units[target];
    if(!alive(u) || u->generation!=generation || !dota_unit_visible(g,g->player,target))return 0;
    if(!g->paused && g->state==DOTA_PLAYING){dota_objective_cancel(g);dota_rune_cancel(g);}
    if(!g->paused && g->state==DOTA_PLAYING && (skill==4 || (!dota_skill_is_passive(g->units[g->player].hero_id,skill) && dota_progression_rank(&g->units[g->player],skill))))dota_items_cancel_teleport(g,g->player);
    old_target=g->cast_target_plus_one;old_generation=g->cast_target_generation;
    g->cast_target_plus_one=target+1;g->cast_target_generation=generation;sync_navigation(g);
    if(skill==4)result=dota_roster_is_hero(&g->units[g->player])?dota_roster_cast_extra(g,g->player,u->x,u->y):dota_extra_cast_shard(g,g->player,u->x,u->y);
    else result=cast_for(g,g->player,skill,u->x,u->y);
    g->cast_target_plus_one=old_target;g->cast_target_generation=old_generation;
    if(result&&skill==4)dota_sight_invalidate();
    return result;
}

static int at_fountain(const DotaGame *g,int index) {
    const DotaUnit *u;DotaPoint p;
    if(index<0 || index>=DOTA_HERO_COUNT)return 0;
    u=&g->units[index];if(!alive(u) || u->type!=DOTA_HERO || (unsigned)u->team>=2)return 0;
    p=dota_fountains[u->team];
    return dist2(u->x,u->y,p.x,p.y)<=DOTA_SHOP_RANGE*DOTA_SHOP_RANGE;
}
static int shop_kind_for(const DotaGame *g,int index) {
    const DotaUnit *u;
    if(!g || (unsigned)index>=DOTA_HERO_COUNT)return DOTA_SHOP_NONE;
    u=&g->units[index];
    /* Turbo's remote catalogue includes base and former secret-shop goods.
     * Dead heroes may order; courier delivery itself requires a living owner. */
    return u->active && u->type==DOTA_HERO && (unsigned)u->team<2?DOTA_SHOP_BASE:DOTA_SHOP_NONE;
}
int dota_shop_kind(const DotaGame *g) {
    return !g || g->state!=DOTA_PLAYING?DOTA_SHOP_NONE:shop_kind_for(g,g->player);
}
int dota_shop_available(const DotaGame *g) { return dota_shop_kind(g)!=DOTA_SHOP_NONE; }
int dota_shop_item_available(const DotaGame *g,int item) { DotaItemRecord r;return dota_shop_kind(g)!=DOTA_SHOP_NONE && dota_items_read(item,&r) && (r.flags&ITEM_F_BUY); }

static int buy_for(DotaGame *g,int index,int item) {
    return dota_courier_buy(g,index,item);
}
int dota_buy(DotaGame *g,int item) { return g?buy_for(g,g->player,item):0; }

static int use_for(DotaGame *g,int index,int slot) {
    DotaUnit *u=&g->units[index];
    return dota_items_use(g,index,slot,u->x,u->y,index);
}
int dota_use_item(DotaGame *g,int slot) { return g?use_for(g,g->player,slot):0; }
int dota_use_item_at(DotaGame *g,int slot,int x,int y,int target){return g?dota_items_use(g,g->player,slot,x,y,target):0;}
int dota_hero_has_scepter(const DotaGame *g,int index){return dota_items_has_upgrade(g,index,0);}

void dota_move_to(DotaGame *g,int x,int y) {
    DotaUnit *u=&g->units[g->player];
    if(!g->paused&&g->state==DOTA_PLAYING)dota_roster_try_leave(g,g->player);
    if(g->paused || g->state!=DOTA_PLAYING || !alive(u) || u->taunt_ticks || (dota_unit_invulnerable(g,g->player) && !dota_items_wind_move_source(g,g->player)))return;
    sync_navigation(g);dota_items_cancel_teleport(g,g->player);dota_objective_cancel(g);dota_rune_cancel(g);omnislash_cancel_cast(g,g->player);dota_roster_cancel_cast(g,g->player);
    u->move_order=1;u->move_x=clamp(x,0,DOTA_WORLD-1);u->move_y=clamp(y,0,DOTA_WORLD-1);u->target=-1;
    click_path(g,u->move_x,u->move_y);
    if(!g->player_path_count)u->move_order=0;
}
void dota_attack_target(DotaGame *g,int index) {
    DotaUnit *u=&g->units[g->player];
    if(g->paused || g->state!=DOTA_PLAYING || !alive(u) || !targetable(g,index) || !dota_unit_visible(g,g->player,index) || g->units[index].team==u->team || dota_unit_invulnerable(g,g->player))return;
    sync_navigation(g);dota_items_cancel_teleport(g,g->player);dota_objective_cancel(g);dota_rune_cancel(g);omnislash_cancel_cast(g,g->player);dota_roster_cancel_cast(g,g->player);
    u->target=index;u->move_order=0;
    g->player_path_count=g->player_path_index=0;
}

static void hero_create(DotaGame *g,int index,int hero,int team,int lane) {
    DotaUnit *u=&g->units[index];int i;
    u->active=1;u->type=DOTA_HERO;u->team=team;u->hero_id=hero;u->lane=lane;u->level=1;u->gold=DOTA_TURBO_STARTING_GOLD;
    legal_position(u,dota_hero_spawns[team][index%5].x,dota_hero_spawns[team][index%5].y);
    u->target=u->taunt_source=u->dot_source=-1;u->waypoint=0;u->generation=1;
    for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
    recalc(g,index);u->hp=u->max_hp;u->mana=u->max_mana;
}

static void structure_create(DotaGame *g,int index,int type,int team,int lane,int tier,int x,int y) {
    DotaUnit *u=&g->units[index];int i;
    u->active=1;u->type=type;u->team=team;u->hero_id=-1;u->lane=lane;u->tier=tier;
    u->x=x;u->y=y;u->max_hp=type==DOTA_BASE?3000:(type==DOTA_BUILDING?1000:(tier==3?2600:(tier?2500:1800)));u->hp=u->max_hp;
    u->armor=type==DOTA_BASE?5:(type==DOTA_BUILDING?10:(tier==3?21:(tier?16:12)));u->damage=type==DOTA_TOWER?(tier?172:90):0;u->range=type==DOTA_TOWER?700:0;u->target=-1;u->generation=1;
    for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
}

static void neutral_create(DotaGame *g,int camp) {
    int role,i,group,low=dota_jungle_camp_low[camp],high=dota_jungle_camp_high[camp];
    do{g->seed=g->seed*1664525u+1013904223u;group=low+(int)((g->seed>>8)%(unsigned)(high-low+1));}while(high>low && group==g->neutral_group[camp]);
    g->neutral_group[camp]=(signed char)group;
    for(role=0;role<dota_jungle_camp_capacity[camp];++role) {
        int index=dota_jungle_first(camp)+role,species=dota_jungle_combinations[dota_jungle_camp_type[camp]][group][role];
        DotaUnit *u=&g->units[index];const DotaJungleDefinition *d;int generation=u->generation+1;
        zero_bytes(u,(unsigned)sizeof(*u));u->generation=generation;
        g->neutral_regen_fraction[index-DOTA_NEUTRAL_FIRST]=0;
        if(species<0)continue;
        d=&dota_jungle_definitions[species];
        u->active=1;u->type=DOTA_NEUTRAL;u->team=DOTA_NEUTRAL_TEAM;u->hero_id=-1;
        u->lane=camp;u->tier=species;u->level=dota_jungle_levels[species];neutral_home(camp,role,&u->x,&u->y);
        u->hp=u->max_hp=d->hp;u->mana=u->max_mana=d->mana;u->damage=d->damage;u->armor=d->armor;
        u->range=d->range;u->speed=d->speed;u->target=u->taunt_source=u->dot_source=-1;
        for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
    }
    g->neutral_state[camp]=DOTA_NEUTRAL_IDLE;g->neutral_respawn[camp]=0;
}

void dota_init(DotaGame *g,int hero,unsigned seed) {
    int i,team,lane,tier,index=10,roster[9],remaining=0;
    zero_bytes(g,(unsigned)sizeof(*g));g->seed=seed?seed:0x48414432u;
    dota_sight_reset();
    dota_nav_reset_static_trees(g);
    dota_items_init(g);
    dota_nav_set_towers(0);
    dota_nav_reset_stats();
    for(i=0;i<DOTA_MAX_SUMMONS;++i)g->summon_owner[i]=-1;
    g->selected_hero=clamp(hero,0,DOTA_HERO_TYPES-1);g->player=0;g->spawn_wait=dota_match_pregame_ticks();
    hero_create(g,0,g->selected_hero,0,1);
    for(i=0;i<DOTA_HERO_TYPES;++i)if(i!=g->selected_hero)roster[remaining++]=i;
    for(i=remaining-1;i>0;--i){int pick,swap;g->seed=g->seed*1664525u+1013904223u;pick=(int)((g->seed>>8)%(unsigned)(i+1));swap=roster[i];roster[i]=roster[pick];roster[pick]=swap;}
    for(i=1;i<5;++i)hero_create(g,i,roster[i-1],0,i<3?0:2);
    for(i=5;i<10;++i)hero_create(g,i,roster[i-1],1,i<7?0:(i==7?1:2));
    for(team=0;team<2;++team)for(lane=0;lane<3;++lane)for(tier=0;tier<2;++tier) {
        DotaPoint p=dota_tower_positions[team][lane][tier];
        g->towers[team][lane][tier]=index;
        structure_create(g,index++,DOTA_TOWER,team,lane,tier,p.x,p.y);
    }
    for(team=0;team<2;++team) {
        DotaPoint p=dota_ancients[team];
        g->bases[team]=index;structure_create(g,index++,DOTA_BASE,team,1,0,p.x,p.y);
    }
    for(team=0;team<2;++team)for(tier=0;tier<DOTA_HIGH_TOWERS_PER_TEAM;++tier){
        DotaPoint p=dota_high_tower_positions[team][tier];
        structure_create(g,high_tower_slot(team,tier),DOTA_TOWER,team,tier<3?tier:1,tier<3?2:3,p.x,p.y);
    }
    for(i=0;i<DOTA_FILLER_COUNT;++i){
        DotaPoint p=dota_filler_positions[i];
        structure_create(g,DOTA_FILLER_FIRST+i,DOTA_BUILDING,filler_teams[i],1,0,p.x,p.y);
        g->units[DOTA_FILLER_FIRST+i].hero_id=i;
    }
    for(team=0;team<2;++team)for(lane=0;lane<3;++lane)dota_barracks_create(g,team,lane,DOTA_BARRACKS_RANGED);
    for(i=0;i<DOTA_NEUTRAL_CAMPS;++i){g->neutral_group[i]=-1;g->neutral_respawn[i]=dota_match_elapsed_tick(60*DOTA_HZ);}
    g->tormentor.spawn_tick=dota_match_elapsed_tick(DOTA_TORMENTOR_FIRST_SPAWN);g->tormentor.location=1;
    g->tormentor.last_damage_tick=-200;g->units[DOTA_TORMENTOR_INDEX].type=DOTA_TORMENTOR;
    for(i=0;i<DOTA_HERO_COUNT;++i)g->assist_generation[i]=g->units[i].generation;
    dota_bosses_init(g);
    dota_objectives_init(g);
    dota_runes_init(g);
    dota_couriers_init(g);
    dota_item_world_init(g);
    dota_roster_init(g);
    for(i=0;i<DOTA_HERO_COUNT;++i){dota_progression_level_changed(g,i);if(i!=g->player)dota_progression_bot(g,i);}
    sync_navigation(g);
}
void dota_restart(DotaGame *g) { int hero=g->selected_hero;unsigned seed=g->seed;dota_init(g,hero,seed); }
void dota_set_paused(DotaGame *g,int paused) { g->paused=paused!=0; }

static void spawn_wave(DotaGame *g) {
    int team,lane,number,i,available=0;int count=3;
    ++g->wave;
    for(i=DOTA_CREEP_FIRST;i<DOTA_CREEP_END;++i)if(!g->units[i].active)++available;
    for(lane=0;lane<3;++lane)for(number=0;number<count;++number) {
      /* Capacity pressure never grants a one-sided wave: spawn matched pairs. */
      if(available<2)return;
      for(team=0;team<2;++team) {
        DotaUnit *u=0;const DotaLaneCreepSource *source;int generation,upgrades;
        for(i=DOTA_CREEP_FIRST;i<DOTA_CREEP_END;++i)if(!g->units[i].active){u=&g->units[i];break;}
        if(!u)return;
        --available;
        generation=u->generation+1;zero_bytes(u,(unsigned)sizeof(*u));u->generation=generation;
        u->active=1;u->type=DOTA_CREEP;u->team=team;u->hero_id=-1;u->lane=lane;
        u->waypoint=0;legal_position(u,dota_creep_spawns[team][lane].x+(number-1)*40,
        dota_creep_spawns[team][lane].y+(number-1)*35);
        u->tier=number==2;source=&dota_lane_creep_source[u->tier];
        /* Native Turbo keeps the450s upgrade clock and triples each gain.
         * Wave count never contributes stats, and there is no3min bonus. */
        upgrades=clamp(dota_match_game_ticks(g)/(DOTA_LANE_UPGRADE_TURBO_SECONDS*DOTA_HZ),0,source->max_upgrades);
        u->hp=u->max_hp=source->health+upgrades*source->turbo_health_upgrade;
        u->damage=(source->damage_min+source->damage_max)/2+upgrades*source->turbo_damage_upgrade;
        u->speed=source->speed;u->range=source->range;u->armor=source->armor100/100;u->level=source->level;
        u->mana=u->max_mana=source->mana;u->target=-1;
        {int slot;for(slot=0;slot<DOTA_ITEM_SLOTS;++slot)u->inventory[slot]=-1;}
      }
    }
}

static void lane_progress(DotaUnit *u) {
    int last=dota_lane_point_counts[u->team][u->lane]-1;
    u->waypoint=clamp(u->waypoint,0,last);
    while(u->waypoint<last) {
        DotaPoint p=dota_lanes[u->team][u->lane][u->waypoint];
        DotaPoint previous=u->waypoint?dota_lanes[u->team][u->lane][u->waypoint-1]:dota_creep_spawns[u->team][u->lane];
        int dx=p.x-previous.x,dy=p.y-previous.y,len=dx*dx+dy*dy;
        long long along=(long long)(u->x-previous.x)*dx+(long long)(u->y-previous.y)*dy;
        long long cross=(long long)(u->x-previous.x)*dy-(long long)(u->y-previous.y)*dx;
        if(dist2(u->x,u->y,p.x,p.y)>=90*90 &&
           (!len || along<len || cross*cross>(long long)300*300*len))break;
        ++u->waypoint;
    }
}
static void follow_lane(DotaGame *g,DotaUnit *u) {
    int last;
    DotaPoint p;
    if((unsigned)u->team>=2 || (unsigned)u->lane>=3)return;
    last=dota_lane_point_counts[u->team][u->lane]-1;
    if(u->type==DOTA_CREEP)lane_progress(u);
    u->waypoint=clamp(u->waypoint,0,last);p=dota_lanes[u->team][u->lane][u->waypoint];
    dota_nav_nearest(p.x,p.y,256,&p.x,&p.y);
    if(dist2(u->x,u->y,p.x,p.y)<90*90) {
        if(u->waypoint<last)++u->waypoint;
        p=dota_lanes[u->team][u->lane][u->waypoint];
        dota_nav_nearest(p.x,p.y,256,&p.x,&p.y);
    }
    if(u->waypoint==last && dist2(u->x,u->y,p.x,p.y)<300*300){
        int enemy=1-u->team,objective=-1,i,best=0x7fffffff;
        /* The original path stops before the Ancient. Continue the assault
         * through live laneT3, then the two final towers, then the Ancient. */
        for(i=0;i<2;++i)if(alive(&g->units[g->towers[enemy][u->lane][i]])){objective=g->towers[enemy][u->lane][i];break;}
        if(objective<0 && dota_high_tower_alive(g,enemy,u->lane))objective=high_tower_slot(enemy,u->lane);
        if(objective<0)for(i=1;i<=2;++i){int index=dota_barracks_index(enemy,u->lane,i),d=dist2(u->x,u->y,g->units[index].x,g->units[index].y);if(dota_barracks_kind(&g->units[index]) && alive(&g->units[index]) && d<best){best=d;objective=index;}}
        if(objective<0)for(i=3;i<5;++i){int index=high_tower_slot(enemy,i),d=dist2(u->x,u->y,g->units[index].x,g->units[index].y);if(dota_high_tower_alive(g,enemy,i) && d<best){best=d;objective=index;}}
        if(objective<0 && alive(&g->units[g->bases[enemy]]))objective=g->bases[enemy];
        if(objective>=0){move_for_attack(g,u,&g->units[objective]);return;}
    }
    move_towards(g,u,p.x,p.y);
}

static int attack_interval_q16(const DotaGame *g,int index) {
    const DotaUnit *u=&g->units[index];
    ItemAttackTraits traits;int auxiliary_hero=index>=DOTA_ITEM_UNIT_FIRST && g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION;
    static const unsigned char hero_bat_ticks[DOTA_HERO_TYPES]={34,32,28,34,34,34};
    int base=(u->type==DOTA_HERO || auxiliary_hero) && (unsigned)u->hero_id<DOTA_HERO_TYPES?hero_bat_ticks[u->hero_id]:(u->type==DOTA_TOWER?18:((u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN)?dota_boss_bat_ticks(u):(u->type==DOTA_NEUTRAL?40:20)));
    if(item_attack_traits(g,index,&traits))base=traits.bat_ticks;
    return (int)((long long)base*DOTA_MOVE_ONE*10000*dota_items_special_bat_percent(g,index)/(100ll*attack_speed_x100(g,index)));
}
static int attack_interval(DotaGame *g,int index){
    DotaUnit *u=&g->units[index];int *carry=u->type==DOTA_HERO && index<DOTA_HERO_COUNT?&g->passive_move_fraction[index]:&u->cooldown[2];
    int total=attack_interval_q16(g,index)+*carry,ticks=total/DOTA_MOVE_ONE;
    *carry=total%DOTA_MOVE_ONE;return ticks>0?ticks:1;
}

static void projectile_snapshot(DotaProjectile *p,const DotaGame *g,int index,int target,int amount) {
    const DotaUnit *u=&g->units[index],*v=&g->units[target];
    zero_bytes(p,(unsigned)sizeof(*p));p->style=attack_style_for(g,index);p->source=index;
    p->source_generation=u->generation;p->source_type=u->type;p->team=u->team;
    p->target=target;p->target_generation=v->generation;
    p->origin_x=p->x=p->previous_x=u->x;p->origin_y=p->y=p->previous_y=u->y;
    p->target_x=v->x;p->target_y=v->y;p->damage=amount;p->started_tick=g->tick;
    p->launch_z=u->type==DOTA_TOWER?90:(u->type==DOTA_BASE?(u->team==DOTA_RADIANT?115:160):
                 (u->type==DOTA_NEUTRAL?40:(u->type==DOTA_CREEP?32:48)));
    p->aim_z=structure(v)?90:(v->type==DOTA_CREEP?22:34);
}

static void cancel_projectile(DotaGame *g,DotaProjectile *p) {
    attack_event(g,DOTA_ATTACK_EVENT_CANCEL,p,4);p->state=0;
}

static void attack_impact(DotaGame *g,DotaProjectile *p){
    int source=p->source,amount=p->damage,before,dealt,raw,blade_crit=100;
    if((unsigned)p->target>=DOTA_MAX_UNITS)return;
    if((unsigned)source>=DOTA_MAX_UNITS || g->units[source].generation!=p->source_generation)source=-1;
    if(source>=0)amount=dota_items_attack_damage(g,source,p->target,amount);
    if(amount<=0)return;
    if((unsigned)source<DOTA_HERO_COUNT)blade_crit=g->attack_crit_percent[source];
    if(source>=0)amount=dota_roster_attack_damage(g,source,p->target,amount);
    raw=amount;amount-=dota_items_attack_block(g,source,p->target);if(amount<0)amount=0;
    before=g->units[p->target].hp;
    damage_snapshot_ex(g,p->target,amount,source,p->source_generation,p->team,0,1);
    dealt=before-g->units[p->target].hp;if(dealt<0)dealt=0;
    if(source>=0 && dealt>0 && !g->items.reflection_guard)dota_items_attack_hit(g,source,p->target,dealt);
    if((unsigned)source<DOTA_HERO_COUNT&&g->units[source].hero_id==DOTA_HERO_JUGGERNAUT&&blade_crit>100&&dealt>0&&!g->items.reflection_guard){int heal=dealt*skill_value(&g->units[source],2,"blade_dance_lifesteal",0)/100;if(heal>0)dota_items_restore(g,source,source,heal,0);}
    if(source>=0&&!g->items.reflection_guard)dota_roster_attack_hit(g,source,p->target,raw,dealt);
    if(!g->items.reflection_guard)received_attack(g,p->target,source,p->team);
}
static void projectiles(DotaGame *g) {
    int i;
    for(i=0;i<DOTA_MAX_PROJECTILES;++i) {
        DotaProjectile *p=&g->projectiles[i];DotaUnit *target;int dx,dy,distance,step;
        if(!p->state)continue;
        if(!targetable(g,p->target) || g->units[p->target].generation!=p->target_generation
           || !dota_pit_same_region(p->origin_x,p->origin_y,g->units[p->target].x,g->units[p->target].y)
           || g->units[p->target].team==p->team || --p->ticks<=0) {cancel_projectile(g,p);continue;}
        target=&g->units[p->target];p->target_x=target->x;p->target_y=target->y;
        if(p->state==DOTA_PROJECTILE_PENDING) {
            DotaUnit *source=&g->units[p->source];
            /* Windup may be cancelled; a released flight never consults these
             * shooter conditions. Stop/move input has already been applied. */
            if(!alive(source) || source->generation!=p->source_generation || source->team!=p->team
               || source->stun_ticks || source->channel_ticks || dota_roster_stun(g,p->source) || dota_roster_disarmed(g,p->source) || dota_bosses_disarmed(g,p->source) || dota_extra_disarmed(g,p->source) || dota_items_disarmed(g,p->source) || dota_extra_physical_immune(g,p->target) || source->target!=p->target
               || source->x!=p->origin_x || source->y!=p->origin_y) {cancel_projectile(g,p);continue;}
            if(g->tick==p->started_tick)continue;
            if(--p->delay>0)continue;
            if(p->style==DOTA_ATTACK_MELEE){
                if(!source_range(dist2(source->x,source->y,target->x,target->y),attack_reach_for(g,source,target))){cancel_projectile(g,p);continue;}
                p->state=0;attack_event(g,DOTA_ATTACK_EVENT_IMPACT,p,6);
                effect(g,DOTA_FX_ATTACK,p->team,p->origin_x,p->origin_y,target->x,target->y,16,6);
                attack_impact(g,p);continue;
            }
            if(p->speed<=0){cancel_projectile(g,p);continue;}
            p->state=DOTA_PROJECTILE_FLYING;p->age=0;p->delay=0;
            distance=move_length((p->target_x-p->x)*DOTA_MOVE_ONE,(p->target_y-p->y)*DOTA_MOVE_ONE);
            step=(int)((long long)p->speed*DOTA_MOVE_ONE/(4*DOTA_HZ));
            p->initial_flight_ticks=(distance+step-1)/step;
            if(p->initial_flight_ticks<1)p->initial_flight_ticks=1;
            attack_event(g,DOTA_ATTACK_EVENT_RELEASE,p,p->initial_flight_ticks);continue;
        }
        if(p->speed<=0){cancel_projectile(g,p);continue;}
        p->previous_x=p->x;p->previous_y=p->y;++p->age;
        dx=(target->x-p->x)*DOTA_MOVE_ONE-(int)((unsigned)p->delay&65535u);
        dy=(target->y-p->y)*DOTA_MOVE_ONE-(int)((unsigned)p->delay>>16);
        distance=move_length(dx,dy);step=(int)((long long)p->speed*DOTA_MOVE_ONE/(4*DOTA_HZ));
        if(distance<=step) {
            p->x=target->x;p->y=target->y;p->delay=0;
            attack_event(g,DOTA_ATTACK_EVENT_IMPACT,p,6);p->state=0;
            attack_impact(g,p);
            if(g->state!=DOTA_PLAYING)return;
        }else {
            int x=p->x*DOTA_MOVE_ONE+(int)((unsigned)p->delay&65535u)+(int)((long long)dx*step/distance);
            int y=p->y*DOTA_MOVE_ONE+(int)((unsigned)p->delay>>16)+(int)((long long)dy*step/distance);
            p->x=x/DOTA_MOVE_ONE;p->y=y/DOTA_MOVE_ONE;
            p->delay=(int)(((unsigned)x&65535u)|(((unsigned)y&65535u)<<16));
        }
    }
}

static int basic_attack_damage(DotaGame *g,int index,int target,int bonus){
    DotaUnit *u=&g->units[index];int amount=u->damage+bonus;(void)target;
    {int reduction=dota_roster_base_damage_reduction(g,index);if(reduction>0){
        DotaItemStats stats;int white;dota_items_stats(g,index,&stats);white=u->damage-stats.value[ITEM_STAT_DAMAGE]-dota_roster_bonus_damage(g,index);
        if(dota_roster_is_hero(u)){
            const DotaRosterBase *b=dota_roster_base(u->hero_id);int delta[3]={0,0,0},attribute,gain;
            dota_extra_stat_delta(g,index,&delta[0],&delta[1],&delta[2]);dota_roster_stat_delta(g,index,&delta[0],&delta[1],&delta[2]);
            attribute=b->primary==0?b->str100:(b->primary==1?b->agi100:b->int100);gain=b->primary==0?b->str_gain100:(b->primary==1?b->agi_gain100:b->int_gain100);
            attribute+=gain*(u->level-1);white=b->naked_damage+attribute/100+stats.value[ITEM_STAT_STRENGTH+b->primary]/100+clamp(delta[b->primary],1-attribute/100,10000);
        }
        if(white>0)amount-=white*reduction/100;
    }}
    return amount;
}
static void melee_attack(DotaGame *g,int index,int target,int bonus){
    DotaUnit *u=&g->units[index],*v=&g->units[target];DotaProjectile p;
    int amount=basic_attack_damage(g,index,target,bonus);
    projectile_snapshot(&p,g,index,target,amount);attack_event(g,DOTA_ATTACK_EVENT_START,&p,6);
    if(u->type!=DOTA_NEUTRAL)effect(g,DOTA_FX_ATTACK,u->team,u->x,u->y,v->x,v->y,35,6);
    u->attack_wait=attack_interval(g,index);attack_impact(g,&p);
    attack_event(g,DOTA_ATTACK_EVENT_IMPACT,&p,6);
}
static void attack(DotaGame *g,int index,int target) {
    DotaUnit *u=&g->units[index],*v=&g->units[target];int amount,style=attack_style_for(g,index);
    if(g->state!=DOTA_PLAYING || (u->type==DOTA_SUMMON && index<DOTA_ITEM_UNIT_FIRST) || u->type==DOTA_BASE || u->range<=0 || u->attack_wait || u->stun_ticks || u->channel_ticks || dota_roster_stun(g,index) || dota_roster_disarmed(g,index) || dota_bosses_disarmed(g,index) || dota_extra_disarmed(g,index) || dota_items_disarmed(g,index) || dota_extra_moving(g,index) || dota_roster_moving(g,index) || dota_extra_physical_immune(g,target) || !targetable(g,target) || !dota_unit_visible(g,index,target) || v->team==u->team)return;
    {
        DotaProjectile *p=0;int i;
        for(i=0;i<DOTA_MAX_PROJECTILES;++i)if(!g->projectiles[i].state){p=&g->projectiles[i];break;}
        if(!p)return; /* Back pressure: retry next tick, never fake a hit. */
        dota_rune_on_action(g,index);
        amount=basic_attack_damage(g,index,target,0);
        projectile_snapshot(p,g,index,target,amount);p->state=DOTA_PROJECTILE_PENDING;
        /* Original .65s Lina / .45s CM / .5s caster creep / .3s satyr /
         * .6s tower attack points. Source speeds are /4 and rounded per tick. */
        {ItemAttackTraits traits;int auxiliary_hero=index>=DOTA_ITEM_UNIT_FIRST && g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION;
         int point_ms=(u->type==DOTA_HERO || auxiliary_hero)?(u->hero_id==0?400:(u->hero_id==1?650:(u->hero_id==2?330:(u->hero_id==4?500:(u->hero_id==5?300:450))))):(u->type==DOTA_TOWER?600:((u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN)?dota_boss_attack_point_ms(u):(u->type==DOTA_NEUTRAL?300:(u->tier?500:467))));
         if(item_attack_traits(g,index,&traits))point_ms=traits.point_ms;
         int as=attack_speed_x100(g,index);p->delay=(point_ms*DOTA_HZ*10000+1000*as-1)/(1000*as);}
        p->speed=style==DOTA_ATTACK_MELEE?0:(style==DOTA_ATTACK_NECROPHOS?1200:((u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN)?dota_boss_projectile_speed(u):(style==DOTA_ATTACK_NEUTRAL?1500:(u->type==DOTA_TOWER?750:(style==DOTA_ATTACK_LINA?1000:900)))));
        {ItemAttackTraits traits;if(item_attack_traits(g,index,&traits))p->speed=traits.projectile_speed;}
        p->ticks=8*DOTA_HZ;u->target=target;u->attack_wait=attack_interval(g,index);
        attack_event(g,DOTA_ATTACK_EVENT_START,p,p->delay);
    }
}

static int omnislash_interval_q16(const DotaGame *g,int caster){
    int speed=attack_speed_x100(g,caster);
    long long interval=(long long)attack_interval_q16(g,caster)*100*speed/(140*(speed+4000));
    return interval<DOTA_MOVE_ONE?DOTA_MOVE_ONE:(int)interval;
}
static int omnislash_nearby(DotaGame *g,int caster,int random){
    DotaUnit *u=&g->units[caster];int i,best=-1,count=0;
    for(i=0;i<DOTA_MAX_UNITS;++i)if(omnislash_target(g,caster,i)
       && source_range(dist2(u->skill_x,u->skill_y,g->units[i].x,g->units[i].y),DOTA_OMNISLASH_JUMP_RANGE_SOURCE)){
        if(!random)return i;
        ++count;g->seed=g->seed*1664525u+1013904223u;
        if(((g->seed>>8)%(unsigned)count)==0)best=i;
    }
    return best;
}
static int omnislash_strike(DotaGame *g,int caster,int target){
    DotaUnit *u=&g->units[caster],*v=&g->units[target];DotaOmnislashState *s=&g->omnislash[caster];
    DotaSkillEvent *visual;int x,y,interval=omnislash_interval_q16(g,caster);
    if(!omnislash_target(g,caster,target) || !omnislash_landing(g,caster,target,s->step,&x,&y))return 0;
    s->target=target;s->target_generation=v->generation;
    visual=skill_event(g,DOTA_SKILL_EVENT_STRIKE,caster,3,target,v->x,v->y,s->step++);
    visual->duration_ticks=(interval+DOTA_MOVE_ONE-1)/DOTA_MOVE_ONE;
    skill_effect(g,caster,3,DOTA_FX_LINE,u->x,u->y,v->x,v->y,20,visual->duration_ticks);
    u->skill_x=v->x;u->skill_y=v->y;u->x=x;u->y=y;u->target=target;u->move_order=0;
    zero_bytes(&g->unit_routes[caster],(unsigned)sizeof(g->unit_routes[caster]));
    zero_bytes(&g->movement[caster],(unsigned)sizeof(g->movement[caster]));
    melee_attack(g,caster,target,20+5*s->phase_ticks);
    s->attack_wait_q16+=interval;
    if(omnislash_nearby(g,caster,0)<0){omnislash_clear(g,caster);u->target=-1;}
    return 1;
}
static int omnislash_update(DotaGame *g,int caster){
    DotaUnit *u=&g->units[caster];DotaOmnislashState *s=&g->omnislash[caster];
    int phase=dota_omnislash_phase(g,caster),target,x,y,i,cost,rank;
    DotaSkillEvent *visual;
    if(!phase){if(s->phase)omnislash_clear(g,caster);return 0;}
    if(phase==DOTA_OMNISLASH_CASTING){
        target=s->target;
        if(u->stun_ticks || u->taunt_ticks || u->channel_ticks || !omnislash_target(g,caster,target)
           || g->units[target].generation!=s->target_generation
           || !source_range(dist2(u->x,u->y,g->units[target].x,g->units[target].y),DOTA_OMNISLASH_CAST_RANGE_SOURCE)){
            omnislash_clear(g,caster);return 1;
        }
        if(--s->phase_ticks>0)return 1;
        cost=dota_skill_mana_cost(g,caster,3);rank=dota_omnislash_rank(u);
        if(dota_items_available_mana(g,caster)<cost || u->cooldown[3] || !rank || !omnislash_landing(g,caster,target,0,&x,&y)){
            omnislash_clear(g,caster);return 1;
        }
        g->item_world.last_mana_spent[caster]=cost;dota_items_spend_mana(g,caster,cost);u->cooldown[3]=dota_skill_cooldown_ticks(g,caster,3);dota_items_on_cast(g,caster);
        s->phase=DOTA_OMNISLASH_ACTIVE;s->phase_ticks=rank;s->remaining_ticks=skill_time(u,3,"duration",55+5*rank);
        u->skill_ticks[3]=s->remaining_ticks;u->skill_ticks[0]=0; /* H1: R ends an earlier Blade Fury. */
        visual=skill_event(g,DOTA_SKILL_EVENT_CAST,caster,3,target,g->units[target].x,g->units[target].y,0);
        visual->duration_ticks=s->remaining_ticks;
        if(dota_bosses_spell_block(g,target) || dota_items_blocks_spell(g,caster,target)){omnislash_clear(g,caster);return 1;}
        dota_items_on_target_spell(g,caster,target);
        for(i=0;i<DOTA_MAX_PROJECTILES;++i)if(g->projectiles[i].state && g->projectiles[i].target==caster)cancel_projectile(g,&g->projectiles[i]);
        if(!omnislash_strike(g,caster,target)){omnislash_clear(g,caster);u->target=-1;}
        return 1;
    }
    if(--s->remaining_ticks<=0 || omnislash_nearby(g,caster,0)<0){
        omnislash_clear(g,caster);u->target=-1;return 1;
    }
    u->skill_ticks[3]=s->remaining_ticks;s->attack_wait_q16-=DOTA_MOVE_ONE;
    if(s->attack_wait_q16<=0){
        target=omnislash_nearby(g,caster,1);
        if(target<0 || !omnislash_strike(g,caster,target)){omnislash_clear(g,caster);u->target=-1;}
    }
    return 1;
}

static void neutral_return(DotaGame *g,int camp) {
    int i;g->neutral_state[camp]=DOTA_NEUTRAL_RETURN;
    for(i=dota_jungle_first(camp);i<dota_jungle_end(camp);++i)g->units[i].target=-1;
}

static void neutral_bot(DotaGame *g,int index) {
    DotaUnit *u=&g->units[index];int camp=u->lane,target=u->target,i,home=1;
    const DotaJungleDefinition *definition=dota_jungle_definition(u);
    DotaPoint p;
    if(camp<0 || camp>=DOTA_NEUTRAL_CAMPS)return;
    p=dota_neutral_camps[camp];
    if(u->taunt_ticks && u->taunt_source>=0 && u->taunt_source<DOTA_MAX_UNITS
       && alive(&g->units[u->taunt_source]) && g->units[u->taunt_source].team!=u->team) {
        target=u->taunt_source;u->target=target;g->neutral_state[camp]=DOTA_NEUTRAL_AGGRO;
    }
    if(g->neutral_state[camp]!=DOTA_NEUTRAL_RETURN && !source_range(dist2(u->x,u->y,p.x,p.y),1200))
        neutral_return(g,camp);
    if(g->neutral_state[camp]==DOTA_NEUTRAL_RETURN) {
        int home_x,home_y;neutral_home(camp,index-dota_jungle_first(camp),&home_x,&home_y);
        u->target=-1;move_towards(g,u,home_x,home_y);u->hp=clamp(u->hp+5,0,u->max_hp);
        for(i=dota_jungle_first(camp);i<dota_jungle_end(camp);++i){
            neutral_home(camp,i-dota_jungle_first(camp),&home_x,&home_y);
            if(alive(&g->units[i]) && dist2(g->units[i].x,g->units[i].y,home_x,home_y)>16*16)home=0;
        }
        if(home)g->neutral_state[camp]=DOTA_NEUTRAL_IDLE;
        return;
    }
    if(target<0 || !targetable(g,target) || !dota_unit_visible(g,index,target) || g->units[target].team==u->team)target=-1;
    if(target<0)target=enemy_source(g,index,definition?definition->acquisition:500,0);
    if(target<0) {
        u->target=-1;if(g->neutral_state[camp]==DOTA_NEUTRAL_AGGRO)neutral_return(g,camp);
        return;
    }
    if(!source_range(dist2(g->units[target].x,g->units[target].y,p.x,p.y),1500)) {neutral_return(g,camp);return;}
    if(g->neutral_state[camp]==DOTA_NEUTRAL_IDLE) {
        g->neutral_state[camp]=DOTA_NEUTRAL_AGGRO;
        for(i=dota_jungle_first(camp);i<dota_jungle_end(camp);++i)
            if(alive(&g->units[i]))g->units[i].target=target;
    }
    u->target=target;
    if(source_range(dist2(u->x,u->y,g->units[target].x,g->units[target].y),attack_reach_for(g,u,&g->units[target])))attack(g,index,target);
    else move_for_attack(g,u,&g->units[target]);
}

static void neutral_camps(DotaGame *g) {
    int camp,i,occupied,living,match=dota_match_ticks(g);
    if(match<60*DOTA_HZ || match%(60*DOTA_HZ))return;
    for(camp=0;camp<DOTA_NEUTRAL_CAMPS;++camp) {
        living=0;for(i=dota_jungle_first(camp);i<dota_jungle_end(camp);++i)if(alive(&g->units[i]))living=1;
        g->neutral_respawn[camp]=g->tick+60*DOTA_HZ;
        if(living)continue;
        occupied=0;
        /* Original spawn origins and subtype pools are exact. Pending the
         * original trigger hulls, this bounded600 Source occupancy radius is
         * the documented H1 camp-blocking approximation, applied to all units. */
        for(i=0;i<DOTA_MAX_UNITS;++i)if(alive(&g->units[i])
            && dist2(g->units[i].x,g->units[i].y,dota_neutral_camps[camp].x,dota_neutral_camps[camp].y)<150*150)occupied=1;
        if(!occupied)neutral_create(g,camp);
    }
}

static void tormentor_create(DotaGame *g,int location) {
    DotaUnit *u=&g->units[DOTA_TORMENTOR_INDEX];int i,generation=u->generation+1;
    zero_bytes(u,(unsigned)sizeof(*u));u->generation=generation;
    u->active=1;u->type=DOTA_TORMENTOR;u->team=DOTA_NEUTRAL_TEAM;u->hero_id=-1;
    u->x=dota_tormentor_locations[location].x;u->y=dota_tormentor_locations[location].y;
    u->hp=u->max_hp=3001+50*g->tormentor.growth_minutes;u->armor=20;
    u->target=u->taunt_source=u->dot_source=-1;
    u->skill_y=g->tick; /* Real birth tick for the offline spawn clip. */
    u->skill_x=-200; /* Last real reflection wave, distinct from incoming hits. */
    for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
    g->tormentor.location=location;g->tormentor.regen_fraction=0;
    g->tormentor.last_damage_tick=-200;
    effect(g,DOTA_FX_BURST,DOTA_NEUTRAL_TEAM,u->x,u->y,u->x,u->y,100,20);
}
static void tormentor_update(DotaGame *g) {
    DotaUnit *u=&g->units[DOTA_TORMENTOR_INDEX];int i,count=0,amount,location;
    g->tormentor.growth_minutes=dota_match_game_ticks(g)/(60*DOTA_HZ);
    /* Reflect the accumulated shield damage after the source0.8s preview.
     * Only heroes share this wave; damage cannot recursively hit the boss. */
    if(g->tormentor.reflect_wait && --g->tormentor.reflect_wait==0) {
        for(i=0;i<DOTA_HERO_COUNT;++i)if(alive(&g->units[i]) && g->units[i].type==DOTA_HERO
            && dist2(u->x,u->y,g->units[i].x,g->units[i].y)<=300*300)++count;
        if(count) {
            amount=g->tormentor.reflect_damage_q200/(200*count);
            if(amount>0)u->skill_x=g->tick;
            if(amount>0)for(i=0;i<DOTA_HERO_COUNT;++i)if(alive(&g->units[i]) && g->units[i].type==DOTA_HERO
                && dist2(u->x,u->y,g->units[i].x,g->units[i].y)<=300*300) {
                effect(g,DOTA_FX_LINE,DOTA_NEUTRAL_TEAM,u->x,u->y,g->units[i].x,g->units[i].y,20,8);
                damage_snapshot(g,i,amount,DOTA_TORMENTOR_INDEX,u->generation,DOTA_NEUTRAL_TEAM,1);
            }
        }
        g->tormentor.reflect_damage_q200=0;
    }
    location=dota_match_is_night(g)?0:1; /* Current7.41: Bottom by day. */
    if(!alive(u)) {
        if(u->type!=DOTA_TORMENTOR)return; /* Do not overwrite controlled test actors. */
        if(u->respawn_ticks>0) {
            int age=g->tick-(g->tormentor.spawn_tick-DOTA_TORMENTOR_RESPAWN);
            u->respawn_ticks=clamp(g->tormentor.spawn_tick-g->tick,0,DOTA_TORMENTOR_RESPAWN);
            /* Alleviation is a15s death aura, not periodic healing while
             * alive. Cumulative percentage differences retain fractional
             * regeneration for every nearby mobile unit without an array. */
            if(age>0 && age<=15*DOTA_HZ)for(i=0;i<DOTA_MAX_UNITS;++i) {
                DotaUnit *v=&g->units[i];
                if(alive(v) && !structure(v) && v->type!=DOTA_TORMENTOR
                   && dist2(u->x,u->y,v->x,v->y)<=225*225) {
                    amount=(int)((long long)v->max_hp*225*age/(10000*DOTA_HZ))-
                           (int)((long long)v->max_hp*225*(age-1)/(10000*DOTA_HZ));
                    v->hp=clamp(v->hp+amount,0,v->max_hp);
                    if(g->tick%10==0 && amount>0)effect(g,DOTA_FX_HEAL,v->team,v->x,v->y,u->x,u->y,50,8);
                }
            }
        }
        if(g->tick>=g->tormentor.spawn_tick)tormentor_create(g,location);
        return;
    }
    if(u->type!=DOTA_TORMENTOR)return;
    if(g->tormentor.location!=location) {
        g->tormentor.location=location;u->x=dota_tormentor_locations[location].x;u->y=dota_tormentor_locations[location].y;
        g->tormentor.reflect_damage_q200=0;g->tormentor.reflect_wait=0;
        for(i=0;i<DOTA_MAX_PROJECTILES;++i)if(g->projectiles[i].state && g->projectiles[i].target==DOTA_TORMENTOR_INDEX)cancel_projectile(g,&g->projectiles[i]);
    }
    /* hp is a compact effective bar: one body HP plus the source barrier. */
    u->max_hp=3001+50*g->tormentor.growth_minutes;
    g->tormentor.regen_fraction+=20+5*g->tormentor.growth_minutes;
    amount=g->tormentor.regen_fraction/DOTA_HZ;g->tormentor.regen_fraction%=DOTA_HZ;
    u->hp=clamp(u->hp+amount,1,u->max_hp);
    if(g->tick-g->tormentor.last_damage_tick<10*DOTA_HZ && g->tick%4==0) {
        /* Source Radiance is expressed per second, pulsing every0.2s. */
        u->skill_ticks[0]+=20+2*g->tormentor.growth_minutes;
        amount=u->skill_ticks[0]/5;u->skill_ticks[0]%=5;
        for(i=0;i<DOTA_HERO_COUNT;++i)if(alive(&g->units[i]) && g->units[i].type==DOTA_HERO
            && dist2(u->x,u->y,g->units[i].x,g->units[i].y)<=250*250)
                damage_snapshot(g,i,amount,DOTA_TORMENTOR_INDEX,u->generation,DOTA_NEUTRAL_TEAM,1);
    }
}

static void summon_update(DotaGame *g,int index) {
    DotaUnit *w=&g->units[index];int summon=index-DOTA_SUMMON_FIRST,owner,i;
    DotaUnit *hero;
    if(summon<0 || summon>=DOTA_MAX_SUMMONS)return;
    if(!summon_valid(g,summon) || g->summon_life[summon]<=0){summon_remove(g,index);return;}
    owner=g->summon_owner[summon];hero=&g->units[owner];
    if(g->tick==w->respawn_ticks){hero->skill_ticks[1]=alive(hero)?g->summon_life[summon]:0;return;}
    if(w->respawn_ticks==g->tick)return;
    --g->summon_life[summon];
    if(w->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && w->tier==-1){if(g->summon_life[summon]<=0)crystal_clone_shatter(g,index);return;}
    /* Only AI owners reposition automatically. Player orders remain fixed.
     * A dead owner's ward is independent: it may still help living allies. */
    if(owner!=g->player && g->tick%DOTA_HZ==index%DOTA_HZ) {
        int best=-1,best_deficit=0;
        for(i=0;i<DOTA_HERO_COUNT;++i)if(alive(&g->units[i]) && g->units[i].team==w->team
            && source_range(dist2(w->x,w->y,g->units[i].x,g->units[i].y),1000)) {
            int deficit=(g->units[i].max_hp-g->units[i].hp)*1000/g->units[i].max_hp;
            if(deficit>best_deficit){best=i;best_deficit=deficit;}
        }
        if(best<0 && alive(hero))best=owner;
        if(best>=0 && !source_range(dist2(w->x,w->y,g->units[best].x,g->units[best].y),250)) {
            w->move_order=1;w->move_x=g->units[best].x;w->move_y=g->units[best].y;
        }
    }
    if(w->move_order) {
        move_towards(g,w,w->move_x,w->move_y);
        if(dist2(w->x,w->y,w->move_x,w->move_y)<16*16)w->move_order=0;
    }
    hero->skill_ticks[1]=alive(hero)?g->summon_life[summon]:0;
    hero->skill_x=w->x;hero->skill_y=w->y;
    {int age=g->tick-w->respawn_ticks,pct100=dota_progression_value100(hero,1,"healing_ward_heal_amount",(1+clamp(w->tier,1,4))*100)+(dota_hero_has_shard(g,owner)?150:0);
     for(i=0;i<DOTA_HERO_COUNT;++i)if(alive(&g->units[i]) && g->units[i].team==w->team
        && source_range(dist2(w->x,w->y,g->units[i].x,g->units[i].y),dota_rules_aoe_radius(g,owner,400))){
         int amount=(int)((long long)g->units[i].max_hp*pct100*age/(10000*DOTA_HZ))-(int)((long long)g->units[i].max_hp*pct100*(age-1)/(10000*DOTA_HZ));
         g->units[i].hp=clamp(g->units[i].hp+amount,0,g->units[i].max_hp);
     }
     if(g->tick%10==0)skill_effect(g,owner,1,DOTA_FX_HEAL,w->x,w->y,w->x,w->y,100,9);
    }
    if(g->summon_life[summon]<=0)summon_remove(g,index);
}

static int near_lane(const DotaUnit *u,int x,int y,int radius) {
    DotaPoint p=dota_lanes[u->team][u->lane][u->waypoint];
    DotaPoint previous=u->waypoint?dota_lanes[u->team][u->lane][u->waypoint-1]:dota_creep_spawns[u->team][u->lane];
    int dx=p.x-previous.x,dy=p.y-previous.y,len=dx*dx+dy*dy,t;
    long long along=(long long)(x-previous.x)*dx+(long long)(y-previous.y)*dy;
    t=len?(int)(along*256/len):0;t=clamp(t,0,256);
    return dist2(x,y,previous.x+dx*t/256,previous.y+dy*t/256)<=radius*radius;
}
static void creep_bot(DotaGame *g,int index) {
    DotaUnit *u=&g->units[index];int target=-1,i,best=0x7fffffff,acquire=u->tier?600:500;
    lane_progress(u);
    if(u->taunt_ticks && (unsigned)u->taunt_source<DOTA_MAX_UNITS && targetable(g,u->taunt_source)
       && g->units[u->taunt_source].team!=u->team && dota_unit_visible(g,index,u->taunt_source))target=u->taunt_source;
    else if(u->skill_ticks[0]>g->tick && (unsigned)u->target<DOTA_MAX_UNITS && targetable(g,u->target)
       && g->units[u->target].team!=u->team && dota_unit_visible(g,index,u->target) && source_range(dist2(u->x,u->y,g->units[u->target].x,g->units[u->target].y),1000)
       && source_range(dist2(u->x,u->y,u->skill_x,u->skill_y),800))target=u->target;
    else {
        u->skill_ticks[0]=0;
        for(i=0;i<DOTA_MAX_UNITS;++i) {
            const DotaUnit *v=&g->units[i];int d;
            if(v->team==u->team || v->team==DOTA_NEUTRAL_TEAM || !alive(v))continue;
            d=dist2(u->x,u->y,v->x,v->y);
            if(d>=best || !source_range(d,acquire+dota_unit_hull_source(v))){
                if(!dota_sight_is_current(g) && targetable(g,i))(void)dota_unit_visible(g,index,i);
                continue;
            }
            if(!targetable(g,i) || !dota_unit_visible(g,index,i))continue;
            if(source_range(d,attack_reach_for(g,u,v)) || near_lane(u,v->x,v->y,70)){target=i;best=d;}
        }
    }
    u->target=target;
    if(target>=0) {
        DotaUnit *v=&g->units[target];
        if(source_range(dist2(u->x,u->y,v->x,v->y),attack_reach_for(g,u,v)))attack(g,index,target);
        else move_for_attack(g,u,v);
    }else follow_lane(g,u);
}
static int bot_enemy(const DotaGame *g,int observer,int radius) {
    return enemy_source(g,observer,radius,1);
}
static void bot(DotaGame *g,int index) {
    DotaUnit *u=&g->units[index];int target,i;
    if(u->type==DOTA_NEUTRAL){neutral_bot(g,index);return;}
    if(u->type==DOTA_ANCIENT || u->type==DOTA_ROSHAN)return;
    if(u->type==DOTA_CREEP){creep_bot(g,index);return;}
    if(u->type==DOTA_TORMENTOR)return;
    if(roster_busy(g,index))return;
    if(u->type==DOTA_HERO && u->hero_id==DOTA_HERO_PUDGE && g->roster.hero[index].rot_on && u->hp<u->max_hp/4)cast_for(g,index,1,u->x,u->y);
    if(u->type==DOTA_HERO && (unsigned)g->player<DOTA_HERO_COUNT
       && alive(&g->units[g->player]) && g->units[g->player].team==u->team
       && g->units[g->player].target==DOTA_TORMENTOR_INDEX && dota_tormentor_index(g)>=0
       && u->hp<u->max_hp*60/100
       && dist2(u->x,u->y,g->units[DOTA_TORMENTOR_INDEX].x,g->units[DOTA_TORMENTOR_INDEX].y)<=500*500) {
        DotaPoint p=dota_fountains[u->team];u->target=-1;move_towards(g,u,p.x,p.y);return;
    }
    if(u->type==DOTA_HERO && u->hp<u->max_hp/3) {
        for(i=0;i<DOTA_ITEM_SLOTS;++i)if(dota_items_equipped_item(g,index,i)==DOTA_FLASK && !u->heal_ticks){use_for(g,index,i);break;}
    }
    if(u->type==DOTA_HERO && u->hp<u->max_hp/4 && !u->taunt_ticks) {
        DotaPoint p=dota_fountains[u->team];
        move_towards(g,u,p.x,p.y);return;
    }
    if(u->type==DOTA_HERO && g->tick%20==index%20) {
        int has_boots=0,pending=dota_courier_pending_count(g,index);
        for(i=0;i<DOTA_ITEM_SLOTS;++i)if(u->inventory[i]==DOTA_BOOTS)has_boots=1;
        for(i=0;i<pending;++i)if(dota_courier_pending_item(g,index,i)==DOTA_BOOTS)has_boots=1;
        if(!has_boots)buy_for(g,index,DOTA_BOOTS);
        else if(!pending)buy_for(g,index,(u->level+index)%2+1);
    }
    if(u->type==DOTA_BASE || u->type==DOTA_BUILDING)return;
    target=bot_enemy(g,index,structure(u)?u->range:(u->hero_id==1 || u->hero_id==3?800:600));
    if(target<0 && u->type==DOTA_HERO && (unsigned)g->player<DOTA_HERO_COUNT
       && alive(&g->units[g->player]) && g->units[g->player].team==u->team
       && g->units[g->player].target==DOTA_TORMENTOR_INDEX && dota_tormentor_index(g)>=0
       && u->hp>=u->max_hp*60/100 && dist2(u->x,u->y,g->units[DOTA_TORMENTOR_INDEX].x,g->units[DOTA_TORMENTOR_INDEX].y)<=500*500)
        target=DOTA_TORMENTOR_INDEX;
    if(u->taunt_ticks && u->taunt_source>=0 && alive(&g->units[u->taunt_source]))target=u->taunt_source;
    u->target=target;
    if(u->type==DOTA_HERO && u->hero_id==DOTA_HERO_PUDGE && g->tick%5==index%5){
        int want=target>=0 && source_range(dist2(u->x,u->y,g->units[target].x,g->units[target].y),dota_skill_effect_radius_source(g,index,1)) && u->hp>=u->max_hp/4;
        if(want!=(g->roster.hero[index].rot_on!=0))cast_for(g,index,1,u->x,u->y);
    }
    if(target>=0) {
        DotaUnit *v=&g->units[target];int distance=dist2(u->x,u->y,v->x,v->y);
        if(u->type==DOTA_HERO && !structure(v) && g->tick%5==index%5) {
            int order[4]={0,1,3,2};
            if(u->hero_id==DOTA_HERO_PUDGE){order[2]=2;order[3]=3;}
            for(i=0;i<4;++i) {
                int skill=order[i];
                if(dota_skill_is_passive(u->hero_id,skill))continue;
                if(u->hero_id==DOTA_HERO_PUDGE && skill==1)continue;
                if(u->hero_id==DOTA_HERO_TIDEHUNTER && (skill==2 || skill==3) && !source_range(distance,dota_skill_effect_radius_source(g,index,skill)))continue;
                if(u->hero_id==DOTA_HERO_SNIPER && skill==0){int f,covered=0;for(f=0;f<DOTA_ROSTER_FIELDS;++f){const DotaRosterField *field=&g->roster.field[f];if(field->until>g->tick && field->source==index && source_range(dist2(field->x,field->y,v->x,v->y),field->radius))covered=1;}if(covered)continue;}
                if(u->hero_id==DOTA_HERO_SLARK && skill==0 && !source_range(distance,325))continue;
                if(u->hero_id==DOTA_HERO_SLARK && skill==3 && u->hp>u->max_hp*2/3)continue;
                if(u->hero_id==DOTA_HERO_NECROPHOS && skill==0 && !source_range(distance,500))continue;
                if(u->hero_id==DOTA_HERO_NECROPHOS && skill==1 && u->hp>u->max_hp*2/3)continue;
                if(u->hero_id==DOTA_HERO_NECROPHOS && skill==3 && v->hp>v->max_hp/2)continue;
                if(u->hero_id==DOTA_HERO_AXE && skill==0 && !source_range(distance,315))continue;
                if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==0 && !source_range(distance,260))continue;
                if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==1 && dota_healing_ward(g,index)>=0)continue;
                if(u->hero_id==DOTA_HERO_JUGGERNAUT && skill==1 && u->hp>u->max_hp*3/4)continue;
                if(cast_for(g,index,skill,v->x,v->y)){if(dota_omnislash_phase(g,index) || roster_busy(g,index))return;break;}
            }
        }
        if(source_range(distance,attack_reach_for(g,u,v)))attack(g,index,target);
        else if(!structure(u))move_for_attack(g,u,v);
    }else if(!structure(u))follow_lane(g,u);
}

static void player_click_move(DotaGame *g) {
    DotaUnit *u=&g->units[g->player];int steps=0;
    while(u->move_order && g->player_path_index<g->player_path_count && steps++<DOTA_MAX_PATH) {
        DotaPoint p=g->player_path[g->player_path_index];
        move_step(g,u,p.x,p.y);
        if(u->x!=p.x || u->y!=p.y)return;
        ++g->player_path_index;
        if(g->player_path_index>=g->player_path_count){
            if(!g->player_path_complete){click_path(g,u->move_x,u->move_y);if(!g->player_path_count)u->move_order=0;}
            else u->move_order=0;
        }
        if(g->movement[g->player].remaining_q16<=0)return;
    }
}

static void player(DotaGame *g,const DotaInput *input) {
    DotaUnit *u=&g->units[g->player];int i,target=u->target;
    if(input&&(input->move_x||input->move_y||input->actions))dota_roster_try_leave(g,g->player);
    if(input && (input->move_x || input->move_y || (input->actions&(DOTA_INPUT_STOP|DOTA_INPUT_ATTACK))))dota_items_cancel_teleport(g,g->player);
    if(input && (input->move_x || input->move_y || (input->actions&(DOTA_INPUT_STOP|DOTA_INPUT_ATTACK))))dota_roster_cancel_cast(g,g->player);
    if(dota_items_wind_move_source(g,g->player)){
        if(input && (input->actions&DOTA_INPUT_STOP)){u->move_order=0;g->player_path_count=g->player_path_index=0;}
        u->target=-1;
        if(input && (input->move_x || input->move_y)){
            u->move_order=0;g->player_path_count=g->player_path_index=0;
            move_step_direction(g,u,u->x+clamp(input->move_x,-1,1)*100,u->y+clamp(input->move_y,-1,1)*100,1);
        }else if(u->move_order)player_click_move(g);
        return;
    }
    if(u->taunt_ticks && u->taunt_source>=0 && alive(&g->units[u->taunt_source])) {
        DotaUnit *source=&g->units[u->taunt_source];
        u->move_order=0;u->target=u->taunt_source;
        g->player_path_count=g->player_path_index=0;
        if(source_range(dist2(u->x,u->y,source->x,source->y),attack_reach_for(g,u,source)))attack(g,g->player,u->taunt_source);
        else move_for_attack(g,u,source);
        return;
    }
    if(dota_objective_player(g,input))return;
    if(dota_rune_player(g,input))return;
    if(input) {
        if(input->actions&DOTA_INPUT_STOP){u->move_order=0;u->target=-1;target=-1;g->player_path_count=g->player_path_index=0;}
        for(i=0;i<4;++i)if(input->actions&(DOTA_INPUT_Q<<i))dota_cast(g,i,input->target_x,input->target_y);
        if(roster_busy(g,g->player))return;
        if(input->actions&DOTA_INPUT_FLASK) {
            for(i=0;i<DOTA_ITEM_SLOTS;++i)if(dota_items_equipped_item(g,g->player,i)==DOTA_FLASK){dota_use_item(g,i);break;}
            if(i==DOTA_ITEM_SLOTS)event(g,9,0);
        }
        if(dota_omnislash_phase(g,g->player)==DOTA_OMNISLASH_CASTING){
            if(input->move_x || input->move_y || (input->actions&(DOTA_INPUT_STOP|DOTA_INPUT_ATTACK)))omnislash_cancel_cast(g,g->player);
            else return;
        }
        if(input->actions&DOTA_INPUT_ATTACK) {
            int selected=dota_nearest_enemy(g,u->team,input->target_x,input->target_y,300);
            if(selected>=0){dota_attack_target(g,selected);target=selected;}
        }
        if(input->move_x || input->move_y) {
            int dx=clamp(input->move_x,-1,1),dy=clamp(input->move_y,-1,1);
            u->move_order=0;u->target=-1;target=-1;g->player_path_count=g->player_path_index=0;
            g->unit_routes[g->player].count=g->unit_routes[g->player].index=0;
            move_step_direction(g,u,u->x+dx*100,u->y+dy*100,1);
        }else if(u->move_order) {
            player_click_move(g);
        }
    }else if(u->move_order)player_click_move(g);
    if(u->taunt_ticks && u->taunt_source>=0 && alive(&g->units[u->taunt_source]))target=u->taunt_source;
    if(target>=0 && targetable(g,target) && dota_unit_visible(g,g->player,target) && g->units[target].team!=u->team) {
        int distance=dist2(u->x,u->y,g->units[target].x,g->units[target].y);
        if(source_range(distance,attack_reach_for(g,u,&g->units[target])))attack(g,g->player,target);
        else if(!input || (!input->move_x && !input->move_y))move_for_attack(g,u,&g->units[target]);
    }else {
        u->target=-1;target=bot_enemy(g,g->player,u->range);
        if(target>=0)attack(g,g->player,target);
    }
}

static void statuses(DotaGame *g,int index) {
    DotaUnit *u=&g->units[index];int i;
    const DotaJungleDefinition *definition=dota_jungle_definition(u);
    if(dota_barracks_kind(u)==DOTA_BARRACKS_MELEE){
        u->cooldown[0]+=500; /* npc_units:5.00 HP/s at20Hz. */
        if(u->cooldown[0]>=DOTA_HZ*100){u->hp=clamp(u->hp+u->cooldown[0]/(DOTA_HZ*100),0,u->max_hp);u->cooldown[0]%=DOTA_HZ*100;}
    }
    if(u->type==DOTA_CREEP){
        const DotaLaneCreepSource *lane=&dota_lane_creep_source[u->tier!=0];
        /* Lane creeps have no learned abilities. Their first two cooldown
         * slots retain sub-HP/sub-mana regeneration at the20Hz clock. */
        u->cooldown[0]+=lane->regen100;u->cooldown[1]+=lane->mana_regen100;
        if(u->cooldown[0]>=DOTA_HZ*100){u->hp=clamp(u->hp+u->cooldown[0]/(DOTA_HZ*100),0,u->max_hp);u->cooldown[0]%=DOTA_HZ*100;}
        if(u->cooldown[1]>=DOTA_HZ*100){u->mana=clamp(u->mana+u->cooldown[1]/(DOTA_HZ*100),0,u->max_mana);u->cooldown[1]%=DOTA_HZ*100;}
    }
    if(definition && index>=DOTA_NEUTRAL_FIRST && index<DOTA_ITEM_UNIT_FIRST){
        unsigned fraction=g->neutral_regen_fraction[index-DOTA_NEUTRAL_FIRST]+definition->regen100;
        if(u->hp<u->max_hp)u->hp=clamp(u->hp+(int)(fraction/(DOTA_HZ*100)),0,u->max_hp);
        g->neutral_regen_fraction[index-DOTA_NEUTRAL_FIRST]=(unsigned short)(fraction%(DOTA_HZ*100));
        /* All30 installed ordinary NPC definitions have1.00 mana/s. */
        if(g->tick%DOTA_HZ==0&&u->mana<u->max_mana)++u->mana;
    }

    if(u->attack_wait)--u->attack_wait;
    if(u->stun_ticks)--u->stun_ticks;
    if(u->slow_ticks)--u->slow_ticks;
    if(!u->slow_ticks)u->buff_ticks=0;
    if(u->taunt_ticks)--u->taunt_ticks;
    if(u->taunt_ticks && (u->taunt_source<0 || u->taunt_source>=DOTA_MAX_UNITS || !alive(&g->units[u->taunt_source])))u->taunt_ticks=0;
    if(u->channel_ticks)--u->channel_ticks;
    if(u->dot_ticks) {
        --u->dot_ticks;if(u->dot_ticks%20==0){
            if(index<DOTA_HERO_COUNT && u->type==DOTA_HERO){
                int source=u->dot_source,team=(unsigned)source<DOTA_MAX_UNITS?g->units[source].team:-1;
                assist_sync(g,index);
                damage_snapshot(g,index,u->dot_damage,source,g->hero_dot_source_generation[index],team,2);
            }else damage(g,index,u->dot_damage,u->dot_source,2);
        }
        if(!alive(u))return;
    }
    if(u->type!=DOTA_HERO)return;
    for(i=0;i<4;++i){
        if(dota_skill_is_passive(u->hero_id,i)){
            u->cooldown[i]=0;
        }else{if(u->cooldown[i])--u->cooldown[i];if(u->skill_ticks[i] && !(i==3 && dota_omnislash_phase(g,index)))--u->skill_ticks[i];}
    }
    if(u->heal_ticks){--u->heal_ticks;u->hp=clamp(u->hp+3,0,u->max_hp);}
    if(index<DOTA_HERO_COUNT){int hp=0,mp=0;
        if(dota_progression_base(u->hero_id)){
            DotaItemStats before,after;
            dota_items_stats(g,index,&before);recalc_with_stats(g,index,&before);
            /* Recalc can change max HP and clamp current HP. Heart, Hulking
             * and Greaves may therefore differ from the pre-recalc query.
             * HP/MP restoration below is still one mutation-free operation. */
            dota_items_stats(g,index,&after);
            hp=effective_health_regen_with_stats(g,index,&after);mp=mana_regen_with_stats(g,index,&after);
        }
        g->item_world.hp_fraction[index]+=hp>0?hp:0;g->item_world.mana_fraction[index]+=mp>0?mp:0;
        if(g->item_world.hp_fraction[index]>=2000){int gain=g->item_world.hp_fraction[index]/2000;g->item_world.hp_fraction[index]%=2000;u->hp=clamp(u->hp+gain,0,u->max_hp);}
        if(g->item_world.mana_fraction[index]>=2000){u->mana=clamp(u->mana+g->item_world.mana_fraction[index]/2000,0,u->max_mana);g->item_world.mana_fraction[index]%=2000;}
    }
    if(at_fountain(g,index)){u->hp=clamp(u->hp+6,0,u->max_hp);u->mana=clamp(u->mana+4,0,u->max_mana);}
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && u->skill_ticks[0] && g->tick%4==0) {
            for(i=0;i<DOTA_MAX_UNITS;++i)if(targetable(g,i) && !structure(&g->units[i]) && g->units[i].team!=u->team &&
                source_range(dist2(u->x,u->y,g->units[i].x,g->units[i].y),dota_rules_aoe_radius(g,index,260))){
                int amount=skill_value(u,0,"blade_fury_damage",55+30*skill_rank(u,0))*blade_dance(g,index,i,1)/(100*5);
                damage(g,i,amount,index,1);
            }
            skill_effect(g,index,0,DOTA_FX_BURST,u->x,u->y,u->x,u->y,65,3);
    }
    if(g->tick%10==0) {
        if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && u->skill_ticks[3] && !u->stun_ticks) {
            area_damage(g,index,u->x,u->y,skill_value(u,3,"radius",810),skill_value(u,3,"damage",110)*4/5,0,20);
            skill_effect(g,index,3,DOTA_FX_BURST,u->x,u->y,u->x,u->y,203,9);
        }
    }
}

/* Current H1 mana-bearing units are heroes. Auras are global and choose the
 * strongest applicable source; proximity triples that source's contribution.
 * Fixed-point accumulation retains source0.4mana/s at a20Hz integer clock. */
static void respawn(DotaGame *g,int index) {
    DotaUnit *u=&g->units[index];int i,old_generation=u->generation;
    for(i=0;i<DOTA_SKILLS;++i)if(u->cooldown[i])--u->cooldown[i];
    /* Death can happen before or after this hero's turn in the tick. Aegis
     * uses its absolute deadline rather than consuming a tick at death. */
    if(g->bosses.aegis_owner==index && g->bosses.aegis_generation==old_generation && g->bosses.aegis_revive_tick>0){
        int wait=g->bosses.aegis_revive_tick-g->tick;
        u->respawn_ticks=wait>0?wait+1:1;
    }
    if(u->respawn_ticks>0 && --u->respawn_ticks==0) {
        ++u->generation;
        for(i=0;i<DOTA_MAX_SUMMONS;++i)if(g->summon_owner[i]==index && g->summon_owner_generation[i]==old_generation
           && u->hero_id==DOTA_HERO_JUGGERNAUT && alive(&g->units[DOTA_SUMMON_FIRST+i])
           && g->units[DOTA_SUMMON_FIRST+i].type==DOTA_SUMMON && g->units[DOTA_SUMMON_FIRST+i].team==u->team && g->summon_life[i]>0)
            g->summon_owner_generation[i]=u->generation;
        assist_sync(g,index);
        {int rx=dota_hero_spawns[u->team][index%5].x,ry=dota_hero_spawns[u->team][index%5].y;
         dota_bosses_aegis_respawn(g,index,old_generation,&rx,&ry);legal_position(u,rx,ry);}
        u->hp=u->max_hp;u->mana=u->max_mana;
        u->waypoint=0;u->target=-1;u->move_order=0;
        effect(g,DOTA_FX_HEAL,u->team,u->x,u->y,u->x,u->y,100,20);
    }
}

void dota_tick(DotaGame *g,const DotaInput *input) {
    int i;
    if(!g->paused && g->state==DOTA_PLAYING)dota_barracks_commit_tower_deaths(g);
    sync_navigation(g);
    if(g->paused || g->state!=DOTA_PLAYING)return;
    ++g->tick;
    dota_progression_tick(g);
    for(i=0;i<DOTA_HERO_COUNT;++i){
        DotaUnit *u=&g->units[i];assist_sync(g,i);passive_sync(g,i);
        if(u->active && u->type==DOTA_HERO)u->gold+=dota_turbo_passive_gold_at_tick(dota_match_game_ticks(g));
        if(g->omnislash[i].phase && !dota_omnislash_phase(g,i))omnislash_clear(g,i);
        if(!alive(u) || u->type!=DOTA_HERO)continue;
        if(g->passive_wait[i])--g->passive_wait[i];
        if(u->hero_id!=DOTA_HERO_CRYSTAL_MAIDEN && dota_skill_is_passive(u->hero_id,2) && u->skill_ticks[2])--u->skill_ticks[2];
        if(u->hero_id==DOTA_HERO_LINA && !u->skill_ticks[2])g->passive_stacks[i]=0;
    }
    if(g->last_event_ticks && --g->last_event_ticks==0)g->last_event=0;
    for(i=0;i<DOTA_MAX_EFFECTS;++i)if(g->effects[i].active && --g->effects[i].ticks<=0){g->effects[i].active=0;g->skill_effect_tags[i]=0;}
    {int match=dota_match_ticks(g),period=DOTA_LANE_WAVE_SECONDS*DOTA_HZ;
     g->spawn_wait=match<0?-match:period-match%period;
     if(match>=0&&match%period==0)spawn_wave(g);}
    neutral_camps(g);
    tormentor_update(g);
    for(i=0;i<DOTA_MAX_SUMMONS;++i)if(g->units[DOTA_SUMMON_FIRST+i].active
        && g->units[DOTA_SUMMON_FIRST+i].type==DOTA_SUMMON
        && (!alive(&g->units[DOTA_SUMMON_FIRST+i]) || !summon_valid(g,i) || g->summon_life[i]<=0)){int at=DOTA_SUMMON_FIRST+i;if(g->units[at].hero_id==DOTA_HERO_CRYSTAL_MAIDEN&&g->units[at].tier==-1&&summon_valid(g,i))crystal_clone_shatter(g,at);else summon_remove(g,at);}
    for(i=0;i<DOTA_MAX_UNITS;++i)if(g->units[i].active) {
        if(!alive(&g->units[i])){if(g->units[i].type==DOTA_HERO)respawn(g,i);continue;}
        statuses(g,i);
        if(g->state!=DOTA_PLAYING)break;
        if(!alive(&g->units[i]))continue;
        if(dota_roster_stun(g,i) || dota_roster_moving(g,i)){if(i<DOTA_HERO_COUNT)dota_roster_cancel_cast(g,i);continue;}
        if(i<DOTA_HERO_COUNT && g->units[i].type==DOTA_HERO){
            if(i==g->player && input && dota_omnislash_phase(g,i)==DOTA_OMNISLASH_CASTING
               && (input->move_x || input->move_y || (input->actions&(DOTA_INPUT_STOP|DOTA_INPUT_ATTACK))))omnislash_cancel_cast(g,i);
            if(omnislash_update(g,i)){
                if(i==g->player && input && (input->actions&DOTA_INPUT_FLASK)){
                    int slot;for(slot=0;slot<DOTA_ITEM_SLOTS;++slot)if(dota_items_equipped_item(g,i,slot)==DOTA_FLASK){dota_use_item(g,slot);break;}
                    if(slot==DOTA_ITEM_SLOTS)event(g,9,0);
                }
                continue;
            }
        }
        if(dota_roster_controls_aux(g,i))dota_roster_aux_tick(g,i);
        else if(i>=DOTA_ITEM_UNIT_FIRST)dota_item_world_update_unit(g,i);
        else if(g->units[i].type==DOTA_SUMMON)summon_update(g,i);
        else if(i==g->player)player(g,input);else bot(g,i);
        if(g->state!=DOTA_PLAYING)break;
    }
    if(g->state==DOTA_PLAYING)dota_extra_tick(g);
    if(g->state==DOTA_PLAYING)dota_roster_tick(g);
    if(g->state==DOTA_PLAYING)dota_items_tick(g);
    if(g->state==DOTA_PLAYING)dota_item_world_tick(g);
    if(g->state==DOTA_PLAYING)projectiles(g);
    if(g->state==DOTA_PLAYING)dota_bosses_tick(g);
    if(g->state==DOTA_PLAYING)dota_objectives_tick(g);
    if(g->state==DOTA_PLAYING)dota_runes_tick(g);
    if(g->state==DOTA_PLAYING)dota_couriers_tick(g);
}

void dota_rules_attack_or_move(DotaGame *g,int index,int target){
    DotaUnit *u;if(!g || (unsigned)index>=DOTA_MAX_UNITS || (unsigned)target>=DOTA_MAX_UNITS || !targetable(g,target) || !dota_unit_visible(g,index,target))return;
    u=&g->units[index];if(source_range(dist2(u->x,u->y,g->units[target].x,g->units[target].y),attack_reach_for(g,u,&g->units[target])))attack(g,index,target);
    else move_for_attack(g,u,&g->units[target]);
}
void dota_rules_move(DotaGame *g,int index,int x,int y){if(g && (unsigned)index<DOTA_MAX_UNITS)move_towards(g,&g->units[index],x,y);}
void dota_rules_damage(DotaGame *g,int victim,int amount,int source,int magical){damage(g,victim,amount,source,magical);}
int dota_rules_find_enemy(DotaGame *g,int index,int radius){return enemy_source(g,index,radius,0);}
void dota_rules_grant_xp(DotaGame *g,int index,int amount){if((unsigned)index<DOTA_HERO_COUNT)add_xp(g,index,amount);}
void dota_rules_effect(DotaGame *g,int kind,int team,int x,int y,int tx,int ty,int radius,int ticks){effect(g,kind,team,x,y,tx,ty,radius,ticks);}

void dota_rules_skill_event(DotaGame *g,int caster,int skill,int target,int x,int y,int duration,int kind){
    DotaSkillEvent *e=skill_event(g,kind,caster,skill,target,x,y,0);e->duration_ticks=duration;
}
void dota_rules_skill_effect(DotaGame *g,int caster,int skill,int kind,int x,int y,int tx,int ty,int radius,int ticks){skill_effect(g,caster,skill,kind,x,y,tx,ty,radius,ticks);}
void dota_rules_attack_once(DotaGame *g,int source,int target){if(g && (unsigned)source<DOTA_MAX_UNITS && (unsigned)target<DOTA_MAX_UNITS)attack(g,source,target);}
void dota_rules_set_position(DotaGame *g,int hero,int x,int y){
    DotaUnit *u=&g->units[hero];DotaMovementState *m=&g->movement[hero];
    dota_sight_invalidate();
    u->x=clamp(x,0,DOTA_WORLD-1);u->y=clamp(y,0,DOTA_WORLD-1);m->x_q16=u->x*DOTA_MOVE_ONE;m->y_q16=u->y*DOTA_MOVE_ONE;m->generation=u->generation;m->tick=g->tick;m->remaining_q16=0;
    g->unit_routes[hero].count=g->unit_routes[hero].index=0;
    if(hero==g->player)g->player_path_count=g->player_path_index=0;
}

void dota_rules_recalc(DotaGame *g,int hero){if(g && (unsigned)hero<DOTA_HERO_COUNT)recalc(g,hero);}
void dota_rules_item_recalc(DotaGame *g,int hero){
    DotaUnit *u;int old_hp,old_mana,current_hp,current_mana,living;if(!g || (unsigned)hero>=DOTA_HERO_COUNT)return;
    u=&g->units[hero];old_hp=u->max_hp;old_mana=u->max_mana;current_hp=u->hp;current_mana=u->mana;living=alive(u);recalc(g,hero);
    if(living){u->hp=clamp(current_hp+u->max_hp-old_hp,1,u->max_hp);u->mana=clamp(current_mana+u->max_mana-old_mana,0,u->max_mana);}
}
int dota_rules_item_damage(DotaGame *g,int source,int target,int amount,int magical){
    int before;if(!g || (unsigned)target>=DOTA_MAX_UNITS)return 0;before=g->units[target].hp;
    damage(g,target,amount,source,magical);return clamp(before-g->units[target].hp,0,before);
}
int dota_rules_item_move(DotaGame *g,int index,int x,int y,int blink){
    int xx,yy;DotaUnit *u;if(!g || (unsigned)index>=DOTA_MAX_UNITS)return 0;u=&g->units[index];
    sync_navigation(g);x=clamp(x,0,DOTA_WORLD-1);y=clamp(y,0,DOTA_WORLD-1);
    if(!dota_extra_allow_move(g,index,x,y) || !dota_roster_allow_move(g,index,x,y))return 0;
    if(blink){if(!dota_nav_nearest(x,y,96,&xx,&yy))return 0;}
    else {xx=x;yy=y;if(!dota_nav_segment(u->x,u->y,xx,yy) && !dota_nav_nearest(x,y,48,&xx,&yy))return 0;}
    dota_rules_set_position(g,index,xx,yy);u->move_order=0;u->target=-1;return 1;
}
void dota_rules_item_finish_free_path(DotaGame *g,int hero){
    int x,y;if(!g || (unsigned)hero>=DOTA_HERO_COUNT)return;
    sync_navigation(g);
    if(!dota_nav_walkable(g->units[hero].x,g->units[hero].y) && dota_nav_nearest(g->units[hero].x,g->units[hero].y,256,&x,&y))dota_rules_set_position(g,hero,x,y);
    if(hero==g->player){g->player_path_count=g->player_path_index=0;g->units[hero].move_order=0;}
}
void dota_rules_item_effect(DotaGame *g,int kind,int team,int x,int y,int xx,int yy,int radius,int ticks){effect(g,kind,team,x,y,xx,yy,radius,ticks);}
int dota_rules_item_attack_range(const DotaGame *g,int hero){return g && (unsigned)hero<DOTA_MAX_UNITS?g->units[hero].range:0;}
void dota_rules_item_xp(DotaGame *g,int hero,int amount){if(g && (unsigned)hero<DOTA_HERO_COUNT)add_xp(g,hero,amount);}
void dota_rules_item_attack_once(DotaGame *g,int source,int target){dota_rules_attack_once(g,source,target);}
void dota_rules_item_counterattack(DotaGame *g,int source,int target,int percent){
    DotaProjectile p;int amount;
    if(!g || (unsigned)source>=DOTA_MAX_UNITS || (unsigned)target>=DOTA_MAX_UNITS || !alive(&g->units[source]) || !targetable(g,target) || !dota_unit_visible(g,source,target))return;
    amount=basic_attack_damage(g,source,target,0)*clamp(percent,0,1000)/100;
    projectile_snapshot(&p,g,source,target,amount);attack_event(g,DOTA_ATTACK_EVENT_START,&p,1);
    attack_impact(g,&p);attack_event(g,DOTA_ATTACK_EVENT_IMPACT,&p,6);
}
int dota_rules_roster_attack_scaled(DotaGame *g,int source,int target,int bonus_damage,int percent){
    DotaProjectile p;int before;
    if(!g || (unsigned)source>=DOTA_MAX_UNITS || (unsigned)target>=DOTA_MAX_UNITS || !alive(&g->units[source]) || !targetable(g,target) || !dota_unit_visible(g,source,target))return 0;
    before=g->units[target].hp;projectile_snapshot(&p,g,source,target,basic_attack_damage(g,source,target,bonus_damage)*clamp(percent,0,1000)/100);
    attack_event(g,DOTA_ATTACK_EVENT_START,&p,1);attack_impact(g,&p);attack_event(g,DOTA_ATTACK_EVENT_IMPACT,&p,6);
    return clamp(before-g->units[target].hp,0,before);
}
int dota_rules_roster_attack(DotaGame *g,int source,int target,int bonus_damage){return dota_rules_roster_attack_scaled(g,source,target,bonus_damage,100);}

void dota_rules_roster_interrupt(DotaGame *g,int target){
    DotaUnit *u;if(!g||(unsigned)target>=DOTA_MAX_UNITS)return;u=&g->units[target];
    if(target<DOTA_HERO_COUNT){
        /* A released Meteor continues to land after its channel has ended. */
        if(u->channel_ticks>0)g->item_world.channel[target].until=0;
        if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN)u->skill_ticks[3]=0;
        g->extra.hero[target].scythe_cast_at=0;
        omnislash_cancel_cast(g,target);dota_roster_cancel_cast(g,target);
    }else if(target>=DOTA_ITEM_UNIT_FIRST)g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST].cast_at=0;
    u->channel_ticks=0;
}
int dota_rules_roster_spawn_aux(DotaGame *g,int owner,int kind,int x,int y,int hp){
    int local,i,index,generation;DotaUnit *u;DotaItemWorldUnit *s;
    if(!g || (unsigned)owner>=DOTA_HERO_COUNT || (kind!=ITEM_UNIT_ROSTER_FISH && kind!=ITEM_UNIT_ROSTER_ANCHOR))return -1;
    for(local=0;local<DOTA_ITEM_UNIT_COUNT;++local)if(!g->units[DOTA_ITEM_UNIT_FIRST+local].active)break;
    if(local==DOTA_ITEM_UNIT_COUNT)return -1;
    index=DOTA_ITEM_UNIT_FIRST+local;u=&g->units[index];s=&g->item_world.unit[local];generation=u->generation+1;
    zero_bytes(u,sizeof(*u));zero_bytes(s,sizeof(*s));u->generation=generation;u->active=1;u->type=DOTA_SUMMON;u->hero_id=-1;
    u->team=g->units[owner].team;u->hp=u->max_hp=clamp(hp,1,1000);u->target=u->taunt_source=u->dot_source=-1;
    for(i=0;i<DOTA_ITEM_SLOTS;++i)u->inventory[i]=-1;
    s->kind=kind;s->owner=owner;s->generation=g->units[owner].generation;s->until=0x7fffffff;s->source_item=-1;
    s->incoming_percent=s->damage_percent=100;
    dota_rules_set_position(g,index,x,y);return index;
}
int dota_rules_last_mana_spent(const DotaGame *g,int hero){return g && (unsigned)hero<DOTA_HERO_COUNT?g->item_world.last_mana_spent[hero]:0;}
int dota_rules_unit_in_water(const DotaGame *g,int unit){return g && (unsigned)unit<DOTA_MAX_UNITS && g->units[unit].active?dota_water_world(g->units[unit].x,g->units[unit].y):0;}
void dota_rules_item_move_order(DotaGame *g,int index,int x,int y){
    DotaUnit *u;if(!g || (unsigned)index>=DOTA_MAX_UNITS)return;u=&g->units[index];u->move_order=1;u->move_x=x;u->move_y=y;move_towards(g,u,x,y);
}
void dota_rules_item_kill(DotaGame *g,int unit,int source){
    if(g && (unsigned)unit<DOTA_MAX_UNITS && alive(&g->units[unit])){g->units[unit].hp=0;kill_unit(g,unit,source,(unsigned)source<DOTA_MAX_UNITS?g->units[source].team:-1);}
}
void dota_rules_attributes(const DotaGame *g,int hero,int *str,int *agi,int *intel){
    int bs=0,ba=0,bi=0,ds=0,da=0,di=0;DotaItemStats st;
    if(str)*str=0;
    if(agi)*agi=0;
    if(intel)*intel=0;
    if(!g||(unsigned)hero>=DOTA_HERO_COUNT)return;
    dota_progression_base_attributes100(&g->units[hero],&bs,&ba,&bi);hero_stat_delta(g,hero,&ds,&da,&di);dota_items_stats((DotaGame*)g,hero,&st);
    if(str)*str=clamp((bs+st.value[ITEM_STAT_STRENGTH])/100+ds,1,10000);
    if(agi)*agi=clamp((ba+st.value[ITEM_STAT_AGILITY])/100+da,1,10000);
    if(intel)*intel=clamp((bi+st.value[ITEM_STAT_INTELLECT])/100+di,1,10000);
}
static int raw_health_regen_with_stats(const DotaGame *g,int hero,const DotaItemStats *st){
    const DotaProgressionBase *base;int str=0,agi=0,intel=0,bs,ba,bi;
    if(!g||(unsigned)hero>=DOTA_HERO_COUNT||!(base=dota_progression_base(g->units[hero].hero_id)))return 0;
    dota_progression_base_attributes100(&g->units[hero],&bs,&ba,&bi);hero_stat_delta100(g,hero,&str,&agi,&intel);
    return (base->hp_regen100+bs/10+str/10+st->value[ITEM_STAT_HEALTH_REGEN]+dota_items_timed_regen100(g,hero)+dota_extra_regen_flat100(g,hero)+dota_extra_additional_regen100(g,hero)+dota_rune_hp_regen100(g,hero))*dota_items_health_regen_amp_percent((DotaGame*)g,hero)/100;
}
int dota_rules_item_raw_regen100(const DotaGame *g,int hero){DotaItemStats st;if(!g||(unsigned)hero>=DOTA_HERO_COUNT||!dota_progression_base(g->units[hero].hero_id))return 0;dota_items_stats((DotaGame*)g,hero,&st);return raw_health_regen_with_stats(g,hero,&st);}
int dota_rules_health_regen100(const DotaGame *g,int hero){return dota_rules_item_raw_regen100(g,hero)*dota_items_regen_percent(g,hero)/100;}
int dota_rules_effective_health_regen100(const DotaGame *g,int hero){return dota_items_heal_amount((DotaGame*)g,hero,hero,dota_rules_health_regen100(g,hero));}
static int effective_health_regen_with_stats(const DotaGame *g,int hero,const DotaItemStats *st){return dota_items_heal_amount_with_stats((DotaGame*)g,hero,hero,raw_health_regen_with_stats(g,hero,st)*dota_items_regen_percent(g,hero)/100,st);}
static int mana_regen_with_stats(const DotaGame *g,int hero,const DotaItemStats *st){
    const DotaProgressionBase *base;int str=0,agi=0,intel=0,bs,ba,bi,mp;
    if(!g||(unsigned)hero>=DOTA_HERO_COUNT||!(base=dota_progression_base(g->units[hero].hero_id)))return 0;
    dota_progression_base_attributes100(&g->units[hero],&bs,&ba,&bi);hero_stat_delta100(g,hero,&str,&agi,&intel);
    mp=base->mp_regen100+bi/20+intel/20+st->value[ITEM_STAT_MANA_REGEN]+dota_items_timed_mana_regen100(g,hero);
    {int source,best=0;for(source=0;source<DOTA_HERO_COUNT;++source){const DotaUnit *cm=&g->units[source];int rate;if(!alive(cm)||cm->team!=g->units[hero].team||cm->hero_id!=DOTA_HERO_CRYSTAL_MAIDEN||!skill_rank(cm,2)||dota_items_broken(g,source))continue;rate=dota_progression_value100(cm,2,"base_mana_regen",0);if(source_range(dist2(cm->x,cm->y,g->units[hero].x,g->units[hero].y),skill_value(cm,2,"proximity_bonus_radius",1200)))rate=rate*skill_value(cm,2,"proximity_bonus_factor",3);if(rate>best)best=rate;}mp+=best;}
    mp+=dota_extra_additional_mana_regen100(g,hero)+dota_rune_mana_regen100(g,hero);
    if(g->units[hero].hero_id==DOTA_HERO_CRYSTAL_MAIDEN&&skill_rank(&g->units[hero],2)>0&&!dota_items_broken(g,hero))mp=mp*(100+skill_value(&g->units[hero],2,"mana_regen_amp",0))/100;
    return dota_extra_mana_restore_amount(g,hero,mp*dota_items_mana_regen_percent((DotaGame*)g,hero)/100);
}
int dota_rules_mana_regen100(const DotaGame *g,int hero){DotaItemStats st;if(!g||(unsigned)hero>=DOTA_HERO_COUNT||!dota_progression_base(g->units[hero].hero_id))return 0;dota_items_stats((DotaGame*)g,hero,&st);return mana_regen_with_stats(g,hero,&st);}
int dota_rules_aoe_radius(const DotaGame *g,int hero,int radius){
    DotaItemStats st;if(radius<=0)return 0;if(!g || (unsigned)hero>=DOTA_HERO_COUNT)return radius;
    dota_items_stats((DotaGame*)g,hero,&st);return (radius+st.value[ITEM_STAT_AOE_BONUS])*dota_items_special_aoe_percent(g,hero)/100;
}
