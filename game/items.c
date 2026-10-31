#include "items.h"
#include "items_ids.h"
#include "items_params.h"
#include "items_actions.h"
#include "extra_heroes.h"
#include "neutral.h"
#include "items_internal.h"
#include "items_special.h"
#include "roster_heroes.h"
#include "item_world.h"
#include "turbo.h"
#include "progression.h"
#include "lane_creep_data.h"
#include "teleport_rules.h"
#include "runes.h"
#define ITEM_RAW(g,h,s) (*((s)<6?&(g)->units[h].inventory[s]:&(g)->items.neutral_item[h]))
#define ITEM(g,h,s) dota_items_equipped_item(g,h,s)
/* Bounded integer rules. Provider failure rejects purchases/casts, never creates
 * a fake item. Source units are converted only at the world-distance boundary. */

static int imax(int a,int b){
    return a>b?a:b;
}
static int imin(int a,int b){
    return a<b?a:b;
}
static int valid(const DotaGame*g,int h){
    return h>=0&&h<DOTA_HERO_COUNT&&g->units[h].active&&g->units[h].type==DOTA_HERO;
}
static int live(const DotaGame*g,int h){
    return h>=0&&h<DOTA_MAX_UNITS&&g->units[h].active&&g->units[h].hp>0;
}
static int item_ranged(const DotaGame*g,int h){
    if(valid(g,h)){int id=g->units[h].hero_id;return id==DOTA_HERO_LINA||id==DOTA_HERO_CRYSTAL_MAIDEN||id==DOTA_HERO_NECROPHOS||id==DOTA_HERO_SNIPER;}
    return h>=0&&h<DOTA_MAX_UNITS&&g->units[h].range>200;
}
static int distance(int x,int y,int xx,int yy){
    int dx=x-xx,dy=y-yy;
    return dx*dx+dy*dy;
}
static int within(const DotaUnit*a,const DotaUnit*b,int source){
    int w=(source+3)/4;
    return distance(a->x,a->y,b->x,b->y)<=w*w;
}
static unsigned random_item(DotaGame*g){
    g->seed=g->seed*1664525u+1013904223u;
    return g->seed;
}
static int roll(DotaGame*g,int pct){
    return pct>=100||(pct>0&&(int)(random_item(g)%100u)<pct);
}
static int par(int id,int key,int fallback){
    return dota_items_parameter(id,key,-1,fallback*100)/100;
}
static int hpar(DotaGame*g,int h,int id,int key,int fallback){return dota_items_special_parameter(g,h,id,key,fallback*100)/100;}
static int hpx(DotaGame*g,int h,int id,int key,int fallback){return dota_items_special_parameter(g,h,id,key,fallback);}
static int hticks(DotaGame*g,int h,int id,int key,int fallback){return dota_items_special_parameter(g,h,id,key,fallback*5)/5;}
static int attribute(DotaGame*g,int h,int attr){
    DotaItemStats stats;int base[3],delta[3]={0,0,0},innate[3]={0,0,0};
    if(!valid(g,h)||(unsigned)attr>=3u)return 0;
    dota_progression_base_attributes100(&g->units[h],&base[0],&base[1],&base[2]);
    dota_items_stats(g,h,&stats);dota_extra_stat_delta(g,h,&delta[0],&delta[1],&delta[2]);dota_roster_stat_delta(g,h,&delta[0],&delta[1],&delta[2]);
    dota_progression_stat_delta100(g,h,&innate[0],&innate[1],&innate[2]);
    return (base[attr]+stats.value[ITEM_STAT_STRENGTH+attr]+innate[attr])/100+delta[attr];
}
static int ticks(int id,int key,int fallback){
    return dota_items_parameter(id,key,-1,fallback*5)/5;
}
static void zero(void*p,int n){
    unsigned char*b=(unsigned char*)p;
    while(n--)*b++=0;
}
int dota_items_equipped_item(const DotaGame*g,int h,int slot){
    if(!g||(unsigned)h>=10u||(unsigned)slot>=DOTA_ITEMS_EQUIP_SLOTS)return -1;
    if(slot<6)return g->items.inactive_ticks[h][slot]? -1:g->units[h].inventory[slot];
    return g->items.neutral_item[h];
}
static int item_slot(const DotaGame*g,int h,int id){
    int i;
    for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS;i++)if(ITEM(g,h,i)==id)return i;
    return -1;
}
static int has(const DotaGame*g,int h,int id){
    return valid(g,h)&&item_slot(g,h,id)>=0;
}
static void sync(DotaGame*g,int h){
    int s,id;
    DotaItemRecord r;
    DotaItemsState*st=&g->items;
    if(!valid(g,h))return;
    if(st->generation[h]!=g->units[h].generation){
        st->generation[h]=g->units[h].generation;
        
        st->inventory_hash[h]=0;
    }
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
        id=ITEM_RAW(g,h,s);
        if(st->tracked[h][s]!=id){
            st->tracked[h][s]=(int16_t)id;
            st->rapier_owner[h][s]=(int8_t)(id==DOTA_ITEM_RAPIER?h:-2);
            st->cooldown[h][s]=st->active_ticks[h][s]=st->uses[h][s]=0;
            st->proc_wait[h][s]=(uint16_t)(id==DOTA_ITEM_HAND_OF_MIDAS?ticks(id,IP_ABILITYCHARGERESTORETIME,1800):(id==DOTA_ITEM_AEON_DISK?ticks(id,IP_INITIAL_COOLDOWN,120):0));
            st->buy_age[h][s]=201;
            st->toggle_bits[h]&=(uint8_t)~(1u<<s);
            st->charges[h][s]=(id>=0&&dota_items_read(id,&r))?r.initial_charges:0;
            st->special.wrap_present[h][s]=st->special.wrap_charges[h][s]=0;st->special.wrap_wait[h][s]=0;st->special.water_cd_fraction[h][s]=0;
            if(id==DOTA_ITEM_RADIANCE)st->toggle_bits[h]|=(uint8_t)(1u<<s);
            if(s<6){st->inactive_ticks[h][s]=0;st->backpack_seen[h][s]=0;st->backpack_grace_until[h][s]=0;st->backpack_rate_phase[h][s]=0;}
            st->inventory_hash[h]=0;
        }
    }
}
void dota_items_init(DotaGame*g){
    int h,s;
    zero(&g->items,sizeof(g->items));
    g->items.last_tick=-1;
    for(h=0;h<10;h++){
        g->items.generation[h]=-1;
        for(s=0;s<3;s++){g->items.backpack[h][s].item=-1;g->items.backpack[h][s].rapier_owner=-2;}
        for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){g->items.tracked[h][s]=-2;g->items.rapier_owner[h][s]=-2;}
    }
    for(s=0;s<DOTA_ITEMS_STOCKS;s++)g->items.stocks[s].item=-1;
 dota_neutral_init(g);
 dota_items_special_init(g);
}
void dota_items_invalidate(DotaGame*g,int h){
    if(h>=0&&h<10)g->items.inventory_hash[h]=0;
}
static int compose_defense(DotaGame*g,int h,int stat,int total){
 int s,sum=0,remaining=10000,enchant=g->items.neutral.enchantment[h];DotaItemRecord r;
 for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS+1;s++){int id=s<DOTA_ITEMS_EQUIP_SLOTS?ITEM(g,h,s):enchant,value;
  if(id<0||!dota_items_read(id,&r))continue;
  value=r.stats[stat];
  if(stat==ITEM_STAT_MAGIC_RESIST&&id==DOTA_ITEM_METAMORPHIC_MANDIBLE)value=0;
  if(stat==ITEM_STAT_EVASION&&id==DOTA_ITEM_IDOL_OF_SCREEAUK)value=0;
  if(stat==ITEM_STAT_STATUS_RESIST&&id==DOTA_ITEM_AEON_DISK&&!dota_items_effect_value(g,h,IE_AEON))value=0;
  if(s==DOTA_ITEMS_EQUIP_SLOTS){int key=stat==ITEM_STAT_MAGIC_RESIST?IP_MAGIC_RES:(stat==ITEM_STAT_EVASION?IP_EVASION:IP_STATUS_RESISTANCE);value=hpx(g,h,id,key,value);}
  sum+=value;remaining=remaining*(10000-imin(10000,imax(0,value)))/10000;
 }
 return 10000-remaining*(10000-imin(10000,total-sum))/10000;
}
static unsigned hash_inventory(const DotaGame*g,int h){
    unsigned v=2166136261u;
    int s;
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++)v=(v^(unsigned)(ITEM(g,h,s)+2))*16777619u;
    v=(v^(unsigned)(g->items.neutral.enchantment[h]+2))*16777619u;
 return v?v:1;
}
void dota_items_remove_effect(DotaGame*g,int index){
    if((unsigned)index>=DOTA_ITEMS_EFFECTS)return;
    g->items.effects[index].ticks=0;
    g->items.effect_active[(unsigned)index/32]&=~(1u<<((unsigned)index%32));
}
static void effect(DotaGame*g,int source,int target,int kind,int duration,int value,int tag){
    int i,free=-1;
    DotaItemEffect*e;
    if(!live(g,target)||duration<=0)return;
    if(source>=0&&source<DOTA_MAX_UNITS&&g->units[source].team!=g->units[target].team&&dota_items_debuff_immune(g,target)){
        if(kind==IE_SILENCE||kind==IE_ROOT||kind==IE_DISARM||kind==IE_CYCLONE||kind==IE_ETHEREAL||kind==IE_SLOW||kind==IE_DECAY_SLOW||kind==IE_NULLIFY||kind==IE_HEAL_REDUCTION||kind==IE_VULNERABLE||kind==IE_BREAK||((kind==IE_AS||kind==IE_ARMOR||kind==IE_DAMAGE_AMP)&&value<0))return;
    }
    if(source>=0&&source<10&&g->units[source].team!=g->units[target].team&&duration>22&&(kind==IE_SILENCE||kind==IE_ROOT||kind==IE_DISARM||kind==IE_SLOW||kind==IE_DECAY_SLOW||kind==IE_DOT||kind==IE_BATTLE_HUNGER||kind==IE_PERCENT_DOT||kind==IE_HP_DRAIN||kind==IE_BREAK||kind==IE_ETHEREAL||kind==IE_NULLIFY||((kind==IE_ARMOR||kind==IE_AS)&&value<0))){duration=duration*dota_items_special_debuff_percent(g,source)/100;duration=imax(1,duration*(100-dota_items_status_resist_percent(g,target))/100);}
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        e=&g->items.effects[i];
        if(e->ticks&&e->target==target&&e->source==source&&e->kind==kind&&e->tag==tag&&kind!=IE_BATTLE_HUNGER){
            free=i;
            break;
        }
        if(!e->ticks&&free<0)free=i;
    }
    if(free<0){
        /* Do not silently overwrite a different actor's timed effect. */return;
    }
    e=&g->items.effects[free];
    e->generation=g->units[target].generation;
    e->ticks=(uint16_t)imin(duration,65535);
    e->interval=kind==IE_DECAY_SLOW?(uint16_t)duration:0;
    e->value=value;
    e->source=(uint8_t)source;
    e->target=(uint8_t)target;
    e->kind=(uint8_t)kind;
    e->tag=(uint16_t)tag;
    e->pad=0;
    g->items.effect_active[(unsigned)free/32]|=1u<<((unsigned)free%32);
}
void dota_items_clear_hunger(DotaGame*g,int target){
    int i;if(!g||(unsigned)target>=DOTA_MAX_UNITS)return;
    if(g->units[target].dot_ticks&&(unsigned)g->units[target].dot_source<DOTA_HERO_COUNT&&g->units[g->units[target].dot_source].hero_id==DOTA_HERO_AXE){g->units[target].dot_ticks=g->units[target].dot_damage=g->units[target].slow_ticks=g->units[target].buff_ticks=0;}
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++)if(g->items.effects[i].ticks&&g->items.effects[i].target==target&&(g->items.effects[i].kind==IE_BATTLE_HUNGER||(g->items.effects[i].kind==IE_SLOW&&g->items.effects[i].tag==DOTA_ITEM_AGHANIMS_SHARD&&g->items.effects[i].source<DOTA_HERO_COUNT&&g->units[g->items.effects[i].source].hero_id==DOTA_HERO_AXE)))dota_items_remove_effect(g,i);
}
void dota_items_battle_hunger(DotaGame*g,int source,int target,int duration,int dps100,int slow,int stack){
    int i;if(!g||!live(g,target)||duration<=0||dota_items_debuff_immune(g,target))return;
    if(!stack)for(i=0;i<DOTA_ITEMS_EFFECTS;i++)if(g->items.effects[i].ticks&&g->items.effects[i].source==source&&g->items.effects[i].target==target&&g->items.effects[i].kind==IE_BATTLE_HUNGER)dota_items_remove_effect(g,i);
    effect(g,source,target,IE_BATTLE_HUNGER,duration,dps100,DOTA_ITEM_AGHANIMS_SHARD);
    effect(g,source,target,IE_SLOW,duration,slow,DOTA_ITEM_AGHANIMS_SHARD);
}
static int estatus(const DotaGame*g,int h,int kind){
    int word,v=0;
    const DotaItemEffect*e;
    if(h<0||h>=DOTA_MAX_UNITS)return 0;
    /* Sparse active slots also make the common empty pool four reads. The mask
     * changes with every allocation/cancellation, including within one tick. */
    for(word=0;word<DOTA_ITEMS_EFFECT_WORDS;word++){
        uint32_t active=g->items.effect_active[word];
        int index=word*32;
        while(active){
            if(!(active&1u)){active>>=1;index++;continue;}
            e=&g->items.effects[index];
            if(kind==IE_SLOW&&e->ticks&&e->target==h&&e->kind==IE_INVISIBLE&&(e->tag&32767)==DOTA_ITEM_SHADOW_AMULET&&e->generation==g->units[h].generation)v=imax(v,hpar((DotaGame*)g,e->source,DOTA_ITEM_SHADOW_AMULET,IP_MOVEMENT_SPEED_REDUCTION,35));
            if(e->ticks&&e->target==h&&(e->kind==kind||(kind==IE_SLOW&&e->kind==IE_DECAY_SLOW))&&e->generation==g->units[h].generation){
                if(kind==IE_ARMOR||kind==IE_AS||kind==IE_DAMAGE||kind==IE_MAXHP||kind==IE_DAMAGE_AMP)v+=e->value;
                else if(e->kind==IE_DECAY_SLOW)v=imax(v,e->value*e->ticks/imax(1,e->interval));
                else v=imax(v,e->value?e->value:1);
            }
            active>>=1;index++;
        }
    }
    return v;
}
static void purge(DotaGame*g,int h,int positive){
    int i;
    DotaItemEffect*e;
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        e=&g->items.effects[i];
        if(e->target==h&&e->ticks){
            if(e->kind==IE_IMMUNITY||e->kind==IE_AEON||e->kind==IE_NULLIFY||e->kind==IE_PIKE)continue;
            int bad=e->kind==IE_SILENCE||e->kind==IE_ROOT||e->kind==IE_DISARM||e->kind==IE_SLOW||e->kind==IE_DECAY_SLOW||e->kind==IE_DOT||e->kind==IE_BATTLE_HUNGER||e->kind==IE_PERCENT_DOT||e->kind==IE_HP_DRAIN||e->kind==IE_BREAK||e->kind==IE_REGEN_STEAL||e->kind==IE_HEAL_REDUCTION||e->kind==IE_VULNERABLE||(e->kind==IE_ETHEREAL&&e->source<DOTA_MAX_UNITS&&g->units[e->source].team!=g->units[h].team);
            if(bad!=!!positive){
                dota_items_remove_effect(g,i);
                if(e->kind==IE_CYCLONE&&(e->tag&32767)==DOTA_ITEM_WIND_WAKER&&e->source==h)dota_rules_item_finish_free_path(g,h);
            }
        }
    }
    if(!positive){
        g->units[h].slow_ticks=g->units[h].dot_ticks=0;
    }
    dota_extra_dispel(g,h,positive);
    dota_roster_dispel(g,h,positive,0);
    dota_item_world_purge(g,h,positive);
    dota_items_special_dispel(g,h,positive);
}
int dota_items_regen_percent(const DotaGame*g,int h){return imax(0,100-estatus(g,h,IE_REGEN_STEAL));}
int dota_items_timed_regen100(const DotaGame*g,int h){int i,n=0;if(!live(g,h))return 0;for(i=0;i<DOTA_ITEMS_EFFECTS;i++){const DotaItemEffect*e=&g->items.effects[i];if(e->ticks&&e->target==h&&e->generation==g->units[h].generation&&e->kind==IE_REGEN_HP&&(e->tag&32767)!=DOTA_ITEM_BOTTLE)n+=e->value;}return n;}
int dota_items_timed_mana_regen100(const DotaGame*g,int h){int i,n=0;if(!live(g,h))return 0;for(i=0;i<DOTA_ITEMS_EFFECTS;i++){const DotaItemEffect*e=&g->items.effects[i];if(e->ticks&&e->target==h&&e->generation==g->units[h].generation&&e->kind==IE_REGEN_MP&&(e->tag&32767)==DOTA_ITEM_CLARITY)n+=e->value;}return n;}
int dota_items_heal_amount_with_stats(DotaGame*g,int source,int h,int amount,const DotaItemStats*stats){
    DotaItemStats queried;
    int cut=estatus(g,h,IE_HEAL_REDUCTION);
    if(amount<=0)return amount;
    if(valid(g,h)){
        if(!stats){dota_items_stats(g,h,&queried);stats=&queried;}
        amount=amount*(100+stats->value[ITEM_STAT_HEAL_AMP]/100)/100;
    }
    if(has(g,source,DOTA_ITEM_HOLY_LOCKET))amount=amount*(100+hpar(g,source,DOTA_ITEM_HOLY_LOCKET,IP_HEAL_INCREASE_PASSIVE,10))/100;
    amount=amount*(100+estatus(g,h,IE_INCOMING_HEAL_AMP))/100;
    amount=amount*imax(0,100-cut)/100;
    return dota_extra_heal_amount(g,h,amount)*dota_roster_heal_percent(g,h)/100;
}
int dota_items_heal_amount(DotaGame*g,int source,int h,int amount){return dota_items_heal_amount_with_stats(g,source,h,amount,0);}
static void heal(DotaGame*g,int source,int h,int hp,int mp){
    if(!live(g,h))return;
    if(hp>0)hp=dota_items_heal_amount(g,source,h,hp);
    g->units[h].hp=imin(g->units[h].max_hp,g->units[h].hp+hp);
    if(mp>0&&h<10)dota_extra_mana_restore(g,h,mp);else g->units[h].mana=imin(g->units[h].max_mana,g->units[h].mana+mp);
    if(hp>0)dota_rules_item_effect(g,DOTA_FX_HEAL,g->units[source].team,g->units[h].x,g->units[h].y,g->units[h].x,g->units[h].y,12,8);
}
int dota_items_silenced(const DotaGame*g,int h){
    return estatus(g,h,IE_SILENCE)||estatus(g,h,IE_CYCLONE);
}
int dota_items_disarmed(const DotaGame*g,int h){
    return estatus(g,h,IE_DISARM)||estatus(g,h,IE_GHOST)||estatus(g,h,IE_ETHEREAL)||estatus(g,h,IE_CYCLONE);
}
int dota_items_invulnerable(const DotaGame*g,int h){
    return estatus(g,h,IE_CYCLONE);
}
int dota_items_debuff_immune(const DotaGame*g,int h){return estatus(g,h,IE_IMMUNITY)!=0;}
int dota_items_free_path(const DotaGame*g,int h){int s;if(!valid(g,h))return 0;for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++)if(ITEM(g,h,s)==DOTA_ITEM_SPIDER_LEGS&&g->items.active_ticks[h][s])return 1;return 0;}
int dota_items_wind_move_source(const DotaGame*g,int h){
 int word;if(!live(g,h))return 0;
 for(word=0;word<DOTA_ITEMS_EFFECT_WORDS;word++){
  uint32_t active=g->items.effect_active[word];
  while(active){
   int i=word*32+(int)__builtin_ctz(active);const DotaItemEffect*e=&g->items.effects[i];active&=active-1;
   if(e->ticks&&e->target==h&&e->source==h&&e->generation==g->units[h].generation&&e->kind==IE_CYCLONE&&(e->tag&32767)==DOTA_ITEM_WIND_WAKER)return hpar((DotaGame*)g,h,DOTA_ITEM_WIND_WAKER,IP_TORNADO_SPEED,300);
  }
 }
 return 0;
}
int dota_items_rooted(const DotaGame*g,int h){
    return estatus(g,h,IE_ROOT)||estatus(g,h,IE_CYCLONE)||dota_item_world_rooted(g,h);
}
int dota_items_broken(const DotaGame*g,int h){
    return estatus(g,h,IE_BREAK)!=0;
}
void dota_items_purge(DotaGame*g,int h,int strong){
    purge(g,h,0);
    if(strong){
        g->units[h].stun_ticks=g->units[h].taunt_ticks=0;
        dota_roster_dispel(g,h,0,1);
    }
}
void dota_items_dispel(DotaGame*g,int h,int positive){if(live(g,h))purge(g,h,!!positive);}
int dota_items_invisible(const DotaGame*g,int h){
    return (estatus(g,h,IE_INVISIBLE)||dota_rune_invisible(g,h))&&!estatus(g,h,IE_REVEAL);
}
int dota_items_status_percent(const DotaGame*g,int h,int stat){
    switch(stat){
        case ITEM_STAT_MOVE_PERCENT:return (estatus(g,h,IE_HASTE)-estatus(g,h,IE_SLOW))*100;
        case ITEM_STAT_ARMOR:return estatus(g,h,IE_ARMOR)*100;
        case ITEM_STAT_ATTACK_SPEED:{int word,base=estatus(g,h,IE_AS),n=base;if(h<10)for(word=0;word<DOTA_ITEMS_EFFECT_WORDS;word++){
            uint32_t active=g->items.effect_active[word];int index=word*32;
            while(active){if(active&1u){const DotaItemEffect*e=&g->items.effects[index];
                if(e->ticks&&e->kind==IE_PIKE&&e->source==h&&e->value>0&&e->target<DOTA_MAX_UNITS&&g->units[h].target==e->target&&e->generation==g->units[e->target].generation)n=imax(n,base+100);
            }active>>=1;index++;}
        }return n;}
        case ITEM_STAT_AGILITY:return estatus(g,h,IE_AGILITY)*100;
        case ITEM_STAT_DAMAGE:return estatus(g,h,IE_DAMAGE);
        case ITEM_STAT_HEALTH:return estatus(g,h,IE_MAXHP);
        case ITEM_STAT_EVASION:return estatus(g,h,IE_EVASION)*100;
        default:return 0;
    }
}
/* The stronger Greaves aura follows the holder's actual current health.
 * Keeping this query outside the1-second aura cache makes crossing25% take
 * effect immediately, and multiple Greaves still supply only one aura. */
