/* Source-derived Pudge, Sniper, Sven and Tidehunter. Integer20Hz simulation;
 * expensive area scans run only while a real cast/field is active. */
#include "roster_heroes.h"
#include "progression.h"
#include "roster_heroes_data.h"
#include "extra_heroes.h"
#include "items.h"
#include "items_internal.h"
#include "items_special.h"
#include "item_world.h"
#include "navigation.h"
#include "vision.h"
#include "bosses.h"
typedef char roster_cap_must_match[(DOTA_ROSTER_TARGET_CAP==DOTA_MAX_UNITS)?1:-1];

static int min_i(int a,int b){return a<b?a:b;}
static int max_i(int a,int b){return a>b?a:b;}
static int cap(int a,int l,int h){return a<l?l:a>h?h:a;}
static void clear(void *p,unsigned n){unsigned char *b=p;while(n--)*b++=0;}
static int live(const DotaGame*g,int i){return g && (unsigned)i<DOTA_MAX_UNITS && g->units[i].active && g->units[i].hp>0;}
static int sq(int x,int y,int xx,int yy){int dx=x-xx,dy=y-yy;return dx*dx+dy*dy;}
static int root_i(unsigned n){unsigned x=n,y=(x+1)/2;while(y<x){x=y;y=(x+n/x)/2;}return (int)x;}
static int within(int x,int y,int xx,int yy,int radius){return (long long)sq(x,y,xx,yy)*16<=(long long)radius*radius;}
static int hero_index(const DotaGame*g,int i){return g && (unsigned)i<10u && dota_roster_is_hero(&g->units[i]);}
static int rank(const DotaUnit*u,int skill){return dota_progression_rank(u,skill);}
static int value100(const DotaUnit*u,int skill,const char*field,int fallback){return dota_progression_value100(u,skill,field,fallback);}
static int value(const DotaUnit*u,int skill,const char*field,int fallback){return value100(u,skill,field,fallback*100)/100;}
static int ticks(const DotaUnit*u,int skill,const char*field,int fallback){return value100(u,skill,field,fallback*5)/5;}
static void anchor_smash_scaled(DotaGame*g,int h,int power);
static int structure(const DotaUnit*u){return u->type==DOTA_TOWER || u->type==DOTA_BASE || u->type==DOTA_BUILDING;}
static int valid_target(const DotaGame*g,int target,int generation){return live(g,target) && g->units[target].generation==generation;}
static int enemy_basic(const DotaGame*g,int h,int t){return live(g,t) && g->units[t].team!=g->units[h].team && !structure(&g->units[t]) && g->units[t].type!=DOTA_TORMENTOR && !dota_roster_controls_aux(g,t);}
static const DotaRosterHero *hero_state(const DotaGame*g,int h){return hero_index(g,h)&&g->roster.hero[h].generation==g->units[h].generation?&g->roster.hero[h]:0;}
int dota_roster_swallowed(const DotaGame*g,int h){const DotaRosterHero*s=hero_state(g,h);return s&&g->units[h].hero_id==6&&s->channel_rank==255&&valid_target(g,s->channel_target,s->channel_generation)?s->channel_target:-1;}
int dota_roster_swallowed_by(const DotaGame*g,int t){const DotaRosterTarget*s;int h;if(!g||(unsigned)t>=DOTA_HERO_COUNT)return -1;s=&g->roster.target[t];if(s->generation!=g->units[t].generation)return -1;h=s->hold_owner;return (unsigned)h<DOTA_HERO_COUNT&&dota_roster_swallowed(g,h)==t?h:-1;}
static const DotaRosterTarget *target_state(const DotaGame*g,int t){return live(g,t)&&(g->roster.target_active[(unsigned)t/32]&(1u<<((unsigned)t%32)))&&g->roster.target[t].generation==g->units[t].generation?&g->roster.target[t]:0;}
static DotaRosterTarget *touch(DotaGame*g,int t){DotaRosterTarget*s=&g->roster.target[t];if(s->generation!=g->units[t].generation){clear(s,sizeof(*s));s->generation=g->units[t].generation;}g->roster.target_active[(unsigned)t/32]|=1u<<((unsigned)t%32);return s;}
static int eject_ally(DotaGame*g,int h){
    DotaRosterHero*s=&g->roster.hero[h];int t=dota_roster_swallowed(g,h),x,y;
    if(s->channel_rank!=255)return 0;
    s->channel_rank=0;s->channel_target=DOTA_ROSTER_NO_TARGET;s->channel_until=0;
    if(t<0)return 1;
    {DotaRosterTarget*v=touch(g,t);if(v->hold_owner==h)v->hold_ticks=0;}
    x=g->units[h].x+(g->units[h].team? -24:24);y=g->units[h].y;
    if(!dota_nav_nearest(x,y,128,&x,&y)){x=g->units[h].x;y=g->units[h].y;}
    dota_rules_set_position(g,t,x,y);g->units[t].move_order=0;g->units[t].target=-1;
    dota_rules_skill_event(g,h,3,t,x,y,8,DOTA_SKILL_EVENT_STRIKE);return 1;
}
int dota_roster_try_leave(DotaGame*g,int t){int h=dota_roster_swallowed_by(g,t);return h>=0&&g->tick>=g->roster.hero[h].next_pulse?eject_ally(g,h):0;}
static void sync_hero(DotaGame*g,int h){DotaRosterHero*s=&g->roster.hero[h];if(s->generation!=g->units[h].generation){unsigned flesh=s->flesh_stacks,fish=s->fish_count,level=s->level_seen,charges=s->generation?s->shrapnel_charges:3;int restore=s->shrapnel_restore_at,extra=s->extra_cooldown;clear(s,sizeof(*s));s->generation=g->units[h].generation;s->flesh_stacks=(uint16_t)flesh;s->fish_count=(uint16_t)fish;s->level_seen=(uint8_t)level;s->shrapnel_charges=(uint8_t)charges;s->shrapnel_restore_at=restore;s->extra_cooldown=extra;s->seen_x=(int16_t)g->units[h].x;s->seen_y=(int16_t)g->units[h].y;s->vision_dx=(int16_t)(dota_fountains[!g->units[h].team].x-g->units[h].x);s->vision_dy=(int16_t)(dota_fountains[!g->units[h].team].y-g->units[h].y);s->cast_target=s->headshot_target=s->anchor_unit=s->channel_target=DOTA_ROSTER_NO_TARGET;}}
static int resisted(DotaGame*g,int source,int target,int duration){int sr=dota_items_status_resist_percent(g,target),boss=dota_boss_status_resist(&g->units[target]);duration=duration*dota_items_special_debuff_percent(g,source)/100;return max_i(1,duration*(100-sr)*(100-boss)/10000);}
static void stun(DotaGame*g,int source,int target,int ticks,int pierces){DotaRosterTarget*s=touch(g,target);s->stun_ticks=(uint16_t)max_i(s->stun_ticks,resisted(g,source,target,ticks));s->stun_pierces=(uint8_t)pierces;if(pierces||!dota_items_debuff_immune(g,target))dota_rules_roster_interrupt(g,target);}
static int total_attr(DotaGame*g,int h,int attr){return dota_items_total_attribute(g,h,attr);}
static void message(DotaGame*g,int caster,int kind,int value){if(caster==g->player){g->last_event=kind;g->last_event_value=value;g->last_event_ticks=50;}}
static int pay(DotaGame*g,int h,int skill,int extra){int cost=extra?dota_roster_extra_mana(g,h):dota_roster_mana(&g->units[h],skill);cost=cost*dota_items_mana_cost_percent(g,h)/100;if(dota_items_available_mana(g,h)<cost){message(g,h,1,skill);return 0;}g->item_world.last_mana_spent[h]=cost;dota_items_spend_mana(g,h,cost);if(extra)g->roster.hero[h].extra_cooldown=g->tick+dota_roster_extra_cooldown(g,h);else g->units[h].cooldown[skill]=dota_roster_cooldown(g,h,skill);dota_items_on_cast(g,h);return 1;}
static int damage(DotaGame*g,int h,int t,int amount,int mode){return dota_rules_item_damage(g,h,t,max_i(0,amount),mode);}
static void event(DotaGame*g,int h,int skill,int target,int x,int y,int ticks,int kind,int extra){dota_rules_skill_event(g,h,skill,target,x,y,ticks,kind);if(extra)g->skill_events[g->skill_event_serial%DOTA_MAX_SKILL_EVENTS].step=-4;}
static void mark_tide(DotaGame*g,int h,int target,int ticks){DotaRosterTarget*s=touch(g,target);s->tide_owner=(uint8_t)h;s->tide_mark_ticks=(uint16_t)max_i(s->tide_mark_ticks,ticks);}
const DotaRosterBase *dota_roster_base(int hero){return (unsigned)(hero-6)<4u?&roster_bases[hero-6]:0;}
int dota_roster_is_hero(const DotaUnit*u){return u&&u->type==DOTA_HERO&&(unsigned)(u->hero_id-6)<4u;}
int dota_roster_passive(int hero,int skill){return (hero==7 || hero==8)&&skill==1;}
void dota_roster_init(DotaGame*g){int i;clear(&g->roster,sizeof(g->roster));g->roster.last_tick=-1;for(i=0;i<10;i++)sync_hero(g,i);}
int dota_roster_mana(const DotaUnit*u,int skill){return dota_roster_is_hero(u)&&(unsigned)skill<4&&rank(u,skill)>0?value(u,skill,"AbilityManaCost",roster_mana[u->hero_id-6][skill][rank(u,skill)-1]):0;}
int dota_roster_cooldown(const DotaGame*g,int h,int skill){const DotaUnit*u;int cd;if(!g||(unsigned)h>=10u)return 0;u=&g->units[h];if(!dota_roster_is_hero(u)||(unsigned)skill>=4||!rank(u,skill))return 0;cd=ticks(u,skill,"AbilityCooldown",roster_cd[u->hero_id-6][skill][rank(u,skill)-1]);if(u->hero_id==9&&skill==0&&dota_hero_has_scepter(g,h))cd=140;return cd*(100-dota_items_cooldown_percent((DotaGame*)g,h))/100;}
int dota_roster_range(const DotaGame*g,int h,int skill){const DotaUnit*u;int range=0;if(!g||(unsigned)h>=10u)return 0;u=&g->units[h];if(!dota_roster_is_hero(u)||(unsigned)skill>=4)return 0;if(u->hero_id==6)range=skill==0?1300:skill==3?200:0;else if(u->hero_id==7)range=skill==0?1800:skill==3?3000:0;else if(u->hero_id==8)range=skill==0?(dota_hero_has_scepter(g,h)?750:600):0;else range=skill==0?(dota_hero_has_scepter(g,h)?2200:700):0;if(range&&!((u->hero_id==8||u->hero_id==9)&&skill==0&&dota_hero_has_scepter(g,h)))range=value(u,skill,"AbilityCastRange",range);return range?range+dota_items_range_bonus((DotaGame*)g,h,0):0;}
int dota_roster_radius(const DotaGame*g,int h,int skill){const DotaUnit*u;int r,base=0;if(!g||(unsigned)h>=10u||(unsigned)skill>=4u)return 0;u=&g->units[h];r=rank(u,skill)-1;if(!dota_roster_is_hero(u)||r<0)return 0;if(u->hero_id==6)base=skill==0?100:skill==1?(dota_hero_has_scepter(g,h)?450:250):0;else if(u->hero_id==7)base=skill==0?sniper_shrapnel_radius[r]:0;else if(u->hero_id==8)base=skill==0?sven_bolt_radius[r]:skill==1?sven_cleave_distance[r]:skill==2?(dota_hero_has_shard(g,h)?900:700):0;else base=skill==0?(dota_hero_has_scepter(g,h)?260:0):skill==2?225:skill==3?1250:0;if(u->hero_id==6&&skill==0)return base;if(u->hero_id==9&&skill==2)return g->units[h].range+dota_rules_aoe_radius(g,h,225);return base?dota_rules_aoe_radius(g,h,base):0;}
int dota_roster_extra_mana(const DotaGame*g,int h){return hero_index(g,h)?(g->units[h].hero_id==7?50:g->units[h].hero_id==9?110:0):0;}
int dota_roster_extra_range(const DotaGame*g,int h){int v=hero_index(g,h)?(g->units[h].hero_id==7?600:g->units[h].hero_id==9?350:0):0;return v?v+dota_items_range_bonus((DotaGame*)g,h,0):0;}
int dota_roster_extra_cooldown(const DotaGame*g,int h){return !hero_index(g,h)?0:(g->units[h].hero_id==7?200:g->units[h].hero_id==9?700:0)*(100-dota_items_cooldown_percent((DotaGame*)g,h))/100;}
void dota_roster_stat_delta(const DotaGame*g,int h,int*str,int*agi,int*intel){(void)agi;(void)intel;if(hero_index(g,h)&&g->units[h].hero_id==6)*str+=g->roster.hero[h].flesh_stacks*value(&g->units[h],5,"flesh_heap_strength_buff_amount",2);}
int dota_roster_max_health_bonus(const DotaGame*g,int h){return hero_index(g,h)&&g->units[h].hero_id==9?g->roster.hero[h].fish_count*value(&g->units[h],5,"fish_health_bonus",3):0;}
int dota_roster_attack_range_bonus(const DotaGame*g,int h){const DotaRosterHero*s=hero_state(g,h);const DotaUnit*u;if(!hero_index(g,h))return 0;u=&g->units[h];if(u->hero_id==7&&rank(u,2)>0)return value(u,2,"passive_attack_range_bonus",sniper_aim_range[rank(u,2)-1])+(s&&s->aim_until>g->tick?value(u,2,"active_attack_range_bonus",sniper_aim_active_range[rank(u,2)-1]):0);return u->hero_id==9?g->roster.hero[h].fish_count*value(u,5,"fish_attack_range",2):0;}
int dota_roster_vision_bonus(const DotaGame*g,int h){const DotaRosterHero*s=hero_state(g,h);return s&&g->units[h].hero_id==7&&s->aim_until>g->tick?value(&g->units[h],2,"bonus_vision",sniper_aim_vision[rank(&g->units[h],2)-1]):0;}
int dota_roster_vision_point(const DotaGame*g,int h,int x,int y,int base){
    const DotaRosterHero*s=hero_state(g,h);long long dx,dy,dot,cross;int bonus=dota_roster_vision_bonus(g,h);
    if(!s||!bonus||!within(g->units[h].x,g->units[h].y,x,y,base+bonus))return 0;
    dx=x-g->units[h].x;dy=y-g->units[h].y;dot=dx*s->vision_dx+dy*s->vision_dy;
    if(!dx&&!dy)return 1;
    if(dot<=0)return 0;
    cross=dx*s->vision_dy-dy*s->vision_dx;if(cross<0)cross=-cross;
    /* tan(70deg)=2.747477; scale-invariant even for a one-unit movement
     * heading. Both products fit signed64 for every map coordinate. */
    return cross*1000000<=dot*2747477;
}
int dota_roster_area_vision(const DotaGame*g,int team,int x,int y){
    unsigned bits;int i=0;if(!g||!g->roster.field_mask)return 0;bits=g->roster.field_mask;
    while(bits){if(bits&1u){const DotaRosterField*f=&g->roster.field[i];if(f->until>=g->tick&&g->tick>=f->starts_at&&g->units[f->source].team==team&&within(f->x,f->y,x,y,f->radius)&&dota_pit_same_region(f->x,f->y,x,y))return 1;}bits>>=1;i++;}return 0;
}
int dota_roster_attack_speed_delta(const DotaGame*g,int unit){const DotaRosterTarget*t=target_state(g,unit);const DotaRosterHero*h=(unsigned)unit<10u?hero_state(g,unit):0;int n=t&&t->headshot_ticks&&!dota_items_debuff_immune(g,unit)?-100:0;if(h&&g->units[unit].hero_id==7&&h->aim_until>g->tick)n+=value(&g->units[unit],2,"attack_speed",0);return n;}
int dota_roster_base_damage_percent(const DotaGame*g,int h){const DotaRosterHero*s=hero_state(g,h);return 100+(s&&g->units[h].hero_id==8&&s->gods_until>g->tick?value(&g->units[h],3,"gods_strength_damage",sven_gods_pct[rank(&g->units[h],3)-1]):0);}
int dota_roster_base_damage_reduction(const DotaGame*g,int unit){const DotaRosterTarget*t=target_state(g,unit);return t&&t->smash_ticks&&!dota_items_debuff_immune(g,unit)?t->smash_reduction:0;}
int dota_roster_bonus_damage(const DotaGame*g,int h){return hero_index(g,h)&&g->units[h].hero_id==8&&!dota_items_broken(g,h)?total_attr((DotaGame*)g,h,0)*value100(&g->units[h],5,"bonus_damage_per_str",8+2*(g->units[h].level-1))/100:0;}
int dota_roster_armor(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);int armor=0;if(!s)return 0;if(s->gush_ticks&&!dota_items_debuff_immune(g,t))armor-=s->gush_armor;if(s->warcry_ticks)armor+=s->warcry_armor;return armor;}
int dota_roster_move_percent(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);const DotaRosterHero*h=(unsigned)t<10u?hero_state(g,t):0;int move=0,slow=0;if(s){if(s->warcry_ticks)move=s->warcry_move;if(!dota_items_debuff_immune(g,t)){if(s->gush_ticks)slow=s->gush_slow;if(s->rot_ticks)slow=max_i(slow,s->rot_slow&127);if(s->headshot_ticks)slow=100;if(s->grenade_ticks)slow=max_i(slow,50);if(s->shrapnel_ticks)slow=max_i(slow,s->shrapnel_slow);}}if(h&&g->units[t].hero_id==7&&h->aim_until>g->tick)slow=max_i(slow,value(&g->units[t],2,"slow",65));if(h&&g->units[t].hero_id==9&&h->shell_until>g->tick)slow=max_i(slow,value(&g->units[t],1,"active_move_speed_penalty_pct",40));return 100+move-slow;}
int dota_roster_slow_resist(const DotaGame*g,int h){const DotaRosterHero*s=hero_state(g,h);return s&&g->units[h].hero_id==8&&s->gods_until>g->tick?value(&g->units[h],3,"bonus_slow_resistance",40):0;}
int dota_roster_heal_percent(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);return s&&s->rot_ticks&&s->rot_slow&128&&!dota_items_debuff_immune(g,t)?80:100;}
int dota_roster_disarmed(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);const DotaRosterHero*h=t<10?hero_state(g,t):0;return (s&&s->grenade_ticks&&!dota_items_debuff_immune(g,t))||(h&&h->cast_at>g->tick);}
int dota_roster_stun(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);if(!s)return 0;return max_i((s->stun_pierces||!dota_items_debuff_immune(g,t))?s->stun_ticks:0,s->hold_ticks);}
int dota_roster_invulnerable(const DotaGame*g,int t){if(dota_roster_swallowed_by(g,t)>=0)return 1;const DotaRosterHero*h=(unsigned)t<10u?hero_state(g,t):0;const DotaRosterSpell*s;if(!h||!h->fly_spell)return 0;s=&g->roster.spell[h->fly_spell-1];return s->kind==RS_STORM&&s->phase==2&&s->source==t&&s->source_generation==g->units[t].generation;}
int dota_roster_moving(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);if(s&&s->knock_ticks)return 1;if(s&&s->hook_spell){const DotaRosterSpell*p=&g->roster.spell[s->hook_spell-1];if(p->kind==RS_HOOK&&p->phase==1&&p->target==t&&p->target_generation==g->units[t].generation)return 1;}return dota_roster_invulnerable(g,t);}
int dota_roster_move_cap_source(const DotaGame*g,int t){const DotaRosterTarget*s=target_state(g,t);return s&&s->tether_ticks&&live(g,s->tether_anchor)&&!dota_items_debuff_immune(g,t)&&!within(g->units[t].x,g->units[t].y,g->units[s->tether_anchor].x,g->units[s->tether_anchor].y,350)?100:0;}
int dota_roster_allow_move(const DotaGame*g,int t,int x,int y){(void)g;(void)t;(void)x;(void)y;return 1;}
int dota_roster_cast_while_channel(const DotaGame*g,int h,int skill){return hero_index(g,h)&&g->units[h].hero_id==6&&(skill==1||skill==2);}
void dota_roster_cancel_cast(DotaGame*g,int h){DotaRosterHero*s;if(!hero_index(g,h))return;s=&g->roster.hero[h];if(s->channel_until){if(valid_target(g,s->channel_target,s->channel_generation)){DotaRosterTarget*t=touch(g,s->channel_target);if(t->hold_owner==h)t->hold_ticks=0;}g->units[h].channel_ticks=0;}s->cast_at=s->channel_until=0;}
void dota_roster_refresh(DotaGame*g,int h){DotaRosterHero*s;if(!hero_index(g,h))return;s=&g->roster.hero[h];s->extra_cooldown=0;if(g->units[h].hero_id==7){s->shrapnel_charges=3;s->shrapnel_restore_at=0;g->units[h].cooldown[0]=0;}}

