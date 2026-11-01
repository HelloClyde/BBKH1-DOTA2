#include "trees.h"
#include "sight.h"
static const uint16_t *tree_xy;
static unsigned geometry_revision;
void dota_trees_detach(void){tree_xy=0;++geometry_revision;dota_sight_reset();}
int dota_trees_attach(const uint16_t *xy,unsigned count){
    unsigned i;dota_trees_detach();
    if(!xy || ((uintptr_t)xy&1u) || count!=DOTA_STATIC_TREE_COUNT)return 0;
    for(i=0;i<count*2;++i)if(xy[i]>=DOTA_WORLD)return 0;
    tree_xy=xy;return 1;
}
int dota_trees_ready(void){return tree_xy!=0;}
unsigned dota_trees_geometry_revision(void){return geometry_revision;}
int dota_trees_position(unsigned i,int *x,int *y){
    if(!tree_xy || i>=DOTA_STATIC_TREE_COUNT)return 0;
    if(x)*x=tree_xy[i*2];
    if(y)*y=tree_xy[i*2+1];
    return 1;
}
int dota_trees_is_cut(const DotaGame *g,unsigned i){return g && i<DOTA_STATIC_TREE_COUNT && ((g->trees.cut[i>>5]>>(i&31))&1u);}
unsigned dota_trees_revision(const DotaGame *g){return g?g->trees.revision:0;}
int dota_trees_pick(const DotaGame *g,int x,int y,int radius){
    unsigned i,best;int found=-1;
    if(!g || !tree_xy || radius<0 || radius>16384 || (unsigned)(x|y)>=DOTA_WORLD)return -1;
    best=(unsigned)radius*(unsigned)radius/16;
    for(i=0;i<DOTA_STATIC_TREE_COUNT;++i)if(!dota_trees_is_cut(g,i)){
        int dx=x-tree_xy[i*2],dy=y-tree_xy[i*2+1];unsigned d=(unsigned)(dx*dx+dy*dy);
        if(d<=best && (found<0 || d<best)){best=d;found=(int)i;}
    }
    return found;
}
int dota_trees_cut(DotaGame *g,unsigned i){
    if(!g || !tree_xy || i>=DOTA_STATIC_TREE_COUNT || dota_trees_is_cut(g,i))return 0;
    g->trees.cut[i>>5]|=1u<<(i&31);g->trees.regrow_at[i]=(uint16_t)((unsigned)g->tick+DOTA_TREE_REGROW_TICKS);
    ++g->trees.cut_count;++g->trees.revision;dota_sight_invalidate();return 1;
}
int dota_trees_cut_radius(DotaGame *g,int x,int y,int radius){
    unsigned i,budget;int count=0;
    if(!g || radius<0 || radius>16384 || (unsigned)(x|y)>=DOTA_WORLD)return 0;
    budget=(unsigned)radius*(unsigned)radius/16;
    for(i=0;tree_xy && i<DOTA_STATIC_TREE_COUNT;++i)if(!dota_trees_is_cut(g,i)){
        int dx=x-tree_xy[i*2],dy=y-tree_xy[i*2+1];if((unsigned)(dx*dx+dy*dy)<=budget)count+=dota_trees_cut(g,i);
    }
    for(i=0;i<DOTA_ITEM_TREES;++i){DotaItemTree *t=&g->item_world.tree[i];int dx=x-t->x,dy=y-t->y;
        if(t->until>g->tick && !t->removed && (unsigned)(dx*dx+dy*dy)<=budget){t->removed=1;++count;dota_sight_invalidate();}}
    return count;
}
void dota_trees_tick(DotaGame *g){
    unsigned w;int changed=0;if(!g || !g->trees.cut_count)return;
    for(w=0;w<DOTA_STATIC_TREE_WORDS;++w){unsigned bits=g->trees.cut[w],b;
        for(b=0;bits;++b,bits>>=1)if(bits&1u){unsigned i=w*32+b;
            if(i<DOTA_STATIC_TREE_COUNT && (int16_t)((uint16_t)g->tick-g->trees.regrow_at[i])>=0){
                g->trees.cut[w]&=~(1u<<b);g->trees.regrow_at[i]=0;--g->trees.cut_count;changed=1;}}
    }
    if(changed){++g->trees.revision;dota_sight_invalidate();}
}