static int greaves_regen100(DotaGame*g,int target){
    int h,best=0;
    for(h=0;h<DOTA_HERO_COUNT;h++)if(live(g,h)&&g->units[h].team==g->units[target].team&&has(g,h,DOTA_ITEM_GUARDIAN_GREAVES)&&within(&g->units[h],&g->units[target],hpar(g,h,DOTA_ITEM_GUARDIAN_GREAVES,IP_AURA_RADIUS,1200))){
        int rate=hpx(g,h,DOTA_ITEM_GUARDIAN_GREAVES,IP_AURA_HEALTH_REGEN,250);
        if((long long)g->units[h].hp*100<(long long)g->units[h].max_hp*hpar(g,h,DOTA_ITEM_GUARDIAN_GREAVES,IP_AURA_BONUS_THRESHOLD,25))rate=hpx(g,h,DOTA_ITEM_GUARDIAN_GREAVES,IP_AURA_HEALTH_REGEN_BONUS,1450);
        best=imax(best,rate);
    }
    return best;
}
void dota_items_stats(DotaGame*g,int h,DotaItemStats*out){
    int s,k,boots=0,rapier_amp=0;
    unsigned hash;
    DotaItemRecord r;
    DotaItemsState*st=&g->items;
    zero(out,sizeof(*out));
    if(!valid(g,h)){
        for(k=0;k<ITEM_STAT_COUNT;k++)out->value[k]=dota_items_status_percent(g,h,k);
        dota_item_world_apply_stats(g,h,out);
        return;
    }
    sync(g,h);
    hash=hash_inventory(g,h);
    if(st->inventory_hash[h]!=hash){
        zero(&st->cache[h],sizeof(st->cache[h]));
        for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
            int id=ITEM(g,h,s);
            if(id<0||!dota_items_read(id,&r))continue;
            for(k=0;k<ITEM_STAT_COUNT;k++){
                if(k==ITEM_STAT_MOVE_FLAT&&(r.flags&ITEM_F_BOOTS)){
                    boots=imax(boots,r.stats[k]);
                    continue;
                }
                if(k==ITEM_STAT_ATTACK_RANGE){
                    if(item_ranged(g,h))st->cache[h].value[k]=imax(st->cache[h].value[k],r.stats[k]);
                    continue;
                }
                if(k==ITEM_STAT_CAST_RANGE){st->cache[h].value[k]=imax(st->cache[h].value[k],r.stats[k]);continue;}
                st->cache[h].value[k]+=r.stats[k];
            }
            st->cache[h].passive_flags|=r.passive_flags;
        }
        {int enchant=g->items.neutral.enchantment[h];if(enchant>=0&&dota_items_read(enchant,&r))for(k=0;k<ITEM_STAT_COUNT;k++)st->cache[h].value[k]+=r.stats[k];}
 st->cache[h].value[ITEM_STAT_MOVE_FLAT]+=boots;
        st->inventory_hash[h]=hash;
    }
    *out=st->cache[h];
    for(k=0;k<ITEM_STAT_COUNT;k++)out->value[k]+=dota_progression_stat_bonus(&g->units[h],k);
    for(k=0;k<ITEM_STAT_COUNT;k++)out->value[k]+=st->aura[h].value[k];
    out->value[ITEM_STAT_HEALTH_REGEN]+=greaves_regen100(g,h);
 {int enchant=g->items.neutral.enchantment[h],primary=ITEM_STAT_STRENGTH+dota_progression_primary(g->units[h].hero_id);if(enchant>=0)out->value[primary]+=par(enchant,IP_PRIMARY_STAT,0)*100;}
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
        int id=ITEM(g,h,s);
        int on=(st->toggle_bits[h]>>s)&1;
        if(id>=0)out->value[ITEM_STAT_HEAL_AMP]-=hpar(g,h,id,IP_HP_REGEN_AMP,0)*100;
        /* Locket's passive amplifies healing supplied by its holder; it is
         * not incoming restoration amplification on a Locket-owning patient. */
        if(id==DOTA_ITEM_HOLY_LOCKET)out->value[ITEM_STAT_HEAL_AMP]-=hpx(g,h,id,IP_HEAL_INCREASE_PASSIVE,1000);
        if(id==DOTA_ITEM_POWER_TREADS){
            int attr=st->uses[h][s]%3;
            out->value[ITEM_STAT_STRENGTH+attr]+=1000;
            if(item_ranged(g,h))out->value[ITEM_STAT_MOVE_FLAT]-=10;
        }
        if(id==DOTA_ITEM_ARMLET&&on){
            out->value[ITEM_STAT_DAMAGE]+=35;
            out->value[ITEM_STAT_STRENGTH]+=2500;
            out->value[ITEM_STAT_ARMOR]+=400;
        }
        if(id==DOTA_ITEM_RAPIER){
            if(st->rapier_owner[h][s]>=0&&st->rapier_owner[h][s]!=h){out->value[ITEM_STAT_DAMAGE]-=100;continue;}
            if(on){if(!rapier_amp){out->value[ITEM_STAT_SPELL_AMP]+=2500;rapier_amp=1;}}
            else out->value[ITEM_STAT_DAMAGE]+=250;
        }
        if(id==DOTA_ITEM_DESOLATOR)out->value[ITEM_STAT_DAMAGE]+=st->uses[h][s];
        if(id==DOTA_ITEM_PHASE_BOOTS)out->value[ITEM_STAT_DAMAGE]+=item_ranged(g,h)?12:18;
        if(id==DOTA_ITEM_MONKEY_KING_BAR&&!item_ranged(g,h)&&item_slot(g,h,id)==s)out->value[ITEM_STAT_ATTACK_RANGE]+=hpar(g,h,id,IP_MELEE_ATTACK_RANGE,50);
        if(id==DOTA_ITEM_HEART)out->value[ITEM_STAT_HEALTH_REGEN]+=g->units[h].max_hp+(g->units[h].max_hp-g->units[h].hp)*3/2;
        if(id==DOTA_ITEM_STONEFEATHER_SATCHEL){
            if(on)out->value[ITEM_STAT_MOVE_PERCENT]+=1200;
            else out->value[ITEM_STAT_ARMOR]+=300;
        }
        if(id==DOTA_ITEM_HARMONIZER){
            int idle=0;
            for(k=0;k<4;k++)if(!g->units[h].cooldown[k]&&!dota_skill_is_passive(g->units[h].hero_id,k))idle++;
            out->value[ITEM_STAT_SPELL_AMP]+=idle*600;
        }
        if(id==DOTA_ITEM_OCTARINE_CORE&&item_slot(g,h,id)==s)out->value[ITEM_STAT_COOLDOWN_REDUCTION]=10000-(10000-out->value[ITEM_STAT_COOLDOWN_REDUCTION])*(100-hpar(g,h,id,IP_BONUS_COOLDOWN,25))/100;
        if(id==DOTA_ITEM_AEON_DISK&&!estatus(g,h,IE_AEON))out->value[ITEM_STAT_STATUS_RESIST]-=7500;
        if(id==DOTA_ITEM_TRANQUIL_BOOTS&&st->proc_wait[h][s]){out->value[ITEM_STAT_MOVE_FLAT]-=25;out->value[ITEM_STAT_HEALTH_REGEN]-=1400;}
    }
    if(st->permanent[h]&1u)out->value[ITEM_STAT_ATTACK_SPEED]+=60;
    for(k=0;k<ITEM_STAT_COUNT;k++)out->value[k]+=dota_items_status_percent(g,h,k);
    dota_items_special_stats(g,h,out);
    dota_item_world_apply_stats(g,h,out);
    out->value[ITEM_STAT_MAGIC_RESIST]=compose_defense(g,h,ITEM_STAT_MAGIC_RESIST,out->value[ITEM_STAT_MAGIC_RESIST]);
    out->value[ITEM_STAT_EVASION]=compose_defense(g,h,ITEM_STAT_EVASION,out->value[ITEM_STAT_EVASION]);
    out->value[ITEM_STAT_STATUS_RESIST]=compose_defense(g,h,ITEM_STAT_STATUS_RESIST,out->value[ITEM_STAT_STATUS_RESIST]);
    out->value[ITEM_STAT_HEALTH]+=out->value[ITEM_STAT_STRENGTH]*22/100;
    out->value[ITEM_STAT_MANA]+=out->value[ITEM_STAT_INTELLECT]*12/100;
    out->value[ITEM_STAT_ATTACK_SPEED]+=out->value[ITEM_STAT_AGILITY]/100;
    out->value[ITEM_STAT_ARMOR]+=out->value[ITEM_STAT_AGILITY]/6;
    out->value[ITEM_STAT_HEALTH_REGEN]+=out->value[ITEM_STAT_STRENGTH]/10;
    out->value[ITEM_STAT_MANA_REGEN]+=out->value[ITEM_STAT_INTELLECT]/20;
}
int dota_items_cooldown_percent(DotaGame*g,int h){
    DotaItemStats s;
    dota_items_stats(g,h,&s);
    return 100-(100-imin(75,s.value[ITEM_STAT_COOLDOWN_REDUCTION]/100))*dota_rune_cooldown_percent(g,h)/100;
}
int dota_items_range_bonus(DotaGame*g,int h,int attack){
    DotaItemStats s;
    dota_items_stats(g,h,&s);
    return s.value[attack?ITEM_STAT_ATTACK_RANGE:ITEM_STAT_CAST_RANGE];
}
int dota_items_spell_amp_percent(DotaGame*g,int h){
    DotaItemStats s;
    dota_items_stats(g,h,&s);
    return s.value[ITEM_STAT_SPELL_AMP]/100+dota_rune_spell_amp_percent(g,h);
}
int dota_items_status_resist_percent(DotaGame*g,int h){DotaItemStats st;dota_items_stats(g,h,&st);return imin(95,imax(0,st.value[ITEM_STAT_STATUS_RESIST]/100));}
void dota_items_on_target_spell(DotaGame*g,int h,int target){
    int s,best=-1,bonus=0,id;DotaItemRecord r;
    if(!valid(g,h)||!live(g,target)||g->units[h].team==g->units[target].team)return;
    sync(g,h);
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){id=ITEM(g,h,s);if((id==DOTA_ITEM_PHYLACTERY||id==DOTA_ITEM_ANGELS_DEMISE)&&!g->items.proc_wait[h][s]){int amount=hpar(g,h,id,IP_BONUS_SPELL_DAMAGE,150);if(amount>bonus){best=s;bonus=amount;}}}
    if(best<0)return;
    id=ITEM(g,h,best);dota_items_read(id,&r);
    effect(g,h,target,IE_SLOW,hticks(g,h,id,IP_SLOW_DURATION,60),hpar(g,h,id,IP_SLOW,30),id);
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++)if(ITEM(g,h,s)==DOTA_ITEM_PHYLACTERY||ITEM(g,h,s)==DOTA_ITEM_ANGELS_DEMISE)g->items.proc_wait[h][s]=r.cooldown_ticks;
    dota_rules_item_damage(g,h,target,bonus,1);
}
int dota_items_mana_cost_percent(DotaGame*g,int h){
    int s,pct=0;
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
        int id=ITEM(g,h,s);
        if(id>=0)pct=imax(pct,par(id,IP_MANACOST_REDUCTION,par(id,IP_MANA_REDUCTION_PCT,0)));
        if(id==DOTA_ITEM_HARMONIZER){
            int k;
            for(k=0;k<4;k++)if(g->units[h].cooldown[k]&&!dota_skill_is_passive(g->units[h].hero_id,k))pct+=7;
        }
    }
    {int enchant=g->items.neutral.enchantment[h];if(enchant>=0)pct+=hpar(g,h,enchant,IP_MANACOST_REDUCTION,hpar(g,h,enchant,IP_MANA_REDUCTION_PCT,0));}
    return imax(10,100-pct)*dota_rune_mana_percent(g,h)/100;
}
int dota_items_mana_regen_percent(DotaGame*g,int h){int s,pct=0;if(!valid(g,h))return 100;for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){int id=ITEM(g,h,s);if(id>=0)pct=imax(pct,hpar(g,h,id,IP_MANA_REGEN_MULTIPLIER,0));}return 100+pct;}
int dota_items_health_regen_amp_percent(DotaGame*g,int h){int s,pct=0;if(!valid(g,h))return 100;for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){int id=ITEM(g,h,s);if(id>=0)pct=imax(pct,hpar(g,h,id,IP_HP_REGEN_AMP,0));}return 100+pct;}
static int stock_count(const DotaGame*g,int team,int id,DotaItemRecord*r){
    int i,count;
    const DotaItemStock*s;
    int elapsed;
    if(!r->stock_max)return 9999;
    for(i=0;i<DOTA_ITEMS_STOCKS;i++){
        s=&g->items.stocks[i];
        if(s->item==id&&s->team==team){
            count=s->count;
            elapsed=g->tick-s->last_tick;
            if(r->stock_ticks&&elapsed>0)count+=elapsed/r->stock_ticks;
            return imin(r->stock_max,count);
        }
    }
    if(g->tick<r->stock_start_ticks)return 0;
    count=r->stock_initial;
    if(r->stock_ticks)count+=(g->tick-r->stock_start_ticks)/r->stock_ticks;
    return imin(r->stock_max,count);
}
typedef struct {
    int owned[9+DOTA_COURIER_SLOTS+DOTA_COURIER_STASH_SLOTS],n;
    DotaItemPurchasePlan*p;
    int failed;
}
Planner;
static int plan_need(Planner*p,int id,int depth,int take){
    int i,found=0,saw_recipe=0;
    Planner best;DotaItemPurchasePlan best_plan;
    DotaItemRecord r;
    DotaItemRecipe recipe;
    if(depth>16||id<0||!dota_items_read(id,&r))return 0;
    if(take)for(i=0;i<p->n;i++)if(p->owned[i]==id){
        p->owned[i]=-1;
        return 1;
    }
    if((int)r.recipe_first+r.recipe_count>dota_items_recipes_count())return 0;
    for(i=r.recipe_first;i<(int)r.recipe_first+r.recipe_count;i++)if(dota_items_recipe_read(i,&recipe)&&recipe.result==id&&recipe.count){
        int j,ok=1;Planner trial=*p;DotaItemPurchasePlan trial_plan=*p->p;trial.p=&trial_plan;saw_recipe=1;
        for(j=0;j<recipe.count;j++)if(!plan_need(&trial,recipe.component[j],depth+1,1)){ok=0;break;}
        if(!ok)continue;
        if(!(r.flags&ITEM_F_RECIPE)){
            DotaItemRecord rr;
            if(!dota_items_read(recipe.recipe,&rr))continue;
            if(!(rr.flags&ITEM_F_AUTO_RECIPE)&&!plan_need(&trial,recipe.recipe,depth+1,1))continue;
        }
        if(!found||trial_plan.cost<best_plan.cost||(trial_plan.cost==best_plan.cost&&trial_plan.count<best_plan.count)){best=trial;best_plan=trial_plan;found=1;}
    }
    if(saw_recipe){if(found){DotaItemPurchasePlan*output=p->p;*p=best;p->p=output;*output=best_plan;}return found;}
    if(!(r.flags&ITEM_F_BUY)||p->p->count>=DOTA_ITEMS_MAX_PLAN)return 0;
    p->p->missing[p->p->count++]=(int16_t)id;
    p->p->cost+=r.cost;
    return 1;
}
int dota_items_purchase_plan(const DotaGame*g,int h,int id,DotaItemPurchasePlan*plan){
    Planner p;
    DotaItemRecord r;
    int i,j;
    if(!valid(g,h)||!dota_items_read(id,&r)||!(r.flags&ITEM_F_BUY))return 0;
    if(id==DOTA_ITEM_AGHANIMS_SHARD&&dota_hero_has_shard(g,h))return 0;
    zero(plan,sizeof(*plan));
    zero(&p,sizeof(p));
    plan->item=id;
    p.p=plan;
    for(i=0;i<9;i++)p.owned[p.n++]=dota_inventory_item(g,h,i);
    for(i=0;i<DOTA_COURIER_SLOTS;i++)p.owned[p.n++]=g->couriers.hero[h].cargo[i];
    for(i=0;i<DOTA_COURIER_STASH_SLOTS;i++)p.owned[p.n++]=g->couriers.hero[h].stash[i];
    for(i=9;i<p.n;i++)if(p.owned[i]==id)return 1;
    /* already coming, no duplicate charge */
    if(!plan_need(&p,id,0,0))return 0;
    for(i=0;i<plan->count;i++){
        int need=1;
        if(!dota_items_read(plan->missing[i],&r))return 0;
        for(j=0;j<i;j++)if(plan->missing[j]==plan->missing[i])need++;
        if(stock_count(g,g->units[h].team,plan->missing[i],&r)<need)return 0;
    }
    return 1;
}
int dota_items_purchase_commit(DotaGame*g,int h,const DotaItemPurchasePlan*p){
    int i,j,free;
    DotaItemRecord r;
    DotaItemStock*s;
    if(!valid(g,h)||p->count<0||p->count>DOTA_ITEMS_MAX_PLAN)return 0;
    for(i=0;i<p->count;i++){
        int need=1;
        if(!dota_items_read(p->missing[i],&r)||!(r.flags&ITEM_F_BUY))return 0;
        for(j=0;j<i;j++)if(p->missing[j]==p->missing[i])need++;
        if(stock_count(g,g->units[h].team,p->missing[i],&r)<need)return 0;
    }
    for(i=0;i<p->count;i++){
        int id=p->missing[i];
        dota_items_read(id,&r);
        if(!r.stock_max)continue;
        free=-1;
        for(j=0;j<DOTA_ITEMS_STOCKS;j++){
            s=&g->items.stocks[j];
            if(s->item==id&&s->team==g->units[h].team){
                free=j;
                break;
            }
            if(s->item<0&&free<0)free=j;
        }
        if(free<0)return 0;
        s=&g->items.stocks[free];
        s->count=(uint16_t)(stock_count(g,g->units[h].team,id,&r)-1);
        s->item=(int16_t)id;
        s->team=(uint8_t)g->units[h].team;
        s->last_tick=g->tick;
    }
    return 1;
}
int dota_items_lotus_value(int id){return id==DOTA_ITEM_FAMANGO?1:(id==DOTA_ITEM_GREAT_FAMANGO?3:(id==DOTA_ITEM_GREATER_FAMANGO?6:0));}
#include "inventory_impl.h"
int dota_items_cooldown(const DotaGame*g,int h,int s){
    int id,wait;if(h<0||h>=10||s<0||s>=DOTA_ITEMS_EQUIP_SLOTS)return 0;id=ITEM_RAW(g,h,s);wait=imax(g->items.cooldown[h][s],dota_items_special_cooldown(g,h,id));if(dota_items_lotus_value(id))wait=imax(wait,g->objectives.lotus_cooldown[h]);if(id==DOTA_ITEM_TPSCROLL)wait=imax(wait,dota_items_teleport_cooldown(g,h));if(id==DOTA_ITEM_HELM_OF_THE_DOMINATOR||id==DOTA_ITEM_HELM_OF_THE_OVERLORD)wait=imax(wait,dota_item_world_dominate_cooldown(g,h));if(id!=DOTA_ITEM_HAND_OF_MIDAS||!g->items.charges[h][s])wait=imax(wait,g->items.proc_wait[h][s]);return wait;
}
int dota_items_charges(const DotaGame*g,int h,int s){
    int n;
    if(h<0||h>=10||s<0||s>=DOTA_ITEMS_EQUIP_SLOTS)return 0;
    n=dota_items_special_charges(g,h,ITEM_RAW(g,h,s),s);
    if(n>=0)return n;
    n=g->items.charges[h][s];
    return ITEM_RAW(g,h,s)==DOTA_ITEM_WARD_DISPENSER?(n&255)+(n>>8):n;
}
int dota_items_is_active(int id){
    DotaItemRecord r;
    return dota_items_read(id,&r)&&r.action!=0&&!(r.flags&ITEM_F_RECIPE);
}
int dota_items_cost(int id){
    DotaItemRecord r;
    return dota_items_read(id,&r)?r.cost:0;
}
int dota_items_has_upgrade(const DotaGame*g,int h,int shard){
    if(!valid(g,h))return 0;
    return shard?((g->items.permanent[h]&4u)!=0):((g->items.permanent[h]&2u)!=0||has(g,h,DOTA_ITEM_ULTIMATE_SCEPTER));
}
int dota_items_can_receive(const DotaGame*g,int h,int id){int16_t incoming=(int16_t)id;return dota_items_can_receive_batch(g,h,&incoming,1);}
static int primitive(DotaGame*g,int id,int action,int h,int t,int x,int y,int amount,int duration){
    return dota_rules_item_primitive(g,action|(id<<8),h,t,x,y,amount,duration);
}
static void aoe_status(DotaGame*g,int h,int x,int y,int radius,int friendly,int kind,int duration,int value,int tag){
    int i,w=(radius+3)/4;
    for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&((g->units[i].team==g->units[h].team)==friendly)&&distance(x,y,g->units[i].x,g->units[i].y)<=w*w)effect(g,h,i,kind,duration,value,tag);
}
static void aoe_damage(DotaGame*g,int h,int x,int y,int radius,int amount,int magical){
    int i,w=(radius+3)/4;
    for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&g->units[i].team!=g->units[h].team&&distance(x,y,g->units[i].x,g->units[i].y)<=w*w)dota_rules_item_damage(g,h,i,amount,magical);
}
static void team_restore(DotaGame*g,int h,int radius,int hp,int mp){
    int i;
    for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&g->units[i].team==g->units[h].team&&within(&g->units[h],&g->units[i],radius))heal(g,h,i,hp,mp);
}
static int hostile(const DotaGame*g,int h,int t){
    return live(g,t)&&g->units[t].team!=g->units[h].team;
}
int dota_items_attack_damage(DotaGame*g,int source,int target,int amount){
    int s,id,best=100,pierce;
    DotaItemRecord r;
    if(!live(g,target))return amount;
    pierce=dota_items_special_true_strike(g,source,target);
    if(roll(g,dota_item_world_accuracy(g,source)))pierce=1;
    if(valid(g,source)){g->items.hit_proc[source]=0;for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){id=ITEM(g,source,s);if((id==DOTA_ITEM_MONKEY_KING_BAR||id==DOTA_ITEM_JAVELIN)&&item_slot(g,source,id)==s&&roll(g,hpar(g,source,id,IP_BONUS_CHANCE,id==DOTA_ITEM_MONKEY_KING_BAR?80:25))){g->items.hit_proc[source]|=(uint8_t)(1u<<s);pierce=1;}}}
    if(valid(g,target)){
        DotaItemStats st;
        dota_items_stats(g,target,&st);
        if(!pierce&&((int)(random_item(g)%10000u)<st.value[ITEM_STAT_EVASION]))return 0;
    }
    amount=dota_item_world_attack_damage(g,source,target,amount);
    if(!valid(g,source))return amount;
    sync(g,source);
    best=imax(best,dota_rules_attack_critical(g,source,target));
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
        id=ITEM(g,source,s);
        if(id<0||!dota_items_read(id,&r))continue;
        if(id!=DOTA_ITEM_REVENANTS_BROOCH){
            int chance=hpar(g,source,id,IP_CRIT_CHANCE,0);
            if(chance&&roll(g,chance))best=imax(best,hpar(g,source,id,IP_CRIT_MULTIPLIER,100));
        }
        if(g->units[target].type!=DOTA_HERO&&(id==DOTA_ITEM_QUELLING_BLADE||id==DOTA_ITEM_BFURY))amount+=hpar(g,source,id,item_ranged(g,source)?IP_DAMAGE_BONUS_RANGED:IP_DAMAGE_BONUS,hpar(g,source,id,IP_QUELLING_BONUS,0));
        if(dota_items_invisible(g,source)&&(id==DOTA_ITEM_INVIS_SWORD||id==DOTA_ITEM_SILVER_EDGE))amount+=hpar(g,source,id,IP_WINDWALK_BONUS_DAMAGE,175);
    }
    return dota_items_special_attack_damage(g,source,target,amount*best/100);
}
void dota_items_attack_hit(DotaGame*g,int source,int target,int dealt){
    int s,id,i;
    DotaItemRecord r;
    DotaItemStats st;
    if(source>=0&&source<DOTA_MAX_UNITS&&target>=0&&target<DOTA_MAX_UNITS&&dealt>0&&!g->items.recursion)dota_item_world_on_attack_hit(g,source,target,dealt);
    if(!valid(g,source)||target<0||target>=DOTA_MAX_UNITS||dealt<=0||g->items.recursion)return;
    sync(g,source);
    if(estatus(g,target,IE_NULLIFY))effect(g,source,target,IE_SLOW,10,10,DOTA_ITEM_NULLIFIER);
    g->items.recursion++;
    dota_items_stats(g,source,&st);
    heal(g,source,source,dealt*(st.value[ITEM_STAT_LIFESTEAL]/100+estatus(g,source,IE_LIFESTEAL))/100,0);
    for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
        int ranged=item_ranged(g,source),duration;
        id=ITEM(g,source,s);
        if(id<0||!dota_items_read(id,&r))continue;
        if(hpar(g,source,id,IP_FEEDBACK_MANA_BURN,0)){
            int burn=imin(g->units[target].mana,hpar(g,source,id,IP_FEEDBACK_MANA_BURN,0));
            g->units[target].mana-=burn;
            dota_rules_item_damage(g,source,target,burn*hpar(g,source,id,IP_DAMAGE_PER_BURN,1),0);
        }
        if(id==DOTA_ITEM_DESOLATOR||id==DOTA_ITEM_DESOLATOR_2||id==DOTA_ITEM_BLIGHT_STONE||id==DOTA_ITEM_ORB_OF_CORROSION)effect(g,source,target,IE_ARMOR,hticks(g,source,id,IP_CORRUPTION_DURATION,hticks(g,source,id,IP_DURATION,140)),hpar(g,source,id,IP_CORRUPTION_ARMOR,-2),id);
        if(id==DOTA_ITEM_ORB_OF_VENOM||id==DOTA_ITEM_ORB_OF_FROST||id==DOTA_ITEM_ORB_OF_CORROSION){
            duration=hticks(g,source,id,IP_DURATION,60);
            effect(g,source,target,IE_SLOW,duration,-hpar(g,source,id,ranged?IP_SLOW_RANGED:IP_SLOW_MELEE,-8),id);
            if(id==DOTA_ITEM_ORB_OF_VENOM)effect(g,source,target,IE_DOT,duration,hpar(g,source,id,IP_POISON_DAMAGE_PER_SECOND,2)*100,id);
            else effect(g,source,target,IE_HEAL_REDUCTION,duration,hpar(g,source,id,IP_HEAL_REDUCTION,15),id);
        }
        if(id==DOTA_ITEM_SKADI){
            duration=hticks(g,source,id,IP_COLD_DURATION,60);
            effect(g,source,target,IE_SLOW,duration,-hpar(g,source,id,ranged?IP_COLD_SLOW_RANGED:IP_COLD_SLOW_MELEE,-25),id);
            effect(g,source,target,IE_AS,duration,-25,id);
            effect(g,source,target,IE_HEAL_REDUCTION,duration,hpar(g,source,id,IP_RESTORATION_REDUCTION,50),id);
        }
        if(id==DOTA_ITEM_SILVER_EDGE&&dota_items_invisible(g,source))effect(g,source,target,IE_BREAK,hticks(g,source,id,IP_BACKSTAB_DURATION,100),1,id);
        if(id==DOTA_ITEM_MAGE_SLAYER){
            effect(g,source,target,IE_DAMAGE_AMP,r.duration_ticks,-hpar(g,source,id,IP_SPELL_AMP_DEBUFF,40),id);
            effect(g,source,target,IE_DOT,r.duration_ticks,hpar(g,source,id,IP_DPS,35)*100,id);
        }
        if((id==DOTA_ITEM_BASHER||id==DOTA_ITEM_ABYSSAL_BLADE)&&!g->items.proc_wait[source][s]&&roll(g,hpar(g,source,id,ranged?IP_BASH_CHANCE_RANGED:IP_BASH_CHANCE_MELEE,25))){
            g->units[target].stun_ticks=imax(g->units[target].stun_ticks,hticks(g,source,id,IP_BASH_DURATION,24));
            dota_rules_item_damage(g,source,target,hpar(g,source,id,IP_BONUS_CHANCE_DAMAGE,100),0);
            g->items.proc_wait[source][s]=(uint16_t)hticks(g,source,id,IP_BASH_COOLDOWN,46);
        }
        if((id==DOTA_ITEM_MAELSTROM||id==DOTA_ITEM_MJOLLNIR)&&!g->items.proc_wait[source][s]&&roll(g,hpar(g,source,id,IP_CHAIN_CHANCE,25))){
            int left=hpar(g,source,id,IP_CHAIN_STRIKES,4);
            g->items.proc_wait[source][s]=(uint16_t)hticks(g,source,id,IP_CHAIN_COOLDOWN,4);
            dota_rules_item_damage(g,source,target,hpar(g,source,id,IP_CHAIN_DAMAGE,110),1);
            left--;
            for(i=0;i<DOTA_MAX_UNITS&&left>0;i++)if(i!=target&&hostile(g,source,i)&&within(&g->units[target],&g->units[i],hpar(g,source,id,IP_CHAIN_RADIUS,650))){
                dota_rules_item_damage(g,source,i,hpar(g,source,id,IP_CHAIN_DAMAGE,110),1);
                left--;
            }
        }
        if(id==DOTA_ITEM_MONKEY_KING_BAR&&((g->items.hit_proc[source]>>s)&1))dota_rules_item_damage(g,source,target,hpar(g,source,id,IP_BONUS_CHANCE_DAMAGE,70),1);
        if(id==DOTA_ITEM_JAVELIN&&((g->items.hit_proc[source]>>s)&1))dota_rules_item_damage(g,source,target,hpar(g,source,id,IP_BONUS_CHANCE_DAMAGE,60),1);
        if(id==DOTA_ITEM_REVENANTS_BROOCH&&roll(g,hpar(g,source,id,IP_CRIT_CHANCE,30)))dota_rules_item_damage(g,source,target,dealt*hpar(g,source,id,IP_CRIT_MULTIPLIER,80)/100,1);
        if(id==DOTA_ITEM_BLOODTHORN&&roll(g,hpar(g,source,id,IP_PROC_CHANCE,40)))dota_rules_item_damage(g,source,target,hpar(g,source,id,IP_PASSIVE_PROC_DAMAGE,60),1);
        if((id==DOTA_ITEM_ECHO_SABRE||id==DOTA_ITEM_HARPOON)&&!ranged&&!g->items.proc_wait[source][s]){
            dota_rules_item_damage(g,source,target,g->units[source].damage,0);
            effect(g,source,target,IE_SLOW,hticks(g,source,id,IP_SLOW_DURATION,16),100,id);
            g->items.proc_wait[source][s]=(uint16_t)hticks(g,source,id,IP_PASSIVE_COOLDOWN,r.cooldown_ticks?r.cooldown_ticks:100);
        }
        if((id==DOTA_ITEM_WITCH_BLADE||id==DOTA_ITEM_DEVASTATOR)&&!g->items.proc_wait[source][s]){
            int intel=attribute(g,source,2);
            effect(g,source,target,IE_DOT,hticks(g,source,id,IP_SLOW_DURATION,80),intel*hpx(g,source,id,IP_INT_DAMAGE_MULTIPLIER,75),id);
            effect(g,source,target,IE_SLOW,hticks(g,source,id,IP_SLOW_DURATION,80),25,id);
            if(id==DOTA_ITEM_DEVASTATOR)effect(g,source,target,IE_VULNERABLE,80,20,id);
            g->items.proc_wait[source][s]=(uint16_t)hticks(g,source,id,IP_PASSIVE_COOLDOWN,180);
        }
        if(id==DOTA_ITEM_BFURY&&!ranged){
            int dx=g->units[target].x-g->units[source].x,dy=g->units[target].y-g->units[source].y;
            for(i=0;i<DOTA_MAX_UNITS;i++)if(i!=target&&hostile(g,source,i)&&within(&g->units[target],&g->units[i],hpar(g,source,id,IP_CLEAVE_DISTANCE,650))){
                int xx=g->units[i].x-g->units[source].x,yy=g->units[i].y-g->units[source].y;
                if(dx*xx+dy*yy>0)dota_rules_item_damage(g,source,i,dealt*hpar(g,source,id,g->units[i].type==DOTA_HERO?IP_CLEAVE_DAMAGE_PERCENT:IP_CLEAVE_DAMAGE_PERCENT_CREEP,60)/100,0);
            }
        }
        if((id==DOTA_ITEM_SPECIALISTS_ARRAY||id==DOTA_ITEM_HYDRAS_BREATH)&&!g->items.proc_wait[source][s]&&roll(g,hpar(g,source,id,IP_PROC_CHANCE,30))){
            int left=hpar(g,source,id,IP_COUNT,2);
            for(i=0;i<DOTA_MAX_UNITS&&left>0;i++)if(i!=target&&hostile(g,source,i)&&within(&g->units[target],&g->units[i],525)){
                dota_rules_item_damage(g,source,i,(dealt+hpar(g,source,id,IP_BASE_PROC_DMG,20))*hpar(g,source,id,IP_PROC_DMG_PCT,75)/100,0);
                if(id==DOTA_ITEM_HYDRAS_BREATH)effect(g,source,i,IE_DOT,hticks(g,source,id,IP_POISON_DURATION,60),hpx(g,source,id,IP_POISON_DAMAGE_PER_SECOND,250),id);
                left--;
            }
            g->items.proc_wait[source][s]=1;
        }
        if(id==DOTA_ITEM_TRANQUIL_BOOTS&&g->units[target].type==DOTA_HERO)g->items.proc_wait[source][s]=(uint16_t)hticks(g,source,id,IP_BREAK_TIME,260);
        if(id==DOTA_ITEM_HEAVY_BLADE)dota_rules_item_damage(g,source,target,g->units[target].max_mana*hpar(g,source,id,IP_DAMAGE,4)/100,0);
    }
    dota_items_special_attack_hit(g,source,target,dealt);
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){DotaItemEffect*e=&g->items.effects[i];if(e->ticks&&e->kind==IE_PIKE&&e->source==source&&e->target==target&&e->value>0){e->value--;if(!e->value)dota_items_remove_effect(g,i);}}
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++)if(g->items.effects[i].target==source&&g->items.effects[i].kind==IE_INVISIBLE)dota_items_remove_effect(g,i);
    g->items.recursion--;
}
int dota_items_attack_block(DotaGame*g,int source,int target){
    int i,id,block=0;DotaItemRecord r;
    if(!g||!live(g,target))return 0;
    block=imax(dota_roster_physical_block(g,source,target),estatus(g,target,IE_CRIMSON));
    /* Native innate melee block is independent of Break. It also applies
     * against hero attacks, and uses the strongest ordinary damage block
     * instead of adding16 on top of Vanguard/Crimson/Kraken Shell. */
    {int hero=valid(g,target)||(target>=DOTA_ITEM_UNIT_FIRST&&g->item_world.unit[target-DOTA_ITEM_UNIT_FIRST].kind==ITEM_UNIT_ILLUSION);
     int type=g->units[target].hero_id,ranged=type==DOTA_HERO_LINA||type==DOTA_HERO_CRYSTAL_MAIDEN||type==DOTA_HERO_NECROPHOS||type==DOTA_HERO_SNIPER;
     if(hero&&!ranged&&(unsigned)source<DOTA_MAX_UNITS&&g->units[source].type!=DOTA_NONE&&g->units[source].type!=DOTA_TOWER&&g->units[source].type!=DOTA_BASE&&g->units[source].type!=DOTA_BUILDING&&roll(g,DOTA_MELEE_BLOCK_CHANCE))block=imax(block,DOTA_MELEE_BLOCK_AMOUNT+(g->units[target].level-1)*DOTA_MELEE_BLOCK_PER_LEVEL);}
    if(valid(g,target))for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS;i++){
        int b,ranged=item_ranged(g,target);id=ITEM(g,target,i);
        if(id<0||!dota_items_read(id,&r))continue;
        b=hpar(g,target,id,ranged?IP_BLOCK_DAMAGE_RANGED:IP_BLOCK_DAMAGE_MELEE,hpar(g,target,id,ranged?IP_DAMAGE_BLOCK_RANGED:IP_DAMAGE_BLOCK_MELEE,0));
        if(b&&roll(g,hpar(g,target,id,IP_BLOCK_CHANCE,60)))block=imax(block,b);
    }
    return block;
}
int dota_items_before_damage(DotaGame*g,int source,int target,int amount,int magical){
    DotaItemStats st;
    DotaItemRecord r;
    int i,id,is_magic=(magical==1||magical==3||magical==5||magical==7||magical==9);
    DotaItemEffect*e;
    if(amount<=0||!live(g,target))return 0;
    if(estatus(g,target,IE_AEON)||(source>=0&&estatus(g,source,IE_AEON)))return 0;
    amount=dota_items_special_damage_percent(g,source,target,amount,magical);
    if(dota_items_invulnerable(g,target))return 0;
    if(magical==0&&(estatus(g,target,IE_GHOST)||estatus(g,target,IE_ETHEREAL)))return 0;
    if(valid(g,target)){
        sync(g,target);
        dota_items_stats(g,target,&st);
        if(is_magic)amount=amount*(10000-imin(9000,st.value[ITEM_STAT_MAGIC_RESIST]))/10000;
        for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS;i++){
            id=ITEM(g,target,i);
            if(id<0||!dota_items_read(id,&r))continue;
            if(is_magic&&id==DOTA_ITEM_INFUSED_RAINDROP&&!g->items.proc_wait[target][i]&&g->items.charges[target][i]&&amount>=hpar(g,target,id,IP_MIN_DAMAGE,75)){
                amount=imax(0,amount-hpar(g,target,id,IP_MAGIC_DAMAGE_BLOCK,120));g->items.charges[target][i]--;g->items.proc_wait[target][i]=r.cooldown_ticks;
                if(!g->items.charges[target][i]){ITEM_RAW(g,target,i)=-1;g->items.tracked[target][i]=-1;dota_items_invalidate(g,target);dota_rules_item_recalc(g,target);}
            }
        }
    }
    if(is_magic){
        if(magical!=5&&magical!=7)amount=amount*(100-estatus(g,target,IE_IMMUNITY))/100;
        amount=amount*(100+estatus(g,target,IE_GHOST)+estatus(g,target,IE_ETHEREAL)+estatus(g,target,IE_VULNERABLE))/100;
    }
    for(i=0;i<DOTA_ITEMS_EFFECTS&&amount>0;i++){
        e=&g->items.effects[i];
        if(e->ticks&&e->target==target&&e->generation==g->units[target].generation&&e->kind==(is_magic?IE_BARRIER_MAGIC:IE_BARRIER_PHYSICAL)&&(is_magic||magical==0)){
            int absorb=imin(amount,e->value);
            e->value-=absorb;
            amount-=absorb;
            if(!e->value)dota_items_remove_effect(g,i);
        }
    }
    if(valid(g,target)&&amount>0){
        int slot=item_slot(g,target,DOTA_ITEM_AEON_DISK);
        if(slot>=0&&!g->items.proc_wait[target][slot]){
            int threshold=g->units[target].max_hp*par(DOTA_ITEM_AEON_DISK,IP_HEALTH_THRESHOLD_PCT,70)/100;
            if(g->units[target].hp-amount<=threshold){
                amount=imin(amount,imax(0,g->units[target].hp-threshold));dota_items_purge(g,target,1);
                effect(g,target,target,IE_AEON,ticks(DOTA_ITEM_AEON_DISK,IP_BUFF_DURATION,50),75,DOTA_ITEM_AEON_DISK);
                g->items.proc_wait[target][slot]=(uint16_t)(dota_items_parameter(DOTA_ITEM_AEON_DISK,IP_COOLDOWN_DURATION,imin(3,g->items.uses[target][slot]),10500)/5);
                g->items.uses[target][slot]++;
            }
        }
    }
    return dota_items_special_before_damage(g,source,target,amount,magical);
}
void dota_items_after_damage(DotaGame*g,int source,int target,int dealt,int magical){
    if(magical==9)return;
    int s,id,i;
    DotaItemRecord r;
    if(dealt<=0||target<0||target>=DOTA_MAX_UNITS)return;
    dota_item_world_on_damage(g,target,source,dealt);
    dota_items_special_after_damage(g,source,target,dealt,magical);
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        DotaItemEffect*e=&g->items.effects[i];
        if(!e->ticks||e->target!=target)continue;
        if(e->kind==IE_SILENCE&&e->value>1)e->interval=(uint16_t)imin(65535,e->interval+dealt);
        if(magical!=3&&magical!=5&&(e->kind==IE_REGEN_HP||e->kind==IE_REGEN_MP)&&source>=0&&source<DOTA_MAX_UNITS&&g->units[source].team!=g->units[target].team&&(g->units[source].type==DOTA_HERO||g->units[source].type==DOTA_ROSHAN)){
            int item=e->tag&32767;
            if(item==DOTA_ITEM_FLASK||item==DOTA_ITEM_CLARITY||item==DOTA_ITEM_BOTTLE||item==DOTA_ITEM_URN_OF_SHADOWS||item==DOTA_ITEM_SPIRIT_VESSEL||item==DOTA_ITEM_ESSENCE_DISTILLER||item==DOTA_ITEM_MANA_DRAUGHT)dota_items_remove_effect(g,i);
        }
    }
    if(valid(g,source)&&magical!=0&&magical!=3&&magical!=5&&!g->items.recursion){
        DotaItemStats st;
        dota_items_stats(g,source,&st);
        heal(g,source,source,dealt*(st.value[ITEM_STAT_SPELL_LIFESTEAL]/100+estatus(g,source,IE_SPELL_LIFESTEAL))/(g->units[target].type==DOTA_HERO?100:500),0);
    }
    if(valid(g,target)){
        sync(g,target);
        for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
            id=ITEM(g,target,s);
            if(id<0||!dota_items_read(id,&r))continue;
            if(id==DOTA_ITEM_TRANQUIL_BOOTS&&magical!=3&&magical!=5&&dealt>=hpar(g,target,id,IP_BREAK_THRESHOLD,20)&&source>=0&&g->units[source].team!=g->units[target].team)g->items.proc_wait[target][s]=(uint16_t)hticks(g,target,id,IP_BREAK_TIME,260);
            if(magical!=3&&magical!=5&&source>=0&&source<DOTA_MAX_UNITS&&g->units[source].team!=g->units[target].team&&(g->units[source].type==DOTA_HERO||g->units[source].type==DOTA_ROSHAN)&&hpar(g,target,id,IP_BLINK_DAMAGE_COOLDOWN,0))g->items.cooldown[target][s]=(uint16_t)imax(g->items.cooldown[target][s],hticks(g,target,id,IP_BLINK_DAMAGE_COOLDOWN,60));

        }
    }
    if(!g->items.recursion&&estatus(g,target,IE_STATIC)&&roll(g,20)&&live(g,source)){
        g->items.recursion++;
        aoe_damage(g,target,g->units[source].x,g->units[source].y,600,estatus(g,target,IE_STATIC),1);
        g->items.recursion--;
    }

}
void dota_items_on_cast(DotaGame*g,int caster){
    int h,s,id;
    if(!live(g,caster))return;
    dota_items_cancel_teleport(g,caster);
    dota_rune_on_action(g,caster);
    dota_items_special_on_cast(g,caster);
    for(h=0;h<DOTA_HERO_COUNT;h++)if(valid(g,h)&&live(g,h)&&g->units[h].team!=g->units[caster].team&&within(&g->units[h],&g->units[caster],1200)){
        sync(g,h);
        for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
            id=ITEM(g,h,s);
            if(id==DOTA_ITEM_MAGIC_STICK||id==DOTA_ITEM_MAGIC_WAND||id==DOTA_ITEM_HOLY_LOCKET){
                DotaItemRecord r;
                if(dota_items_read(id,&r)&&g->items.charges[h][s]<r.max_charges)g->items.charges[h][s]++;
            }
        }
    }
    for(s=0;s<DOTA_ITEMS_EFFECTS;s++)if(g->items.effects[s].target==caster&&g->items.effects[s].kind==IE_INVISIBLE&&g->items.effects[s].value!=2)dota_items_remove_effect(g,s);
}
int dota_items_blocks_spell(DotaGame*g,int source,int target){
    (void)source;
    int i,s;
    DotaItemEffect*e;
    DotaItemRecord r;
    if(!live(g,target))return 0;
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        e=&g->items.effects[i];
        if(e->ticks&&e->target==target){
            if(e->kind==IE_SPELLBLOCK){
                dota_items_remove_effect(g,i);
                return 1;
            }
        }
    }
    if(valid(g,target)){
        sync(g,target);
        s=item_slot(g,target,DOTA_ITEM_SPHERE);
        if(s>=0&&!g->items.cooldown[target][s]&&dota_items_read(DOTA_ITEM_SPHERE,&r)){
            g->items.cooldown[target][s]=(uint16_t)ticks(DOTA_ITEM_SPHERE,IP_BLOCK_COOLDOWN,280);
            return 1;
        }
    }
    return 0;
}
static int move_distance(DotaGame*g,int h,int t,int amount,int toward){
    if(g->units[h].team!=g->units[t].team)amount=amount*(100-dota_items_special_knockback_resistance(g,t))/100;
    int dx=g->units[t].x-g->units[h].x,dy=g->units[t].y-g->units[h].y,n=imax(dx<0?-dx:dx,dy<0?-dy:dy);
    if(!n){
        dx=1;
        n=1;
    }
    return dota_rules_item_move(g,t,g->units[t].x+(toward?-1:1)*dx*(amount/4)/n,g->units[t].y+(toward?-1:1)*dy*(amount/4)/n,0);
}
static void consume(DotaGame*g,int h,int s,const DotaItemRecord*r){
    if(ITEM(g,h,s)==DOTA_ITEM_TPSCROLL&&(has(g,h,DOTA_ITEM_TRAVEL_BOOTS)||has(g,h,DOTA_ITEM_TRAVEL_BOOTS_2)))return;
    if(r->flags&ITEM_F_CONSUMABLE){
        if(g->items.charges[h][s]>0)g->items.charges[h][s]--;
        if(!g->items.charges[h][s]){
            ITEM_RAW(g,h,s)=-1;
            g->items.tracked[h][s]=-1;
            dota_items_invalidate(g,h);
            dota_rules_item_recalc(g,h);
        }
    }
}
static int resolve_item_effect(DotaGame*g,int h,int s,int id,const DotaItemRecord*record,int x,int y,int target,int parameter_owner,int reflected){
 if((unsigned)h>=DOTA_MAX_UNITS)return 0;
 DotaItemRecord r=*record;DotaUnit*u=&g->units[h],*v;int t=live(g,target)?target:h,enemy,duration=hticks(g,parameter_owner,id,IP_DURATION,r.duration_ticks),radius=r.radius,i,success=0,action=r.action;
 v=&g->units[t];enemy=v->team!=u->team;
    switch(action){
        case ITEM_ACTION_BLINK:{
            int dx=x-u->x,dy=y-u->y,n=imax(dx<0?-dx:dx,dy<0?-dy:dy),range=hpar(g,parameter_owner,id,IP_BLINK_RANGE,1200)/4;
            if(dota_items_rooted(g,h))return 0;
            if(n>range){
                int clamp=hpar(g,parameter_owner,id,IP_BLINK_RANGE_CLAMP,range*4)/4;
                dx=dx*clamp/n;
                dy=dy*clamp/n;
            }
            success=dota_rules_item_move(g,h,u->x+dx,u->y+dy,1);
            if(success){
                if(id==DOTA_ITEM_OVERWHELMING_BLINK){
                    aoe_damage(g,h,u->x,u->y,hpar(g,parameter_owner,id,IP_RADIUS,800),hpar(g,parameter_owner,id,IP_DAMAGE_BASE,100)+hpar(g,parameter_owner,id,IP_DAMAGE_PCT_INSTANT,50)*attribute(g,h,0)/100,1);
                    aoe_status(g,h,u->x,u->y,800,0,IE_SLOW,duration,50,id);
                    aoe_status(g,h,u->x,u->y,800,0,IE_AS,duration,-50,id);
                    aoe_status(g,h,u->x,u->y,800,0,IE_DOT,duration,attribute(g,h,0)*100/6,id);
                }
                if(id==DOTA_ITEM_SWIFT_BLINK){
                    effect(g,h,h,IE_HASTE,duration,40,id);
                    effect(g,h,h,IE_AGILITY,duration,35,id);
                }
                if(id==DOTA_ITEM_ARCANE_BLINK)heal(g,h,h,hpar(g,parameter_owner,id,IP_HEAL_AMOUNT,250),hpar(g,parameter_owner,id,IP_MANA_AMOUNT,100));
                if(id==DOTA_ITEM_FALLEN_SKY){
                    aoe_damage(g,h,u->x,u->y,hpar(g,parameter_owner,id,IP_IMPACT_RADIUS,315),hpar(g,parameter_owner,id,IP_IMPACT_DAMAGE_UNITS,150),1);
                    for(i=0;i<DOTA_MAX_UNITS;i++)if(hostile(g,h,i)&&within(u,&g->units[i],315)){
                        g->units[i].stun_ticks=imax(g->units[i].stun_ticks,hticks(g,parameter_owner,id,IP_STUN_DURATION,32));
                        effect(g,h,i,IE_DOT,hticks(g,parameter_owner,id,IP_BURN_DURATION,120),hpar(g,parameter_owner,id,IP_BURN_DPS_UNITS,60)*100,id);
                    }
                }
            }
            break;
        }
        case ITEM_ACTION_TREE:success=primitive(g,id,action,h,t,x,y,0,duration);
        if(success&&id==DOTA_ITEM_TANGO)effect(g,h,h,IE_REGEN_HP,hticks(g,parameter_owner,id,IP_BUFF_DURATION,320)*success,hpx(g,parameter_owner,id,IP_HEALTH_REGEN,700),id);
        break;
        case ITEM_ACTION_TREE_PLANT:success=primitive(g,id,action,h,-1,x,y,0,hticks(g,parameter_owner,id,IP_TREE_DURATION,400));
        /* Branches are permanent stat items until this particular active
         * succeeds; Source consumes the actual branch used to plant. */
        if(success&&id==DOTA_ITEM_BRANCHES&&!reflected&&s>=0){
            ITEM_RAW(g,h,s)=-1;g->items.tracked[h][s]=-1;g->items.charges[h][s]=0;
            dota_items_invalidate(g,h);dota_rules_item_recalc(g,h);
        }
        break;
        case ITEM_ACTION_HEAL:
        if(id==DOTA_ITEM_ENCHANTED_MANGO)heal(g,h,t,0,hpar(g,parameter_owner,id,IP_REPLENISH_AMOUNT,100));
        else if(id==DOTA_ITEM_MANA_DRAUGHT)effect(g,h,t,IE_REGEN_MP,hticks(g,parameter_owner,id,IP_HEAL_DURATION,120),(hpar(g,parameter_owner,id,IP_MANA_RESTORE,70)+(int)((long long)v->max_mana*hpx(g,parameter_owner,id,IP_MANA_RESTORE_PCT,300)/10000))*100/6,id);
        else if(id==DOTA_ITEM_CHEESE)heal(g,h,h,hpar(g,parameter_owner,id,IP_HEALTH_RESTORE,2500),hpar(g,parameter_owner,id,IP_MANA_RESTORE,1500));
        else if(id==DOTA_ITEM_FAERIE_FIRE)heal(g,h,h,hpar(g,parameter_owner,id,IP_HP_RESTORE,85),0);
        else{
            int amount=hpar(g,parameter_owner,id,IP_REPLENISH_AMOUNT,125);
            heal(g,h,t,amount,amount);
        }
        success=1;
        break;
        case ITEM_ACTION_REGEN:if(id==DOTA_ITEM_BOTTLE){
            if(!reflected&&!g->items.charges[h][s])return 0;
            effect(g,h,t,IE_REGEN_HP,duration,hpar(g,parameter_owner,id,IP_HEALTH_RESTORE,110)*2000/imax(1,duration),id);
            effect(g,h,t,IE_REGEN_MP,duration,hpar(g,parameter_owner,id,IP_MANA_RESTORE,60)*2000/imax(1,duration),id);
            g->items.charges[h][s]--;
        }
        else effect(g,h,t,id==DOTA_ITEM_CLARITY?IE_REGEN_MP:IE_REGEN_HP,duration,hpx(g,parameter_owner,id,id==DOTA_ITEM_CLARITY?IP_MANA_REGEN:IP_HEALTH_REGEN,id==DOTA_ITEM_CLARITY?600:3000),id);
        success=1;
        break;
        case ITEM_ACTION_WARD:{
            int kind=id==DOTA_ITEM_WARD_SENTRY;
            if(id==DOTA_ITEM_WARD_DISPENSER){
                if(!reflected&&!g->items.charges[h][s])return 0;
                kind=(g->items.charges[h][s]&255)?0:1;
            }
            success=primitive(g,id,action,h,-1,x,y,kind,duration?duration:(kind?8400:7200));
            if(success&&id==DOTA_ITEM_WARD_DISPENSER){
                g->items.charges[h][s]-=(uint16_t)(kind?256:1);
                if(!g->items.charges[h][s]){
                    ITEM_RAW(g,h,s)=-1;
                    g->items.tracked[h][s]=-1;
                }
            }
            break;
        }
        case ITEM_ACTION_CHARGES:{
            int n=g->items.charges[h][s];
            if(!n)return 0;
            if(id==DOTA_ITEM_HOLY_LOCKET)effect(g,h,t,IE_INCOMING_HEAL_AMP,hticks(g,parameter_owner,id,IP_ACTIVE_BUFF_DURATION,80),hpar(g,parameter_owner,id,IP_INCOMING_HEAL_INCREASE_ACTIVE,15),id);
            heal(g,h,t,n*hpar(g,parameter_owner,id,IP_HEALTH_RESTORE_PER_CHARGE,hpar(g,parameter_owner,id,IP_RESTORE_PER_CHARGE,15)),n*hpar(g,parameter_owner,id,IP_MANA_RESTORE_PER_CHARGE,hpar(g,parameter_owner,id,IP_RESTORE_PER_CHARGE,15)));
            g->items.charges[h][s]=0;
            success=1;
            break;
        }
        case ITEM_ACTION_TELEPORT:if(dota_items_rooted(g,h))return 0;
        {int boot=has(g,h,DOTA_ITEM_TRAVEL_BOOTS_2)?DOTA_ITEM_TRAVEL_BOOTS_2:(has(g,h,DOTA_ITEM_TRAVEL_BOOTS)?DOTA_ITEM_TRAVEL_BOOTS:-1);
         int channel=hticks(g,h,id,IP_ABILITYCHANNELTIME,60);
         if(boot==DOTA_ITEM_TRAVEL_BOOTS_2)channel=hticks(g,h,boot,IP_TP_CHANNEL_TIME,40);
         success=primitive(g,boot>=0?boot:id,action,h,target,x,y,0,channel);}
        break;
        case ITEM_ACTION_PHASE:effect(g,h,h,IE_HASTE,duration,hpar(g,parameter_owner,id,item_ranged(g,h)?IP_PHASE_MOVEMENT_SPEED_RANGE:IP_PHASE_MOVEMENT_SPEED,20),id);
        success=1;
        break;
        case ITEM_ACTION_TREADS:g->items.uses[h][s]=(uint8_t)((g->items.uses[h][s]+1)%3);
        dota_items_invalidate(g,h);
        dota_rules_item_recalc(g,h);
        success=1;
        break;
        case ITEM_ACTION_MIDAS:if(!g->items.charges[h][s]||!enemy||dota_item_world_dominated(g,t)||v->type==DOTA_HERO||v->type==DOTA_ANCIENT||v->type==DOTA_ROSHAN||v->type==DOTA_TOWER||v->type==DOTA_BASE)return 0;
        success=primitive(g,id,action,h,t,x,y,hpar(g,parameter_owner,id,IP_BONUS_GOLD,160),0);
        if(success){
            g->items.charges[h][s]--;
            if(!g->items.proc_wait[h][s])g->items.proc_wait[h][s]=(uint16_t)hticks(g,parameter_owner,id,IP_ABILITYCHARGERESTORETIME,1800);
        }
        break;
        case ITEM_ACTION_TEAM_RESTORE:if(id==DOTA_ITEM_MEKANSM)team_restore(g,h,hpar(g,parameter_owner,id,IP_HEAL_RADIUS,1200),hpar(g,parameter_owner,id,IP_HEAL_AMOUNT,250),0);
        else if(id==DOTA_ITEM_ARCANE_BOOTS)team_restore(g,h,hpar(g,parameter_owner,id,IP_REPLENISH_RADIUS,1200),0,hpar(g,parameter_owner,id,IP_REPLENISH_AMOUNT,150));
        else{
            purge(g,h,0);
            team_restore(g,h,1200,hpar(g,parameter_owner,id,IP_REPLENISH_HEALTH,325),hpar(g,parameter_owner,id,IP_REPLENISH_MANA,200));
        }
        success=1;
        break;
        case ITEM_ACTION_BARRIER:
        if(id==DOTA_ITEM_PIPE)aoe_status(g,h,u->x,u->y,1200,1,IE_BARRIER_MAGIC,duration,hpar(g,parameter_owner,id,IP_BARRIER_BLOCK,425),id);
        else if(id==DOTA_ITEM_CRIMSON_GUARD)aoe_status(g,h,u->x,u->y,1200,1,IE_CRIMSON,duration,hpar(g,parameter_owner,id,IP_BLOCK_DAMAGE_ACTIVE,70)+u->max_hp*hpar(g,parameter_owner,id,IP_MAX_HP_PCT,2)/100,id);
        else if(id==DOTA_ITEM_GLIMMER_CAPE){
            effect(g,h,t,IE_BARRIER_MAGIC,duration,hpar(g,parameter_owner,id,IP_BARRIER_BLOCK,375),id);
            effect(g,h,t,IE_INVISIBLE,duration,1,id);
            effect(g,h,t,IE_HASTE,duration,hpar(g,parameter_owner,id,IP_ACTIVE_MOVEMENT_SPEED,20),id);
        }
        else{
            effect(g,h,t,IE_BARRIER_PHYSICAL,duration,hpar(g,parameter_owner,id,IP_ABSORB_AMOUNT,250),id);
            if(id==DOTA_ITEM_SOLAR_CREST){
                effect(g,h,t,IE_HASTE,duration,15,id);
                effect(g,h,t,IE_ARMOR,duration,5,id);
                effect(g,h,t,IE_AS,duration,60,id);
            }
        }
        success=1;
        break;
        case ITEM_ACTION_SOUL:if(!reflected&&!g->items.charges[h][s])return 0;
        if(target<0&&id==DOTA_ITEM_ESSENCE_DISTILLER){
            success=primitive(g,id,action,h,-1,x,y,hpar(g,parameter_owner,id,IP_SOUL_HEAL_AMOUNT,40),hticks(g,parameter_owner,id,IP_GROUND_DURATION,300));
        }
        else{
            effect(g,h,t,enemy?IE_DOT:IE_REGEN_HP,duration,hpar(g,parameter_owner,id,enemy?IP_SOUL_DAMAGE_AMOUNT:IP_SOUL_HEAL_AMOUNT,enemy?25:30)*100,id);
            if(enemy&&id==DOTA_ITEM_ESSENCE_DISTILLER)effect(g,h,t,IE_REVEAL,duration,1,id);
            if(enemy&&id==DOTA_ITEM_SPIRIT_VESSEL){
                effect(g,h,t,IE_HEAL_REDUCTION,duration,70,id);
                effect(g,h,t,IE_HP_DRAIN,duration,hpar(g,parameter_owner,id,IP_ENEMY_HP_DRAIN,4),id|32768);
            }
            success=1;
        }
        if(success&&!reflected)g->items.charges[h][s]--;
        break;
        case ITEM_ACTION_HEX:if(!enemy)return 0;
        purge(g,t,1);
        effect(g,h,t,IE_SILENCE,duration,1,id);
        effect(g,h,t,IE_DISARM,duration,1,id);
        effect(g,h,t,IE_SLOW,duration,60,id);
        success=1;
        break;
        case ITEM_ACTION_SILENCE:if(!enemy)return 0;
        effect(g,h,t,IE_SILENCE,duration,hpar(g,parameter_owner,id,IP_SILENCE_DAMAGE_PERCENT,30),id);
        if(id==DOTA_ITEM_BLOODTHORN)effect(g,h,t,IE_VULNERABLE,duration,hpar(g,parameter_owner,id,IP_SPELL_AMP_DEBUFF,35),id);
        success=1;
        break;
        case ITEM_ACTION_CYCLONE:if(!enemy&&id==DOTA_ITEM_CYCLONE&&t!=h)return 0;
        purge(g,t,enemy);
        effect(g,h,t,IE_CYCLONE,duration,enemy?hpar(g,parameter_owner,id,IP_TOOLTIP_DROP_DAMAGE,50):0,id);
        success=1;
        break;
        case ITEM_ACTION_FORCE:if(id==DOTA_ITEM_POGO_STICK)t=h;
        success=move_distance(g,h,t,id==DOTA_ITEM_HURRICANE_PIKE&&enemy?hpar(g,parameter_owner,id,IP_ENEMY_LENGTH,425):hpar(g,parameter_owner,id,IP_PUSH_LENGTH,hpar(g,parameter_owner,id,IP_LEAP_DISTANCE,600)),0);
        if(success&&enemy&&id==DOTA_ITEM_HURRICANE_PIKE){
            move_distance(g,t,h,hpar(g,parameter_owner,id,IP_ENEMY_LENGTH,425),0);
            effect(g,h,t,IE_PIKE,hticks(g,parameter_owner,id,IP_RANGE_DURATION,100),hpar(g,parameter_owner,id,IP_MAX_ATTACKS,5),id);u->target=t;
        }
        break;
        case ITEM_ACTION_DAMAGE:if(id==DOTA_ITEM_BLOOD_GRENADE){
            aoe_damage(g,h,x,y,radius,hpar(g,parameter_owner,id,IP_IMPACT_DAMAGE,50),1);
            aoe_status(g,h,x,y,radius,0,IE_DOT,duration,hpar(g,parameter_owner,id,IP_DAMAGE_OVER_TIME,15)*100,id);
            aoe_status(g,h,x,y,radius,0,IE_SLOW,duration,15,id);
        }
        else{
            if(!enemy)return 0;
            dota_rules_item_effect(g,DOTA_FX_LINE,u->team,u->x,u->y,v->x,v->y,3,10);
            dota_rules_item_damage(g,h,t,hpar(g,parameter_owner,id,IP_DAMAGE,400),1);
        }
        success=1;
        break;
        case ITEM_ACTION_REFRESH:for(i=0;i<4;i++)u->cooldown[i]=0;
        dota_extra_refresh(g,h);dota_roster_refresh(g,h);
        success=1;
        break;
        case ITEM_ACTION_IMMUNITY:purge(g,h,0);
        duration=id==DOTA_ITEM_BLACK_KING_BAR?dota_items_parameter(id,IP_DURATION,imin(2,g->items.uses[h][s]),900)/5:duration;
        effect(g,h,h,IE_IMMUNITY,duration,hpar(g,parameter_owner,id,IP_SPELL_REDUCE,hpar(g,parameter_owner,id,IP_MAGIC_RESIST,60)),id);
        g->items.uses[h][s]++;
        success=1;
        break;
        case ITEM_ACTION_SHIVA:aoe_damage(g,h,u->x,u->y,hpar(g,parameter_owner,id,IP_BLAST_RADIUS,825),hpar(g,parameter_owner,id,IP_BLAST_DAMAGE,225),1);
        aoe_status(g,h,u->x,u->y,825,0,IE_SLOW,hticks(g,parameter_owner,id,IP_BLAST_DEBUFF_DURATION,80),40,id);
        success=1;
        break;
        case ITEM_ACTION_LIFESTEAL:purge(g,h,0);
        effect(g,h,h,id==DOTA_ITEM_SATANIC?IE_LIFESTEAL:IE_SPELL_LIFESTEAL,duration,id==DOTA_ITEM_SATANIC?hpar(g,parameter_owner,id,IP_UNHOLY_LIFESTEAL_TOTAL_TOOLTIP,175)-hpar(g,parameter_owner,id,IP_LIFESTEAL_PERCENT,25):hpar(g,parameter_owner,id,IP_SPELL_LIFESTEAL_WHILE_ACTIVE,60)-hpar(g,parameter_owner,id,IP_SPELL_LIFESTEAL,20),id);
        success=1;
        break;
        case ITEM_ACTION_SPELLBLOCK:effect(g,h,t,IE_SPELLBLOCK,hticks(g,parameter_owner,id,IP_UPGRADE_ABSORB_DURATION,200),1,id);
        success=1;
        break;
        case ITEM_ACTION_REFLECT:purge(g,t,0);
        effect(g,h,t,IE_REFLECT,duration,1,id);
        success=1;
        break;
        case ITEM_ACTION_METEOR:success=primitive(g,id,action,h,-1,x,y,hpar(g,parameter_owner,id,IP_IMPACT_DAMAGE_UNITS,130),hticks(g,parameter_owner,id,IP_MAX_DURATION,40));
        break;
        case ITEM_ACTION_PURGE:if(!enemy&&id!=DOTA_ITEM_DISPERSER)return 0;
        purge(g,t,enemy);
        if(id==DOTA_ITEM_NULLIFIER){effect(g,h,t,IE_NULLIFY,duration?duration:80,1,id);effect(g,h,t,IE_SLOW,10,hpar(g,parameter_owner,id,IP_SLOW_PCT,10),id);}
        else effect(g,h,t,enemy?IE_DECAY_SLOW:IE_HASTE,duration?duration:80,100,id);
        if(!enemy&&id==DOTA_ITEM_DISPERSER)effect(g,h,t,IE_SLOW_RESIST,duration?duration:80,hpar(g,parameter_owner,id,IP_SLOW_RESIST,40),id);
        success=1;
        break;
        case ITEM_ACTION_GHOST:if(id==DOTA_ITEM_ETHEREAL_BLADE){
            if(enemy){
                int attr;
                attr=attribute(g,parameter_owner,0)+attribute(g,parameter_owner,1)+attribute(g,parameter_owner,2);
                dota_rules_item_damage(g,h,t,hpar(g,parameter_owner,id,IP_BLAST_DAMAGE_BASE,50)+attr*hpar(g,parameter_owner,id,IP_BLAST_STAT_MULTIPLIER,1),1);
                effect(g,h,t,IE_SLOW,duration,80,id);
            }
            effect(g,h,t,IE_ETHEREAL,duration,30,id);
        }
        else{
            effect(g,h,h,IE_GHOST,duration,30,id);
            if(id==DOTA_ITEM_CRELLAS_CROZIER)g->items.active_ticks[h][s]=(uint16_t)duration;
        }
        success=1;
        break;
        case ITEM_ACTION_BLADEMAIL:g->items.active_ticks[h][s]=(uint16_t)duration;
        success=1;
        break;
        case ITEM_ACTION_TOGGLE:g->items.toggle_bits[h]^=(uint8_t)(1u<<s);
        dota_items_invalidate(g,h);
        dota_rules_item_recalc(g,h);
        success=1;
        break;
        case ITEM_ACTION_ILLUSION:success=primitive(g,id,action,h,-1,x,y,hpar(g,parameter_owner,id,IP_IMAGES_COUNT,1),duration?duration:hticks(g,parameter_owner,id,IP_ILLUSION_DURATION,360));
        if(success)purge(g,h,0);
        break;
        case ITEM_ACTION_INVISIBLE:if(id==DOTA_ITEM_SMOKE_OF_DECEIT)aoe_status(g,h,u->x,u->y,1200,1,IE_INVISIBLE,duration,2,id);
        else{
            int move=hpar(g,parameter_owner,id,IP_WINDWALK_MOVEMENT_SPEED,0);
            effect(g,h,t,IE_INVISIBLE,duration,1,id);
            if(move)effect(g,h,t,IE_HASTE,duration,move,id);
        }
        success=1;
        break;
        case ITEM_ACTION_STATIC:effect(g,h,t,IE_STATIC,duration,hpar(g,parameter_owner,id,IP_STATIC_DAMAGE,225),id);
        success=1;
        break;
        case ITEM_ACTION_DOMINATE:if(!enemy||v->type==DOTA_HERO||(v->type==DOTA_ANCIENT&&id!=DOTA_ITEM_HELM_OF_THE_OVERLORD))return 0;
        success=primitive(g,id,action,h,t,x,y,hpar(g,parameter_owner,id,IP_HEALTH_MIN,1000),0);
        break;
        case ITEM_ACTION_ROOT:if(id==DOTA_ITEM_GUNGIR)aoe_status(g,h,x,y,hpar(g,parameter_owner,id,IP_RADIUS,325),0,IE_ROOT,duration,1,id);
        else{
            if(!enemy)return 0;
            effect(g,h,t,IE_ROOT,duration,1,id);
        }
        success=1;
        break;
        case ITEM_ACTION_BERSERK:effect(g,h,h,IE_AS,duration,hpar(g,parameter_owner,id,IP_BERSERK_BONUS_ATTACK_SPEED,100),id);
        effect(g,h,h,IE_HASTE,duration,hpar(g,parameter_owner,id,item_ranged(g,h)?IP_BERSERK_BONUS_MOVEMENT_SPEED:IP_BERSERK_BONUS_MOVEMENT_SPEED_MELEE,item_ranged(g,h)?6:12),id);
        effect(g,h,h,IE_ARMOR,duration,-hpar(g,parameter_owner,id,IP_BERSERK_ARMOR_REDUCTION,7),id);
        effect(g,h,h,IE_SILENCE,duration,1,id);
        effect(g,h,h,IE_SLOW_RESIST,duration,hpar(g,parameter_owner,id,item_ranged(g,h)?IP_BERSERK_SLOW_RESIST:IP_BERSERK_SLOW_RESIST_MELEE,item_ranged(g,h)?15:30),id);
        success=1;
        break;
        case ITEM_ACTION_SOULRING:if(u->hp<=hpar(g,parameter_owner,id,IP_ABILITYHEALTHCOST,170))return 0;
        u->hp-=hpar(g,parameter_owner,id,IP_ABILITYHEALTHCOST,170);
        g->items.soul_mana[h]=hpar(g,parameter_owner,id,IP_MANA_GAIN,170);
        g->items.soul_until[h]=g->tick+duration;
        g->items.active_ticks[h][s]=(uint16_t)duration;
        success=1;
        break;
        case ITEM_ACTION_DRUM:if(!g->items.charges[h][s]&&id==DOTA_ITEM_ANCIENT_JANGGO)return 0;
        aoe_status(g,h,u->x,u->y,1200,1,IE_AS,duration,hpar(g,parameter_owner,id,IP_BONUS_ATTACK_SPEED_PCT,35),id);
        aoe_status(g,h,u->x,u->y,1200,1,IE_HASTE,duration,hpar(g,parameter_owner,id,IP_BONUS_MOVEMENT_SPEED_PCT,13),id);
        if(id==DOTA_ITEM_BOOTS_OF_BEARING)aoe_status(g,h,u->x,u->y,1200,1,IE_SLOW_RESIST,hticks(g,parameter_owner,id,IP_BONUS_MS_DURATION,30),100,id);
        if(id==DOTA_ITEM_ANCIENT_JANGGO)g->items.charges[h][s]--;
        success=1;
        break;
        case ITEM_ACTION_VEIL:aoe_status(g,h,u->x,u->y,hpar(g,parameter_owner,id,IP_DEBUFF_RADIUS,900),0,IE_VULNERABLE,duration,hpar(g,parameter_owner,id,IP_SPELL_AMP,10),id);
        success=1;
        break;
        case ITEM_ACTION_STUN:if(!enemy)return 0;
        v->stun_ticks=imax(v->stun_ticks,hticks(g,parameter_owner,id,IP_STUN_DURATION,32));
        success=1;
        break;
        case ITEM_ACTION_DISARM:if(!enemy)return 0;
        if(id==DOTA_ITEM_CRIPPLING_CROSSBOW){
            dota_rules_item_damage(g,h,t,hpar(g,parameter_owner,id,IP_DAMAGE,25),1);
            effect(g,h,t,IE_SLOW,duration,50,id);
            effect(g,h,t,IE_HEAL_REDUCTION,duration,40,id);
        }
        else effect(g,h,t,IE_DISARM,duration,1,id);
        success=1;
        break;
        case ITEM_ACTION_PULL:if(!enemy)return 0;
        {int hx=u->x,hy=u->y,tx=v->x,ty=v->y,pct=hpar(g,parameter_owner,id,IP_PULL_DISTANCE_PCT,35);
         success=dota_rules_item_move(g,t,tx+(hx-tx)*pct/100,ty+(hy-ty)*pct/100,0);
         if(success)dota_rules_item_move(g,h,hx+(tx-hx)*pct/100,hy+(ty-hy)*pct/100,0);}
        break;
        case ITEM_ACTION_REVEAL:success=primitive(g,id,action,h,-1,x,y,hpar(g,parameter_owner,id,IP_ACTIVE_RADIUS,hpar(g,parameter_owner,id,IP_RADIUS,1050)),duration);

        break;
        case ITEM_ACTION_MOON:if(t>=10||(g->items.permanent[t]&1u))return 0;
        g->items.permanent[t]|=1u;
        ITEM_RAW(g,h,s)=-1;
        g->items.tracked[h][s]=-1;
        dota_rules_item_recalc(g,t);
        success=1;
        break;
        case ITEM_ACTION_SUMMON:success=primitive(g,id,action,h,-1,x,y,2,hticks(g,parameter_owner,id,IP_SUMMON_DURATION,1300));
        break;
        case ITEM_ACTION_NEUTRAL:
        if(id==DOTA_ITEM_KOBOLD_CUP)aoe_status(g,h,u->x,u->y,hpar(g,parameter_owner,id,IP_BUFF_RADIUS,1000),1,IE_HASTE,duration,10,id);
        else if(id==DOTA_ITEM_POLLIWOG_CHARM)effect(g,h,t,IE_REGEN_HP,duration,800,id);
        else if(id==DOTA_ITEM_ASH_LEGION_SHIELD){
            int area=hpar(g,parameter_owner,id,IP_BLOCK_RADIUS,800);
            for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&g->units[i].team==u->team&&within(u,&g->units[i],area)&&(g->units[i].type==DOTA_HERO||dota_item_world_owner(g,i)>=0||(i>=DOTA_SUMMON_FIRST&&i<DOTA_SUMMON_FIRST+DOTA_MAX_SUMMONS&&g->units[i].type==DOTA_SUMMON)))effect(g,h,i,IE_BARRIER_PHYSICAL,duration,hpar(g,parameter_owner,id,IP_BLOCK_AMOUNT,160),id);
            effect(g,h,h,IE_SLOW,duration,hpar(g,parameter_owner,id,IP_SLOW,20),id);
        }
        else if(id==DOTA_ITEM_DAGGER_OF_RISTUL)effect(g,h,h,IE_DAMAGE,duration,25,id);
        else if(id==DOTA_ITEM_STONEFEATHER_SATCHEL){
            g->items.toggle_bits[h]^=(uint8_t)(1u<<s);
            dota_items_invalidate(g,h);
        }
        else if(id==DOTA_ITEM_ESSENCE_RING){
            effect(g,h,h,IE_MAXHP,hticks(g,parameter_owner,id,IP_HEALTH_GAIN_DURATION,200),hpar(g,parameter_owner,id,IP_HEALTH_GAIN,240),id);
            u->max_hp+=240;
            heal(g,h,h,240,0);
        }
        else if(id==DOTA_ITEM_MEDALLION_OF_COURAGE)effect(g,h,t,IE_ARMOR,duration,enemy?-4:7,id);
        else if(id==DOTA_ITEM_SEEDS_OF_SERENITY){
            int current=dota_rules_item_raw_regen100(g,h)*dota_items_regen_percent(g,h)/100;
            int rate=hpx(g,parameter_owner,id,IP_AURA_HEALTH_REGEN,800)+(int)((long long)current*hpx(g,parameter_owner,id,IP_AURA_HEALTH_REGEN_PCT,2500)/10000);
            aoe_status(g,h,x,y,hpar(g,parameter_owner,id,IP_RADIUS,400),1,IE_REGEN_HP,duration,rate,id);
        }
        else if(id==DOTA_ITEM_JIDI_POLLEN_BAG){
            int life=hticks(g,parameter_owner,id,IP_DEBUFF_DURATION,180),step=hticks(g,parameter_owner,id,IP_DAMAGE_INTERVAL,20),area=hpar(g,parameter_owner,id,IP_DEBUFF_RADIUS,900);
            aoe_status(g,h,u->x,u->y,area,0,IE_PERCENT_DOT,life,hpx(g,parameter_owner,id,IP_HP_DAMAGE,900)*step/imax(1,life),id);
            aoe_status(g,h,u->x,u->y,area,0,IE_HEAL_REDUCTION,life,hpar(g,parameter_owner,id,IP_HEALTH_REGEN_LOSS,50),id);
        }
        else if(id==DOTA_ITEM_IDOL_OF_SCREEAUK)effect(g,h,h,IE_EVASION,duration,35,id);
        else if(id==DOTA_ITEM_FLAYERS_BOTA){
            effect(g,h,h,IE_DAMAGE,duration,15,id);
            effect(g,h,h,IE_AS,duration,30,id);
        }
        else if(id==DOTA_ITEM_METAMORPHIC_MANDIBLE){
            effect(g,h,h,IE_IMMUNITY,duration,50,id);
            effect(g,h,h,IE_HASTE,duration,20,id);
            effect(g,h,h,IE_ARMOR,duration,-u->armor*45/100,id);
        }
        else if(id==DOTA_ITEM_SPIDER_LEGS){
            effect(g,h,h,IE_HASTE,duration,20,id);
                g->items.active_ticks[h][s]=(uint16_t)duration;
            success=primitive(g,id,action,h,h,x,y,20,duration);
            break;
        }
        else if(id==DOTA_ITEM_HEAVY_BLADE){
            int w=radius/4;
            for(i=0;i<DOTA_MAX_UNITS;i++)if(live(g,i)&&distance(x,y,g->units[i].x,g->units[i].y)<=w*w)purge(g,i,hostile(g,h,i));
        }
        else return 0;
        success=1;
        break;
        default:return 0;
    }
 return success;
}
int dota_items_use(DotaGame*g,int h,int s,int x,int y,int target){
    DotaItemRecord r;
    DotaUnit*u,*v;
    int id,t=target,i,success=0,enemy=0,cost,action,blocked;
    if(!g||g->paused||g->state!=DOTA_PLAYING||!valid(g,h)||!live(g,h)||s<0||s>=DOTA_ITEMS_EQUIP_SLOTS)return 0;
    sync(g,h);
    u=&g->units[h];
    id=ITEM(g,h,s);
    if(id<0||!dota_items_read(id,&r)||!r.action||g->items.cooldown[h][s]||u->stun_ticks>0||dota_roster_stun(g,h)||dota_items_invulnerable(g,h))return 0;
    action=r.action;
    if(action==ITEM_ACTION_TELEPORT&&dota_items_teleport_cooldown(g,h))return 0;
    if(dota_items_lotus_value(id)&&g->objectives.lotus_cooldown[h])return 0;
    if(action==ITEM_ACTION_DOMINATE&&dota_item_world_dominate_cooldown(g,h)>0)return 0;
    cost=r.mana_cost*dota_items_mana_cost_percent(g,h)/100;
    if(cost>dota_items_available_mana(g,h))return 0;
    if(r.flags&ITEM_F_RECIPE)return 0;
    if(!live(g,t))t=h;
    v=&g->units[t];
    enemy=v->team!=u->team;
    if(enemy&&!(r.flags&ITEM_F_POINT)&&!dota_unit_visible(g,h,t))return 0;
    if((r.flags&ITEM_F_ENEMY)&&!(r.flags&ITEM_F_FRIENDLY)&&!enemy&&!(r.flags&ITEM_F_POINT))return 0;
    if((r.flags&ITEM_F_FRIENDLY)&&!(r.flags&ITEM_F_ENEMY)&&enemy)return 0;
    if(target>=0&&target<DOTA_MAX_UNITS&&live(g,target)&&r.cast_range){int cast_range=enemy?hpar(g,h,id,IP_CAST_RANGE_ENEMY,hpar(g,h,id,IP_ENEMY_CAST_RANGE,r.cast_range)):r.cast_range;if(!within(u,v,cast_range+dota_items_range_bonus(g,h,0)))return 0;}
    if((r.flags&ITEM_F_POINT)&&r.cast_range){
        int w=(r.cast_range+dota_items_range_bonus(g,h,0)+3)/4;
        if(distance(u->x,u->y,x,y)>w*w&&action!=ITEM_ACTION_BLINK)return 0;
    }
    if(action!=ITEM_ACTION_TELEPORT&&(r.flags&ITEM_F_CHANNEL))dota_items_cancel_teleport(g,h);
    blocked=enemy&&!(r.flags&ITEM_F_POINT)&&dota_items_blocks_spell(g,h,t);
    success=blocked?1:resolve_item_effect(g,h,s,id,&r,x,y,target,h,0);
    if(success&&enemy&&!(r.flags&(ITEM_F_POINT|ITEM_F_NO_TARGET))&&dota_items_reflects_spell(g,t)&&!g->items.recursion){
      g->items.recursion++;
      if(!dota_items_blocks_spell(g,t,h))resolve_item_effect(g,t,-1,id,&r,u->x,u->y,h,h,1);
      g->items.recursion--;
    }

    if(!success)return 0;
    if(action!=ITEM_ACTION_TELEPORT&&((unsigned)id>=sizeof(dota_item_channel_safe)*8u||!(dota_item_channel_safe[(unsigned)id/8]&(1u<<((unsigned)id%8)))))dota_items_cancel_teleport(g,h);
    dota_items_spend_mana(g,h,cost);
    if(action!=ITEM_ACTION_TREADS){
        int cd=r.cooldown_ticks*(100-dota_items_cooldown_percent(g,h))/100;
        if(id==DOTA_ITEM_HOLY_LOCKET)cd=hticks(g,h,id,IP_USE_COOLDOWN,260)*(100-dota_items_cooldown_percent(g,h))/100;
        if(action==ITEM_ACTION_TELEPORT){int boot=has(g,h,DOTA_ITEM_TRAVEL_BOOTS_2)?DOTA_ITEM_TRAVEL_BOOTS_2:(has(g,h,DOTA_ITEM_TRAVEL_BOOTS)?DOTA_ITEM_TRAVEL_BOOTS:-1);if(boot>=0)cd=hticks(g,h,boot,IP_TP_COOLDOWN,800)*(100-dota_items_cooldown_percent(g,h))/100;cd/=2;g->item_world.teleport.ready_tick[h]=g->tick+cd;}
        g->items.cooldown[h][s]=(uint16_t)cd;
        if(dota_items_lotus_value(id))g->objectives.lotus_cooldown[h]=(uint16_t)cd;
        if(r.shared_cooldown||action==ITEM_ACTION_REFRESH)for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS+3;i++){
            DotaItemRecord other;
            int other_id=i<7?ITEM_RAW(g,h,i):g->items.backpack[h][i-7].item;
            if(i!=s&&other_id>=0&&dota_items_read(other_id,&other)&&((r.shared_cooldown&&other.shared_cooldown==r.shared_cooldown)||(action==ITEM_ACTION_REFRESH&&other.action==ITEM_ACTION_REFRESH))){
                if(i<7)g->items.cooldown[h][i]=(uint16_t)imax(cd,g->items.cooldown[h][i]);
                else g->items.backpack[h][i-7].cooldown=(uint16_t)imax(cd,g->items.backpack[h][i-7].cooldown);
            }
        }
    }
    consume(g,h,s,&r);
    if((unsigned)id>=sizeof(dota_item_invisible_safe)*8u||!(dota_item_invisible_safe[(unsigned)id/8]&(1u<<((unsigned)id%8))))dota_rune_on_action(g,h);
    dota_rules_item_recalc(g,h);
    dota_rules_item_effect(g,DOTA_FX_BURST,u->team,u->x,u->y,x,y,imax(8,r.radius/4),10);
    return 1;
}
int dota_items_reflects_spell(const DotaGame*g,int target){
    return estatus(g,target,IE_REFLECT)!=0;
}
static void update_auras(DotaGame*g){
    unsigned seen[10][13];int old_damage[10],h,s,t,k;DotaItemRecord r;
    zero(seen,sizeof(seen));
    for(t=0;t<10;t++){old_damage[t]=g->items.aura[t].value[ITEM_STAT_DAMAGE];zero(&g->items.aura[t],sizeof(DotaItemStats));}
    for(h=0;h<10;h++)if(valid(g,h)&&live(g,h))for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
        int id=ITEM(g,h,s),radius,hp,mp,armor,as,mr,ls,damage;
        if(id<0||!dota_items_read(id,&r))continue;
        radius=hpar(g,h,id,IP_AURA_RADIUS,1200);hp=id==DOTA_ITEM_GUARDIAN_GREAVES?0:hpx(g,h,id,IP_AURA_HEALTH_REGEN,0);mp=hpx(g,h,id,IP_AURA_MANA_REGEN,hpx(g,h,id,IP_MANA_REGEN_AURA,0));
        armor=hpar(g,h,id,IP_AURA_POSITIVE_ARMOR,hpar(g,h,id,IP_ARMOR_AURA,0));as=hpar(g,h,id,IP_AURA_ATTACK_SPEED,0);mr=hpx(g,h,id,IP_MAGIC_RESISTANCE_AURA,0);ls=hpx(g,h,id,IP_LIFESTEAL_AURA,0);damage=hpar(g,h,id,IP_DAMAGE_AURA,0);
        for(t=0;t<10;t++)if(valid(g,t)&&live(g,t)&&within(&g->units[h],&g->units[t],radius)){
            DotaItemStats*a=&g->items.aura[t];unsigned mask=1u<<(id&31);
            if(seen[t][id>>5]&mask)continue;
            seen[t][id>>5]|=mask;
            if(g->units[t].team==g->units[h].team){
                a->value[ITEM_STAT_HEALTH_REGEN]+=hp;a->value[ITEM_STAT_MANA_REGEN]+=mp;a->value[ITEM_STAT_ARMOR]+=armor*100;
                if(as>0)a->value[ITEM_STAT_ATTACK_SPEED]+=as;
                a->value[ITEM_STAT_MAGIC_RESIST]=10000-(10000-a->value[ITEM_STAT_MAGIC_RESIST])*(10000-mr)/10000;
                a->value[ITEM_STAT_LIFESTEAL]+=ls;a->value[ITEM_STAT_DAMAGE]+=imax(0,g->units[t].damage-old_damage[t])*damage/100;
                a->value[ITEM_STAT_MOVE_FLAT]+=hpar(g,h,id,IP_AURA_MOVEMENT_SPEED,0);
            }else{
                if(as<0)a->value[ITEM_STAT_ATTACK_SPEED]+=as;
                a->value[ITEM_STAT_ARMOR]+=hpar(g,h,id,IP_AURA_NEGATIVE_ARMOR,0)*100;
                if(hpar(g,h,id,IP_AURA_SPELL_VULNERABILITY,0))effect(g,h,t,IE_VULNERABLE,22,hpar(g,h,id,IP_AURA_SPELL_VULNERABILITY,0),id);
            }
        }
        /* Tower/lane-creep targets share the original debuff auras. */
        for(k=10;k<DOTA_MAX_UNITS;k++)if(live(g,k)&&g->units[k].team!=g->units[h].team&&within(&g->units[h],&g->units[k],radius)){
            if(as<0)effect(g,h,k,IE_AS,22,as,id);
            if(hpar(g,h,id,IP_AURA_NEGATIVE_ARMOR,0))effect(g,h,k,IE_ARMOR,22,hpar(g,h,id,IP_AURA_NEGATIVE_ARMOR,0),id);
        }
    }
}
void dota_items_tick(DotaGame*g){
    int h,s,id,i,inventory_changed=0;
    DotaItemRecord r;
    DotaItemEffect*e;
    if(g->items.last_tick==g->tick)return;
    g->items.last_tick=g->tick;
 dota_neutral_tick(g);
 dota_items_special_tick(g);
    if(g->tick%20==0)update_auras(g);
    for(h=0;h<DOTA_HERO_COUNT;h++)if(valid(g,h)){
        if(g->items.soul_until[h]<=g->tick||!live(g,h)){g->items.soul_mana[h]=0;g->items.soul_until[h]=0;}
        sync(g,h);inventory_changed|=inventory_backpack_tick(g,h);
        for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
            if(g->items.cooldown[h][s])g->items.cooldown[h][s]=(uint16_t)imax(0,g->items.cooldown[h][s]-1-dota_items_special_water_cooldown_step(g,h,ITEM(g,h,s),s));
            if(g->items.active_ticks[h][s]){g->items.active_ticks[h][s]--;if(!g->items.active_ticks[h][s]&&ITEM(g,h,s)==DOTA_ITEM_SPIDER_LEGS)dota_rules_item_finish_free_path(g,h);}
            if(g->items.proc_wait[h][s])g->items.proc_wait[h][s]--;
            /* Charges are cooldown state, including while the returned item
             * is inactive or its owner is dead. No item passive is applied. */
            if(ITEM_RAW(g,h,s)==DOTA_ITEM_HAND_OF_MIDAS&&!g->items.proc_wait[h][s]&&g->items.charges[h][s]<2){g->items.charges[h][s]++;if(g->items.charges[h][s]<2)g->items.proc_wait[h][s]=(uint16_t)hticks(g,h,DOTA_ITEM_HAND_OF_MIDAS,IP_ABILITYCHARGERESTORETIME,1800);}
            if(g->items.buy_age[h][s]<201)g->items.buy_age[h][s]++;
        }
        {int gpm=0,enchant=g->items.neutral.enchantment[h];for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){int gear=ITEM(g,h,s);if(gear>=0)gpm+=hpx(g,h,gear,IP_BONUS_GPM,0);}if(enchant>=0)gpm+=hpx(g,h,enchant,IP_BONUS_GPM,0);
         g->items.income_fraction[h]+=dota_turbo_gold_reward(gpm);g->units[h].gold+=g->items.income_fraction[h]/(60*DOTA_HZ*100);g->items.income_fraction[h]%=(60*DOTA_HZ*100);}
        if(!live(g,h))continue;
        for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
            id=ITEM(g,h,s);
            if(id<0||!dota_items_read(id,&r))continue;
            if(id==DOTA_ITEM_BOTTLE){
                DotaPoint f=dota_fountains[g->units[h].team];
                if(distance(g->units[h].x,g->units[h].y,f.x,f.y)<=160*160)g->items.charges[h][s]=3;
            }
            if(id==DOTA_ITEM_HOLY_LOCKET&&g->tick%200==0&&g->items.charges[h][s]<r.max_charges)g->items.charges[h][s]++;
            if(id==DOTA_ITEM_ARMLET&&((g->items.toggle_bits[h]>>s)&1)){
                if(g->tick%4==0)g->units[h].hp=imax(1,g->units[h].hp-9);
            }
            if(g->tick%20==0){
                if(id==DOTA_ITEM_RADIANCE&&((g->items.toggle_bits[h]>>s)&1))aoe_damage(g,h,g->units[h].x,g->units[h].y,hpar(g,h,id,IP_AURA_RADIUS,650),hpar(g,h,id,IP_AURA_DAMAGE,60),1);
                if(id==DOTA_ITEM_CLOAK_OF_FLAMES)aoe_damage(g,h,g->units[h].x,g->units[h].y,hpar(g,h,id,IP_RADIUS,375),hpar(g,h,id,IP_DAMAGE,40),1);
                if(id==DOTA_ITEM_STORMCRAFTER){
                    int left=hpar(g,h,id,IP_MAX_TARGETS,2);
                    for(i=0;i<DOTA_MAX_UNITS&&left>0;i++)if(hostile(g,h,i)&&within(&g->units[h],&g->units[i],hpar(g,h,id,IP_RANGE,700))){
                        dota_rules_item_damage(g,h,i,hpar(g,h,id,IP_DAMAGE,70),1);
                        effect(g,h,i,IE_SLOW,hticks(g,h,id,IP_SLOW_DURATION,8),40,id);
                        left--;
                    }
                }
                if(id==DOTA_ITEM_CRELLAS_CROZIER){
                    for(i=0;i<10;i++)if(hostile(g,h,i)&&within(&g->units[h],&g->units[i],900)){

                        effect(g,h,i,IE_REGEN_STEAL,22,g->items.active_ticks[h][s]?90:30,id);
                        g->items.regen_transfer_fraction[h]+=dota_rules_item_raw_regen100(g,i)*(g->items.active_ticks[h][s]?90:30)/100;
                        if(g->items.regen_transfer_fraction[h]>=100){heal(g,h,h,g->items.regen_transfer_fraction[h]/100,0);g->items.regen_transfer_fraction[h]%=100;}
                        if(g->items.active_ticks[h][s]){
                            effect(g,h,i,IE_SLOW,40,6,id);
                            effect(g,h,h,IE_HASTE,40,6,id);
                        }
                    }
                }
                if(id==DOTA_ITEM_ANCIENT_JANGGO||id==DOTA_ITEM_BOOTS_OF_BEARING){
                    aoe_status(g,h,g->units[h].x,g->units[h].y,1200,1,IE_HASTE,22,0,id);
                }

            }
        }
    }
    if(inventory_changed){update_auras(g);for(h=0;h<10;h++)if(valid(g,h))dota_rules_item_recalc(g,h);}
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        int amount;
        e=&g->items.effects[i];
        if(!e->ticks)continue;
        if(!live(g,e->target)||g->units[e->target].generation!=e->generation){
            dota_items_remove_effect(g,i);
            continue;
        }
        /* Hero true regeneration is accumulated centrally. Basic units have
         * no central HP/MP pipeline, so their real item rates stay here. */
        if((e->kind==IE_REGEN_HP&&((e->tag&32767)==DOTA_ITEM_BOTTLE||e->target>=DOTA_HERO_COUNT))||(e->kind==IE_REGEN_MP&&((e->tag&32767)!=DOTA_ITEM_CLARITY||e->target>=DOTA_HERO_COUNT))||e->kind==IE_DOT||e->kind==IE_BATTLE_HUNGER){
            int value=e->value;
            if(e->kind==IE_REGEN_HP&&(e->tag&32767)!=DOTA_ITEM_BOTTLE)value=value*dota_items_regen_percent(g,e->target)/100;
            if(value>0){
                int sum=e->interval+value;
                amount=sum/2000;
                e->interval=(uint16_t)(sum%2000);
                if(amount){
                    if(e->kind==IE_DOT||e->kind==IE_BATTLE_HUNGER)dota_rules_item_damage(g,e->source,e->target,amount,e->kind==IE_BATTLE_HUNGER?2:1);
                    else heal(g,e->source,e->target,e->kind==IE_REGEN_HP?amount:0,e->kind==IE_REGEN_MP?amount:0);
                }
            }
        }
        if(e->kind==IE_PERCENT_DOT){
            int step=imin(255,imax(1,hticks(g,e->source,e->tag&32767,IP_DAMAGE_INTERVAL,20)));
            if(++e->pad>=step){
                long long scaled=(long long)g->units[e->target].max_hp*e->value+e->interval;
                e->pad=0;e->interval=(uint16_t)(scaled%10000);
                amount=(int)(scaled/10000);
                if(amount>0)dota_rules_item_damage(g,e->source,e->target,amount,1);
            }
        }
        if(e->kind==IE_HP_DRAIN&&g->tick%20==0)dota_rules_item_damage(g,e->source,e->target,g->units[e->target].hp*e->value/100,1);
        if(e->kind==IE_NULLIFY&&!dota_items_debuff_immune(g,e->target)&&g->tick%10==0)purge(g,e->target,1);
        /* A damage/purge callback may have cancelled this very effect. */
        if(!e->ticks)continue;
        e->ticks--;
        if(!e->ticks){
            dota_items_remove_effect(g,i);
            if(e->kind==IE_CYCLONE&&g->units[e->source].team!=g->units[e->target].team)dota_rules_item_damage(g,e->source,e->target,e->value,1);
            if(e->kind==IE_CYCLONE&&(e->tag&32767)==DOTA_ITEM_WIND_WAKER&&e->source==e->target)dota_rules_item_finish_free_path(g,e->target);
            if(e->kind==IE_SILENCE&&e->value>1&&e->interval)dota_rules_item_damage(g,e->source,e->target,e->interval*e->value/100,1);
            if(e->kind==IE_MAXHP)dota_rules_item_recalc(g,e->target);
        }
    }
}
int dota_items_statuses(const DotaGame*g,int h,DotaItemStatus*out,int capacity){
    int i,j,n=0;
    if(h<0||h>=DOTA_MAX_UNITS||capacity<=0)return 0;
    for(i=0;i<DOTA_ITEMS_EFFECTS;i++){
        const DotaItemEffect*e=&g->items.effects[i];
        int harmful,id;
        if(!e->ticks||(e->kind==IE_PIKE?e->source!=h:e->target!=h)||e->generation!=g->units[e->target].generation)continue;
        id=e->tag&32767;
        harmful=e->kind==IE_DOT||e->kind==IE_BATTLE_HUNGER||e->kind==IE_PERCENT_DOT||e->kind==IE_HP_DRAIN||e->kind==IE_REGEN_STEAL||e->kind==IE_SILENCE||e->kind==IE_ROOT||e->kind==IE_DISARM||e->kind==IE_SLOW||e->kind==IE_DECAY_SLOW||e->kind==IE_NULLIFY||e->kind==IE_HEAL_REDUCTION||e->kind==IE_VULNERABLE||e->kind==IE_BREAK||e->kind==IE_REVEAL||(e->kind==IE_ETHEREAL&&e->source<DOTA_MAX_UNITS&&g->units[e->source].team!=g->units[h].team)||e->value<0;
        for(j=0;j<n;j++)if(out[j].item==id&&out[j].harmful==harmful)break;
        if(j==n){
            if(n>=capacity)continue;
            out[n].item=id;
            out[n].harmful=harmful;
            out[n].ticks=e->ticks;
            out[n].stacks=1;
            n++;
        }
        else out[j].ticks=imax(out[j].ticks,e->ticks);
    }
    if(h<10)for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS&&n<capacity;i++)if(g->items.active_ticks[h][i]){
        int id=ITEM(g,h,i);
        for(j=0;j<n;j++)if(out[j].item==id&&!out[j].harmful)break;
        if(j==n){
            out[n].item=id;
            out[n].harmful=0;
            out[n].ticks=g->items.active_ticks[h][i];
            out[n].stacks=1;
            n++;
        }
    }
    if(n<capacity)n+=dota_items_special_statuses(g,h,out+n,capacity-n);
    if(n<capacity)n+=dota_item_world_statuses(g,h,out+n,capacity-n);
    return n;
}

