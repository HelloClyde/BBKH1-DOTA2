#include "status_effects.h"
#include "hud_layout.h"
#include "bosses.h"
#include "items.h"
#include "extra_heroes.h"
#include "roster_heroes.h"
#include "progression.h"
#include "runes.h"
#include "progression.h"

static int alive(const DotaUnit *u){return u->active && u->hp>0;}
static int distance(int x,int y,int xx,int yy){int dx=x-xx,dy=y-yy;return dx*dx+dy*dy;}
static void add(DotaStatusEffect *out,int capacity,int *count,int id,int harmful,int ticks,int stacks){
    DotaStatusEffect *e;if(*count>=capacity)return;e=&out[(*count)++];
    e->id=id;e->harmful=harmful;e->ticks=ticks;e->stacks=stacks;
}
static void extra_statuses(const DotaGame *g,int h,DotaStatusEffect *out,int capacity,int *count){
    const DotaExtraHero *s=&g->extra.hero[h];const DotaUnit *u=&g->units[h];int i,gain=0,loss=0,sadist=0,shiv_gain=0,shiv_loss=0,shiv_gain_ticks=0,shiv_loss_ticks=0,depth=0;
    if(s->pact_at>0)add(out,capacity,count,DOTA_STATUS_PACT,0,u->skill_ticks[0],0);
    if(s->leash_until>g->tick && !dota_items_debuff_immune(g,h))add(out,capacity,count,DOTA_STATUS_LEASH,1,s->leash_until-g->tick,0);
    for(i=0;i<DOTA_EXTRA_STACKS;++i){const DotaExtraStack *v=&g->extra.stacks[i];
        if(v->source==h && v->source_generation==(unsigned short)u->generation){if(v->target==255){if(v->remaining>sadist)sadist=v->remaining;}else if(v->remaining>gain)gain=v->remaining;}
        if(v->target==h && v->target_generation==(unsigned short)u->generation && v->remaining>loss)loss=v->remaining;
    }
    if(s->essence_gain>0)add(out,capacity,count,DOTA_STATUS_ESSENCE_GAIN,0,gain,s->essence_gain);
    if(s->essence_loss>0)add(out,capacity,count,DOTA_STATUS_ESSENCE_LOSS,1,loss,s->essence_loss);
    if(s->permanent_agi>0)add(out,capacity,count,DOTA_STATUS_ESSENCE_PERMANENT,0,0,s->permanent_agi);
    if(s->sadist_stacks>0)add(out,capacity,count,DOTA_STATUS_SADIST,0,sadist,s->sadist_stacks);
    if(s->scythe_kills>0)add(out,capacity,count,DOTA_STATUS_SCYTHE_REGEN,0,0,s->scythe_kills);
    if(s->shadow_until>g->tick)add(out,capacity,count,DOTA_STATUS_SHADOW,0,s->shadow_until-g->tick,0);
    else if(u->hero_id==DOTA_HERO_SLARK && dota_progression_rank(u,3)>0 && !dota_items_broken(g,h) && s->unseen_since>0 && g->tick-s->unseen_since>=10 && s->neutral_until<=g->tick)add(out,capacity,count,DOTA_STATUS_SHADOW,0,0,0);
    if(s->ghost_until>g->tick)add(out,capacity,count,DOTA_STATUS_GHOST,0,s->ghost_until-g->tick,0);
    for(i=0;i<DOTA_EXTRA_SHIVS;++i){const DotaExtraShiv *v=&g->extra.shivs[i];if(v->until<=g->tick)continue;
        if(v->source==h && v->source_generation==(unsigned short)u->generation){++shiv_gain;if(v->until-g->tick>shiv_gain_ticks)shiv_gain_ticks=v->until-g->tick;}
        if(v->target==h && v->target_until>g->tick && !dota_items_debuff_immune(g,h) && v->target_generation==(unsigned short)u->generation){++shiv_loss;if(v->target_until-g->tick>shiv_loss_ticks)shiv_loss_ticks=v->target_until-g->tick;}
    }
    if(shiv_gain)add(out,capacity,count,DOTA_STATUS_SHIV_GAIN,0,shiv_gain_ticks,shiv_gain);
    if(shiv_loss)add(out,capacity,count,DOTA_STATUS_SHIV_LOSS,1,shiv_loss_ticks,shiv_loss);
    for(i=0;i<DOTA_HERO_COUNT;++i){const DotaUnit *v=&g->units[i];const DotaExtraHero *e=&g->extra.hero[i];
        if(v->team==u->team){if(e->shroud_until>g->tick && (long long)distance(u->x,u->y,e->shroud_x,e->shroud_y)*16<=(long long)e->shroud_radius*e->shroud_radius && e->shroud_until-g->tick>depth)depth=e->shroud_until-g->tick;continue;}
        if(e->scythe_at>g->tick && e->scythe_target==h && e->scythe_generation==u->generation && !dota_items_debuff_immune(g,h))add(out,capacity,count,DOTA_STATUS_REAPERS_SCYTHE,1,e->scythe_at-g->tick,0);
        {const DotaExtraScythe *saved=&g->extra.scythe_saved[i];if(saved->at>g->tick && saved->target==h && saved->generation==u->generation && !dota_items_debuff_immune(g,h))add(out,capacity,count,DOTA_STATUS_REAPERS_SCYTHE,1,saved->at-g->tick,0);}
        if(!alive(v) || v->type!=DOTA_HERO || v->hero_id!=DOTA_HERO_NECROPHOS)continue;
        {int radius=dota_skill_effect_radius_source(g,i,2);if(!dota_items_broken(g,i) && (long long)distance(u->x,u->y,v->x,v->y)*16<=(long long)radius*radius)add(out,capacity,count,DOTA_STATUS_HEARTSTOPPER,1,0,0);}
        {int radius=dota_skill_effect_radius_source(g,i,1);if(e->ghost_until>g->tick && !dota_items_debuff_immune(g,h) && (long long)distance(u->x,u->y,v->x,v->y)*16<=(long long)radius*radius)add(out,capacity,count,DOTA_STATUS_GHOST_SLOW,1,e->ghost_until-g->tick,0);}
    }
    if(depth)add(out,capacity,count,DOTA_STATUS_DEPTH_SHROUD,0,depth,0);
}
int dota_status_effects(const DotaGame *g,int hero,DotaStatusEffect *out,int capacity){
    const DotaUnit *u,*v;int count=0,i,source,ward,wards=0,ward_ticks=0,rate,best=0,age,remaining;
    if(!g || !out || capacity<=0 || (unsigned)hero>=DOTA_HERO_COUNT)return 0;
    u=&g->units[hero];if(u->type!=DOTA_HERO || !alive(u))return 0;
    if(capacity>DOTA_STATUS_MAX)capacity=DOTA_STATUS_MAX;
    for(i=0;i<DOTA_RUNE_COUNT;++i){int ticks=dota_rune_status(g,hero,i);if(ticks>0)add(out,capacity,&count,DOTA_STATUS_RUNE+i,0,ticks,i==DOTA_RUNE_SHIELD?g->runes.hero[hero].shield:0);}
    extra_statuses(g,hero,out,capacity,&count);
    {const DotaProgressionHero *p=&g->progression.hero[hero];
        if(p->generation==u->generation){
            if(u->hero_id==DOTA_HERO_AXE&&p->axe_strength100>0)add(out,capacity,&count,DOTA_STATUS_ROSTER+5,0,0,0);
            if(u->hero_id==DOTA_HERO_JUGGERNAUT&&p->blade_stacks>0)add(out,capacity,&count,DOTA_STATUS_ROSTER+2*8+5,0,p->blade_linger_until>g->tick?p->blade_linger_until-g->tick:0,p->blade_stacks);
            if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN&&p->glacial_barrier>0)add(out,capacity,&count,DOTA_STATUS_ROSTER+3*8+5,0,p->glacial_until-g->tick,0);
        }
        {int n=0,ticks=0,j;for(j=0;j<DOTA_PROGRESSION_BURNS;++j){const DotaProgressionBurn *b=&g->progression.burn[j];if(b->remaining>0&&b->target==hero&&b->target_generation==u->generation){++n;if(b->remaining>ticks)ticks=b->remaining;}}if(n)add(out,capacity,&count,DOTA_STATUS_ROSTER+8+5,1,ticks,n);}
    }
    {DotaRosterStatus roster[DOTA_STATUS_MAX];int n=dota_roster_statuses(g,hero,roster,DOTA_STATUS_MAX);for(i=0;i<n;++i)add(out,capacity,&count,DOTA_STATUS_ROSTER+roster[i].hero*8+roster[i].skill,roster[i].harmful,roster[i].ticks,roster[i].stacks);}
    {DotaItemStatus item_status[DOTA_STATUS_MAX];int n=dota_items_statuses(g,hero,item_status,DOTA_STATUS_MAX);for(i=0;i<n;++i)add(out,capacity,&count,DOTA_STATUS_ITEM_GENERIC+item_status[i].item,item_status[i].harmful,item_status[i].ticks,item_status[i].stacks);}
    remaining=dota_bosses_aegis_ticks(g,hero);if(remaining>0)add(out,capacity,&count,DOTA_STATUS_AEGIS,0,remaining,0);
    if(g->bosses.roar_disarm_ticks[hero]>0)add(out,capacity,&count,DOTA_STATUS_ROSHAN_DISARM,1,g->bosses.roar_disarm_ticks[hero],0);
    if(g->bosses.roar_vulnerable_ticks[hero]>0)add(out,capacity,&count,DOTA_STATUS_ROSHAN_VULNERABLE,1,g->bosses.roar_vulnerable_ticks[hero],0);
    if(u->stun_ticks>0)add(out,capacity,&count,DOTA_STATUS_STUN,1,u->stun_ticks,0);
    if(u->slow_ticks>0)add(out,capacity,&count,DOTA_STATUS_SLOW,1,u->slow_ticks,0);
    source=u->taunt_source;
    if(u->taunt_ticks>0 && (unsigned)source<DOTA_MAX_UNITS && alive(&g->units[source]))
        add(out,capacity,&count,DOTA_STATUS_TAUNT,1,u->taunt_ticks,0);
    if(u->dot_ticks>0){
        source=u->dot_source;
        i=(unsigned)source<DOTA_HERO_COUNT && g->units[source].type==DOTA_HERO
            && g->units[source].hero_id==DOTA_HERO_AXE && g->units[source].team!=u->team
            && g->hero_dot_source_generation[hero]==g->units[source].generation?DOTA_STATUS_HUNGER:DOTA_STATUS_DOT;
        add(out,capacity,&count,i,1,u->dot_ticks,0);
    }
    if(u->hero_id==DOTA_HERO_AXE && u->skill_ticks[0]>0)add(out,capacity,&count,DOTA_STATUS_CALL,0,u->skill_ticks[0],0);
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && u->skill_ticks[0]>0)add(out,capacity,&count,DOTA_STATUS_SPIN,0,u->skill_ticks[0],0);
    if(u->hero_id==DOTA_HERO_LINA && u->skill_ticks[2]>0 && g->passive_stacks[hero]>0 && g->passive_generation[hero]==u->generation)
        add(out,capacity,&count,DOTA_STATUS_FIERY,0,u->skill_ticks[2],g->passive_stacks[hero]>7?7:g->passive_stacks[hero]);
    if(dota_omnislash_phase(g,hero)==DOTA_OMNISLASH_ACTIVE && g->omnislash[hero].remaining_ticks>0)
        add(out,capacity,&count,DOTA_STATUS_OMNI,0,g->omnislash[hero].remaining_ticks,0);
    if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && u->channel_ticks>0 && u->skill_ticks[3]>0 && !u->stun_ticks)
        add(out,capacity,&count,DOTA_STATUS_FIELD,0,u->channel_ticks<u->skill_ticks[3]?u->channel_ticks:u->skill_ticks[3],0);
    if(u->heal_ticks>0)add(out,capacity,&count,DOTA_STATUS_FLASK,0,u->heal_ticks,0);
    for(i=0;i<DOTA_HERO_COUNT;++i){
        ward=dota_healing_ward(g,i);
        if(ward>=0 && g->units[ward].team==u->team &&
           (long long)distance(u->x,u->y,g->units[ward].x,g->units[ward].y)*16<=
           (long long)dota_healing_ward_radius_source()*dota_healing_ward_radius_source()){
            ++wards;remaining=g->summon_life[ward-DOTA_SUMMON_FIRST];if(remaining>ward_ticks)ward_ticks=remaining;
        }
        v=&g->units[i];
        if(u->max_mana>0 && alive(v) && v->type==DOTA_HERO && v->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && dota_passive_rank(v)>0 && v->team==u->team){
            rate=200+200*dota_passive_rank(v);if(distance(u->x,u->y,v->x,v->y)<=300*300)rate*=3;
            if(rate>best)best=rate;
        }
    }
    if(wards)add(out,capacity,&count,DOTA_STATUS_WARD,0,ward_ticks,wards>1?wards:0);
    if(best)add(out,capacity,&count,DOTA_STATUS_ARCANE,0,0,0);
    if(dota_hero_has_shard(g,hero))add(out,capacity,&count,DOTA_STATUS_SHARD,0,0,0);
    if((unsigned)u->team<2 && distance(u->x,u->y,dota_fountains[u->team].x,dota_fountains[u->team].y)<=DOTA_SHOP_RANGE*DOTA_SHOP_RANGE)
        add(out,capacity,&count,DOTA_STATUS_FOUNTAIN,0,0,0);
    v=&g->units[DOTA_TORMENTOR_INDEX];
    if(v->type==DOTA_TORMENTOR){
        age=g->tick-g->tormentor.last_damage_tick;
        if(alive(v) && age>=0 && age<10*DOTA_HZ && distance(u->x,u->y,v->x,v->y)<=250*250 && !dota_unit_invulnerable(g,hero))
            add(out,capacity,&count,DOTA_STATUS_TORMENTOR_BURN,1,10*DOTA_HZ-age,0);
        age=g->tick-(g->tormentor.spawn_tick-DOTA_TORMENTOR_RESPAWN);
        if(!alive(v) && v->respawn_ticks>0 && age>0 && age<=15*DOTA_HZ && distance(u->x,u->y,v->x,v->y)<=225*225)
            add(out,capacity,&count,DOTA_STATUS_TORMENTOR_HEAL,0,age==15*DOTA_HZ?1:15*DOTA_HZ-age,0);
    }
    return count;
}
const char *dota_status_name(int id){
    if(id>=DOTA_STATUS_RUNE && id<DOTA_STATUS_RUNE+DOTA_RUNE_COUNT)return dota_rune_name(id-DOTA_STATUS_RUNE);
    static const char *const roster_names[4][8]={{"肉钩","腐烂","肉盾","肢解","","血肉堆积","",""},{"榴霰弹","爆头","瞄准","暗杀","震荡手雷","精准射击","",""},{"风暴之拳","巨力挥舞","战吼","神之力量","飞行模式","神之愤怒","",""},{"巨浪","海妖外壳","锚击","毁灭","锚定","海兽盛宴","",""}};
    static const char *const names[]={"","眩晕","减速","嘲讽","持续伤害","战斗饥渴","狂战士之吼","剑刃风暴","炽魂","无敌斩","极寒领域","治疗药膏","治疗守卫","奥术光环","魔晶增幅","泉水恢复","魔方灼烧","镇痛","不朽之守护","肉山缴械","肉山易伤",
      "黑暗契约","突袭束缚","精华转移","精华流失","永久敏捷","海浪短刀窃取","海浪短刀削弱","暗影之舞","幽魂护罩","幽魂减速","竭心光环","施虐之心","镰刀永久恢复","深海护罩","死神镰刀"};
    if(id>=DOTA_STATUS_ROSTER){int code=id-DOTA_STATUS_ROSTER,hero=code/8;return hero>=0&&hero<10&&code%8==5?dota_progression_innate_name(hero):hero>=6&&hero<10?roster_names[hero-6][code%8]:"";}
    if(id>=DOTA_STATUS_ITEM_GENERIC)return "装备效果";
    return id>0 && (unsigned)id<sizeof(names)/sizeof(names[0])?names[id]:"";
}
int dota_status_hit(const DotaGame *g,int hero,int x,int y){
    DotaStatusEffect effects[DOTA_STATUS_MAX];int used[2]={0,0},count,i,group,left;
    if(y<DOTA_HUD_STATUS_Y || y>=DOTA_HUD_STATUS_Y+DOTA_HUD_STATUS_H)return 0;
    count=dota_status_effects(g,hero,effects,DOTA_STATUS_MAX);
    for(i=0;i<count;++i){
        group=effects[i].harmful?1:0;left=group?DOTA_HUD_DEBUFF_X:DOTA_HUD_BUFF_X;
        if(used[group]<DOTA_HUD_STATUS_SLOTS && dota_hud_contains(x,y,left+DOTA_HUD_STATUS_LABEL_W+used[group]*DOTA_HUD_STATUS_SLOT_W,DOTA_HUD_STATUS_Y,DOTA_HUD_STATUS_ICON_W,DOTA_HUD_STATUS_H))return effects[i].id;
        ++used[group];
    }
    if((used[0] && dota_hud_contains(x,y,DOTA_HUD_BUFF_X,DOTA_HUD_STATUS_Y,DOTA_HUD_STATUS_LABEL_W,DOTA_HUD_STATUS_H))
       || (used[1] && dota_hud_contains(x,y,DOTA_HUD_DEBUFF_X,DOTA_HUD_STATUS_Y,DOTA_HUD_STATUS_LABEL_W,DOTA_HUD_STATUS_H)))return -1;
    return 0;
}
