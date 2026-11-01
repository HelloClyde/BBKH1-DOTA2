#include "extra_heroes.h"
#include "trees.h"
#include "extra_heroes_data.h"
#include "bosses.h"
#include "navigation.h"
#include "items.h"
#include "roster_heroes.h"
#include "progression.h"
#include "items_internal.h"
#include "sight.h"

static int cap(int x,int a,int b){return x<a?a:(x>b?b:x);}
static int live(const DotaUnit *u){return u->active && u->hp>0;}
static int sq(int x,int y,int xx,int yy){int dx=x-xx,dy=y-yy;return dx*dx+dy*dy;}
static int within(int x,int y,int xx,int yy,int r){return (long long)sq(x,y,xx,yy)*16<=(long long)r*r;}
static int rank(const DotaUnit *u,int s){return dota_progression_rank(u,s);}
static int value100(const DotaUnit *u,int s,const char *field,int fallback){return dota_progression_value100(u,s,field,fallback);}
static int value(const DotaUnit *u,int s,const char *field,int fallback){return value100(u,s,field,fallback*100)/100;}
static int ticks(const DotaUnit *u,int s,const char *field,int fallback){return value100(u,s,field,fallback*5)/5;}
static int hero_index(const DotaGame *g,int i){return (unsigned)i<10 && g->units[i].type==DOTA_HERO;}
static int valid(const DotaGame *g,int i,int generation){return hero_index(g,i) && g->units[i].generation==generation;}
static int duration(DotaGame *g,int target,int ticks){return (ticks*(100-dota_items_status_resist_percent(g,target))+99)/100;}
static void clear(void *p,unsigned n){unsigned char *b=p;while(n--)*b++=0;}
static void event(DotaGame *g,int caster,int code,int value){if(caster==g->player){g->last_event=code;g->last_event_value=value;g->last_event_ticks=40;}}
int dota_extra_is_hero(const DotaUnit *u){return u && u->type==DOTA_HERO && (u->hero_id==4 || u->hero_id==5);}
int dota_extra_mana(const DotaUnit *u,int s){return dota_extra_is_hero(u) && (unsigned)s<4&&rank(u,s)>0?value(u,s,"AbilityManaCost",extra_mana[u->hero_id-4][s][rank(u,s)-1]):0;}
int dota_extra_cooldown(const DotaUnit *u,int s){return dota_extra_is_hero(u) && (unsigned)s<4&&rank(u,s)>0?ticks(u,s,"AbilityCooldown",extra_cd[u->hero_id-4][s][rank(u,s)-1]):0;}
static int pounce_recharge(DotaGame *g,int i){return cap(240*(100-dota_items_cooldown_percent(g,i))/100,1,240);}
void dota_extra_refresh(DotaGame *g,int i){DotaExtraHero *s;if(!g || !hero_index(g,i) || (!dota_extra_is_hero(&g->units[i]) && g->units[i].hero_id!=3))return;
    s=&g->extra.hero[i];s->shard_cooldown=0;
    if(g->units[i].hero_id==4 && dota_hero_has_scepter(g,i)){s->pounce_upgrade=1;s->pounce_charges=2;s->pounce_restore_at=0;g->units[i].cooldown[1]=0;}
}
int dota_extra_range(const DotaUnit *u,int s){return dota_extra_is_hero(u) && (unsigned)s<4?value(u,s,"AbilityCastRange",extra_ranges[u->hero_id-4][s]):0;}
int dota_extra_radius(const DotaUnit *u,int s){const char *field;if(!dota_extra_is_hero(u)||(unsigned)s>=4)return 0;field=u->hero_id==4?(s==0?"radius":s==1?"leash_radius":"radius"):(s==0?"area_of_effect":s==1?"slow_aoe":s==2?"aura_radius":"radius");return value(u,s,field,extra_radii[u->hero_id-4][s]);}
int dota_extra_effect_radius(const DotaGame *g,int i,int s){const DotaUnit *u=&g->units[i];int r=dota_extra_radius(u,s);return (u->hero_id==4 && s==0) || (u->hero_id==5 && s<3)?dota_rules_aoe_radius(g,i,r):r;}