void dota_items_apply_effect(DotaGame*g,int source,int target,int kind,int duration,int value,int id){effect(g,source,target,kind,duration,value,id);}
int dota_items_effect_value(const DotaGame*g,int target,int kind){return estatus(g,target,kind);}
void dota_items_restore(DotaGame*g,int source,int target,int hp,int mp){heal(g,source,target,hp,mp);}
int dota_items_total_attribute(DotaGame*g,int h,int attr){return attr>=0&&attr<3&&valid(g,h)?attribute(g,h,attr):0;}

int dota_items_base_attribute100(const DotaGame*g,int h,int attr){
    int base[3];if(!valid(g,h)||(unsigned)attr>=3u)return 0;
    dota_progression_base_attributes100(&g->units[h],&base[0],&base[1],&base[2]);return base[attr];
}

int dota_items_slow_resistance(DotaGame*g,int h){int s,pct;if(!valid(g,h))return 0;pct=imax(estatus(g,h,IE_SLOW_RESIST),dota_items_special_slow_resistance(g,h));for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){int id=ITEM(g,h,s);if(id>=0)pct=imax(pct,hpar(g,h,id,IP_SLOW_RESISTANCE,hpar(g,h,id,IP_SLOW_RESIST,0)));}return imin(100,pct);}
int dota_items_available_mana(const DotaGame*g,int h){return h>=0&&h<DOTA_MAX_UNITS?g->units[h].mana+(h<10&&g->items.soul_until[h]>g->tick?g->items.soul_mana[h]:0):0;}
int dota_items_spend_mana(DotaGame*g,int h,int amount){int buffer;if(h<0||h>=DOTA_MAX_UNITS||amount<0||dota_items_available_mana(g,h)<amount)return 0;buffer=h<10&&g->items.soul_until[h]>g->tick?imin(amount,g->items.soul_mana[h]):0;if(h<10)g->items.soul_mana[h]-=buffer;g->units[h].mana-=amount-buffer;return 1;}

