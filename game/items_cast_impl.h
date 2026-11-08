#ifndef DOTA_ITEMS_CAST_IMPL_H
#define DOTA_ITEMS_CAST_IMPL_H
/* Included only by items.c. Orders use this read-only preflight; the normal
 * use path remains responsible for all effects, mana, charges and cooldowns. */
static int cast_range_bonus(const DotaGame*g,int h){
    DotaItemStats stats;DotaItemRecord r;int i,id;
    zero(&stats,sizeof(stats));
    for(i=0;i<DOTA_ITEMS_EQUIP_SLOTS;i++){id=ITEM(g,h,i);if(id>=0&&dota_items_read(id,&r))stats.value[ITEM_STAT_CAST_RANGE]=imax(stats.value[ITEM_STAT_CAST_RANGE],r.stats[ITEM_STAT_CAST_RANGE]);}
    id=g->items.neutral.enchantment[h];if(id>=0&&dota_items_read(id,&r))stats.value[ITEM_STAT_CAST_RANGE]+=r.stats[ITEM_STAT_CAST_RANGE];
    stats.value[ITEM_STAT_CAST_RANGE]+=dota_progression_stat_bonus(&g->units[h],ITEM_STAT_CAST_RANGE)+g->items.aura[h].value[ITEM_STAT_CAST_RANGE]+dota_items_status_percent(g,h,ITEM_STAT_CAST_RANGE);
    /* These parameter/stat providers only write their supplied output. Avoid
     * dota_items_stats here: that public cached path syncs inventory. */
    dota_items_special_stats((DotaGame*)g,h,&stats);
    return stats.value[ITEM_STAT_CAST_RANGE];
}
int dota_items_self_target_valid(const DotaGame*g,int h,int s){
    DotaItemRecord r;int id;
    if(!g||!valid(g,h)||!live(g,h))return 0;
    id=dota_items_cast_item(g,h,s);
    if(id<0||!dota_items_read(id,&r)||!r.action||(r.flags&ITEM_F_RECIPE))return 0;
    if(id==DOTA_ITEM_TPSCROLL)return 1;
    if(r.flags&ITEM_F_POINT)return 0;
    if(id==DOTA_ITEM_SPHERE||id==DOTA_ITEM_MEDALLION_OF_COURAGE)return 0;
    if(r.action==ITEM_ACTION_CYCLONE)return 1;
    if(r.action==ITEM_ACTION_FORCE&&(id==DOTA_ITEM_FORCE_STAFF||id==DOTA_ITEM_HURRICANE_PIKE))return 1;
    if(r.action==ITEM_ACTION_SOUL||id==DOTA_ITEM_DISPERSER)return 1;
    if(r.flags&ITEM_F_FRIENDLY)return 1;
    return (r.flags&ITEM_F_NO_TARGET)&&!(r.flags&ITEM_F_POINT);
}
/* HIT1's original BOTH-team records have neither single-team flag set. Keep
 * that meaning for these active families instead of treating them as enemies. */