static int active_ghost(const DotaGame *g,int i){return hero_index(g,i) && g->units[i].hero_id==5 && live(&g->units[i]) && g->extra.hero[i].ghost_until>g->tick;}
int dota_extra_invulnerable(const DotaGame *g,int i){return g && hero_index(g,i) && g->units[i].hero_id==5 && g->extra.hero[i].seeker_at>0 && g->tick>=g->extra.hero[i].seeker_at;}
int dota_extra_physical_immune(const DotaGame *g,int i){return active_ghost(g,i);}
int dota_extra_disarmed(const DotaGame *g,int i){return active_ghost(g,i) || (hero_index(g,i) && (g->extra.hero[i].scythe_cast_at>g->tick || g->extra.hero[i].seeker_at>0));}
int dota_extra_magic_resist_delta(const DotaGame *g,int i){return active_ghost(g,i)?value(&g->units[i],1,"bonus_damage",-20):0;}
int dota_extra_target_hidden(const DotaGame *g,int observer,int target){
    int i;if(!g || !hero_index(g,target) || (unsigned)observer>=DOTA_MAX_UNITS || g->units[observer].team==g->units[target].team || !live(&g->units[target]))return 0;
    if(g->units[target].hero_id==4 && g->extra.hero[target].shadow_until>g->tick)return 1;
    if(dota_extra_invulnerable(g,target))return 1;
    for(i=0;i<10;++i){const DotaExtraHero *s=&g->extra.hero[i];if(g->units[i].hero_id==4 && s->shroud_until>g->tick && g->units[i].team==g->units[target].team && within(g->units[target].x,g->units[target].y,s->shroud_x,s->shroud_y,s->shroud_radius))return 1;}
    return 0;
}
int dota_extra_base_attributes(const DotaUnit *u,int *str,int *agi,int *intel){
    if(!dota_extra_is_hero(u))return 0;
    dota_progression_base_attributes100(u,str,agi,intel);return 1;
}
void dota_extra_stat_delta(const DotaGame *g,int i,int *str,int *agi,int *intel){
    const DotaExtraHero *s;int gain;if(!g || !hero_index(g,i))return;s=&g->extra.hero[i];
    gain=g->units[i].hero_id==4?value(&g->units[i],5,"agi_gain",3):3;
    *str-=s->essence_loss;*intel-=s->essence_loss;
    *agi+=s->essence_gain*gain+s->permanent_agi-s->essence_loss;
}
int dota_extra_attack_speed(const DotaGame *g,int i){
    const DotaProgressionBase *b;int attr=0,delta[3]={0,0,0};if(!g||!hero_index(g,i)||!dota_extra_is_hero(&g->units[i]))return 0;
    b=dota_progression_base(g->units[i].hero_id);if(!b)return 0;dota_progression_base_attributes100(&g->units[i],0,&attr,0);dota_extra_stat_delta(g,i,&delta[0],&delta[1],&delta[2]);
    return b->attack_speed100+attr+delta[1]*100;
}
static int dance_passive(const DotaGame *g,int i){
    const DotaExtraHero *s=&g->extra.hero[i];const DotaUnit *u=&g->units[i];
    return u->hero_id==4 && rank(u,3)>0 && (s->shadow_until>g->tick ||
      (!dota_items_broken(g,i) && s->unseen_since>0 && g->tick-s->unseen_since>=ticks(u,3,"activation_delay",10) && g->tick>=s->neutral_until && !dota_sight_team_position_visible(g,!u->team,u->x,u->y)));
}
int dota_extra_shadow_dance_passive_active(const DotaGame *g,int i){return g && hero_index(g,i) && live(&g->units[i]) && dance_passive(g,i);}
static int dance_rank(const DotaGame *g,int i){int j,r=0;if(dance_passive(g,i))r=rank(&g->units[i],3);
    for(j=0;j<10;++j){const DotaExtraHero *s=&g->extra.hero[j];if(g->units[j].hero_id==4 && s->shroud_until>g->tick && g->units[j].team==g->units[i].team && within(g->units[i].x,g->units[i].y,s->shroud_x,s->shroud_y,s->shroud_radius) && s->shroud_rank>r)r=s->shroud_rank;}return r;
}
int dota_extra_attack_speed_delta(const DotaGame *g,int i){int j,power=0;if(!g||!hero_index(g,i))return 0;if(g->units[i].hero_id==4&&g->extra.hero[i].shadow_until>g->tick)power=value(&g->units[i],3,"attack_speed",0);for(j=0;j<10;++j)if(g->units[j].hero_id==4&&g->extra.hero[j].shroud_until>g->tick&&g->units[j].team==g->units[i].team&&within(g->units[i].x,g->units[i].y,g->extra.hero[j].shroud_x,g->extra.hero[j].shroud_y,g->extra.hero[j].shroud_radius)){int n=value(&g->units[j],3,"attack_speed",0);if(n>power)power=n;}return power;}
int dota_extra_additional_regen100(const DotaGame *g,int i){const DotaExtraHero *s;const DotaUnit *u;int hp=0,r;if(!g || !hero_index(g,i) || !live(&g->units[i]))return 0;
    s=&g->extra.hero[i];u=&g->units[i];r=dance_rank(g,i);if(r){int source=i,j;for(j=0;j<10;++j)if(g->units[j].hero_id==4&&g->extra.hero[j].shroud_rank==r&&g->extra.hero[j].shroud_until>g->tick&&g->units[j].team==u->team&&within(u->x,u->y,g->extra.hero[j].shroud_x,g->extra.hero[j].shroud_y,g->extra.hero[j].shroud_radius)){source=j;break;}hp=value100(&g->units[source],3,"bonus_regen",(30+30*r)*100);}
    if(u->hero_id==5)hp+=s->sadist_stacks*value100(u,5,"regen",380+(u->level-1)*20)+s->scythe_kills*100;
    return hp;
}
int dota_extra_additional_mana_regen100(const DotaGame *g,int i){const DotaExtraHero *s;const DotaUnit *u;if(!g||!hero_index(g,i)||!live(&g->units[i])||g->units[i].hero_id!=5)return 0;s=&g->extra.hero[i];u=&g->units[i];return s->sadist_stacks*value100(u,5,"regen",380+(u->level-1)*20)+s->scythe_kills*100;}
int dota_extra_move_percent(const DotaGame *g,int i){
    int j,p=100;if(!g || !hero_index(g,i))return p;
    if(dance_rank(g,i)){int source=i,j,r=dance_rank(g,i);for(j=0;j<10;++j)if(g->units[j].hero_id==4&&g->extra.hero[j].shroud_rank==r&&g->extra.hero[j].shroud_until>g->tick&&g->units[j].team==g->units[i].team&&within(g->units[i].x,g->units[i].y,g->extra.hero[j].shroud_x,g->extra.hero[j].shroud_y,g->extra.hero[j].shroud_radius)){source=j;break;}p+=value(&g->units[source],3,"bonus_movement_speed",10+10*r);}
    for(j=0;j<10;++j)if(!dota_items_debuff_immune(g,i) && g->units[j].team!=g->units[i].team && active_ghost(g,j) && within(g->units[i].x,g->units[i].y,g->units[j].x,g->units[j].y,dota_skill_effect_radius_source(g,j,1))){
        int slow=value(&g->units[j],1,"movement_speed",extra_ghost_slow[rank(&g->units[j],1)-1]);if(p>100-slow)p=100-slow;
    }
    return p;
}
int dota_extra_move_flat(const DotaGame *g,int i){
    int j,n=0;if(!g || !hero_index(g,i))return 0;
    for(j=0;j<DOTA_EXTRA_SHIVS;++j){const DotaExtraShiv *s=&g->extra.shivs[j];if(s->until<=g->tick)continue;
        if(s->source==i && valid(g,i,s->source_generation))n+=s->power;
        if(s->target==i && s->target_until>g->tick && !dota_items_debuff_immune(g,i) && valid(g,i,s->target_generation))n-=s->power;
    }return n;
}
int dota_extra_regen_flat100(const DotaGame *g,int i){return dota_extra_move_flat(g,i)*100;}
int dota_extra_moving(const DotaGame *g,int i){return g && hero_index(g,i) && (g->extra.hero[i].leap_at>0 || g->extra.hero[i].seeker_at>0);}
int dota_extra_allow_move(const DotaGame *g,int i,int x,int y){
    const DotaExtraHero *s;if(!g || !hero_index(g,i))return 1;s=&g->extra.hero[i];
    if(s->leap_at>0 || s->scythe_cast_at>g->tick || s->seeker_at>0)return 0;
    return s->leash_until<=g->tick || dota_items_debuff_immune(g,i) || within(x,y,s->leash_x,s->leash_y,400) ||
       sq(x,y,s->leash_x,s->leash_y)<sq(g->units[i].x,g->units[i].y,s->leash_x,s->leash_y);
}
int dota_extra_heal_amount(const DotaGame *g,int i,int amount){
    int j,p=100;if(!g || !hero_index(g,i))return amount;
    if(active_ghost(g,i))p+=value(&g->units[i],1,"heal_bonus",extra_ghost_heal[rank(&g->units[i],1)-1]);
    for(j=0;j<10;++j)if(live(&g->units[j])&&g->units[j].hero_id==5&&rank(&g->units[j],2)>0&&!dota_items_broken(g,j)&&g->units[j].team!=g->units[i].team&&within(g->units[i].x,g->units[i].y,g->units[j].x,g->units[j].y,dota_skill_effect_radius_source(g,j,2)))p-=value(&g->units[j],2,"heal_reduction_pct",0);
    for(j=0;j<DOTA_EXTRA_SHIVS;++j){const DotaExtraShiv *s=&g->extra.shivs[j];if(s->until<=g->tick)continue;
        if(s->source==i && valid(g,i,s->source_generation))p+=s->power;
        if(s->target==i && s->target_until>g->tick && !dota_items_debuff_immune(g,i) && valid(g,i,s->target_generation))p-=s->power;
    }return amount*cap(p,0,1000)/100;
}
void dota_extra_heal(DotaGame *g,int i,int amount){if((unsigned)i<DOTA_MAX_UNITS && live(&g->units[i]))g->units[i].hp=cap(g->units[i].hp+dota_items_heal_amount(g,i,i,amount),0,g->units[i].max_hp);}
void dota_extra_mana_restore(DotaGame *g,int i,int amount){if(hero_index(g,i) && live(&g->units[i])){
    amount=dota_extra_mana_restore_amount(g,i,amount);
    g->units[i].mana=cap(g->units[i].mana+amount,0,g->units[i].max_mana);
}}
int dota_extra_mana_restore_amount(const DotaGame *g,int i,int amount){return g && active_ghost(g,i)?amount*(100+value(&g->units[i],1,"heal_bonus",extra_ghost_heal[rank(&g->units[i],1)-1]))/100:amount;}
void dota_extra_dispel(DotaGame *g,int i,int positive){int j;if(!g || !hero_index(g,i))return;
    if(positive)g->extra.hero[i].ghost_until=0;
    for(j=0;j<DOTA_EXTRA_SHIVS;++j){DotaExtraShiv *s=&g->extra.shivs[j];if(s->until<=g->tick)continue;
        if(positive && s->source==i && s->source_generation==(unsigned short)g->units[i].generation)s->source=255;
        if(!positive && s->target==i && s->target_generation==(unsigned short)g->units[i].generation)s->target=255;
        if(s->source==255 && s->target==255)s->until=0;
    }
}