int dota_items_receive(DotaGame*g,int h,int id,int age){return receive_item(g,h,id,age,h,0);}
int dota_items_receive_rapier(DotaGame*g,int h,int owner,int mode){return receive_item(g,h,DOTA_ITEM_RAPIER,201,owner,mode);}

void dota_items_on_death(DotaGame*g,int target,int source){int s,id,i;if(target<0||target>=DOTA_HERO_COUNT||g->units[target].type!=DOTA_HERO)return;

        dota_items_special_on_death(g,target);
        for(s=0;s<6;s++){int dropped=ITEM(g,target,s);if((dropped==DOTA_ITEM_GEM||dropped==DOTA_ITEM_RAPIER)&&dota_item_world_drop_mode(g,dropped==DOTA_ITEM_RAPIER?g->items.rapier_owner[target][s]:target,dropped,g->units[target].x,g->units[target].y,(g->items.toggle_bits[target]>>s)&1)){ITEM_RAW(g,target,s)=-1;g->items.tracked[target][s]=-1;dota_items_invalidate(g,target);}}
        for(i=0;i<DOTA_HERO_COUNT;i++)if(valid(g,i)&&g->units[i].team!=g->units[target].team){
          int assisted=i==source||(g->assist_tick[target][i]>0&&g->tick+1-g->assist_tick[target][i]<=DOTA_ASSIST_WINDOW_TICKS);
          if(assisted)for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++)if(ITEM(g,i,s)==DOTA_ITEM_DESOLATOR){int add=par(DOTA_ITEM_DESOLATOR,i==source?IP_BONUS_DAMAGE_PER_KILL:IP_BONUS_DAMAGE_PER_ASSIST,i==source?2:1);g->items.uses[i][s]=(uint8_t)imin(par(DOTA_ITEM_DESOLATOR,IP_MAX_DAMAGE,30),g->items.uses[i][s]+add);}
        }

        for(i=0;i<DOTA_HERO_COUNT;i++)if(valid(g,i)&&live(g,i)&&g->units[i].team!=g->units[target].team&&within(&g->units[i],&g->units[target],1500)){
            for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){
                id=ITEM(g,i,s);
                if(id==DOTA_ITEM_URN_OF_SHADOWS||id==DOTA_ITEM_SPIRIT_VESSEL||id==DOTA_ITEM_ESSENCE_DISTILLER){
                    g->items.charges[i][s]=(uint16_t)imin(65535,g->items.charges[i][s]+(g->items.charges[i][s]?1:2));
                    break;
                }
            }
        }
    
}

