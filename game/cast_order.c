#include "cast_order.h"
#include "navigation.h"
#include "items.h"
#include "items_actions.h"
#include "extra_heroes.h"
#include "roster_heroes.h"
#include "progression.h"
#include "objectives.h"
#include "runes.h"
#include "trees.h"
#include "sight.h"

#define ORDER_REPATH_TICKS 10
#define ORDER_STUCK_TICKS (5*DOTA_HZ)
#define ORDER_LIFETIME_TICKS (30*DOTA_HZ)
static int squared(int x,int y,int xx,int yy){int dx=x-xx,dy=y-yy;return dx*dx+dy*dy;}
static int in_range(int x,int y,int xx,int yy,int range){return range>=0&&(long long)squared(x,y,xx,yy)*16<=(long long)range*range;}
static int root_i(unsigned x){unsigned r=0,b=1u<<30;while(b>x)b>>=2;while(b){if(x>=r+b){x-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}return (int)r;}
static int valid_hero(const DotaGame*g,int h){return g&&(unsigned)h<DOTA_HERO_COUNT&&g->units[h].active&&g->units[h].hp>0&&g->units[h].type==DOTA_HERO;}
static int tree_visible(const DotaGame*g,int h,int x,int y){
 static const signed char dx[8]={16,11,0,-11,-16,-11,0,11},dy[8]={0,11,16,11,0,-11,-16,-11};int i,team=g->units[h].team;
 if(dota_sight_team_position_visible(g,team,x,y))return 1;
 /* A trunk is itself an occluder. Its exposed collision boundary may be
  * visible while the centre lies in a blocked sight cell. */
 for(i=0;i<8;++i)if(dota_sight_team_position_visible(g,team,x+dx[i],y+dy[i]))return 1;
 return 0;
}
static int extra_available(const DotaGame*g,int h){int id=g->units[h].hero_id;return id==DOTA_HERO_PUDGE?dota_roster_swallowed(g,h)>=0:id==DOTA_HERO_SVEN?dota_hero_has_scepter(g,h):(id==3||id==4||id==5||id==7||id==9)&&dota_hero_has_shard(g,h);}
int dota_cast_order_skill_kind(const DotaGame*g,int h,int skill){
 int id;if(!g||(unsigned)h>=DOTA_HERO_COUNT||(unsigned)skill>4u)return DOTA_CAST_TARGET_NONE;id=g->units[h].hero_id;
 if(skill==4){if(!extra_available(g,h))return DOTA_CAST_TARGET_NONE;if(id==DOTA_HERO_NECROPHOS)return DOTA_CAST_TARGET_ANY;if(id==DOTA_HERO_TIDEHUNTER)return DOTA_CAST_TARGET_ENEMY;return (dota_roster_is_hero(&g->units[h])?dota_roster_extra_range(g,h):dota_extra_shard_range(g,h))?DOTA_CAST_TARGET_POINT:DOTA_CAST_TARGET_NONE;}
 if(dota_skill_is_passive(id,skill))return DOTA_CAST_TARGET_NONE;
 if(id==DOTA_HERO_PUDGE&&skill==3&&dota_hero_has_shard(g,h))return DOTA_CAST_TARGET_ANY;
 if((id==DOTA_HERO_AXE&&(skill==1||skill==3))||(id==DOTA_HERO_LINA&&skill==3)||(id==DOTA_HERO_JUGGERNAUT&&skill==3)||(id==DOTA_HERO_CRYSTAL_MAIDEN&&skill==1)||(id==DOTA_HERO_SLARK&&skill==2)||(id==DOTA_HERO_NECROPHOS&&skill==3)||(id==DOTA_HERO_PUDGE&&skill==3)||(id==DOTA_HERO_SNIPER&&skill==3)||(id==DOTA_HERO_SVEN&&skill==0)||(id==DOTA_HERO_TIDEHUNTER&&skill==0&&!dota_hero_has_scepter(g,h)))return DOTA_CAST_TARGET_ENEMY;
 return ((id==DOTA_HERO_JUGGERNAUT&&skill==1)||(id==DOTA_HERO_SLARK&&skill==1)||dota_skill_cast_range_source(g,h,skill))?DOTA_CAST_TARGET_POINT:DOTA_CAST_TARGET_NONE;
}
static int skill_unit_valid(const DotaGame*g,int h,int skill,int target,int generation){
 const DotaUnit*u,*v;int kind=dota_cast_order_skill_kind(g,h,skill);
 if(!valid_hero(g,h)||(unsigned)target>=DOTA_MAX_UNITS||target==h||kind<DOTA_CAST_TARGET_ENEMY)return 0;
 u=&g->units[h];v=&g->units[target];
 if(!v->active||v->hp<=0||v->generation!=generation||v->type==DOTA_TOWER||v->type==DOTA_BASE||v->type==DOTA_BUILDING||!dota_unit_visible(g,h,target))return 0;
 if(dota_unit_invulnerable(g,target)&&!(u->hero_id==DOTA_HERO_SVEN&&skill==0&&dota_hero_has_scepter(g,h)))return 0;
 if(kind==DOTA_CAST_TARGET_ENEMY&&v->team==u->team)return 0;
 if(u->hero_id==DOTA_HERO_PUDGE&&skill==3&&v->team==u->team&&v->type!=DOTA_HERO)return 0;
 if((u->hero_id==DOTA_HERO_SLARK&&skill==2)||(u->hero_id==DOTA_HERO_NECROPHOS&&skill==3))if(target>=DOTA_HERO_COUNT||v->type!=DOTA_HERO)return 0;
 if(u->hero_id==DOTA_HERO_TIDEHUNTER&&skill==4&&v->type!=DOTA_HERO)return 0;
 return 1;
}
static int skill_range(const DotaGame*g,int h,int skill,int target){
 int id=g->units[h].hero_id,range;
 if(skill==4){range=dota_roster_is_hero(&g->units[h])?dota_roster_extra_range(g,h):dota_extra_shard_range(g,h);return range+(id==DOTA_HERO_TIDEHUNTER?24:0);}
 if(id==DOTA_HERO_JUGGERNAUT&&skill==3)return DOTA_OMNISLASH_CAST_RANGE_SOURCE;
 if(id==DOTA_HERO_PUDGE&&skill==3&&target>=0&&g->units[target].team==g->units[h].team)return 300;
 range=dota_skill_cast_range_source(g,h,skill);
 if(target>=0&&(dota_roster_is_hero(&g->units[h])||id==DOTA_HERO_SLARK||id==DOTA_HERO_NECROPHOS))range+=24;
 return range;
}
static int directional(const DotaGame*g,int h,int skill){int id=g->units[h].hero_id;return (id==DOTA_HERO_SLARK&&skill==1)||(id==DOTA_HERO_PUDGE&&skill==0)||(id==DOTA_HERO_TIDEHUNTER&&skill==0&&dota_hero_has_scepter(g,h))||(id==DOTA_HERO_CRYSTAL_MAIDEN&&skill==4);}
static int skill_resources(const DotaGame*g,int h,int skill){
 const DotaUnit*u=&g->units[h];int cost;
 if(skill<4){if(!dota_progression_rank(u,skill)||dota_skill_is_passive(u->hero_id,skill)||u->cooldown[skill])return 0;cost=dota_skill_mana_cost(g,h,skill);
  if(u->hero_id==DOTA_HERO_SNIPER&&skill==0&&g->roster.hero[h].generation==u->generation&&!g->roster.hero[h].shrapnel_charges)return 0;
 }else{if(!extra_available(g,h))return 0;if(dota_roster_is_hero(u)){if(g->roster.hero[h].extra_cooldown>g->tick)return 0;cost=dota_roster_extra_mana(g,h);}else{if(dota_extra_shard_cooldown(g,h))return 0;cost=dota_extra_shard_mana(g,h);}cost=cost*dota_items_mana_cost_percent(g,h)/100;}
 return dota_items_available_mana(g,h)>=cost;
}
static int blocked(const DotaGame*g,int h,const DotaCastOrder*o){
 const DotaUnit*u=&g->units[h];const DotaRosterHero*r=&g->roster.hero[h];
 return u->stun_ticks||u->taunt_ticks||u->channel_ticks||dota_roster_stun(g,h)||dota_roster_moving(g,h)||dota_extra_moving(g,h)||dota_omnislash_phase(g,h)||(r->generation==u->generation&&(r->cast_at>g->tick||r->channel_until>g->tick))||(o->kind==DOTA_CAST_ORDER_SKILL&&dota_items_silenced(g,h));
}
int dota_cast_order_active(const DotaGame*g,int h){return g&&(unsigned)h<DOTA_HERO_COUNT&&g->cast_orders[h].kind!=DOTA_CAST_ORDER_NONE;}
void dota_cast_order_cancel(DotaGame*g,int h){
 DotaCastOrder*o;if(!g||(unsigned)h>=DOTA_HERO_COUNT)return;o=&g->cast_orders[h];
 if(o->kind){g->units[h].move_order=0;g->units[h].target=-1;g->unit_routes[h].count=g->unit_routes[h].index=0;if(h==g->player)g->player_path_count=g->player_path_index=0;}
 o->kind=DOTA_CAST_ORDER_NONE;o->executing=0;
}
static int validate_target(DotaGame*g,int h,DotaCastOrder*o){
 if(o->caster_generation!=g->units[h].generation)return 0;
 if(o->target_kind==DOTA_CAST_UNIT){const DotaUnit*v;
  if((unsigned)o->target>=DOTA_MAX_UNITS)return 0;
  v=&g->units[o->target];
  if(!v->active||v->hp<=0||v->generation!=o->target_generation||!dota_unit_visible(g,h,o->target))return 0;
  if(o->kind==DOTA_CAST_ORDER_SKILL&&!skill_unit_valid(g,h,o->action,o->target,o->target_generation))return 0;
  o->x=v->x;o->y=v->y;
 }else if(o->target_kind==DOTA_CAST_TREE){int x,y,stamp;
  if(dota_items_tree_target(g,o->x,o->y,&x,&y,&stamp)!=o->target||x!=o->x||y!=o->y||stamp!=o->target_generation)return 0;
  if(!tree_visible(g,h,x,y))return 0;
 }
 return (unsigned)(o->x|o->y)<DOTA_WORLD;
}
static int query(DotaGame*g,int h,DotaCastOrder*o){
 if(o->kind==DOTA_CAST_ORDER_ITEM){int range=0,result;
  if(dota_items_cast_item(g,h,o->action)!=o->item_id)return 0;
  result=dota_items_cast_query(g,h,o->action,o->x,o->y,o->target_kind==DOTA_CAST_UNIT?o->target:-1,&range);if(result==DOTA_ITEM_CAST_APPROACH)o->range_source=range;return result;
 }
 if(!skill_resources(g,h,o->action))return 0;
 o->range_source=skill_range(g,h,o->action,o->target_kind==DOTA_CAST_UNIT?o->target:-1);
 return in_range(g->units[h].x,g->units[h].y,o->x,o->y,o->range_source)?1:2;
}
static int execute(DotaGame*g,int h){
 DotaCastOrder*o=&g->cast_orders[h];int kind=o->kind,result;
 g->units[h].move_order=0;g->units[h].target=-1;g->unit_routes[h].count=g->unit_routes[h].index=0;if(h==g->player)g->player_path_count=g->player_path_index=0;
 o->kind=0;o->executing=1;
 result=kind==DOTA_CAST_ORDER_SKILL?dota_rules_order_cast(g,h,o->action,o->x,o->y):dota_items_use(g,h,o->action,o->x,o->y,o->target_kind==DOTA_CAST_UNIT?o->target:-1);
 o->executing=0;return result?DOTA_ORDER_EXECUTED:DOTA_ORDER_REJECTED;
}
static int issue(DotaGame*g,int h,DotaCastOrder order){
 int ready;DotaUnit*u=&g->units[h];order.caster_generation=u->generation;order.started_tick=order.progress_tick=g->tick;order.last_x=u->x;order.last_y=u->y;order.next_plan_tick=g->tick;order.executing=0;
 if(!valid_hero(g,h)||g->paused||g->state!=DOTA_PLAYING||!validate_target(g,h,&order)||(ready=query(g,h,&order))==0)return 0;
 g->cast_orders[h]=order;
 if(ready==1&&!blocked(g,h,&order))return execute(g,h);
 if(ready==2&&order.range_source<=0){dota_cast_order_cancel(g,h);return 0;}
 dota_items_cancel_teleport(g,h);if(h==g->player){dota_objective_cancel(g);dota_rune_cancel(g);g->player_path_count=g->player_path_index=0;}u->move_order=0;u->target=-1;g->unit_routes[h].count=g->unit_routes[h].index=0;
 return DOTA_ORDER_QUEUED;
}
int dota_order_cast_on_unit(DotaGame*g,int skill,int target,int generation){
 DotaCastOrder o={0};int h;if(!g||(unsigned)g->player>=DOTA_HERO_COUNT)return 0;h=g->player;dota_cast_order_cancel(g,h);if(!skill_unit_valid(g,h,skill,target,generation))return 0;
 o.kind=DOTA_CAST_ORDER_SKILL;o.action=skill;o.target_kind=DOTA_CAST_UNIT;o.target=target;o.target_generation=generation;o.x=g->units[target].x;o.y=g->units[target].y;return issue(g,h,o);
}
int dota_order_cast(DotaGame*g,int skill,int x,int y){
 DotaCastOrder o={0};int h,kind,i,target=-1,best;if(!g||(unsigned)g->player>=DOTA_HERO_COUNT)return 0;h=g->player;dota_cast_order_cancel(g,h);kind=dota_cast_order_skill_kind(g,h,skill);
 if(kind==DOTA_CAST_TARGET_NONE||directional(g,h,skill)||(g->units[h].hero_id==DOTA_HERO_JUGGERNAUT&&skill==1&&dota_healing_ward(g,h)>=0))return dota_cast(g,skill,x,y)?1:0;
 if((unsigned)(x|y)>=DOTA_WORLD)return 0;
 if(kind>=DOTA_CAST_TARGET_ENEMY){best=dota_roster_is_hero(&g->units[h])?96*96+1:250*250+1;for(i=0;i<DOTA_MAX_UNITS;++i)if(skill_unit_valid(g,h,skill,i,g->units[i].generation)){int d=squared(x,y,g->units[i].x,g->units[i].y);if(d<best){best=d;target=i;}}return target<0?0:dota_order_cast_on_unit(g,skill,target,g->units[target].generation);}
 o.kind=DOTA_CAST_ORDER_SKILL;o.action=skill;o.target_kind=DOTA_CAST_POINT;o.target=-1;o.x=x;o.y=y;return issue(g,h,o);
}
int dota_order_item_tree(DotaGame*g,int slot,int tree_id){
 DotaCastOrder o={0};DotaItemRecord r;int h,item;if(!g||(unsigned)g->player>=DOTA_HERO_COUNT)return 0;h=g->player;dota_cast_order_cancel(g,h);item=dota_items_cast_item(g,h,slot);
 if(item<0||!dota_items_read(item,&r)||r.action!=ITEM_ACTION_TREE||(unsigned)tree_id>=DOTA_STATIC_TREE_COUNT||!dota_trees_position((unsigned)tree_id,&o.x,&o.y))return 0;
 o.kind=DOTA_CAST_ORDER_ITEM;o.action=slot;o.item_id=item;o.target_kind=DOTA_CAST_TREE;o.target=tree_id;return issue(g,h,o);
}
int dota_order_item_at(DotaGame*g,int slot,int x,int y,int target,int generation){
 DotaCastOrder o={0};DotaItemRecord r;int h,item;if(!g||(unsigned)g->player>=DOTA_HERO_COUNT)return 0;h=g->player;dota_cast_order_cancel(g,h);item=dota_items_cast_item(g,h,slot);if(item<0||!dota_items_read(item,&r))return 0;
 if(r.action==ITEM_ACTION_TREE){int tree,stamp,tx,ty;tree=dota_items_tree_target(g,x,y,&tx,&ty,&stamp);if(tree<0)return 0;
  o.kind=DOTA_CAST_ORDER_ITEM;o.action=slot;o.item_id=item;o.target_kind=DOTA_CAST_TREE;o.target=tree;o.target_generation=stamp;o.x=tx;o.y=ty;return issue(g,h,o);}
 o.kind=DOTA_CAST_ORDER_ITEM;o.action=slot;o.item_id=item;o.target_kind=target>=0?DOTA_CAST_UNIT:DOTA_CAST_POINT;o.target=target;o.target_generation=generation;o.x=x;o.y=y;
 /* Preserve existing no-target, TP, blink and self-use semantics. */
 if(target==h||r.action==ITEM_ACTION_TELEPORT||r.action==ITEM_ACTION_BLINK||(r.flags&ITEM_F_NO_TARGET))return dota_items_use(g,h,slot,x,y,target)?1:0;
 return issue(g,h,o);
}

/* Consider a small fixed ring of legal cast positions. No navigation search
 * occurs between replans; a moving target is never tracked through lost fog. */
static void plan(DotaGame*g,int h,DotaCastOrder*o){
 static const signed char vx[16]={2,2,2,1,0,-1,-2,-2,-2,-2,-2,-1,0,1,2,2};
 static const signed char vy[16]={0,1,2,2,2,2,2,1,0,-1,-2,-2,-2,-2,-2,-1};
 DotaUnit*u=&g->units[h];DotaUnitRoute*r=&g->unit_routes[h];int xs[17],ys[17],distance[17],count=0,i,attempts=0,rad=o->range_source>8?o->range_source-8:0;
 int dx=u->x-o->x,dy=u->y-o->y,len=root_i((unsigned)squared(u->x,u->y,o->x,o->y));
 o->next_plan_tick=g->tick+ORDER_REPATH_TICKS;r->count=r->index=0;r->generation=u->generation;
 for(i=-1;i<16;++i){int x,y,nx,ny;if(i<0){if(!len)continue;x=o->x+(int)((long long)dx*rad/(4ll*len));y=o->y+(int)((long long)dy*rad/(4ll*len));}else{int scale=vx[i]*vx[i]+vy[i]*vy[i],length=scale==4?131072:scale==5?146542:185363;x=o->x+(int)((long long)vx[i]*rad*65536/(4ll*length));y=o->y+(int)((long long)vy[i]*rad*65536/(4ll*length));}
  if(!dota_nav_nearest(x,y,32,&nx,&ny)||!in_range(nx,ny,o->x,o->y,o->range_source))continue;
  xs[count]=nx;ys[count]=ny;distance[count++]=squared(u->x,u->y,nx,ny);
 }
 if(g->navigation_tick!=g->tick){g->navigation_tick=g->tick;g->navigation_searches=g->navigation_nodes=0;}
 for(i=0;i<count;++i){int j,best=-1,x,y,complete=0,n=0;DotaPoint points[DOTA_ROUTE_POINTS];
  for(j=0;j<count;++j)if(distance[j]>=0&&(best<0||distance[j]<distance[best]))best=j;
  if(best<0)break;
  x=xs[best];y=ys[best];distance[best]=-1;
  if(dota_nav_segment(u->x,u->y,x,y)){points[0].x=x;points[0].y=y;n=1;complete=1;}
  else {unsigned before,budget;if(attempts>=3||g->navigation_searches>=4||g->navigation_nodes>=4096)break;budget=(unsigned)(4096-g->navigation_nodes);if(budget>768u)budget=768u;before=dota_nav_stats()->expanded_nodes;n=dota_nav_find_path(u->x,u->y,x,y,points,DOTA_ROUTE_POINTS,budget,&complete);++attempts;++g->navigation_searches;g->navigation_nodes+=(int)(dota_nav_stats()->expanded_nodes-before);}
  if(n){r->goal_x=(unsigned short)x;r->goal_y=(unsigned short)y;r->count=(unsigned short)n;r->index=0;r->complete=complete;r->repath_tick=o->next_plan_tick;for(j=0;j<n;++j)r->cells[j]=(unsigned short)dota_nav_cell_at(points[j].x,points[j].y);o->goal_x=o->x;o->goal_y=o->y;return;}
 }
}
int dota_cast_order_tick(DotaGame*g,int h){
 DotaCastOrder*o;DotaUnit*u;DotaUnitRoute*r;int status;
 if(!dota_cast_order_active(g,h))return 0;
 o=&g->cast_orders[h];u=&g->units[h];r=&g->unit_routes[h];
 if(!valid_hero(g,h)||g->state!=DOTA_PLAYING||!validate_target(g,h,o)||(unsigned)(g->tick-o->started_tick)>=ORDER_LIFETIME_TICKS){dota_cast_order_cancel(g,h);return 0;}
 if(g->paused)return 1;
 /* These forced actions have their own per-unit update and must keep running. */
 if(u->taunt_ticks||dota_omnislash_phase(g,h))return 0;
 if(blocked(g,h,o)){o->progress_tick=g->tick;return 1;}
 status=query(g,h,o);if(!status){dota_cast_order_cancel(g,h);return 0;}
 if(status==1){(void)execute(g,h);return 1;}
 if(dota_items_rooted(g,h)){o->progress_tick=g->tick;return 1;}
 if(u->x!=o->last_x||u->y!=o->last_y){o->last_x=u->x;o->last_y=u->y;o->progress_tick=g->tick;}
 if((unsigned)(g->tick-o->progress_tick)>=ORDER_STUCK_TICKS){dota_cast_order_cancel(g,h);return 0;}
 if(g->tick>=o->next_plan_tick&&(r->index>=r->count||squared(o->goal_x,o->goal_y,o->x,o->y)>16*16||!in_range(r->goal_x,r->goal_y,o->x,o->y,o->range_source)))plan(g,h,o);
 if(r->index<r->count)dota_rules_cast_order_step(g,h);
 return 1;
}
