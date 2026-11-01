#ifndef DOTA_INVENTORY_IMPL_H
#define DOTA_INVENTORY_IMPL_H
#include "backpack_data.h"
/* Included only by items.c: the ownership/recipe path sees raw objects,
 * whereas all combat/stat readers use dota_items_equipped_item. */
typedef struct {
    int id,charge,cd,age,uses,toggle,owner,active,proc,inactive;
    int seen,grace,phase,wrap_present,wrap_charges,wrap_wait,water;
} Bag;
static void update_auras(DotaGame *g);
static void bag_new(Bag *b,int id,int age,int binding,int mode){
    DotaItemRecord r;zero(b,sizeof(*b));b->id=id;b->owner=-2;
    if(id<0||!dota_items_read(id,&r))return;
    b->charge=r.initial_charges;b->age=imin(201,age);b->owner=id==DOTA_ITEM_RAPIER?binding:-2;
    b->toggle=id==DOTA_ITEM_RADIANCE||(id==DOTA_ITEM_RAPIER&&mode);
    b->proc=id==DOTA_ITEM_HAND_OF_MIDAS?ticks(id,IP_ABILITYCHARGERESTORETIME,1800):(id==DOTA_ITEM_AEON_DISK?ticks(id,IP_INITIAL_COOLDOWN,120):0);
    if(id==DOTA_ITEM_CONSECRATED_WRAPS){b->wrap_present=1;b->wrap_charges=par(id,IP_ITEMINITIALCHARGES,3);b->wrap_wait=ticks(id,IP_ABILITYCHARGERESTORETIME,160);}
}
static void bag_load_backpack(Bag *b,const DotaBackpackItem *p){
    b->id=p->item;b->charge=p->charges;b->cd=p->cooldown;b->age=p->buy_age;b->uses=p->uses;b->toggle=p->toggle;b->owner=p->rapier_owner;b->active=p->active_ticks;b->proc=p->proc_wait;b->inactive=p->inactive_ticks;
    b->seen=p->backpack_seen;b->grace=p->grace_until;b->phase=p->rate_phase;b->wrap_present=p->wrap_present;b->wrap_charges=p->wrap_charges;b->wrap_wait=p->wrap_wait;b->water=p->water_cd_fraction;
}
static void bag_store_backpack(DotaBackpackItem *p,const Bag *b){
    p->item=(int16_t)b->id;p->charges=(uint16_t)imin(65535,b->charge);p->cooldown=(uint16_t)b->cd;p->buy_age=(uint16_t)b->age;p->uses=(uint8_t)b->uses;p->toggle=(uint8_t)b->toggle;p->rapier_owner=(int8_t)b->owner;p->active_ticks=(uint16_t)b->active;p->proc_wait=(uint16_t)b->proc;p->inactive_ticks=(uint16_t)b->inactive;
    p->backpack_seen=(uint8_t)b->seen;p->grace_until=b->grace;p->rate_phase=(uint8_t)b->phase;p->wrap_present=(uint8_t)b->wrap_present;p->wrap_charges=(uint8_t)b->wrap_charges;p->wrap_wait=(uint16_t)b->wrap_wait;p->water_cd_fraction=(uint8_t)b->water;
}
static void bag_read(DotaGame*g,int h,Bag*b){
    int s;sync(g,h);
    for(s=0;s<6;s++)if(ITEM_RAW(g,h,s)==DOTA_ITEM_CONSECRATED_WRAPS)(void)dota_items_special_charges(g,h,DOTA_ITEM_CONSECRATED_WRAPS,s);
    for(s=0;s<6;s++){
        b[s].id=ITEM_RAW(g,h,s);b[s].charge=g->items.charges[h][s];b[s].cd=g->items.cooldown[h][s];b[s].age=g->items.buy_age[h][s];b[s].uses=g->items.uses[h][s];b[s].toggle=(g->items.toggle_bits[h]>>s)&1;b[s].owner=g->items.rapier_owner[h][s];
        b[s].active=g->items.active_ticks[h][s];b[s].proc=g->items.proc_wait[h][s];b[s].inactive=g->items.inactive_ticks[h][s];b[s].seen=g->items.backpack_seen[h][s];b[s].grace=g->items.backpack_grace_until[h][s];b[s].phase=g->items.backpack_rate_phase[h][s];
        b[s].wrap_present=g->items.special.wrap_present[h][s];b[s].wrap_charges=g->items.special.wrap_charges[h][s];b[s].wrap_wait=g->items.special.wrap_wait[h][s];b[s].water=g->items.special.water_cd_fraction[h][s];
    }
    for(s=0;s<3;s++)bag_load_backpack(&b[6+s],&g->items.backpack[h][s]);
}
static void bag_enter_backpack(DotaGame *g,Bag *b){if(b->id>=0&&!b->seen){b->seen=1;b->grace=g->tick+3*DOTA_HZ;}}
static void bag_write(DotaGame*g,int h,Bag*b){
    int s;
    for(s=0;s<6;s++){
        ITEM_RAW(g,h,s)=b[s].id;g->items.tracked[h][s]=(int16_t)b[s].id;g->items.rapier_owner[h][s]=(int8_t)(b[s].id==DOTA_ITEM_RAPIER&&b[s].owner==-2?h:b[s].owner);
        g->items.charges[h][s]=(uint16_t)imin(65535,b[s].charge);g->items.cooldown[h][s]=(uint16_t)b[s].cd;g->items.buy_age[h][s]=(uint16_t)b[s].age;g->items.uses[h][s]=(uint8_t)b[s].uses;g->items.active_ticks[h][s]=(uint16_t)b[s].active;g->items.proc_wait[h][s]=(uint16_t)b[s].proc;
        g->items.inactive_ticks[h][s]=(uint16_t)b[s].inactive;g->items.backpack_seen[h][s]=(uint8_t)b[s].seen;g->items.backpack_grace_until[h][s]=b[s].grace;g->items.backpack_rate_phase[h][s]=(uint8_t)b[s].phase;
        g->items.special.wrap_present[h][s]=(uint8_t)b[s].wrap_present;g->items.special.wrap_charges[h][s]=(uint8_t)b[s].wrap_charges;g->items.special.wrap_wait[h][s]=(uint16_t)b[s].wrap_wait;g->items.special.water_cd_fraction[h][s]=(uint8_t)b[s].water;
        g->items.toggle_bits[h]=(uint8_t)((g->items.toggle_bits[h]&~(1u<<s))|((unsigned)b[s].toggle<<s));
    }
    for(s=0;s<3;s++){bag_enter_backpack(g,&b[6+s]);bag_store_backpack(&g->items.backpack[h][s],&b[6+s]);}
    dota_items_invalidate(g,h);update_auras(g);
    for(s=0;s<10;s++)if(valid(g,s))dota_rules_item_recalc(g,s);
    dota_sight_invalidate();
}
/* Source ItemStackableMax3/2/3 and ItemRecipeConsumesCharges for the two free
 * lotus recipes. HIT1 max_charges is an ability-charge field, not this limit. */