static int cast_both_teams(int id,int action){return action==ITEM_ACTION_SOUL||action==ITEM_ACTION_CYCLONE||action==ITEM_ACTION_FORCE||id==DOTA_ITEM_DISPERSER||id==DOTA_ITEM_MEDALLION_OF_COURAGE;}
int dota_items_tree_target(const DotaGame*g,int x,int y,int*tx_out,int*ty_out,int*stamp){
    int i,id,tx=0,ty=0,best=24*24+1,revision=0;
    if(!g||(unsigned)x>=DOTA_WORLD||(unsigned)y>=DOTA_WORLD)return -1;
    id=dota_trees_pick(g,x,y,96);
    if(id>=0){dota_trees_position((unsigned)id,&tx,&ty);best=distance(x,y,tx,ty);}
    for(i=0;i<DOTA_ITEM_TREES;i++){const DotaItemTree*p=&g->item_world.tree[i];int d=distance(x,y,p->x,p->y);if(p->until>g->tick&&!p->removed&&d<=24*24&&d<best){tx=p->x;ty=p->y;best=d;id=DOTA_STATIC_TREE_COUNT+i;revision=p->until;}}
    if(id<0)return -1;
    if(tx_out)*tx_out=tx;
    if(ty_out)*ty_out=ty;
    if(stamp)*stamp=revision;
    return id;
}
int dota_items_use_self(DotaGame*g,int h,int s){
    if(!dota_items_self_target_valid(g,h,s))return 0;
    if(dota_items_cast_item(g,h,s)==DOTA_ITEM_TPSCROLL){DotaPoint p=dota_fountains[g->units[h].team];return dota_items_use(g,h,s,p.x,p.y,DOTA_TELEPORT_FOUNTAIN_TARGET(g->units[h].team));}
    return dota_items_use(g,h,s,g->units[h].x,g->units[h].y,h);
}
int dota_items_cast_query(const DotaGame*g,int h,int s,int x,int y,int target,int*range_source){
    DotaItemRecord r;const DotaUnit*u,*v;int id,t=target,enemy,range=0,tx=x,ty=y;
    if(range_source)*range_source=0;
    if(!g||g->paused||g->state!=DOTA_PLAYING||!valid(g,h)||!live(g,h))return DOTA_ITEM_CAST_INVALID;
    id=dota_items_cast_item(g,h,s);
    if(id<0||!dota_items_read(id,&r)||!r.action||(r.flags&ITEM_F_RECIPE)||dota_items_cooldown(g,h,s))return DOTA_ITEM_CAST_INVALID;
    if(r.mana_cost*dota_items_mana_cost_percent((DotaGame*)g,h)/100>dota_items_available_mana(g,h))return DOTA_ITEM_CAST_INVALID;
    if(dota_items_lotus_value(id)&&g->objectives.lotus_cooldown[h])return DOTA_ITEM_CAST_INVALID;
    if(r.action==ITEM_ACTION_DOMINATE&&dota_item_world_dominate_cooldown(g,h)>0)return DOTA_ITEM_CAST_INVALID;
    u=&g->units[h];
    if(r.action==ITEM_ACTION_TELEPORT)return dota_items_teleport_target(g,h,id,x,y,target)>=0?DOTA_ITEM_CAST_READY:DOTA_ITEM_CAST_INVALID;
    if((r.flags&ITEM_F_CONSUMABLE)&&!dota_items_charges(g,h,s))return DOTA_ITEM_CAST_INVALID;
    if((r.action==ITEM_ACTION_CHARGES||r.action==ITEM_ACTION_SOUL||r.action==ITEM_ACTION_MIDAS||id==DOTA_ITEM_BOTTLE||(r.action==ITEM_ACTION_DRUM&&id==DOTA_ITEM_ANCIENT_JANGGO))&&!dota_items_charges(g,h,s))return DOTA_ITEM_CAST_INVALID;
    if(r.action==ITEM_ACTION_TREE){
        if(dota_items_tree_target(g,x,y,&tx,&ty,0)<0)return DOTA_ITEM_CAST_INVALID;
        range=par(id,IP_ABILITYCASTRANGE,id==DOTA_ITEM_TANGO?165:350)+cast_range_bonus(g,h);
    }else if((r.flags&ITEM_F_POINT)&&(target<0||!(r.flags&(ITEM_F_ENEMY|ITEM_F_FRIENDLY)))){
        if((unsigned)x>=DOTA_WORLD||(unsigned)y>=DOTA_WORLD)return DOTA_ITEM_CAST_INVALID;
        if(r.action==ITEM_ACTION_WARD){int kind=target==DOTA_WARD_TARGET_SENTRY?1:(id==DOTA_ITEM_WARD_SENTRY);if(id==DOTA_ITEM_WARD_DISPENSER&&target!=DOTA_WARD_TARGET_SENTRY)kind=!dota_items_ward_available(g,h,s,0);if(!dota_items_ward_available(g,h,s,kind)||!dota_item_world_ward_placeable(g,h,x,y))return DOTA_ITEM_CAST_INVALID;}
        if(r.action==ITEM_ACTION_BLINK)return DOTA_ITEM_CAST_READY;
        range=r.cast_range?r.cast_range+cast_range_bonus(g,h):0;
    }else if(target>=0||!(r.flags&ITEM_F_NO_TARGET)){
        if(t>=0&&!live(g,t))return DOTA_ITEM_CAST_INVALID;
        if(t<0){if(!dota_items_self_target_valid(g,h,s))return DOTA_ITEM_CAST_INVALID;t=h;}
        v=&g->units[t];enemy=v->team!=u->team;
        if(t==h&&!dota_items_self_target_valid(g,h,s))return DOTA_ITEM_CAST_INVALID;
        if(enemy&&(!(r.flags&ITEM_F_ENEMY)&&!cast_both_teams(id,r.action)))return DOTA_ITEM_CAST_INVALID;
        if(enemy&&!dota_unit_visible(g,h,t))return DOTA_ITEM_CAST_INVALID;
        if(!enemy&&t!=h&&!(r.flags&ITEM_F_FRIENDLY)&&!cast_both_teams(id,r.action))return DOTA_ITEM_CAST_INVALID;
        if(r.action==ITEM_ACTION_CYCLONE&&!enemy&&id==DOTA_ITEM_CYCLONE&&t!=h)return DOTA_ITEM_CAST_INVALID;
        if(r.action==ITEM_ACTION_MIDAS&&(!enemy||dota_item_world_dominated(g,t)||v->type==DOTA_HERO||v->type==DOTA_ANCIENT||v->type==DOTA_ROSHAN||v->type==DOTA_TOWER||v->type==DOTA_BASE))return DOTA_ITEM_CAST_INVALID;
        if(r.action==ITEM_ACTION_DOMINATE&&(!enemy||v->type==DOTA_HERO||v->type==DOTA_TOWER||v->type==DOTA_BASE||v->type==DOTA_ROSHAN||(v->type==DOTA_ANCIENT&&id!=DOTA_ITEM_HELM_OF_THE_OVERLORD)))return DOTA_ITEM_CAST_INVALID;
        if(r.action==ITEM_ACTION_MOON&&(t>=10||(g->items.permanent[t]&1u)))return DOTA_ITEM_CAST_INVALID;
        tx=v->x;ty=v->y;
        if(t!=h&&r.cast_range){range=enemy?hpar((DotaGame*)g,h,id,IP_CAST_RANGE_ENEMY,hpar((DotaGame*)g,h,id,IP_ENEMY_CAST_RANGE,r.cast_range)):r.cast_range;range+=cast_range_bonus(g,h);}
    }
    if(range>0){int w=(range+3)/4;if(distance(u->x,u->y,tx,ty)>w*w){if(range_source)*range_source=range;return DOTA_ITEM_CAST_APPROACH;}}
    return DOTA_ITEM_CAST_READY;
}
#endif