static DotaRosterSpell *new_spell(DotaGame*g,int h,int kind,int rank_i,int target,int x,int y){
    int i;for(i=0;i<DOTA_ROSTER_SPELLS;i++)if(!g->roster.spell[i].kind){DotaRosterSpell*s=&g->roster.spell[i];clear(s,sizeof(*s));s->kind=(uint8_t)kind;s->rank=(uint8_t)rank_i;s->source=(uint16_t)h;s->source_generation=g->units[h].generation;s->target=target<0?DOTA_ROSTER_NO_TARGET:(uint16_t)target;s->target_generation=target<0?0:g->units[target].generation;s->origin_x=g->units[h].x;s->origin_y=g->units[h].y;s->x_q16=s->origin_x*DOTA_MOVE_ONE;s->y_q16=s->origin_y*DOTA_MOVE_ONE;s->to_x=x;s->to_y=y;s->life=200;s->upgrades=(uint8_t)((dota_hero_has_scepter(g,h)?1:0)|(dota_hero_has_shard(g,h)?2:0));s->speed=kind==RS_HOOK?1600:kind==RS_ASSASSINATE?2500:kind==RS_STORM?((s->upgrades&1)?1250:1000):kind==RS_GUSH_WAVE?1500:kind==RS_GUSH?2500:kind==RS_GRENADE?1200:1000;s->radius=kind==RS_STORM?dota_rules_aoe_radius(g,h,sven_bolt_radius[rank_i-1]):kind==RS_GUSH_WAVE?dota_rules_aoe_radius(g,h,260):kind==RS_GRENADE?dota_rules_aoe_radius(g,h,375):0;s->knockback=kind==RS_GRENADE?dota_rules_aoe_radius(g,h,475):0;return s;}return 0;
}
static int pick_target(DotaGame*g,int h,int x,int y,int range,int allow_invuln){
    int i,target=-1,best=96*96+1;for(i=0;i<DOTA_MAX_UNITS;i++)if(dota_rules_cast_target_matches(g,h,i) && enemy_basic(g,h,i)){
        int d=sq(x,y,g->units[i].x,g->units[i].y);if(d>=best||!within(g->units[h].x,g->units[h].y,g->units[i].x,g->units[i].y,range+24))continue;
        if((!allow_invuln&&dota_unit_invulnerable(g,i))||!dota_unit_visible(g,h,i))continue;
        best=d;target=i;
    }return target;
}
static int allied_dismember(DotaGame*g,int h,int x,int y){
    int t,best=-1,dist=96*96+1;if(!dota_hero_has_shard(g,h))return -1;
    for(t=0;t<DOTA_HERO_COUNT;t++)if(t!=h&&live(g,t)&&g->units[t].type==DOTA_HERO&&g->units[t].team==g->units[h].team&&dota_rules_cast_target_matches(g,h,t)&&!dota_unit_invulnerable(g,t)&&within(g->units[h].x,g->units[h].y,g->units[t].x,g->units[t].y,300)){
        int d=sq(x,y,g->units[t].x,g->units[t].y);if(d<dist){best=t;dist=d;}
    }return best;
}
static void knock(DotaGame*g,int target,int x,int y,int distance,int ticks){
    DotaRosterTarget*s=touch(g,target);int dx=g->units[target].x-x,dy=g->units[target].y-y,len=root_i((unsigned)(dx*dx+dy*dy));if(!len){dx=1;len=1;}
    s->knock_x=(int16_t)cap(g->units[target].x+(int)((long long)dx*distance/(4*len)),0,DOTA_WORLD-1);s->knock_y=(int16_t)cap(g->units[target].y+(int)((long long)dy*distance/(4*len)),0,DOTA_WORLD-1);s->knock_ticks=(uint16_t)ticks;
}
static int blocked(DotaGame*g,int h,int target){return dota_bosses_spell_block(g,target)||dota_items_blocks_spell(g,h,target);}
static void anchor_smash(DotaGame*g,int h,int power);
static void release_cast(DotaGame*g,int h){
    DotaUnit*u=&g->units[h];DotaRosterHero*s=&g->roster.hero[h];int skill=s->cast_skill,r=s->cast_rank,target=s->cast_target,x=s->cast_x,y=s->cast_y,i;s->cast_at=0;
    if(!live(g,h)||dota_items_silenced(g,h)||dota_roster_stun(g,h)||u->stun_ticks)return;
    if((u->hero_id==6&&skill==3)||(u->hero_id==7&&skill==3)||(u->hero_id==8&&skill==0)||(u->hero_id==9&&skill==0&&!dota_hero_has_scepter(g,h))){if(!valid_target(g,target,s->cast_generation))return;x=g->units[target].x;y=g->units[target].y;}
    if(!pay(g,h,skill,0))return;
    if(u->hero_id==6){
        if(skill==0){DotaRosterSpell*p=new_spell(g,h,RS_HOOK,r,-1,x,y);int dx=x-u->x,dy=y-u->y,len=root_i((unsigned)(dx*dx+dy*dy)),distance=dota_roster_range(g,h,0);if(p){if(!len){dx=0;dy=u->team?1:-1;len=1;}p->to_x=cap(u->x+(int)((long long)dx*distance/(4*len)),0,DOTA_WORLD-1);p->to_y=cap(u->y+(int)((long long)dy*distance/(4*len)),0,DOTA_WORLD-1);p->damage=value(u,0,"damage",pudge_hook_damage[r-1]);}}
        else if(skill==1)s->rot_on=!s->rot_on;
        else if(skill==2)s->flesh_until=g->tick+ticks(u,2,"duration",pudge_block_life[r-1]);
        else if(g->units[target].team==u->team && dota_hero_has_shard(g,h)){
            DotaRosterTarget*t=touch(g,target);s->channel_target=(uint16_t)target;s->channel_generation=g->units[target].generation;s->channel_rank=255;s->channel_until=0;s->next_pulse=g->tick+55;s->fraction=0;t->hold_owner=(uint8_t)h;t->hold_ticks=2;dota_rules_roster_interrupt(g,target);dota_rules_set_position(g,target,u->x,u->y);event(g,h,3,target,u->x,u->y,55,DOTA_SKILL_EVENT_STRIKE,0);
        }
        else if(!blocked(g,h,target)){DotaRosterTarget*t=touch(g,target);int raw_duration=ticks(u,3,"AbilityChannelTime",55),duration=resisted(g,h,target,raw_duration);s->channel_duration=(uint16_t)raw_duration;s->channel_pulse_total=(uint8_t)cap(value(u,3,"ticks",6),1,32);s->channel_target=(uint16_t)target;s->channel_generation=g->units[target].generation;s->channel_rank=(uint8_t)r;s->channel_until=g->tick+duration;s->next_pulse=g->tick;s->pull_x_q16=g->units[target].x*DOTA_MOVE_ONE;s->pull_y_q16=g->units[target].y*DOTA_MOVE_ONE;s->pulses=0;s->fraction=0;u->channel_ticks=duration;t->hold_owner=(uint8_t)h;t->hold_ticks=(uint16_t)duration;dota_rules_roster_interrupt(g,target);}
    }else if(u->hero_id==7){
        if(skill==0){DotaRosterField*f=0;for(i=0;i<DOTA_ROSTER_FIELDS;i++)if(!g->roster.field[i].until){f=&g->roster.field[i];break;}if(f){f->source=(uint16_t)h;f->source_generation=u->generation;f->x=x;f->y=y;f->starts_at=g->tick+ticks(u,0,"damage_delay",24);f->until=f->starts_at+ticks(u,0,"duration",200);f->radius=dota_roster_radius(g,h,0);f->rank=(uint8_t)r;g->roster.field_mask|=1u<<(f-g->roster.field);}if(s->shrapnel_charges)--s->shrapnel_charges;if(!s->shrapnel_restore_at)s->shrapnel_restore_at=g->tick+max_i(1,ticks(u,0,"AbilityChargeRestoreTime",700)*(100-dota_items_cooldown_percent(g,h))/100);}
        else if(skill==2){s->aim_until=g->tick+ticks(u,2,"duration",60);dota_rules_item_recalc(g,h);}
        else if(skill==3){DotaRosterSpell*p=new_spell(g,h,RS_ASSASSINATE,r,target,x,y);if(p)p->damage=value(u,3,"damage",sniper_ult_damage[r-1]);}
    }else if(u->hero_id==8){
        if(skill==0){DotaRosterSpell*p=new_spell(g,h,RS_STORM,r,target,x,y);if(p){p->damage=value(u,0,"AbilityDamage",sven_bolt_damage[r-1]);if(dota_hero_has_scepter(g,h)&&s->sven_fly){p->phase=2;p->damage+=value(u,0,"scepter_bonus_damage",180);s->fly_spell=(uint8_t)(p-g->roster.spell+1);}}}
        else if(skill==2){int radius=dota_roster_radius(g,h,2);for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&g->units[i].team==u->team&&!structure(&g->units[i])&&within(u->x,u->y,g->units[i].x,g->units[i].y,radius)){DotaRosterTarget*t=touch(g,i);t->warcry_ticks=(uint16_t)ticks(u,2,"duration",160);t->warcry_armor=(uint8_t)value(u,2,"bonus_armor",sven_warcry_armor[r-1]);t->warcry_move=(uint8_t)(value(u,2,"movespeed",sven_warcry_move[r-1])+(dota_hero_has_shard(g,h)?3:0));t->warcry_rank=(uint8_t)r;t->warcry_owner=(uint8_t)h;t->warcry_shard=(uint8_t)dota_hero_has_shard(g,h);t->warcry_barrier=t->warcry_shard?300:0;}}
        else if(skill==3){s->gods_until=g->tick+ticks(u,3,"AbilityDuration",600);dota_rules_item_recalc(g,h);}
    }else{
        if(skill==0){int wave=dota_hero_has_scepter(g,h);DotaRosterSpell*p=new_spell(g,h,wave?RS_GUSH_WAVE:RS_GUSH,r,wave?-1:target,x,y);if(p){p->damage=value(u,0,"gush_damage",tide_gush_damage[r-1]);if(wave){int dx=x-u->x,dy=y-u->y,len=root_i((unsigned)(dx*dx+dy*dy)),distance=dota_roster_range(g,h,0);if(!len){dy=u->team?1:-1;len=1;}p->to_x=cap(u->x+(int)((long long)dx*distance/(4*len)),0,DOTA_WORLD-1);p->to_y=cap(u->y+(int)((long long)dy*distance/(4*len)),0,DOTA_WORLD-1);}}}
        else if(skill==1)s->shell_until=g->tick+ticks(u,1,"active_duration",80);
        else if(skill==2)anchor_smash(g,h,r);
        else{s->ravage_at=g->tick;s->ravage_x=u->x;s->ravage_y=u->y;s->ravage_rank=(uint8_t)r;s->ravage_radius=dota_roster_radius(g,h,3);clear(s->ravage_hit,sizeof(s->ravage_hit));event(g,h,3,-1,u->x,u->y,35,DOTA_SKILL_EVENT_STRIKE,0);}
    }
}
int dota_roster_cast(DotaGame*g,int h,int skill,int x,int y){
    DotaUnit*u;DotaRosterHero*s;int target=-1,castpoint,cost;
    if(!g||!hero_index(g,h)||(unsigned)skill>=4||dota_roster_passive(g->units[h].hero_id,skill)||!live(g,h)||g->paused||g->state!=DOTA_PLAYING)return 0;
    u=&g->units[h];sync_hero(g,h);s=&g->roster.hero[h];
    if(dota_items_silenced(g,h)||u->stun_ticks||dota_roster_stun(g,h)||(s->cast_at>g->tick)||((s->channel_until>g->tick)&&!dota_roster_cast_while_channel(g,h,skill)))return 0;
    if(u->hero_id==6&&skill==3&&dota_roster_swallowed(g,h)>=0)return 0;
    if(!rank(u,skill)){message(g,h,3,skill);return 0;}
    if(u->cooldown[skill]||(u->hero_id==7&&skill==0&&!s->shrapnel_charges)){message(g,h,2,skill);return 0;}
    cost=dota_roster_mana(u,skill)*dota_items_mana_cost_percent(g,h)/100;if(dota_items_available_mana(g,h)<cost){message(g,h,1,skill);return 0;}
    x=cap(x,0,DOTA_WORLD-1);y=cap(y,0,DOTA_WORLD-1);
    if((u->hero_id==6&&skill==3)||(u->hero_id==7&&skill==3)||(u->hero_id==8&&skill==0)||(u->hero_id==9&&skill==0&&!dota_hero_has_scepter(g,h))){target=pick_target(g,h,x,y,dota_roster_range(g,h,skill),u->hero_id==8&&dota_hero_has_scepter(g,h));if(target<0&&u->hero_id==6&&skill==3)target=allied_dismember(g,h,x,y);if(target<0){message(g,h,4,skill);return 0;}}
    else if(u->hero_id==7&&skill==0&&!within(u->x,u->y,x,y,dota_roster_range(g,h,0))){message(g,h,15,skill);return 0;}
    castpoint=ticks(u,skill,"AbilityCastPoint",roster_castpoint[u->hero_id-6][skill][rank(u,skill)-1]);if(u->hero_id==7&&skill==3&&dota_hero_has_scepter(g,h))castpoint=10;
    if(x!=u->x||y!=u->y){s->vision_dx=(int16_t)(x-u->x);s->vision_dy=(int16_t)(y-u->y);}s->seen_x=(int16_t)u->x;s->seen_y=(int16_t)u->y;
    s->cast_skill=(uint8_t)skill;s->cast_rank=(uint8_t)rank(u,skill);s->cast_target=target<0?DOTA_ROSTER_NO_TARGET:(uint16_t)target;s->cast_generation=target<0?0:g->units[target].generation;s->cast_x=(int16_t)x;s->cast_y=(int16_t)y;s->cast_at=g->tick+max_i(1,castpoint);
    event(g,h,skill,target,x,y,max_i(castpoint,12),DOTA_SKILL_EVENT_CAST,0);u->skill_ticks[skill]=max_i(castpoint,12);
    if(!castpoint)release_cast(g,h);
    return 1;
}
int dota_roster_cast_extra(DotaGame*g,int h,int x,int y){
    DotaRosterHero*s;DotaRosterSpell*p;int target=-1;if(!hero_index(g,h)||!live(g,h)||g->paused||g->state!=DOTA_PLAYING)return 0;sync_hero(g,h);s=&g->roster.hero[h];
    if(g->units[h].hero_id==6&&dota_roster_swallowed(g,h)>=0){if(dota_items_silenced(g,h)||g->units[h].stun_ticks)return 0;return eject_ally(g,h);}
    if(g->units[h].hero_id==8&&dota_hero_has_scepter(g,h)){s->sven_fly=!s->sven_fly;return 1;}
    if(!dota_hero_has_shard(g,h)||(g->units[h].hero_id!=7&&g->units[h].hero_id!=9)||dota_items_silenced(g,h)||g->units[h].stun_ticks||dota_roster_stun(g,h)||s->extra_cooldown>g->tick)return 0;
    if(g->units[h].hero_id==9){target=pick_target(g,h,x,y,dota_roster_extra_range(g,h),0);if(target<0||g->units[target].type!=DOTA_HERO)return 0;}
    else if(!within(g->units[h].x,g->units[h].y,x,y,dota_roster_extra_range(g,h)))return 0;
    if(!pay(g,h,0,1))return 0;
    p=new_spell(g,h,g->units[h].hero_id==7?RS_GRENADE:RS_ANCHOR,1,target,x,y);if(!p)return 0;p->delay=g->units[h].hero_id==9?6:2;
    event(g,h,g->units[h].hero_id==7?2:3,target,x,y,8,DOTA_SKILL_EVENT_CAST,1);return 1;
}