static void recalc_changed(DotaGame *g,unsigned mask){int i;for(i=0;i<10;++i)if(mask&(1u<<i))dota_rules_recalc(g,i);}
static void stack_add(DotaGame *g,int source,int target,int ticks,int count){
    int i;unsigned changed=0;
    if(dota_items_broken(g,source))return;
    for(i=0;i<DOTA_EXTRA_STACKS && count>0;++i)if(!g->extra.stacks[i].remaining){DotaExtraStack *s=&g->extra.stacks[i];
        s->remaining=(unsigned short)ticks;s->source=(unsigned char)source;s->target=(unsigned char)target;
        s->source_generation=(unsigned short)g->units[source].generation;
        s->target_generation=(unsigned short)(target<10?g->units[target].generation:0);
        if(target==255)++g->extra.hero[source].sadist_stacks;
        else{++g->extra.hero[source].essence_gain;++g->extra.hero[target].essence_loss;changed|=(1u<<source)|(1u<<target);}--count;
    }recalc_changed(g,changed);
}
static void stack_remove(DotaGame *g,DotaExtraStack *s,unsigned *changed){
    if(s->target==255){if(s->source<10 && valid(g,s->source,s->source_generation))--g->extra.hero[s->source].sadist_stacks;}
    else{
        if(s->source<10 && valid(g,s->source,s->source_generation)){--g->extra.hero[s->source].essence_gain;*changed|=1u<<s->source;}
        if(s->target<10 && valid(g,s->target,s->target_generation)){--g->extra.hero[s->target].essence_loss;*changed|=1u<<s->target;}
    }s->remaining=0;
}
static void shiv_apply(DotaGame *g,int source,int target,int power){
    int i;for(i=0;i<DOTA_EXTRA_SHIVS;++i)if(g->extra.shivs[i].until<=g->tick){DotaExtraShiv *s=&g->extra.shivs[i];
        s->until=g->tick+ticks(&g->units[source],2,"duration",240);s->target_until=g->tick+duration(g,target,ticks(&g->units[source],2,"duration",240));s->source=(unsigned char)source;s->target=(unsigned char)target;s->power=(unsigned char)power;
        s->source_generation=(unsigned short)g->units[source].generation;s->target_generation=(unsigned short)g->units[target].generation;break;
    }
}
void dota_extra_on_attack_hit(DotaGame *g,int source,int target){
    DotaExtraHero *s;if(!g || !hero_index(g,source) || !hero_index(g,target) || !live(&g->units[source]) || g->units[source].hero_id!=4 || g->units[source].team==g->units[target].team || !live(&g->units[target]) || dota_extra_physical_immune(g,target))return;
    s=&g->extra.hero[source];stack_add(g,source,target,ticks(&g->units[source],5,"duration",200+(g->units[source].level-1)*50),1);
    if(s->shiv_until>g->tick && s->shiv_target==target && s->shiv_generation==g->units[target].generation){
        shiv_apply(g,source,target,value(&g->units[source],2,"health_rest_steal",s->shiv_rank*4));s->shiv_until=0;
        dota_rules_skill_event(g,source,2,target,g->units[target].x,g->units[target].y,240,DOTA_SKILL_EVENT_STRIKE);
    }
}
void dota_extra_on_damage(DotaGame *g,int victim,int source){
    if(hero_index(g,victim) && g->units[victim].hero_id==4 && (unsigned)source<DOTA_MAX_UNITS && g->units[source].team==DOTA_NEUTRAL_TEAM)g->extra.hero[victim].neutral_until=g->tick+ticks(&g->units[victim],3,"neutral_disable",40);
}
int dota_extra_kill_source(const DotaGame *g,int victim,int source){
    int i;if(!g || !hero_index(g,victim))return source;
    for(i=0;i<10;++i){const DotaExtraHero *s=&g->extra.hero[i];if(s->scythe_at>=g->tick && s->scythe_at>0 && s->scythe_target==victim && valid(g,victim,s->scythe_generation) && hero_index(g,i) && g->units[i].hero_id==5 && g->units[i].team!=g->units[victim].team)return i;}
    for(i=0;i<10;++i){const DotaExtraScythe *s=&g->extra.scythe_saved[i];if(s->at>=g->tick && s->at>0 && s->target==victim && valid(g,victim,s->generation) && hero_index(g,i) && g->units[i].hero_id==5 && g->units[i].team!=g->units[victim].team)return i;}
    return source;
}
void dota_extra_on_kill(DotaGame *g,int victim,int source){
    int i;unsigned changed=0;DotaUnit *v=&g->units[victim];
    if(hero_index(g,source) && g->units[source].hero_id==5 && g->units[source].team!=v->team){
        stack_add(g,source,255,ticks(&g->units[source],5,"regen_duration",160),v->type==DOTA_HERO?value(&g->units[source],5,"hero_multiplier",4):1);
        if(v->type==DOTA_HERO){DotaExtraHero *s=&g->extra.hero[source];const DotaExtraScythe *saved=&g->extra.scythe_saved[source];
            if(s->scythe_at>=g->tick && s->scythe_target==victim && s->scythe_generation==v->generation)s->scythe_kills+=s->scythe_rank;
            else if(saved->at>=g->tick && saved->target==victim && saved->generation==v->generation)s->scythe_kills+=saved->rank;}
    }
    if(v->type!=DOTA_HERO || (unsigned)victim>=10)return;
    g->extra.pact_saved[victim].at=0;
    for(i=0;i<10;++i)if(live(&g->units[i]) && g->units[i].hero_id==4 && g->units[i].team!=v->team && within(g->units[i].x,g->units[i].y,v->x,v->y,dota_rules_aoe_radius(g,i,value(&g->units[i],5,"steal_radius",300)))){
        int j;for(j=0;j<DOTA_EXTRA_STACKS;++j)if(g->extra.stacks[j].remaining && g->extra.stacks[j].source==i && g->extra.stacks[j].target==victim){++g->extra.hero[i].permanent_agi;changed|=1u<<i;break;}
    }
    /* Death clears only the deceased side. Other units retain their own
     * independent temporary bonuses/debuffs until their original expiry. */
    for(i=0;i<DOTA_EXTRA_STACKS;++i){DotaExtraStack *s=&g->extra.stacks[i];if(!s->remaining)continue;
        if(s->source==victim){if(s->target==255){s->remaining=0;continue;}s->source=255;}
        if(s->target==victim)s->target=254;
    }
    {DotaExtraHero *s=&g->extra.hero[victim],saved=*s;clear(s,sizeof(*s));s->generation=saved.generation;s->permanent_agi=saved.permanent_agi;s->scythe_kills=saved.scythe_kills;
        /* An already applied Reaper's Scythe continues after caster death. */
        s->scythe_at=saved.scythe_at;s->scythe_target=saved.scythe_target;s->scythe_generation=saved.scythe_generation;s->scythe_rank=saved.scythe_rank;s->scythe_duration=saved.scythe_duration;
        s->shroud_until=saved.shroud_until;s->shroud_x=saved.shroud_x;s->shroud_y=saved.shroud_y;s->shroud_rank=saved.shroud_rank;s->shroud_radius=saved.shroud_radius;
        s->shard_cooldown=saved.shard_cooldown;s->pounce_upgrade=saved.pounce_upgrade;s->pounce_charges=saved.pounce_charges;s->pounce_restore_at=saved.pounce_restore_at;
    }
    changed|=1u<<victim;recalc_changed(g,changed);
}