static int lotus_stack_cap(int id){return id==DOTA_ITEM_FAMANGO?3:(id==DOTA_ITEM_GREAT_FAMANGO?2:(id==DOTA_ITEM_GREATER_FAMANGO?3:0));}
static int can_stack(const Bag*b,int id,int charge){int cap=lotus_stack_cap(id);return b->id==id&&(!cap||b->charge+charge<=cap);}
static int combine_lotus(Bag*b,int n){
    int guard,kind,changed=0;
    for(guard=0;guard<32;guard++){
        int progress=0;
        for(kind=0;kind<2;kind++){
            int from=kind?DOTA_ITEM_GREAT_FAMANGO:DOTA_ITEM_FAMANGO,to=kind?DOTA_ITEM_GREATER_FAMANGO:DOTA_ITEM_GREAT_FAMANGO;
            int need=kind?2:3,left=need,i,output=-1,used[15]={0},cd=0,age=0,lock=0;
            for(i=0;i<n&&left;i++)if(b[i].id==from){used[i]=imin(left,b[i].charge);left-=used[i];}
            if(left)continue;
            /* Plan the output before consuming anything. Existing result
             * stacks win, then a consumed component slot, then an empty slot. */
            for(i=0;i<n;i++)if(can_stack(&b[i],to,1)){output=i;break;}
            if(output<0)for(i=0;i<n;i++)if(used[i]&&used[i]==b[i].charge){output=i;break;}
            if(output<0)for(i=0;i<n;i++)if(b[i].id<0){output=i;break;}
            if(output<0)continue;
            for(i=0;i<n;i++)if(used[i]||i==output){cd=imax(cd,b[i].cd);age=imax(age,b[i].age);lock=imax(lock,b[i].inactive);}
            for(i=0;i<n;i++)if(used[i]){b[i].charge-=used[i];if(!b[i].charge)bag_new(&b[i],-1,0,-2,0);}
            if(b[output].id==to)b[output].charge++;
            else bag_new(&b[output],to,age,-2,0);
            b[output].cd=cd;b[output].age=age;b[output].inactive=lock;
            changed=progress=1;break;
        }
        if(!progress)break;
    }
    return changed;
}
static int combine(Bag*b,int n){
    int present=0,probe,changed=combine_lotus(b,n),again=1,guard=0,i,j;DotaItemRecipe r;DotaItemRecord rr;
    for(probe=0;probe<n;probe++)if(b[probe].id>=0)present++;
    if(present<2)return changed;
    while(again&&guard++<32){
        again=0;
        for(i=0;i<dota_items_recipes_count();i++){
            int used=0,slots[7],num=0,ok=1,cd=0,charge=0,age=0,lock=0;
            if(!dota_items_recipe_read(i,&r)||!r.count||!dota_items_read(r.recipe,&rr))continue;
            if(r.recipe==DOTA_ITEM_RECIPE_GREAT_FAMANGO||r.recipe==DOTA_ITEM_RECIPE_GREATER_FAMANGO)continue;
            for(j=0;j<r.count+((rr.flags&ITEM_F_AUTO_RECIPE)?0:1);j++){
                int k,want=j<r.count?r.component[j]:r.recipe;
                for(k=0;k<n;k++)if(!(used&(1<<k))&&b[k].id==want)break;
                if(k==n){ok=0;break;}
                slots[num++]=k;used|=1<<k;cd=imax(cd,b[k].cd);age=imax(age,b[k].age);charge+=b[k].charge;lock=imax(lock,b[k].inactive);
            }
            if(ok&&num){
                int at=slots[0],obs=0,sentry=0;
                if(!dota_items_read(r.result,&rr))continue;
                if(r.result==DOTA_ITEM_WARD_DISPENSER)for(j=0;j<num;j++){int k=slots[j];if(b[k].id==DOTA_ITEM_WARD_OBSERVER)obs+=b[k].charge;else if(b[k].id==DOTA_ITEM_WARD_SENTRY)sentry+=b[k].charge;}
                for(j=0;j<num;j++)bag_new(&b[slots[j]],-1,0,-2,0);
                bag_new(&b[at],r.result,age,-2,0);b[at].cd=cd;b[at].inactive=lock;
                if(r.result==DOTA_ITEM_MAGIC_WAND||r.result==DOTA_ITEM_HOLY_LOCKET||r.result==DOTA_ITEM_SPIRIT_VESSEL||r.result==DOTA_ITEM_ESSENCE_DISTILLER)b[at].charge=imin(rr.max_charges?rr.max_charges:65535,charge);
                if(r.result==DOTA_ITEM_WARD_DISPENSER)b[at].charge=imin(255,obs)|(imin(255,sentry)<<8);
                if(r.result==DOTA_ITEM_POWER_TREADS)for(j=0;j<r.count;j++){if(r.component[j]==DOTA_ITEM_ROBE)b[at].uses=2;else if(r.component[j]==DOTA_ITEM_BELT_OF_STRENGTH)b[at].uses=0;else if(r.component[j]==DOTA_ITEM_BOOTS_OF_ELVES)b[at].uses=1;}
                again=changed=1;break;
            }
        }
    }
    return changed;
}
static int bag_fit(Bag *b,int n){
    int i,j,used=0;
    for(i=0;i<n;i++)if(b[i].id>=0)used++;
    if(used>DOTA_INVENTORY_SLOTS)return 0;
    for(i=DOTA_INVENTORY_SLOTS;i<n;i++)if(b[i].id>=0){for(j=0;j<DOTA_INVENTORY_SLOTS;j++)if(b[j].id<0)break;if(j==DOTA_INVENTORY_SLOTS)return 0;b[j]=b[i];bag_new(&b[i],-1,0,-2,0);}
    for(i=6;i<9;i++)if(b[i].id>=0&&!backpack_allowed(b[i].id)){
        for(j=0;j<6;j++)if(b[j].id<0)break;
        if(j==6)return 0;
        b[j]=b[i];bag_new(&b[i],-1,0,-2,0);
    }
    return 1;
}
static int receive_item(DotaGame*g,int h,int id,int age,int binding,int mode){
    Bag b[10];DotaItemRecord r;int s;
    if(!valid(g,h)||!dota_items_read(id,&r))return 0;
    if(id==DOTA_ITEM_AGHANIMS_SHARD||id==DOTA_ITEM_AGHANIMS_SHARD_ROSHAN||id==DOTA_ITEM_ULTIMATE_SCEPTER_2||id==DOTA_ITEM_ULTIMATE_SCEPTER_ROSHAN){unsigned bit=(id==DOTA_ITEM_AGHANIMS_SHARD||id==DOTA_ITEM_AGHANIMS_SHARD_ROSHAN)?4u:2u;if((g->items.permanent[h]&bit)||(bit==4u&&(g->tormentor.shard_mask&(1u<<h))))return 0;g->items.permanent[h]|=bit;dota_rules_item_recalc(g,h);return 1;}
    bag_read(g,h,b);
    if(r.flags&ITEM_F_STACKABLE)for(s=0;s<9;s++)if(can_stack(&b[s],id,r.initial_charges?r.initial_charges:1)){b[s].charge+=r.initial_charges?r.initial_charges:1;b[s].age=imax(b[s].age,age);if(dota_items_lotus_value(id))combine(b,9);bag_write(g,h,b);return 1;}
    bag_new(&b[9],id,age,binding,mode);combine(b,10);if(!bag_fit(b,10))return 0;bag_write(g,h,b);return 1;
}
int dota_items_receive_batch(DotaGame*g,int h,const int16_t*incoming,const uint16_t*age,int count){
    Bag b[15];DotaItemRecord r;int i,j,n=9;unsigned upgrades;
    if(!valid(g,h)||!incoming||count<1||count>6)return 0;
    bag_read(g,h,b);upgrades=g->items.permanent[h];
    for(i=0;i<count;i++){
        int id=incoming[i],stacked=0;if(id<0)continue;if(!dota_items_read(id,&r))return 0;
        if(id==DOTA_ITEM_AGHANIMS_SHARD||id==DOTA_ITEM_AGHANIMS_SHARD_ROSHAN||id==DOTA_ITEM_ULTIMATE_SCEPTER_2||id==DOTA_ITEM_ULTIMATE_SCEPTER_ROSHAN){unsigned bit=(id==DOTA_ITEM_AGHANIMS_SHARD||id==DOTA_ITEM_AGHANIMS_SHARD_ROSHAN)?4u:2u;if((upgrades&bit)||(bit==4u&&(g->tormentor.shard_mask&(1u<<h))))return 0;upgrades|=bit;continue;}
        if(r.flags&ITEM_F_STACKABLE)for(j=0;j<n;j++)if(can_stack(&b[j],id,r.initial_charges?r.initial_charges:1)){b[j].charge+=r.initial_charges?r.initial_charges:1;b[j].age=imax(b[j].age,age?age[i]:201);stacked=1;break;}
        if(!stacked)bag_new(&b[n++],id,age?age[i]:201,h,0);
    }
    combine(b,n);if(!bag_fit(b,n))return 0;g->items.permanent[h]=upgrades;bag_write(g,h,b);return 1;
}
int dota_items_can_receive_batch(const DotaGame*g,int h,const int16_t*incoming,int count){
    Bag b[15];DotaItemRecord r;int i,j,n=9;unsigned upgrades;
    if(!valid(g,h)||!incoming||count<1||count>6)return 0;
    /* A pure capacity query never syncs or mutates inventory. */
    zero(b,sizeof(b));for(i=0;i<9;i++){
        b[i].id=dota_inventory_item(g,h,i);
        if(dota_items_lotus_value(b[i].id)){
            if(i<6&&g->items.tracked[h][i]!=b[i].id){if(!dota_items_read(b[i].id,&r))return 0;b[i].charge=r.initial_charges;}
            else b[i].charge=dota_inventory_charges(g,h,i);
        }
    }upgrades=g->items.permanent[h];
    for(i=0;i<count;i++){
        int id=incoming[i],stacked=0;if(id<0)continue;if(!dota_items_read(id,&r))return 0;
        if(id==DOTA_ITEM_AGHANIMS_SHARD||id==DOTA_ITEM_AGHANIMS_SHARD_ROSHAN||id==DOTA_ITEM_ULTIMATE_SCEPTER_2||id==DOTA_ITEM_ULTIMATE_SCEPTER_ROSHAN){unsigned bit=(id==DOTA_ITEM_AGHANIMS_SHARD||id==DOTA_ITEM_AGHANIMS_SHARD_ROSHAN)?4u:2u;if((upgrades&bit)||(bit==4u&&(g->tormentor.shard_mask&(1u<<h))))return 0;upgrades|=bit;continue;}
        if(r.flags&ITEM_F_STACKABLE)for(j=0;j<n;j++)if(can_stack(&b[j],id,r.initial_charges?r.initial_charges:1)){if(dota_items_lotus_value(id))b[j].charge+=r.initial_charges?r.initial_charges:1;stacked=1;break;}
        if(!stacked)bag_new(&b[n++],id,201,h,0);
    }
    combine(b,n);return bag_fit(b,n);
}
int dota_items_auto_combine(DotaGame*g,int h){Bag b[9];int changed;if(!valid(g,h))return 0;bag_read(g,h,b);changed=combine(b,9);if(changed&&bag_fit(b,9))bag_write(g,h,b);else changed=0;return changed;}
int dota_items_sell(DotaGame*g,int h,int slot){
    Bag b[9];DotaItemRecord r;int price;if(!valid(g,h)||(unsigned)slot>=9u)return 0;bag_read(g,h,b);
    if(b[slot].id<0||!dota_items_read(b[slot].id,&r)||!(r.flags&ITEM_F_SELLABLE))return 0;
    price=b[slot].age<=200?r.cost:r.cost/2;if((r.flags&ITEM_F_STACKABLE)&&r.initial_charges)price=price*b[slot].charge/r.initial_charges;
    g->units[h].gold+=price;bag_new(&b[slot],-1,0,-2,0);bag_write(g,h,b);return price?price:1;
}
int dota_items_disassemble(DotaGame*g,int h,int slot){
    Bag b[9];DotaItemRecipe r;DotaItemRecord item,rr;int i,j,free=0,need;
    if(!valid(g,h)||(unsigned)slot>=9u)return 0;
    bag_read(g,h,b);
    if(b[slot].id<0||!dota_items_read(b[slot].id,&item)||!(item.flags&ITEM_F_DISASSEMBLE))return 0;
    for(i=0;i<9;i++)if(b[i].id<0)free++;
    for(i=0;i<dota_items_recipes_count();i++)if(dota_items_recipe_read(i,&r)&&r.result==b[slot].id&&r.count){
        int cd=b[slot].cd,age=b[slot].age;dota_items_read(r.recipe,&rr);need=r.count+((rr.flags&ITEM_F_AUTO_RECIPE)?0:1);if(need>free+1)return 0;bag_new(&b[slot],-1,0,-2,0);
        for(j=0;j<need;j++){int s;for(s=0;s<9;s++)if(b[s].id<0)break;bag_new(&b[s],j<r.count?r.component[j]:r.recipe,age,h,0);b[s].cd=cd;}
        if(!bag_fit(b,9))return 0;
        bag_write(g,h,b);return 1;
    }
    return 0;
}
int dota_inventory_item(const DotaGame*g,int h,int slot){if(!g||(unsigned)h>=10u||(unsigned)slot>=9u)return -1;return slot<6?g->units[h].inventory[slot]:g->items.backpack[h][slot-6].item;}
int dota_inventory_inactive_ticks(const DotaGame*g,int h,int slot){return g&&(unsigned)h<10u&&(unsigned)slot<6u?g->items.inactive_ticks[h][slot]:0;}
int dota_inventory_cooldown(const DotaGame*g,int h,int slot){
    const DotaBackpackItem *p;int wait;if(!g||(unsigned)h>=10u||(unsigned)slot>=9u)return 0;
    if(slot<6)return imax(dota_items_cooldown(g,h,slot),g->items.inactive_ticks[h][slot]);
    p=&g->items.backpack[h][slot-6];wait=imax(imax(imax(p->cooldown,p->item==DOTA_ITEM_HAND_OF_MIDAS&&p->charges?p->cooldown:p->proc_wait),dota_items_special_cooldown(g,h,p->item)),p->item==DOTA_ITEM_TPSCROLL?dota_items_teleport_cooldown(g,h):0);
    return dota_items_lotus_value(p->item)?imax(wait,g->objectives.lotus_cooldown[h]):wait;
}
int dota_inventory_charges(const DotaGame*g,int h,int slot){
    const DotaBackpackItem *p;int n;if(!g||(unsigned)h>=10u||(unsigned)slot>=9u)return 0;if(slot<6)return dota_items_charges(g,h,slot);
    p=&g->items.backpack[h][slot-6];if(p->item==DOTA_ITEM_CONSECRATED_WRAPS)return p->wrap_charges;n=p->charges;return p->item==DOTA_ITEM_WARD_DISPENSER?(n&255)+(n>>8):n;
}
static int inventory_near_shop(const DotaGame *g,int h){
    int i;const DotaUnit *u=&g->units[h];DotaPoint p=dota_fountains[u->team];if(distance(u->x,u->y,p.x,p.y)<=DOTA_SHOP_RANGE*DOTA_SHOP_RANGE)return 1;
    for(i=0;i<2;i++){p=dota_secret_shops[i];if(distance(u->x,u->y,p.x,p.y)<=DOTA_SHOP_RANGE*DOTA_SHOP_RANGE)return 1;}return 0;
}
int dota_inventory_move(DotaGame*g,int h,int from,int to){
    Bag b[9],temp;int i,old_from,old_to,old_free;
    if(!valid(g,h)||(unsigned)from>=9u||(unsigned)to>=9u||from==to)return 0;
    bag_read(g,h,b);if(b[from].id<0)return 0;
    if((to>=6&&!backpack_allowed(b[from].id))||(from>=6&&b[to].id>=0&&!backpack_allowed(b[to].id)))return 0;
    old_from=b[from].id;old_to=b[to].id;old_free=dota_items_free_path(g,h);temp=b[from];b[from]=b[to];b[to]=temp;
    if((from<6)!=(to<6)){
        int main=from<6?from:to,back=from>=6?from:to;
        if(b[main].id>=0){int no_lock=inventory_near_shop(g,h)||(b[main].grace>g->tick);b[main].inactive=no_lock?0:6*DOTA_HZ;}
        bag_enter_backpack(g,&b[back]);
        if(g->units[h].channel_ticks>0&&(g->item_world.channel[h].item==(from<6?old_from:old_to))){g->item_world.channel[h].until=0;g->units[h].channel_ticks=0;}
    }
    /* Only main-slot positions changed: keep the same item instances and do
     * not auto-combine as a side effect of a deliberate drag/drop. */
    bag_write(g,h,b);if(old_free&&!dota_items_free_path(g,h))dota_rules_item_finish_free_path(g,h);for(i=0;i<6;i++)if(b[i].id==DOTA_ITEM_SPIDER_LEGS&&b[i].inactive)dota_rules_item_finish_free_path(g,h);return 1;
}
static int inventory_backpack_tick(DotaGame *g,int h){
    int i,changed=0;
    for(i=0;i<6;i++)if(g->items.inactive_ticks[h][i]){g->items.inactive_ticks[h][i]--;if(!g->items.inactive_ticks[h][i]){dota_items_invalidate(g,h);changed=1;}}
    for(i=0;i<3;i++){
        DotaBackpackItem *p=&g->items.backpack[h][i];if(p->item<0)continue;
        p->rate_phase^=1u;if(p->active_ticks)p->active_ticks--;if(p->buy_age<201)p->buy_age++;if(p->inactive_ticks)p->inactive_ticks--;
        if(!p->rate_phase&&!backpack_cd_paused(p->item)){
            if(p->cooldown)p->cooldown--;
            if(p->proc_wait)p->proc_wait--;
            if(p->item==DOTA_ITEM_HAND_OF_MIDAS&&!p->proc_wait&&p->charges<2){p->charges++;if(p->charges<2)p->proc_wait=(uint16_t)hticks(g,h,p->item,IP_ABILITYCHARGERESTORETIME,1800);}
            if(p->wrap_present&&p->wrap_charges<par(p->item,IP_ABILITYCHARGES,3)){if(p->wrap_wait)p->wrap_wait--;if(!p->wrap_wait){p->wrap_charges++;p->wrap_wait=(uint16_t)hticks(g,h,p->item,IP_ABILITYCHARGERESTORETIME,160);}}
        }
    }
    return changed;
}
#endif