int dota_roster_attack_damage(DotaGame*g,int h,int target,int amount){
    DotaRosterHero*s;DotaUnit*u;int bonus=0;
    if(amount<=0)return amount;
    if(!hero_index(g,h)||!live(g,target))return amount;
    u=&g->units[h];s=&g->roster.hero[h];s->vision_dx=(int16_t)(g->units[target].x-u->x);s->vision_dy=(int16_t)(g->units[target].y-u->y);
    if(u->hero_id!=7||dota_items_broken(g,h)||structure(&g->units[target])||dota_roster_controls_aux(g,target))return amount;
    bonus=(int)((long long)amount*value100(u,5,"bonus_damage_for_range",150+5*(u->level-1))*root_i((unsigned)sq(u->x,u->y,g->units[target].x,g->units[target].y))*4/1000000);
    if(rank(u,1)>0 && (s->aim_until>g->tick || (g->seed=g->seed*1664525u+1013904223u,g->seed%100u<(unsigned)value(u,1,"proc_chance",40)))){
        int r=rank(u,1);DotaRosterTarget*v=touch(g,target);bonus+=value(u,1,"damage",sniper_headshot_damage[r-1]);s->headshot_tick=g->tick;s->headshot_target=(uint16_t)target;s->headshot_generation=g->units[target].generation;v->headshot_ticks=(uint16_t)resisted(g,h,target,ticks(u,1,"slow_duration",sniper_headshot_life[r-1]));
        if(!dota_items_debuff_immune(g,target))knock(g,target,u->x,u->y,value(u,1,"knockback_distance",50),2);
        event(g,h,1,target,g->units[target].x,g->units[target].y,6,DOTA_SKILL_EVENT_STRIKE,0);
    }return amount+bonus;
}
void dota_roster_attack_hit(DotaGame*g,int h,int target,int raw,int dealt){
    DotaUnit*u;int i,dx,dy,len,distance,width,r;if(!hero_index(g,h)||g->roster.recursion||dealt<=0)return;u=&g->units[h];if(u->hero_id!=8||!rank(u,1)||dota_items_broken(g,h)||structure(&g->units[target])||dota_roster_controls_aux(g,target))return;
    dx=g->units[target].x-u->x;dy=g->units[target].y-u->y;len=root_i((unsigned)(dx*dx+dy*dy));if(!len)return;r=rank(u,1)-1;distance=dota_rules_aoe_radius(g,h,value(u,1,"cleave_distance",sven_cleave_distance[r]));width=dota_rules_aoe_radius(g,h,value(u,1,"cleave_ending_width",sven_cleave_width[r]));g->roster.recursion++;
    for(i=0;i<DOTA_MAX_UNITS;i++)if(i!=target&&enemy_basic(g,h,i)&&!dota_unit_invulnerable(g,i)){
        int xx=g->units[i].x-u->x,yy=g->units[i].y-u->y;long long along=(long long)xx*dx+(long long)yy*dy,cross=(long long)xx*dy-(long long)yy*dx,allowed;if(along<=0||along*4>(long long)distance*len)continue;
        if(cross<0)cross=-cross;
        allowed=(long long)150*len+((long long)(width-150)*along*4/distance);if(cross*4>allowed)continue;
        damage(g,h,i,raw*value(u,1,"great_cleave_damage",sven_cleave_pct[r])/100,0);
    }g->roster.recursion--;event(g,h,1,target,g->units[target].x,g->units[target].y,6,DOTA_SKILL_EVENT_STRIKE,0);
}
int dota_roster_physical_block(const DotaGame*g,int source,int target){
    const DotaRosterHero*s=hero_state(g,target);const DotaUnit*u;int block,r;if(!s||g->units[target].hero_id!=9)return 0;u=&g->units[target];r=rank(u,1);
    block=s->fish_count*value(u,5,"fish_damage_block",1);if(r>0&&!dota_items_broken(g,target))block+=value(u,1,"damage_reduction",tide_block[r-1]);if(r>0&&s->shell_until>g->tick)block=block*value(u,1,"active_pct_effectiveness",200)/100;
    if((unsigned)source<DOTA_MAX_UNITS && g->units[source].type!=DOTA_HERO && !structure(&g->units[source]))block/=2;
    return block;
}
int dota_roster_before_damage(DotaGame*g,int source,int target,int amount,int mode){
    const DotaRosterHero*s=(unsigned)target<10u?hero_state(g,target):0;DotaRosterTarget*t;(void)source;if(!g||(unsigned)target>=DOTA_MAX_UNITS||amount<=0||mode==4)return amount;
    if(s&&g->units[target].hero_id==6&&s->flesh_until>g->tick&&rank(&g->units[target],2)>0)amount=max_i(0,amount-value(&g->units[target],2,"damage_block",pudge_block[rank(&g->units[target],2)-1]));
    t=&g->roster.target[target];if(mode==0&&t->generation==g->units[target].generation&&t->warcry_ticks&&t->warcry_barrier){int blocked_i=min_i(amount,t->warcry_barrier);amount-=blocked_i;t->warcry_barrier=(uint16_t)(t->warcry_barrier-blocked_i);}
    if(dota_roster_controls_aux(g,target)){int kind=g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST].kind;return mode==0?(kind==DOTA_ROSTER_ANCHOR_KIND?((unsigned)source<DOTA_MAX_UNITS&&g->units[source].type==DOTA_HERO?1:0):amount):0;}return amount;
}
void dota_roster_after_damage(DotaGame*g,int source,int target,int dealt,int mode){
    DotaRosterHero*s;int threshold;(void)source;(void)mode;if(dealt<=0||!hero_index(g,target)||g->units[target].hero_id!=9||!rank(&g->units[target],1)||dota_items_broken(g,target))return;s=&g->roster.hero[target];if(g->tick>s->block_reset_at)s->block_damage=0;s->block_reset_at=g->tick+ticks(&g->units[target],1,"damage_reset_interval",140);s->block_damage+=dealt;threshold=value(&g->units[target],1,"damage_cleanse",tide_cleanse[rank(&g->units[target],1)-1]);
    if(s->block_damage>=threshold){s->block_damage=0;g->units[target].stun_ticks=g->units[target].slow_ticks=g->units[target].taunt_ticks=0;g->units[target].dot_ticks=0;dota_items_purge(g,target,1);dota_extra_dispel(g,target,0);dota_roster_dispel(g,target,0,1);if(!g->items.reflection_guard&&rank(&g->units[target],2)>0&&value(&g->units[target],1,"smash_on_purge",0)>0){g->items.reflection_guard++;anchor_smash_scaled(g,target,value(&g->units[target],1,"smash_on_purge",0));g->items.reflection_guard--;}event(g,target,1,target,g->units[target].x,g->units[target].y,8,DOTA_SKILL_EVENT_STRIKE,0);}
}
void dota_roster_on_kill(DotaGame*g,int victim,int source){
    int h,owner;if((unsigned)victim>=DOTA_MAX_UNITS||g->units[victim].type!=DOTA_HERO)return;if(g->units[victim].hero_id==6)eject_ally(g,victim);owner=dota_item_world_owner(g,source);if(owner>=0)source=owner;
    if(hero_index(g,source)&&g->units[source].hero_id==7&&rank(&g->units[source],3)>0&&g->units[source].team!=g->units[victim].team)g->units[source].cooldown[3]=max_i(0,g->units[source].cooldown[3]-ticks(&g->units[source],3,"cooldown_reduction_on_kill",(rank(&g->units[source],3)-1)*100));
    for(h=0;h<10;h++)if(hero_index(g,h)&&g->units[h].team!=g->units[victim].team){DotaRosterHero*s=&g->roster.hero[h];
        if(g->units[h].hero_id==6&&(source==h||(live(g,h)&&within(g->units[h].x,g->units[h].y,g->units[victim].x,g->units[victim].y,dota_rules_aoe_radius(g,h,value(&g->units[h],5,"flesh_heap_range",450)))))){if(s->flesh_stacks<32767)s->flesh_stacks++;dota_rules_item_recalc(g,h);}
        if(g->units[h].hero_id==9){const DotaRosterTarget*t=&g->roster.target[victim];if(source==h||(t->generation==g->units[victim].generation&&t->tide_mark_ticks&&t->tide_owner==h)){int fish=dota_rules_roster_spawn_aux(g,h,DOTA_ROSTER_FISH_KIND,g->units[victim].x,g->units[victim].y,1);if(fish>=0)g->item_world.unit[fish-DOTA_ITEM_UNIT_FIRST].until=g->tick+ticks(&g->units[h],5,"fish_duration",600);}}
    }
}
void dota_roster_dispel(DotaGame*g,int target,int positive,int strong){
    DotaRosterTarget*t;DotaRosterHero*s;if(!g||(unsigned)target>=DOTA_MAX_UNITS)return;t=touch(g,target);s=(unsigned)target<10u?&g->roster.hero[target]:0;
    if(positive){if(t->warcry_ticks&&!t->warcry_shard)t->warcry_ticks=t->warcry_barrier=0;if(s&&g->units[target].hero_id==7&&s->aim_until){s->aim_until=0;dota_rules_item_recalc(g,target);}}
    else{t->gush_ticks=t->smash_ticks=t->headshot_ticks=t->grenade_ticks=0;if(strong){t->stun_ticks=t->hold_ticks=0;g->units[target].stun_ticks=g->units[target].taunt_ticks=0;}}
}
static void anchor_smash_scaled(DotaGame*g,int h,int power){
    DotaUnit*u=&g->units[h];int i,r=rank(u,2),radius=dota_roster_radius(g,h,2),buildings=value(u,2,"targets_buildings",0);if(!r||power<=0)return;g->roster.recursion++;
    for(i=0;i<DOTA_MAX_UNITS;i++)if((enemy_basic(g,h,i)||(buildings&&live(g,i)&&g->units[i].team!=u->team&&structure(&g->units[i])))&&g->units[i].type!=DOTA_ROSHAN&&!dota_unit_invulnerable(g,i)&&within(u->x,u->y,g->units[i].x,g->units[i].y,radius)){
        int generation=g->units[i].generation,pct=power;if(structure(&g->units[i]))pct=pct*value(u,2,"building_pct_damage",50)/100;
        dota_rules_roster_attack_scaled(g,h,i,value(u,2,"attack_damage",tide_smash_damage[r-1]),pct);if(valid_target(g,i,generation)&&!structure(&g->units[i])){DotaRosterTarget*t=touch(g,i);t->smash_ticks=(uint16_t)resisted(g,h,i,ticks(u,2,"reduction_duration",120));t->smash_rank=(uint8_t)r;t->smash_reduction=(uint8_t)-value(u,2,"damage_reduction",-tide_smash_reduction[r-1]);mark_tide(g,h,i,t->smash_ticks);}
    }g->roster.recursion--;event(g,h,2,-1,u->x,u->y,12,DOTA_SKILL_EVENT_STRIKE,0);
}
static void anchor_smash(DotaGame*g,int h,int r){(void)r;anchor_smash_scaled(g,h,100);}
static void gush_hit(DotaGame*g,DotaRosterSpell*p,int target){
    int generation=g->units[target].generation;damage(g,p->source,target,p->damage,1);if(valid_target(g,target,generation)){DotaRosterTarget*t=touch(g,target);t->gush_ticks=(uint16_t)resisted(g,p->source,target,ticks(&g->units[p->source],0,"AbilityDuration",90));t->gush_rank=p->rank;t->gush_armor=(uint8_t)value(&g->units[p->source],0,"negative_armor",tide_gush_armor[p->rank-1]);t->gush_slow=(uint8_t)-value(&g->units[p->source],0,"movement_speed",-40);mark_tide(g,p->source,target,t->gush_ticks);}event(g,p->source,0,target,g->units[target].x,g->units[target].y,8,DOTA_SKILL_EVENT_STRIKE,0);
}
static void impact(DotaGame*g,DotaRosterSpell*p){
    int h=p->source,target=p->target,i,x=p->x_q16/DOTA_MOVE_ONE,y=p->y_q16/DOTA_MOVE_ONE;
    if(p->kind==RS_ASSASSINATE){if(valid_target(g,target,p->target_generation)&&!blocked(g,h,target)){int gen=g->units[target].generation;damage(g,h,target,p->damage,1);if(valid_target(g,target,gen))stun(g,h,target,(p->upgrades&1)?16+(p->rank-1)*4:1,0);event(g,h,3,target,x,y,8,DOTA_SKILL_EVENT_STRIKE,0);}}
    else if(p->kind==RS_STORM){int block=valid_target(g,target,p->target_generation)?blocked(g,h,target):1;if(!block&&(p->upgrades&1))dota_items_dispel(g,target,1);for(i=0;i<DOTA_MAX_UNITS;i++)if(enemy_basic(g,h,i)&&!(i==target&&block)&&within(x,y,g->units[i].x,g->units[i].y,p->radius)){int generation=g->units[i].generation;damage(g,h,i,p->damage,1);if(valid_target(g,i,generation))stun(g,h,i,ticks(&g->units[h],0,"bolt_stun_duration",sven_bolt_stun[p->rank-1]),0);}if(p->phase==2){dota_rules_set_position(g,h,x,y);dota_rules_item_finish_free_path(g,h);g->roster.hero[h].fly_spell=0;}event(g,h,0,target,x,y,12,DOTA_SKILL_EVENT_STRIKE,0);}
    else if(p->kind==RS_GUSH){if(valid_target(g,target,p->target_generation)&&!blocked(g,h,target))gush_hit(g,p,target);}
    else if(p->kind==RS_GRENADE){int radius=p->radius,distance=p->knockback;for(i=0;i<DOTA_MAX_UNITS;i++)if(enemy_basic(g,h,i)&&within(x,y,g->units[i].x,g->units[i].y,radius)){int gen=g->units[i].generation;damage(g,h,i,200,1);if(valid_target(g,i,gen)){DotaRosterTarget*t=touch(g,i);t->grenade_ticks=(uint16_t)resisted(g,h,i,60);if(!dota_items_debuff_immune(g,i))knock(g,i,x,y,distance,8);}}knock(g,h,x,y,distance,8);event(g,h,2,-1,x,y,12,DOTA_SKILL_EVENT_STRIKE,1);}
    else if(p->kind==RS_ANCHOR){if(valid_target(g,target,p->target_generation)&&!blocked(g,h,target)){int a=dota_rules_roster_spawn_aux(g,h,DOTA_ROSTER_ANCHOR_KIND,g->units[target].x,g->units[target].y,4);if(a>=0){DotaRosterTarget*t=touch(g,target);g->item_world.unit[a-DOTA_ITEM_UNIT_FIRST].until=g->tick+200;g->item_world.unit[a-DOTA_ITEM_UNIT_FIRST].cast_target=target;g->item_world.unit[a-DOTA_ITEM_UNIT_FIRST].cast_generation=g->units[target].generation;g->roster.hero[h].anchor_unit=(uint16_t)a;t->tether_anchor=(uint16_t)a;t->tether_ticks=200;t->tether_owner=(uint8_t)h;mark_tide(g,h,target,200);event(g,h,3,target,x,y,12,DOTA_SKILL_EVENT_STRIKE,1);}}}
    p->kind=0;
}
static int segment_hit(int x,int y,int xx,int yy,int tx,int ty,int width){
    int dx=xx-x,dy=yy-y;long long len=(long long)dx*dx+(long long)dy*dy,along=(long long)(tx-x)*dx+(long long)(ty-y)*dy;
    if(!len||along<=0)return within(x,y,tx,ty,width);
    if(along>=len)return within(xx,yy,tx,ty,width);
    return within(x+(int)(dx*along/len),y+(int)(dy*along/len),tx,ty,width);
}
static void spell_tick(DotaGame*g,int index){
    DotaRosterSpell*p=&g->roster.spell[index];int h=p->source,target=p->target,x,y,xx,yy,dx,dy,len,speed,step,i;
    if(!p->kind)return;
    if((unsigned)h>=DOTA_MAX_UNITS||(h>=10&&g->units[h].generation!=p->source_generation)||--p->life<=0){p->kind=0;return;}
    if(p->delay>0){p->delay--;return;}
    x=p->x_q16/DOTA_MOVE_ONE;y=p->y_q16/DOTA_MOVE_ONE;
    if(p->kind==RS_HOOK&&p->phase==1){p->to_x=p->origin_x;p->to_y=p->origin_y;}
    else if(p->target!=DOTA_ROSTER_NO_TARGET){if(!valid_target(g,target,p->target_generation)){p->kind=0;return;}p->to_x=g->units[target].x;p->to_y=g->units[target].y;}
    dx=p->to_x-x;dy=p->to_y-y;len=root_i((unsigned)(dx*dx+dy*dy));speed=p->speed;step=speed*DOTA_MOVE_ONE/80;
    if(!len||(long long)len*DOTA_MOVE_ONE<=step){p->x_q16=p->to_x*DOTA_MOVE_ONE;p->y_q16=p->to_y*DOTA_MOVE_ONE;}
    else{p->x_q16+=(int)((long long)dx*step/len);p->y_q16+=(int)((long long)dy*step/len);}
    xx=p->x_q16/DOTA_MOVE_ONE;yy=p->y_q16/DOTA_MOVE_ONE;
    if(p->kind==RS_HOOK){
        if(p->phase==0){int best=-1,best_d=0x7fffffff;for(i=0;i<DOTA_MAX_UNITS;i++)if(i!=h&&live(g,i)&&!structure(&g->units[i])&&g->units[i].type!=DOTA_ANCIENT&&g->units[i].type!=DOTA_ROSHAN&&g->units[i].type!=DOTA_TORMENTOR&&!dota_roster_controls_aux(g,i)&&segment_hit(x,y,xx,yy,g->units[i].x,g->units[i].y,100)){int d=sq(x,y,g->units[i].x,g->units[i].y);if(d<best_d){best=i;best_d=d;}}
            if(best>=0){DotaRosterTarget*t=touch(g,best);p->target=(uint16_t)best;p->target_generation=g->units[best].generation;p->phase=1;p->travelled=root_i((unsigned)sq(p->origin_x,p->origin_y,g->units[best].x,g->units[best].y))*4;t->hook_spell=(uint8_t)(index+1);if(g->units[best].team!=g->units[h].team){if(g->units[best].type==DOTA_CREEP||g->units[best].type==DOTA_NEUTRAL||g->units[best].type==DOTA_SUMMON)dota_rules_item_kill(g,best,h);else damage(g,h,best,p->damage,2);dota_rules_roster_interrupt(g,best);t->hold_owner=(uint8_t)h;t->hold_ticks=3;}else if(p->upgrades&2)g->units[h].cooldown[0]/=2;event(g,h,0,best,xx,yy,8,DOTA_SKILL_EVENT_STRIKE,0);}
            else if(xx==p->to_x&&yy==p->to_y)p->phase=1;
        }else{if(valid_target(g,p->target,p->target_generation)){DotaRosterTarget*t=touch(g,p->target);dota_rules_set_position(g,p->target,xx,yy);if(g->units[p->target].team!=g->units[h].team){t->hold_owner=(uint8_t)h;t->hold_ticks=3;}}
            if(xx==p->to_x&&yy==p->to_y){if(valid_target(g,p->target,p->target_generation)){DotaRosterTarget*t=touch(g,p->target);t->hold_ticks=0;t->hook_spell=0;dota_rules_item_finish_free_path(g,p->target);if((p->upgrades&2)&&g->units[p->target].team!=g->units[h].team)damage(g,h,p->target,max_i(10,p->travelled*20/100),2);}p->kind=0;}}
    }else if(p->kind==RS_GUSH_WAVE){for(i=0;i<DOTA_MAX_UNITS;i++)if(enemy_basic(g,h,i)&&!(p->hit[i/32]&(1u<<(i%32)))&&segment_hit(x,y,xx,yy,g->units[i].x,g->units[i].y,p->radius)){p->hit[i/32]|=1u<<(i%32);gush_hit(g,p,i);}if(xx==p->to_x&&yy==p->to_y)p->kind=0;}
    else{if(p->kind==RS_STORM&&p->phase==2)dota_rules_set_position(g,h,xx,yy);if(xx==p->to_x&&yy==p->to_y)impact(g,p);}
}