static void purge(DotaUnit *u){u->stun_ticks=u->slow_ticks=u->taunt_ticks=u->dot_ticks=u->buff_ticks=0;}
static void pact_pulse(DotaGame *g,int caster,DotaExtraPact *saved){
    DotaUnit *u=&g->units[caster];DotaExtraHero *s=&g->extra.hero[caster];int i,n=saved?saved->pulse:s->pact_pulse,total=value(u,0,"total_damage",extra_pact_damage[(saved?saved->rank:s->pact_rank)-1]),pulses=cap(value(u,0,"total_pulses",10),1,100),pct=value(u,0,"self_damage_pct",30);
    int amount=(total*(n+1)/pulses)-(total*n/pulses),self=(total*pct*75*(n+1)/(pulses*10000))-(total*pct*75*n/(pulses*10000));
    purge(u);dota_items_purge(g,caster,1);for(i=0;i<DOTA_MAX_UNITS;++i)if(live(&g->units[i]) && g->units[i].team!=u->team && g->units[i].type!=DOTA_TOWER && g->units[i].type!=DOTA_BASE && g->units[i].type!=DOTA_BUILDING && within(u->x,u->y,g->units[i].x,g->units[i].y,dota_skill_effect_radius_source(g,caster,0)))dota_rules_damage(g,i,amount,caster,1);
    /* Retain the existing nonlethal self path: Source30% raw at the
     * base25% magic resistance. Item-specific self-damage flags are separate. */
    if(self>0)u->hp=cap(u->hp-self,1,u->max_hp);
    dota_rules_skill_effect(g,caster,0,DOTA_FX_BURST,u->x,u->y,u->x,u->y,81,3);
    if(n==0)dota_rules_skill_event(g,caster,0,-1,u->x,u->y,20,DOTA_SKILL_EVENT_STRIKE);
    if(saved){if(++saved->pulse>=pulses)saved->at=0;else saved->at=g->tick+cap(ticks(u,0,"pulse_interval",2),1,100);}
    else if(++s->pact_pulse>=pulses)s->pact_at=0;else s->pact_at=g->tick+cap(ticks(u,0,"pulse_interval",2),1,100);
}
static void land(DotaGame *g,int caster){DotaUnit *u=&g->units[caster];int x=u->x,y=u->y;
    /* Native Pounce uses its installed pounce_radius for the landing tree
     * clear, before resolving the final navigable position. */
    dota_trees_cut_radius(g,x,y,value(u,1,"pounce_radius",120));
    dota_nav_nearest(x,y,256,&x,&y);g->extra.hero[caster].leap_at=0;u->skill_ticks[1]=0;dota_rules_set_position(g,caster,x,y);
}
static void leap_tick(DotaGame *g,int caster){
    DotaUnit *u=&g->units[caster];DotaExtraHero *s=&g->extra.hero[caster];int age=g->tick-s->leap_at+1,i,x,y;
    x=s->leap_x+(s->leap_to_x-s->leap_x)*cap(age,0,s->leap_duration)/s->leap_duration;y=s->leap_y+(s->leap_to_y-s->leap_y)*cap(age,0,s->leap_duration)/s->leap_duration;
    dota_rules_set_position(g,caster,x,y);
    for(i=0;i<10;++i)if(i!=caster && live(&g->units[i]) && g->units[i].team!=u->team && !dota_unit_invulnerable(g,i) && !dota_extra_target_hidden(g,caster,i) && within(x,y,g->units[i].x,g->units[i].y,value(u,1,"pounce_radius",120))){
        DotaExtraHero *v=&g->extra.hero[i];int life=duration(g,i,ticks(u,1,"leash_duration",extra_pounce_leash[s->leap_rank-1]));land(g,caster);v->leash_until=g->tick+life;v->leash_x=u->x;v->leash_y=u->y;v->leash_source=caster;v->leash_generation=u->generation;
        stack_add(g,caster,i,ticks(u,5,"duration",200+(u->level-1)*50),value(u,1,"essence_stacks",s->leap_rank));
        dota_rules_skill_event(g,caster,1,i,v->leash_x,v->leash_y,life,DOTA_SKILL_EVENT_STRIKE);return;
    }if(age>=s->leap_duration)land(g,caster);
}
static int isqrt(int n){unsigned x=(unsigned)n,r=0,bit=1u<<30;while(bit>x)bit>>=2;while(bit){if(x>=r+bit){x-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return (int)r;}
static void pulse_begin(DotaGame *g,int caster,int r){
    DotaDeathPulse *p=&g->extra.pulse[caster];DotaUnit *u=&g->units[caster];int i,n;
    for(n=caster;n<DOTA_EXTRA_PULSES;n+=10)if(!g->extra.pulse[n].rank || g->tick-g->extra.pulse[n].at>100){p=&g->extra.pulse[n];break;}
    clear(p,sizeof(*p));p->at=g->tick;p->generation=u->generation;p->rank=r;p->x=u->x;p->y=u->y;
    for(i=0;i<DOTA_MAX_UNITS;++i)if(live(&g->units[i]) && g->units[i].type!=DOTA_TOWER && g->units[i].type!=DOTA_BUILDING && g->units[i].type!=DOTA_BASE && within(u->x,u->y,g->units[i].x,g->units[i].y,dota_skill_effect_radius_source(g,caster,0))){
        p->targets[i>>5]|=1u<<(i&31);p->target_generation[i]=(unsigned short)g->units[i].generation;
    }
}
static void pulse_tick(DotaGame *g,int slot){
    DotaDeathPulse *p=&g->extra.pulse[slot];int caster=slot%10,i,age=g->tick-p->at;
    if(!p->rank)return;
    if(g->units[caster].generation!=p->generation || age>5*DOTA_HZ){p->rank=0;return;}
    for(i=0;i<DOTA_MAX_UNITS;++i)if((p->targets[i>>5]&(1u<<(i&31))) && !(p->hit[i>>5]&(1u<<(i&31))) && within(p->x,p->y,g->units[i].x,g->units[i].y,age*value(&g->units[caster],0,"projectile_speed",400)/DOTA_HZ)){
        p->hit[i>>5]|=1u<<(i&31);if(!live(&g->units[i]) || (unsigned short)g->units[i].generation!=p->target_generation[i])continue;
        if(g->units[i].team==g->units[caster].team)g->units[i].hp=cap(g->units[i].hp+dota_items_heal_amount(g,caster,i,value(&g->units[caster],0,"heal",extra_pulse_heal[p->rank-1])),0,g->units[i].max_hp);
        else dota_rules_damage(g,i,value(&g->units[caster],0,"AbilityDamage",extra_pulse_damage[p->rank-1]),caster,1);
        dota_rules_skill_event(g,caster,0,i,g->units[i].x,g->units[i].y,6,DOTA_SKILL_EVENT_STRIKE);
    }
}
int dota_extra_cast(DotaGame *g,int caster,int skill,int x,int y){
    if(!g||(unsigned)caster>=10u)return 0;
    DotaUnit *u=&g->units[caster];DotaExtraHero *s=&g->extra.hero[caster];int r=rank(u,skill),target=-1,i,d,best=0x7fffffff;
    if(!dota_extra_is_hero(u) || (unsigned)skill>=4 || dota_skill_is_passive(u->hero_id,skill))return 0;
    if(s->scythe_cast_at>0 || s->seeker_at>0 || u->stun_ticks || u->taunt_ticks || dota_roster_stun(g,caster) || dota_items_silenced(g,caster))return 0;
    if(!r){event(g,caster,3,skill);return 0;}
    if(u->hero_id==4 && skill==1 && dota_hero_has_scepter(g,caster) && !s->pounce_upgrade){s->pounce_upgrade=1;s->pounce_charges=2;u->cooldown[1]=0;}
    if(u->cooldown[skill] && !(u->hero_id==4 && skill==1 && s->pounce_upgrade && s->pounce_charges>0)){event(g,caster,2,skill);return 0;}
    if(dota_items_available_mana(g,caster)<dota_extra_mana(u,skill)*dota_items_mana_cost_percent(g,caster)/100){event(g,caster,1,skill);return 0;}
    if((u->hero_id==4 && skill==2) || (u->hero_id==5 && skill==3)){
        for(i=0;i<10;++i)if(dota_rules_cast_target_matches(g,caster,i) && live(&g->units[i]) && g->units[i].team!=u->team && !dota_unit_invulnerable(g,i) && dota_unit_visible(g,caster,i)){
            d=sq(x,y,g->units[i].x,g->units[i].y);if(d<best){best=d;target=i;}}
        if(target<0 || best>250*250){event(g,caster,4,skill);return 0;}
        if(!within(u->x,u->y,g->units[target].x,g->units[target].y,dota_skill_cast_range_source(g,caster,skill)+24)){event(g,caster,15,skill);return 0;}
    }
    if(u->hero_id==4 && skill==1 && ((s->leash_until>g->tick && !dota_items_debuff_immune(g,caster)) || s->leap_at || dota_items_rooted(g,caster))){event(g,caster,4,skill);return 0;}
    g->item_world.last_mana_spent[caster]=dota_extra_mana(u,skill)*dota_items_mana_cost_percent(g,caster)/100;
    dota_items_spend_mana(g,caster,g->item_world.last_mana_spent[caster]);u->cooldown[skill]=dota_skill_cooldown_ticks(g,caster,skill);dota_items_on_cast(g,caster);
    if(u->hero_id==4 && skill==1 && s->pounce_upgrade){if(s->pounce_charges>0)--s->pounce_charges;if(!s->pounce_restore_at)s->pounce_restore_at=g->tick+pounce_recharge(g,caster);u->cooldown[1]=s->pounce_charges?0:cap(s->pounce_restore_at-g->tick,1,240);}
    dota_rules_skill_event(g,caster,skill,target,x,y,skill==3?(u->hero_id==4?ticks(u,3,"duration",extra_shadow_life[r-1]):39):20,DOTA_SKILL_EVENT_CAST);
    if(target>=0 && (dota_bosses_spell_block(g,target) || dota_items_blocks_spell(g,caster,target)))return 1;
    if(u->hero_id==4){switch(skill){
    case 0:if(s->pact_at>0){DotaExtraPact *saved=&g->extra.pact_saved[caster];saved->at=s->pact_at;saved->pulse=s->pact_pulse;saved->rank=s->pact_rank;}
        s->pact_at=g->tick+ticks(u,0,"delay",30);s->pact_pulse=0;s->pact_rank=r;u->skill_ticks[0]=50;break;
    case 1:{int dx=x-u->x,dy=y-u->y,length=isqrt(dx*dx+dy*dy);if(!length){dx=0;dy=u->team?1:-1;length=1;}
        int distance=s->pounce_upgrade?225:175;s->leap_duration=s->pounce_upgrade?20:15;s->leap_at=g->tick+1;s->leap_x=u->x;s->leap_y=u->y;s->leap_to_x=cap(u->x+dx*distance/length,0,DOTA_WORLD-1);s->leap_to_y=cap(u->y+dy*distance/length,0,DOTA_WORLD-1);s->leap_rank=r;u->skill_ticks[1]=s->leap_duration;u->move_order=0;break;}
    case 2:s->shiv_target=target;s->shiv_generation=g->units[target].generation;s->shiv_until=g->tick+60;s->shiv_rank=r;u->target=target;dota_rules_attack_once(g,caster,target);break;
    case 3:s->shadow_until=g->tick+ticks(u,3,"duration",extra_shadow_life[r-1]);u->skill_ticks[3]=ticks(u,3,"duration",extra_shadow_life[r-1]);break;
    }}else switch(skill){
    case 0:pulse_begin(g,caster,r);u->skill_ticks[0]=25;break;
    case 1:s->ghost_until=g->tick+ticks(u,1,"duration",extra_ghost_life[r-1]);u->skill_ticks[1]=ticks(u,1,"duration",extra_ghost_life[r-1]);break;
    case 3:if(s->scythe_at>g->tick){DotaExtraScythe *saved=&g->extra.scythe_saved[caster];saved->at=s->scythe_at;saved->target=s->scythe_target;saved->generation=s->scythe_generation;saved->rank=s->scythe_rank;saved->duration=s->scythe_duration;s->scythe_at=0;}
        s->scythe_target=target;s->scythe_generation=g->units[target].generation;s->scythe_cast_at=g->tick+9;s->scythe_rank=r;u->skill_ticks[3]=39;break;
    }return 1;
}
static int shard_hero(const DotaUnit *u){return u && u->type==DOTA_HERO && (u->hero_id==3 || u->hero_id==4 || u->hero_id==5);}
int dota_extra_shard_mana(const DotaGame *g,int i){return g && hero_index(g,i) && shard_hero(&g->units[i])?value(&g->units[i],4,"AbilityManaCost",g->units[i].hero_id==3?150:g->units[i].hero_id==4?75:160):0;}
int dota_extra_shard_range(const DotaGame *g,int i){return g && hero_index(g,i) && shard_hero(&g->units[i])?(g->units[i].hero_id==3?value(&g->units[i],4,"hop_distance",275):value(&g->units[i],4,"AbilityCastRange",g->units[i].hero_id==4?400:600)+dota_items_range_bonus((DotaGame*)g,i,0)):0;}
int dota_extra_shard_radius(const DotaGame *g,int i){return g && hero_index(g,i) && shard_hero(&g->units[i])?dota_rules_aoe_radius(g,i,g->units[i].hero_id==3?value(&g->units[i],4,"frostbite_radius",450):g->units[i].hero_id==4?value(&g->units[i],4,"radius",225):value(&g->units[i],0,"area_of_effect",500)):0;}
int dota_extra_shard_cooldown(const DotaGame *g,int i){return g && hero_index(g,i)?cap(g->extra.hero[i].shard_cooldown-g->tick,0,1500):0;}
int dota_extra_cast_shard(DotaGame *g,int caster,int x,int y){
    if(!g||(unsigned)caster>=10u)return 0;
    DotaUnit *u;DotaExtraHero *s;int i,t=-1,best=250*250+1,dx,dy,d;
    if(!g || !hero_index(g,caster))return 0;
    u=&g->units[caster];s=&g->extra.hero[caster];
    if(!shard_hero(u) || !dota_hero_has_shard(g,caster) || !live(u) || g->paused || g->state!=DOTA_PLAYING || u->stun_ticks || dota_roster_stun(g,caster) || u->taunt_ticks || (u->channel_ticks && u->hero_id!=3) || s->seeker_at>0 || s->scythe_cast_at>0 || dota_items_silenced(g,caster))return 0;
    if((u->hero_id==3 || u->hero_id==5) && (dota_items_rooted(g,caster) || (s->leash_until>g->tick && !dota_items_debuff_immune(g,caster))))return 0;
    if(dota_extra_shard_cooldown(g,caster)){event(g,caster,2,4);return 0;}
    if(dota_items_available_mana(g,caster)<dota_extra_shard_mana(g,caster)*dota_items_mana_cost_percent(g,caster)/100){event(g,caster,1,4);return 0;}
    if(u->hero_id==5){for(i=0;i<DOTA_MAX_UNITS;++i)if(dota_rules_cast_target_matches(g,caster,i) && i!=caster && live(&g->units[i]) && g->units[i].type!=DOTA_TOWER && g->units[i].type!=DOTA_BASE && g->units[i].type!=DOTA_BUILDING && !dota_unit_invulnerable(g,i) && dota_unit_visible(g,caster,i)){
        d=sq(x,y,g->units[i].x,g->units[i].y);if(d<best){t=i;best=d;}}
        if(t<0){event(g,caster,4,4);return 0;}x=g->units[t].x;y=g->units[t].y;
    }
    if(u->hero_id==3){int distance=value(u,4,"hop_distance",275),length;dx=x-u->x;dy=y-u->y;length=isqrt(dx*dx+dy*dy);if(!length){dx=u->team? -1:1;dy=0;length=1;}x=cap(u->x+dx*distance/(4*length),0,DOTA_WORLD-1);y=cap(u->y+dy*distance/(4*length),0,DOTA_WORLD-1);if(!dota_nav_nearest(x,y,128,&x,&y) || dota_rules_crystal_clone(g,caster)<0){event(g,caster,4,4);return 0;}}
    else if(!within(u->x,u->y,x,y,dota_extra_shard_range(g,caster))){event(g,caster,15,4);return 0;}
    g->item_world.last_mana_spent[caster]=dota_extra_shard_mana(g,caster)*dota_items_mana_cost_percent(g,caster)/100;
    dota_items_spend_mana(g,caster,g->item_world.last_mana_spent[caster]);s->shard_cooldown=g->tick+ticks(u,4,"AbilityCooldown",u->hero_id==3?240:u->hero_id==4?1500:380)*(100-dota_items_cooldown_percent(g,caster))/100;dota_items_on_cast(g,caster);dota_progression_on_cast(g,caster,g->item_world.last_mana_spent[caster]);
    if(t>=0 && g->units[t].team!=u->team && dota_items_blocks_spell(g,caster,t))return 1;
    if(u->hero_id==3){s->seeker_at=g->tick+1;s->seeker_duration=ticks(u,4,"hop_duration",6);s->seeker_x=u->x;s->seeker_y=u->y;s->seeker_to_x=x;s->seeker_to_y=y;u->move_order=0;u->target=-1;dota_rules_skill_event(g,caster,1,-1,u->x,u->y,6,DOTA_SKILL_EVENT_CAST);}
    else if(u->hero_id==4){s->shroud_until=g->tick+ticks(u,4,"duration",40);s->shroud_x=x;s->shroud_y=y;s->shroud_rank=rank(u,3);s->shroud_radius=dota_extra_shard_radius(g,caster);
        dota_rules_skill_event(g,caster,3,-1,x,y,40,DOTA_SKILL_EVENT_CAST);g->skill_events[g->skill_event_serial%DOTA_MAX_SKILL_EVENTS].step=-4;
    }else{dx=x-u->x;dy=y-u->y;d=isqrt(dx*dx+dy*dy);s->seeker_at=g->tick+4;s->seeker_duration=cap((d*80+699)/700,1,30);s->seeker_x=u->x;s->seeker_y=u->y;s->seeker_to_x=x;s->seeker_to_y=y;s->shiv_target=t;s->shiv_generation=t<0?0:g->units[t].generation;
        u->move_order=0;u->target=-1;dota_rules_skill_event(g,caster,0,t,x,y,s->seeker_duration+4,DOTA_SKILL_EVENT_CAST);g->skill_events[g->skill_event_serial%DOTA_MAX_SKILL_EVENTS].step=-4;
    }return 1;
}
static void scythe_tick(DotaGame *g,int caster,int *at,int target,int generation,int r){
    if(*at>g->tick && valid(g,target,generation)){DotaUnit *v=&g->units[target];int left=*at-g->tick;
        if(!dota_items_debuff_immune(g,target)){if(v->stun_ticks<left)v->stun_ticks=left;}
        else if(v->stun_ticks==left)v->stun_ticks=0;
    }
    if(*at>0 && *at<=g->tick){
        if(valid(g,target,generation) && live(&g->units[target])){int amount=(g->units[target].max_hp-g->units[target].hp)*value100(&g->units[caster],3,"damage_per_health",extra_scythe_damage_pct[r-1])/100;
            dota_rules_skill_event(g,caster,3,target,g->units[target].x,g->units[target].y,12,DOTA_SKILL_EVENT_STRIKE);dota_rules_damage(g,target,amount,caster,1);
        }*at=0;
    }
}
void dota_extra_tick(DotaGame *g){
    int i,j;unsigned changed=0;
    /* Synchronize every life before any caster can write a target's state.
     * A first-tick Pounce can hit a later hero slot immediately. */
    for(i=0;i<10;++i){DotaUnit *u=&g->units[i];DotaExtraHero *s=&g->extra.hero[i];
        if(u->type==DOTA_HERO && s->generation!=u->generation){s->generation=u->generation;s->unseen_since=0;s->leash_until=0;}
    }
    for(i=0;i<DOTA_EXTRA_STACKS;++i){DotaExtraStack *s=&g->extra.stacks[i];if(s->remaining && --s->remaining==0)stack_remove(g,s,&changed);}
    recalc_changed(g,changed);
    for(i=0;i<10;++i){DotaUnit *u=&g->units[i];DotaExtraHero *s=&g->extra.hero[i];
        if(u->type!=DOTA_HERO)continue;
        for(j=i;j<DOTA_EXTRA_PULSES;j+=10)pulse_tick(g,j);
        if(s->pounce_upgrade){if(!dota_hero_has_scepter(g,i)){s->pounce_upgrade=s->pounce_restore_at=s->pounce_charges=0;}
            else if(s->pounce_restore_at>0 && s->pounce_restore_at<=g->tick){s->pounce_charges=cap(s->pounce_charges+1,0,2);s->pounce_restore_at=s->pounce_charges<2?g->tick+pounce_recharge(g,i):0;if(s->pounce_charges>0)u->cooldown[1]=0;}}
        if(live(u) && s->pact_at>0 && s->pact_at<=g->tick)pact_pulse(g,i,0);
        if(live(u) && g->extra.pact_saved[i].at>0 && g->extra.pact_saved[i].at<=g->tick)pact_pulse(g,i,&g->extra.pact_saved[i]);
        if(live(u) && s->leap_at>0)leap_tick(g,i);
        if(live(u) && s->seeker_at>0 && s->seeker_at<=g->tick){int age=g->tick-s->seeker_at,x,y;
            x=s->seeker_x+(s->seeker_to_x-s->seeker_x)*cap(age,0,s->seeker_duration)/s->seeker_duration;y=s->seeker_y+(s->seeker_to_y-s->seeker_y)*cap(age,0,s->seeker_duration)/s->seeker_duration;
            dota_rules_set_position(g,i,x,y);if(age>=s->seeker_duration){dota_nav_nearest(x,y,256,&x,&y);dota_rules_set_position(g,i,x,y);s->seeker_at=0;if(u->hero_id==5){int t=s->shiv_target;if((unsigned)t<DOTA_MAX_UNITS && g->units[t].generation==s->shiv_generation && live(&g->units[t]))dota_items_apply_effect(g,i,t,IE_ETHEREAL,ticks(u,4,"ethereal_duration",50),value(u,4,"magic_resistance_reduction",25),DOTA_ITEM_AGHANIMS_SHARD);if(rank(u,0)>0)pulse_begin(g,i,rank(u,0));dota_rules_skill_event(g,i,0,-1,x,y,25,DOTA_SKILL_EVENT_CAST);}}
        }
        if(s->scythe_cast_at>0){int t=s->scythe_target;
            if(u->stun_ticks || !valid(g,t,s->scythe_generation) || !live(&g->units[t]))s->scythe_cast_at=0;
            else if(s->scythe_cast_at<=g->tick){s->scythe_cast_at=0;s->scythe_duration=duration(g,t,ticks(u,3,"stun_duration",30));s->scythe_at=g->tick+s->scythe_duration;if(!dota_items_debuff_immune(g,t))g->units[t].stun_ticks=cap(g->units[t].stun_ticks,s->scythe_duration,0x7fff);}
        }
        scythe_tick(g,i,&s->scythe_at,s->scythe_target,s->scythe_generation,s->scythe_rank);
        {DotaExtraScythe *saved=&g->extra.scythe_saved[i];scythe_tick(g,i,&saved->at,saved->target,saved->generation,saved->rank);}
        if(!live(u))continue;
        if(u->hero_id==4){int seen=dota_sight_team_position_visible(g,!u->team,u->x,u->y);
            if(seen)s->unseen_since=0;else if(!s->unseen_since)s->unseen_since=g->tick;
        }
        /* Saltwater Shiv's +/-flat regeneration is aggregated by the core
         * regen path via dota_extra_regen_flat100, including negative totals. */
    }
    if(g->tick%10==0)for(i=0;i<DOTA_MAX_UNITS;++i){DotaUnit *v=&g->units[i];int source=-1,power=0;
        if(!live(v) || v->type==DOTA_TOWER || v->type==DOTA_BASE || v->type==DOTA_BUILDING || v->type==DOTA_ANCIENT || v->type==DOTA_ROSHAN || v->type==DOTA_TORMENTOR){g->extra.heart_fraction[i]=0;continue;}
        /* Multiple copies of one aura choose the strongest source, just as
         * Arcane Aura does. Carry low-health targets' fractional raw damage
         * until four points avoid the core's one-damage minimum bias. */
        for(j=0;j<10;++j)if(live(&g->units[j]) && g->units[j].hero_id==5 && rank(&g->units[j],2)>0 && !dota_items_broken(g,j) && g->units[j].team!=v->team && within(v->x,v->y,g->units[j].x,g->units[j].y,dota_skill_effect_radius_source(g,j,2))){
            int candidate=value100(&g->units[j],2,"aura_damage",extra_heartstopper_per_mille[rank(&g->units[j],2)-1]*10)/10;if(candidate>power){power=candidate;source=j;}
        }
        if(source<0){g->extra.heart_fraction[i]=0;continue;}
        g->extra.heart_fraction[i]+=v->max_hp*power;
        if(dota_hero_has_scepter(g,source)){int regen=dota_rules_health_regen100(g,source);
            regen=dota_items_heal_amount(g,source,source,regen);
            g->extra.heart_fraction[i]+=regen*5;
        }
        if(g->extra.heart_fraction[i]>=8000){int amount=g->extra.heart_fraction[i]/2000;g->extra.heart_fraction[i]%=2000;dota_rules_damage(g,i,amount,source,5);}
    }
}