int dota_items_outgoing_spell_percent(const DotaGame*g,int h){int i,pct=100;if(h<0||h>=DOTA_MAX_UNITS)return pct;for(i=0;i<DOTA_ITEMS_EFFECTS;i++){const DotaItemEffect*e=&g->items.effects[i];if(e->ticks&&e->target==h&&e->kind==IE_DAMAGE_AMP&&e->generation==g->units[h].generation)pct=imin(pct,100+e->value);}return imax(0,pct);}
void dota_items_capture_incoming(DotaGame*g,int source,int target,int raw,int magical){
 int s,bm=0,active=0,vest=0,returned;if(raw<=0||!valid(g,target)||!live(g,source)||g->units[source].team==g->units[target].team||g->units[source].type==DOTA_TORMENTOR||g->items.reflection_guard||magical==3||magical==4||magical==5||dota_items_debuff_immune(g,source))return;
 sync(g,target);
 for(s=0;s<DOTA_ITEMS_EQUIP_SLOTS;s++){int id=ITEM(g,target,s);if(id==DOTA_ITEM_BLADE_MAIL){if(magical==0&&!g->items.recursion)bm=imax(bm,hpar(g,target,id,IP_PASSIVE_REFLECTION_CONSTANT,10)+raw*hpar(g,target,id,IP_PASSIVE_REFLECTION_PCT,15)/100);if(g->items.active_ticks[target][s])active=imax(active,raw*hpar(g,target,id,IP_ACTIVE_REFLECTION_PCT,85)/100);}if(id==DOTA_ITEM_CHIPPED_VEST&&magical==0&&!g->items.recursion)vest=imax(vest,hpar(g,target,id,g->units[source].type==DOTA_HERO?IP_DAMAGE_RETURN_HERO:IP_DAMAGE_RETURN_CREEP,30));}
 returned=bm+active+vest;if(returned>0){g->items.reflection_guard++;g->items.recursion++;dota_rules_item_damage(g,target,source,returned,magical);g->items.recursion--;g->items.reflection_guard--;}
}

int dota_items_attack_range_for(const DotaGame*g,int h,int target,int normal){int i;if(!valid(g,h))return normal;for(i=0;i<DOTA_ITEMS_EFFECTS;i++){const DotaItemEffect*e=&g->items.effects[i];if(e->ticks&&e->kind==IE_PIKE&&e->source==h&&e->target==target&&e->value>0&&e->generation==g->units[target].generation)return DOTA_WORLD*4;}return normal;}