static void target_tick(DotaGame*g){
    int word;for(word=0;word<DOTA_ROSTER_MASK_WORDS;word++){uint32_t bits=g->roster.target_active[word];int index=word*32;
        while(bits){if(bits&1u){DotaRosterTarget*t=&g->roster.target[index];
            if(!live(g,index)||t->generation!=g->units[index].generation){clear(t,sizeof(*t));g->roster.target_active[word]&=~(1u<<(index%32));}
            else{
                if(t->knock_ticks){int left=t->knock_ticks;dota_rules_set_position(g,index,g->units[index].x+(t->knock_x-g->units[index].x)/left,g->units[index].y+(t->knock_y-g->units[index].y)/left);if(!--t->knock_ticks)dota_rules_item_finish_free_path(g,index);}
                if(t->hook_spell){const DotaRosterSpell*p=&g->roster.spell[t->hook_spell-1];if(p->kind!=RS_HOOK||p->phase!=1||p->target!=index||p->target_generation!=t->generation)t->hook_spell=0;}
                if(t->gush_ticks)--t->gush_ticks;
                if(t->smash_ticks)--t->smash_ticks;
                if(t->warcry_ticks)--t->warcry_ticks;
                if(t->rot_ticks)--t->rot_ticks;
                if(t->headshot_ticks)--t->headshot_ticks;
                if(t->grenade_ticks)--t->grenade_ticks;
                if(t->shrapnel_ticks)--t->shrapnel_ticks;
                if(t->stun_ticks)--t->stun_ticks;
                if(t->hold_ticks)--t->hold_ticks;
                if(t->tide_mark_ticks)--t->tide_mark_ticks;
                if(t->tether_ticks){--t->tether_ticks;if(!live(g,t->tether_anchor))t->tether_ticks=0;}
                if(!t->gush_ticks&&!t->smash_ticks&&!t->warcry_ticks&&!t->rot_ticks&&!t->headshot_ticks&&!t->grenade_ticks&&!t->shrapnel_ticks&&!t->stun_ticks&&!t->hold_ticks&&!t->tide_mark_ticks&&!t->tether_ticks&&!t->knock_ticks&&!t->hook_spell)g->roster.target_active[word]&=~(1u<<(index%32));
            }}bits>>=1;index++;
        }
    }
}
static void fields_tick(DotaGame*g){
    int i,h,r,target,scan=(g->tick%4)==0;uint32_t done[10][DOTA_ROSTER_MASK_WORDS];
    if(!g->roster.field_mask)return;
    for(i=0;i<DOTA_ROSTER_FIELDS;i++){DotaRosterField*f=&g->roster.field[i];if(!f->until)continue;if(g->tick>f->until){f->until=0;g->roster.field_mask&=~(1u<<i);continue;}if(g->tick==f->starts_at)event(g,f->source,0,-1,f->x,f->y,12,DOTA_SKILL_EVENT_STRIKE,0);if(g->tick>=f->starts_at&&(g->tick-f->starts_at)%20==0)scan=1;}
    if(!scan)return;
    clear(done,sizeof(done));
    /* The one selected Sniper maintains one damage clock per affected unit.
     * Refreshing a same-caster overlapping field never adds a second clock.
     * Source deals damage on entry and then once per second:11 pulses over10s. */
    for(r=4;r>=1;r--)for(i=0;i<DOTA_ROSTER_FIELDS;i++){DotaRosterField*f=&g->roster.field[i];if(!f->until||f->rank!=r||g->tick<f->starts_at)continue;h=f->source;if((unsigned)h>=10||g->units[h].hero_id!=7)continue;
        for(target=0;target<DOTA_MAX_UNITS;target++)if(enemy_basic(g,h,target)&&!(done[h][target/32]&(1u<<(target%32)))&&within(f->x,f->y,g->units[target].x,g->units[target].y,f->radius)){
            DotaRosterTarget*t=touch(g,target);int gen=g->units[target].generation;done[h][target/32]|=1u<<(target%32);
            if(!t->shrapnel_ticks||t->shrapnel_owner!=h||g->tick>=t->shrapnel_damage_at){t->shrapnel_damage_at=g->tick+20;t->shrapnel_owner=(uint8_t)h;damage(g,h,target,value(&g->units[h],0,"shrapnel_damage",sniper_shrapnel_damage[r-1]),1);}
            if(valid_target(g,target,gen)){t->shrapnel_ticks=(uint16_t)resisted(g,h,target,40);t->shrapnel_slow=(uint8_t)-value(&g->units[h],0,"slow_movement_speed",-sniper_shrapnel_slow[r-1]);}
        }
    }
}
static void rot_tick(DotaGame*g,int h){
    DotaUnit*u=&g->units[h];int i,r=max_i(1,rank(u,1))-1,amount=(value(u,1,"rot_damage",pudge_rot_damage[r])+(dota_hero_has_scepter(g,h)?90:0))/5,radius=dota_roster_radius(g,h,1);
    /* The Source tooltip explicitly marks self-damage non-lethal. Passing
     * through defenses keeps Meat Shield and current magic resistance real. */
    damage(g,h,h,min_i(amount,max_i(0,u->hp-1)),1);
    for(i=0;i<DOTA_MAX_UNITS;i++)if(enemy_basic(g,h,i)&&within(u->x,u->y,g->units[i].x,g->units[i].y,radius)){
        int gen=g->units[i].generation;damage(g,h,i,amount,1);if(valid_target(g,i,gen)){DotaRosterTarget*t=touch(g,i);t->rot_ticks=(uint16_t)resisted(g,h,i,6);t->rot_slow=(uint8_t)(-value(u,1,"rot_slow",-pudge_rot_slow[r])|(dota_hero_has_scepter(g,h)?128:0));}
    }
}
static void swallow_tick(DotaGame*g,int h){
    DotaRosterHero*s=&g->roster.hero[h];int t=dota_roster_swallowed(g,h),sum,amount;
    if(s->channel_rank!=255)return;
    if(!live(g,h)||t<0){eject_ally(g,h);return;}
    dota_rules_set_position(g,t,g->units[h].x,g->units[h].y);touch(g,t)->hold_owner=(uint8_t)h;touch(g,t)->hold_ticks=2;
    sum=s->fraction+g->units[t].max_hp*5;amount=sum/(100*DOTA_HZ);s->fraction=(uint16_t)(sum%(100*DOTA_HZ));if(amount)dota_items_restore(g,h,t,amount,0);
}
static void channel_tick(DotaGame*g,int h){
    DotaRosterHero*s=&g->roster.hero[h];DotaUnit*u=&g->units[h];const DotaRosterTarget*t=target_state(g,s->channel_target);int target=s->channel_target,dx,dy,len;
    if(!s->channel_until)return;
    if(!live(g,h)||g->tick>=s->channel_until||u->stun_ticks||dota_roster_stun(g,h)||!valid_target(g,target,s->channel_generation)||!t||!t->hold_ticks||t->hold_owner!=h){dota_roster_cancel_cast(g,h);return;}
    u->channel_ticks=s->channel_until-g->tick;dx=u->x-g->units[target].x;dy=u->y-g->units[target].y;len=root_i((unsigned)(dx*dx+dy*dy));
    if(len*4>125){int step=min_i(75*DOTA_MOVE_ONE/80,(len*4-125)*DOTA_MOVE_ONE/4);s->pull_x_q16+=(int)((long long)dx*step/len);s->pull_y_q16+=(int)((long long)dy*step/len);dota_rules_set_position(g,target,s->pull_x_q16/DOTA_MOVE_ONE,s->pull_y_q16/DOTA_MOVE_ONE);}
    if(g->tick>=s->next_pulse&&s->pulses<s->channel_pulse_total){int r=s->channel_rank-1,dps100=value100(u,3,"dismember_damage",pudge_dismember_damage[r]*100)+total_attr(g,h,0)*value100(u,3,"strength_damage",pudge_dismember_str_pct[r]),denominator=100*DOTA_HZ*s->channel_pulse_total,numerator=dps100*s->channel_duration+s->fraction,amount=numerator/denominator,dealt;s->fraction=(uint16_t)(numerator%denominator);s->pulses++;s->next_pulse=g->tick+max_i(1,resisted(g,h,target,s->channel_duration)/s->channel_pulse_total);dealt=damage(g,h,target,amount,7);dota_items_restore(g,h,h,dealt,0);event(g,h,3,target,g->units[target].x,g->units[target].y,8,DOTA_SKILL_EVENT_STRIKE,0);}
}
static void ravage_tick(DotaGame*g,int h){
    DotaRosterHero*s=&g->roster.hero[h];int radius,age,i,r;if(!s->ravage_at)return;age=g->tick-s->ravage_at;radius=min_i(s->ravage_radius,age*value(&g->units[h],3,"speed",725)/20);r=s->ravage_rank-1;
    for(i=0;i<DOTA_MAX_UNITS;i++)if(enemy_basic(g,h,i)&&!(s->ravage_hit[i/32]&(1u<<(i%32)))&&!dota_unit_invulnerable(g,i)&&within(s->ravage_x,s->ravage_y,g->units[i].x,g->units[i].y,radius)){
        int gen=g->units[i].generation;s->ravage_hit[i/32]|=1u<<(i%32);damage(g,h,i,value(&g->units[h],3,"AbilityDamage",tide_ravage_damage[r]),1);if(valid_target(g,i,gen)){stun(g,h,i,ticks(&g->units[h],3,"duration",tide_ravage_stun[r]),0);mark_tide(g,h,i,resisted(g,h,i,ticks(&g->units[h],3,"duration",tide_ravage_stun[r])));}
    }if(radius>=s->ravage_radius)s->ravage_at=0;
}
void dota_roster_tick(DotaGame*g){
    int i;if(!g||g->roster.last_tick==g->tick)return;g->roster.last_tick=g->tick;
    /* Sync every hero before any cross-actor write, including cold first-tick
     * projectile contacts and simultaneous respawns at higher actor indices. */
    for(i=0;i<10;i++)sync_hero(g,i);
    target_tick(g);
    for(i=0;i<10;i++)if(hero_index(g,i)){DotaRosterHero*s=&g->roster.hero[i];DotaUnit*u=&g->units[i];
        if(u->x!=s->seen_x||u->y!=s->seen_y){s->vision_dx=(int16_t)(u->x-s->seen_x);s->vision_dy=(int16_t)(u->y-s->seen_y);s->seen_x=(int16_t)u->x;s->seen_y=(int16_t)u->y;}
        if((s->aim_until>0&&g->tick>=s->aim_until)||(s->gods_until>0&&g->tick>=s->gods_until)){if(g->tick>=s->aim_until)s->aim_until=0;if(g->tick>=s->gods_until)s->gods_until=0;dota_rules_item_recalc(g,i);}
        if(u->hero_id==7){while(s->shrapnel_restore_at>0&&g->tick>=s->shrapnel_restore_at){if(s->shrapnel_charges<3)s->shrapnel_charges++;s->shrapnel_restore_at=s->shrapnel_charges<3?s->shrapnel_restore_at+max_i(1,ticks(u,0,"AbilityChargeRestoreTime",700)*(100-dota_items_cooldown_percent(g,i))/100):0;}if(!s->shrapnel_charges)u->cooldown[0]=max_i(1,s->shrapnel_restore_at-g->tick);else u->cooldown[0]=0;}
        if(u->hero_id==9&&u->level>s->level_seen){int fish=u->level/2-s->level_seen/2;s->level_seen=(uint8_t)u->level;if(fish){s->fish_count=(uint16_t)min_i(32767,s->fish_count+fish);dota_rules_item_recalc(g,i);}}
        if(u->hero_id==9)ravage_tick(g,i);
        if(!live(g,i)){dota_roster_cancel_cast(g,i);s->rot_on=0;continue;}
        if(s->cast_at>0&&g->tick>=s->cast_at)release_cast(g,i);
        if(u->hero_id==6){swallow_tick(g,i);if(s->rot_on&&g->tick%4==0)rot_tick(g,i);channel_tick(g,i);}
    }
    for(i=0;i<DOTA_ROSTER_SPELLS;i++)spell_tick(g,i);
    fields_tick(g);
}
int dota_roster_controls_aux(const DotaGame*g,int index){
    int kind;if(!g||(unsigned)(index-DOTA_ITEM_UNIT_FIRST)>=DOTA_ITEM_UNIT_COUNT)return 0;kind=g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST].kind;return kind==DOTA_ROSTER_FISH_KIND||kind==DOTA_ROSTER_ANCHOR_KIND;
}
void dota_roster_aux_tick(DotaGame*g,int index){
    DotaItemWorldUnit*s;int owner;if(!dota_roster_controls_aux(g,index))return;s=&g->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];owner=s->owner;
    if(!live(g,index)||(s->until&&g->tick>=s->until)){g->units[index].hp=0;g->units[index].active=0;s->kind=0;return;}
    if(s->kind==DOTA_ROSTER_ANCHOR_KIND){int target=s->cast_target;if(valid_target(g,target,s->cast_generation)){int dx=g->units[target].x-g->units[index].x,dy=g->units[target].y-g->units[index].y,len=root_i((unsigned)(dx*dx+dy*dy));if(len*4>350)dota_rules_set_position(g,index,g->units[target].x-dx*350/(4*len),g->units[target].y-dy*350/(4*len));}}
    if(s->kind==DOTA_ROSTER_FISH_KIND&&live(g,owner)&&g->units[owner].hero_id==9&&within(g->units[index].x,g->units[index].y,g->units[owner].x,g->units[owner].y,100)){
        DotaRosterHero*h=&g->roster.hero[owner];h->fish_count=(uint16_t)min_i(32767,h->fish_count+1);g->units[index].hp=0;g->units[index].active=0;s->kind=0;dota_rules_item_recalc(g,owner);event(g,owner,1,index,g->units[owner].x,g->units[owner].y,8,DOTA_SKILL_EVENT_PASSIVE,0);
    }
}
static int add_status(DotaRosterStatus*out,int n,int cap_i,int hero,int skill,int harmful,int ticks,int stacks){if((ticks>0||(ticks==0&&skill==5))&&n<cap_i){out[n].hero=hero;out[n].skill=skill;out[n].harmful=harmful;out[n].ticks=ticks;out[n].stacks=stacks;n++;}return n;}
int dota_roster_statuses(const DotaGame*g,int target,DotaRosterStatus*out,int capacity){
    const DotaRosterTarget*t=target_state(g,target);const DotaRosterHero*h=(unsigned)target<10u?hero_state(g,target):0;int n=0;if(!out||capacity<=0)return 0;
    if(t){n=add_status(out,n,capacity,9,0,1,t->gush_ticks,1);n=add_status(out,n,capacity,9,2,1,t->smash_ticks,1);n=add_status(out,n,capacity,8,2,0,t->warcry_ticks,1);n=add_status(out,n,capacity,6,1,1,t->rot_ticks,1);n=add_status(out,n,capacity,7,0,1,t->shrapnel_ticks,1);n=add_status(out,n,capacity,7,1,1,t->headshot_ticks,1);n=add_status(out,n,capacity,7,4,1,t->grenade_ticks,1);n=add_status(out,n,capacity,9,4,1,t->tether_ticks,1);}
    if(h){int hero=g->units[target].hero_id;n=add_status(out,n,capacity,6,2,0,h->flesh_until-g->tick,1);n=add_status(out,n,capacity,7,2,0,h->aim_until-g->tick,1);n=add_status(out,n,capacity,8,3,0,h->gods_until-g->tick,1);n=add_status(out,n,capacity,9,1,0,h->shell_until-g->tick,1);if(hero==6&&h->flesh_stacks)n=add_status(out,n,capacity,6,5,0,0,h->flesh_stacks);if(hero==9&&h->fish_count)n=add_status(out,n,capacity,9,5,0,0,h->fish_count);}
    return n;
}
