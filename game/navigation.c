/* Bounded navigation over an offline source-derived collision graph.
 * The game never infers passability from rendered height/colour pixels. */
#include "navigation.h"
#include "../private/generated/dota_nav.h"
#include "../private/generated/dota_barracks_nav.h"
#include "../private/generated/dota_tree_nav.h"
#include "trees.h"

#define HASH_SLOTS (DOTA_NAV_PATH_NODES*2)
#define INVALID_NODE 65535u
typedef struct { unsigned short cell,parent,cost,heap,estimate; } SearchNode;
static SearchNode nodes[DOTA_NAV_PATH_NODES];
static unsigned short lookup[HASH_SLOTS],heap[DOTA_NAV_PATH_NODES];
static unsigned node_count,heap_count;
static int goal_cell_x,goal_cell_y;
static DotaNavStats nav_stats;
static unsigned tower_mask;
static unsigned filler_mask;
static unsigned barracks_mask=4095u;
static const DotaItemTree *item_trees;
static unsigned item_tree_count;
static unsigned char active_tree_indices[DOTA_ITEM_TREES];
static unsigned active_tree_count;
static unsigned short exit_cache_cells[256];
static unsigned char exit_cache_values[256];
static const DotaGame *static_tree_game,*static_tree_cached_game;
static unsigned static_tree_revision,static_tree_geometry,static_tree_valid;
static unsigned char static_tree_bits[DOTA_NAV_BITMAP_BYTES];
void dota_nav_set_static_trees(const DotaGame *g){static_tree_game=g;}
void dota_nav_reset_static_trees(const DotaGame *g){static_tree_game=g;static_tree_valid=0;}
static void refresh_static_trees(void){
    unsigned i,rev=dota_trees_revision(static_tree_game),geo=dota_trees_geometry_revision();
    if(static_tree_valid && static_tree_cached_game==static_tree_game && static_tree_revision==rev && static_tree_geometry==geo)return;
    static_tree_valid=1;static_tree_cached_game=static_tree_game;static_tree_revision=rev;static_tree_geometry=geo;
    for(i=0;i<256;++i)exit_cache_values[i]=0;
    for(i=0;i<DOTA_NAV_BITMAP_BYTES;++i)static_tree_bits[i]=0;
    if(!static_tree_game || !dota_trees_ready()){
        for(i=0;i<DOTA_NAV_BITMAP_BYTES;++i)static_tree_bits[i]=dota_nav_source_tree_bits[i];
        return;
    }
    for(i=0;i<DOTA_STATIC_TREE_COUNT;++i)if(!dota_trees_is_cut(static_tree_game,i)){
        int x,y,xx,yy;dota_trees_position(i,&x,&y);
        for(yy=y/DOTA_NAV_CELL-2;yy<=y/DOTA_NAV_CELL+1;++yy)for(xx=x/DOTA_NAV_CELL-2;xx<=x/DOTA_NAV_CELL+1;++xx){
            int dx=xx*DOTA_NAV_CELL+DOTA_NAV_CELL/2-x,dy=yy*DOTA_NAV_CELL+DOTA_NAV_CELL/2-y;
            if((unsigned)xx<DOTA_NAV_SIZE && (unsigned)yy<DOTA_NAV_SIZE && dx*dx+dy*dy<=22*22){unsigned c=(unsigned)(yy*DOTA_NAV_SIZE+xx);static_tree_bits[c>>3]|=(unsigned char)(1u<<(c&7));}
        }
    }
}
void dota_nav_set_item_trees(const DotaItemTree *trees,unsigned count){item_trees=trees;item_tree_count=count>DOTA_ITEM_TREES?DOTA_ITEM_TREES:count;}
/* Trees may change in several item callbacks in one tick. Rebuild once at
 * each public query, never cache by tick or scan empty slots per graph edge. */
static void refresh_item_trees(void){
    unsigned i;active_tree_count=0;
    refresh_static_trees();
    for(i=0;i<item_tree_count;++i)if(item_trees[i].until && !item_trees[i].removed)
        active_tree_indices[active_tree_count++]=(unsigned char)i;
}

static int abs_i(int n){return n<0?-n:n;}
static int clamp(int n,int low,int high){return n<low?low:n>high?high:n;}
static int bit(const unsigned char *data,unsigned index){return (data[index>>3]>>(index&7))&1;}
void dota_nav_set_all_structures(unsigned mask,unsigned fillers,unsigned barracks){
    unsigned i,tower;
    mask&=(1u<<DOTA_NAV_TOWER_COUNT)-1u;
    fillers&=(1u<<DOTA_NAV_FILLER_COUNT)-1u;
    barracks&=4095u;
    if(mask==tower_mask && fillers==filler_mask && barracks==barracks_mask)return;
    for(i=0;i<256;++i)exit_cache_values[i]=0;
    /* Lists contain only cells walkable in the static source base plane.
     * Restoring all lists before applying every live tower also handles
     * overlapping source hulls without freeing a surviving neighbour. */
    for(i=0;i<DOTA_NAV_TOWER_CELL_COUNT;++i){unsigned cell=dota_nav_tower_cells[i];dota_nav_walk_bits[cell>>3]|=(unsigned char)(1u<<(cell&7));}
    for(tower=0;tower<DOTA_NAV_STRUCTURE_COUNT;++tower)if(tower<DOTA_NAV_TOWER_COUNT?(mask&(1u<<tower)):(fillers&(1u<<(tower-DOTA_NAV_TOWER_COUNT)))){
        for(i=dota_nav_tower_offsets[tower];i<dota_nav_tower_offsets[tower+1];++i){unsigned cell=dota_nav_tower_cells[i];dota_nav_walk_bits[cell>>3]&=(unsigned char)~(1u<<(cell&7));}
    }
    for(i=0;i<DOTA_BARRACKS_NAV_CELLS;++i){
        const DotaBarracksNavCell *c=&dota_barracks_nav_cells[i];
        if((barracks&c->owners) || (mask&c->towers) || (fillers&c->fillers))dota_nav_walk_bits[c->cell>>3]&=(unsigned char)~(1u<<(c->cell&7));
        else dota_nav_walk_bits[c->cell>>3]|=(unsigned char)(1u<<(c->cell&7));
    }
    tower_mask=mask;
    filler_mask=fillers;
    barracks_mask=barracks;
}
void dota_nav_set_structures(unsigned mask,unsigned fillers){dota_nav_set_all_structures(mask,fillers,4095u);}
void dota_nav_set_towers(unsigned mask){dota_nav_set_structures(mask,0);}
static int open_cell(int x,int y){
    unsigned i;int wx=x*DOTA_NAV_CELL+DOTA_NAV_CELL/2,wy=y*DOTA_NAV_CELL+DOTA_NAV_CELL/2;
    if(x<0 || y<0 || x>=DOTA_NAV_SIZE || y>=DOTA_NAV_SIZE || !bit(dota_nav_walk_bits,(unsigned)(y*DOTA_NAV_SIZE+x)) || bit(static_tree_bits,(unsigned)(y*DOTA_NAV_SIZE+x)))return 0;
    for(i=0;i<active_tree_count;i++){const DotaItemTree *tree=&item_trees[active_tree_indices[i]];int dx=wx-tree->x,dy=wy-tree->y;if(dx*dx+dy*dy<=22*22)return 0;}
    return 1;
}
static int passage_segment(int x,int y,int xx,int yy){
    int i,ax,ay,dx,dy;long long length;
    if((x<DOTA_NAV_PASSAGE_MIN_X && xx<DOTA_NAV_PASSAGE_MIN_X) ||
       (x>DOTA_NAV_PASSAGE_MAX_X && xx>DOTA_NAV_PASSAGE_MAX_X) ||
       (y<DOTA_NAV_PASSAGE_MIN_Y && yy<DOTA_NAV_PASSAGE_MIN_Y) ||
       (y>DOTA_NAV_PASSAGE_MAX_Y && yy>DOTA_NAV_PASSAGE_MAX_Y))return 1;
    ax=x*32;ay=y*32;dx=(xx-x)*32;dy=(yy-y)*32;
    length=(long long)dx*dx+(long long)dy*dy;
    for(i=0;i<DOTA_NAV_PASSAGE_CIRCLES;++i){
        int cx=dota_nav_passage_circles[i][0],cy=dota_nav_passage_circles[i][1],radius=dota_nav_passage_circles[i][2],px,py;
        long long projection=(long long)(cx-ax)*dx+(long long)(cy-ay)*dy;
        if(!(barracks_mask&(1u<<dota_barracks_passage_owners[i])))continue;
        if(!length || projection<=0){px=ax;py=ay;}
        else if(projection>=length){px=xx*32;py=yy*32;}
        else{px=ax+(int)((long long)dx*projection/length);py=ay+(int)((long long)dy*projection/length);}
        /* Values remain in Q5; all products fit signed64 at4096world. */
        if((long long)(px-cx)*(px-cx)+(long long)(py-cy)*(py-cy)<=(long long)radius*radius)return 0;
    }
    return 1;
}
static inline int edge_clear(int x,int y,int xx,int yy){
    unsigned edge,lo=0,hi=DOTA_BARRACKS_NAV_EDGES;const unsigned char *bits;
    if(yy==y && (xx==x+1 || xx==x-1)){edge=(unsigned)(y*DOTA_NAV_SIZE+(xx<x?xx:x));bits=dota_nav_right_bits;}
    else if(xx==x && (yy==y+1 || yy==y-1)){edge=(unsigned)((yy<y?yy:y)*DOTA_NAV_SIZE+x)+65536u;bits=dota_nav_down_bits;}
    else return xx==x && yy==y;
    if(!bit(bits,edge&65535u))return 1;
    if(barracks_mask==4095u)return 0;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(dota_barracks_nav_edges[mid].edge<edge)lo=mid+1;else hi=mid;}
    return lo<DOTA_BARRACKS_NAV_EDGES && dota_barracks_nav_edges[lo].edge==edge && !(dota_barracks_nav_edges[lo].owners&barracks_mask);
}
static int cell_edge(int x,int y,int xx,int yy){
    return open_cell(x,y) && open_cell(xx,yy) && edge_clear(x,y,xx,yy);
}
static int neighbor(int x,int y,int xx,int yy){
    if(x!=xx && y!=yy)return open_cell(x,y) && open_cell(xx,y) &&
        open_cell(x,yy) && open_cell(xx,yy) && edge_clear(x,y,xx,y) &&
        edge_clear(x,y,x,yy) && edge_clear(xx,y,xx,yy) && edge_clear(x,yy,xx,yy);
    return cell_edge(x,y,xx,yy);
}
static int cell_has_exit_raw(int x,int y){
    unsigned short reached[9];unsigned head=0,count=1;
    reached[0]=(unsigned short)(y*DOTA_NAV_SIZE+x);
    while(head<count){
        int cx=reached[head]%DOTA_NAV_SIZE,cy=reached[head]/DOTA_NAV_SIZE,d;
        ++head;
        for(d=0;d<4;++d){
            int xx=cx+(d==0?-1:d==1?1:0),yy=cy+(d==2?-1:d==3?1:0);unsigned j,cell;
            if(!neighbor(cx,cy,xx,yy))continue;
            if(xx<x-1 || xx>x+1 || yy<y-1 || yy>y+1)return 1;
            cell=(unsigned)(yy*DOTA_NAV_SIZE+xx);
            for(j=0;j<count && reached[j]!=cell;++j){}
            if(j==count)reached[count++]=(unsigned short)cell;
        }
    }
    return 0;
}
static int cell_has_exit(int x,int y){
    unsigned cell=(unsigned)(y*DOTA_NAV_SIZE+x),slot=(cell^(cell>>8))&255u;
    int result;
    /* Without planted trees this predicate depends only on the structure
     * masks above. Any live tree bypasses the cache, including same-tick
     * planting/removal, so dynamic obstacles cannot use a stale result. */
    if(active_tree_count)return cell_has_exit_raw(x,y);
    if(exit_cache_values[slot] && exit_cache_cells[slot]==cell)return exit_cache_values[slot]-1;
    result=cell_has_exit_raw(x,y);exit_cache_cells[slot]=(unsigned short)cell;
    exit_cache_values[slot]=(unsigned char)(result+1);return result;
}
static int walkable_current(int x,int y){
    unsigned cell,i;
    if(x<0 || y<0 || x>=DOTA_WORLD || y>=DOTA_WORLD || !open_cell(x/DOTA_NAV_CELL,y/DOTA_NAV_CELL))return 0;
    if(x<DOTA_NAV_PASSAGE_MIN_X || x>=DOTA_NAV_PASSAGE_MAX_X || y<DOTA_NAV_PASSAGE_MIN_Y || y>=DOTA_NAV_PASSAGE_MAX_Y)return 1;
    cell=(unsigned)((y/DOTA_NAV_CELL)*DOTA_NAV_SIZE+x/DOTA_NAV_CELL);
    for(i=0;i<DOTA_NAV_PASSAGE_CELLS;++i)if(cell==dota_nav_passage_cells[i])return passage_segment(x,y,x,y);
    return 1;
}
int dota_nav_cell_at(int x,int y){
    if(x<0 || y<0 || x>=DOTA_WORLD || y>=DOTA_WORLD)return -1;
    return (y/DOTA_NAV_CELL)*DOTA_NAV_SIZE+x/DOTA_NAV_CELL;
}
void dota_nav_cell_center(unsigned cell,int *x,int *y){
    if(x)*x=(int)(cell%DOTA_NAV_SIZE)*DOTA_NAV_CELL+DOTA_NAV_CELL/2;
    if(y)*y=(int)(cell/DOTA_NAV_SIZE)*DOTA_NAV_CELL+DOTA_NAV_CELL/2;
}
/* Supercover DDA checks every crossed graph edge, even for long steps and
 * exact diagonal corner contacts. Both routes around a corner must be open. */
static int segment_current(int x,int y,int xx,int yy){
    int cx,cy,ex,ey,dx=xx-x,dy=yy-y,sx=dx>0?1:-1,sy=dy>0?1:-1;
    int ax=abs_i(dx),ay=abs_i(dy),tx,ty;
    ++nav_stats.segment_checks;
    if(!walkable_current(x,y) || !walkable_current(xx,yy) || !passage_segment(x,y,xx,yy))goto blocked;
    cx=x/DOTA_NAV_CELL;cy=y/DOTA_NAV_CELL;ex=xx/DOTA_NAV_CELL;ey=yy/DOTA_NAV_CELL;
    tx=sx>0?(cx+1)*DOTA_NAV_CELL-x:x-cx*DOTA_NAV_CELL;
    ty=sy>0?(cy+1)*DOTA_NAV_CELL-y:y-cy*DOTA_NAV_CELL;
    while(cx!=ex || cy!=ey){
        int nx=cx,ny=cy;
        /* A negative-axis endpoint exactly on a cell boundary still belongs
         * to floor(endpoint/cell). Do not advance that axis past its cell. */
        if(cx==ex){ny+=sy;ty+=DOTA_NAV_CELL;}
        else if(cy==ey){nx+=sx;tx+=DOTA_NAV_CELL;}
        else if(tx*ay<ty*ax){nx+=sx;tx+=DOTA_NAV_CELL;}
        else if(tx*ay>ty*ax){ny+=sy;ty+=DOTA_NAV_CELL;}
        else{nx+=sx;ny+=sy;tx+=DOTA_NAV_CELL;ty+=DOTA_NAV_CELL;}
        if(!neighbor(cx,cy,nx,ny))goto blocked;
        cx=nx;cy=ny;
    }
    return 1;
blocked:
    ++nav_stats.blocked_segments;return 0;
}
static int nearest_current(int x,int y,int radius,int *out_x,int *out_y){
    int cx,cy,lo_x,lo_y,hi_x,hi_y,xx,yy,best_x=0,best_y=0,best=0x7fffffff;
    if(!out_x || !out_y || radius<0)return 0;
    x=clamp(x,0,DOTA_WORLD-1);y=clamp(y,0,DOTA_WORLD-1);
    cx=x/DOTA_NAV_CELL;cy=y/DOTA_NAV_CELL;
    /* Source cells can form tiny enclaves inside conservative live hulls.
     * Keep their collision classification; a projected order/spawn needs a
     * legal graph route out of its local3x3 neighbourhood. */
    if(walkable_current(x,y) && cell_has_exit(cx,cy)){*out_x=x;*out_y=y;return 1;}
    lo_x=clamp(cx-radius/DOTA_NAV_CELL-1,0,DOTA_NAV_SIZE-1);hi_x=clamp(cx+radius/DOTA_NAV_CELL+1,0,DOTA_NAV_SIZE-1);
    lo_y=clamp(cy-radius/DOTA_NAV_CELL-1,0,DOTA_NAV_SIZE-1);hi_y=clamp(cy+radius/DOTA_NAV_CELL+1,0,DOTA_NAV_SIZE-1);
    for(yy=lo_y;yy<=hi_y;++yy)for(xx=lo_x;xx<=hi_x;++xx){
        int px=xx*DOTA_NAV_CELL+DOTA_NAV_CELL/2,py=yy*DOTA_NAV_CELL+DOTA_NAV_CELL/2;
        int dx=x-px,dy=y-py,d=dx*dx+dy*dy;
        /* Candidates outside the circle or farther than the current best
         * cannot win. Keep the original row order and strict tie rule. */
        if(d<=radius*radius && d<best && open_cell(xx,yy) && cell_has_exit(xx,yy)){best=d;best_x=px;best_y=py;}
    }
    if(best==0x7fffffff)return 0;
    *out_x=best_x;*out_y=best_y;return 1;
}
static unsigned heuristic(unsigned cell){
    int dx=abs_i((int)(cell%DOTA_NAV_SIZE)-goal_cell_x),dy=abs_i((int)(cell/DOTA_NAV_SIZE)-goal_cell_y);
    return (unsigned)(10*(dx+dy)-6*(dx<dy?dx:dy));
}
static unsigned priority(unsigned node){return nodes[node].cost*4u+nodes[node].estimate;}
static int earlier(unsigned a,unsigned b){
    unsigned pa=priority(a),pb=priority(b);
    return pa<pb || (pa==pb && nodes[a].cell<nodes[b].cell);
}
static void heap_up(unsigned index){
    unsigned n=heap[index];
    while(index){unsigned parent=(index-1)/2;if(!earlier(n,heap[parent]))break;heap[index]=heap[parent];nodes[heap[index]].heap=(unsigned short)index;index=parent;}
    heap[index]=(unsigned short)n;nodes[n].heap=(unsigned short)index;
}
static unsigned heap_pop(void){
    unsigned answer=heap[0],n=heap[--heap_count],i=0;
    nodes[answer].heap=INVALID_NODE;
    if(heap_count){
        while(i*2+1<heap_count){unsigned child=i*2+1;if(child+1<heap_count && earlier(heap[child+1],heap[child]))++child;if(!earlier(heap[child],n))break;heap[i]=heap[child];nodes[heap[i]].heap=(unsigned short)i;i=child;}
        heap[i]=(unsigned short)n;nodes[n].heap=(unsigned short)i;
    }
    return answer;
}
static unsigned locate(unsigned cell,int create){
    unsigned key=((cell^(cell>>7))*40503u)&(HASH_SLOTS-1);
    while(lookup[key]){unsigned index=lookup[key]-1u;if(nodes[index].cell==cell)return index;key=(key+1)&(HASH_SLOTS-1);}
    if(!create || node_count>=DOTA_NAV_PATH_NODES)return INVALID_NODE;
    lookup[key]=(unsigned short)(node_count+1);nodes[node_count].cell=(unsigned short)cell;
    nodes[node_count].parent=nodes[node_count].cost=nodes[node_count].heap=INVALID_NODE;
    /* Goal is fixed for this search. The maximum octile estimate is17850,
     * so retain its exact unsigned value instead of recomputing every heap
     * comparison. Costs and the original priority/tie ordering are unchanged. */
    nodes[node_count].estimate=(unsigned short)(heuristic(cell)*5u);
    return node_count++;
}
int dota_nav_find_path(int x,int y,int gx,int gy,DotaPoint *out,int capacity,unsigned max_expanded,int *complete){
    unsigned i,start,goal,best,expanded=0,current=INVALID_NODE;int count=0,px=x,py=y;
    refresh_item_trees();
    ++nav_stats.path_queries;if(complete)*complete=0;
    if(!out || capacity<=0 || !walkable_current(x,y) || !nearest_current(gx,gy,256,&gx,&gy))return 0;
    if(segment_current(x,y,gx,gy)){out[0].x=gx;out[0].y=gy;if(complete)*complete=1;++nav_stats.paths_completed;return 1;}
    if(!max_expanded)return 0;
    if(max_expanded>DOTA_NAV_PATH_NODES)max_expanded=DOTA_NAV_PATH_NODES;
    for(i=0;i<HASH_SLOTS;++i)lookup[i]=0;
    node_count=heap_count=0;goal=(unsigned)dota_nav_cell_at(gx,gy);goal_cell_x=(int)(goal%DOTA_NAV_SIZE);goal_cell_y=(int)(goal/DOTA_NAV_SIZE);
    start=locate((unsigned)dota_nav_cell_at(x,y),1);nodes[start].cost=0;nodes[start].heap=0;heap[heap_count++]=(unsigned short)start;best=start;
    while(heap_count && expanded<max_expanded){
        int cx,cy,ox,oy;current=heap_pop();++expanded;
        if(nodes[current].estimate<nodes[best].estimate)best=current;
        if(nodes[current].cell==goal){best=current;break;}
        cx=nodes[current].cell%DOTA_NAV_SIZE;cy=nodes[current].cell/DOTA_NAV_SIZE;
        for(oy=-1;oy<=1;++oy)for(ox=-1;ox<=1;++ox)if((ox || oy) && neighbor(cx,cy,cx+ox,cy+oy)){
            unsigned next=locate((unsigned)((cy+oy)*DOTA_NAV_SIZE+cx+ox),1),cost=nodes[current].cost+(ox && oy?14u:10u);
            if(next==INVALID_NODE || cost>=nodes[next].cost)continue;
            nodes[next].cost=(unsigned short)cost;nodes[next].parent=(unsigned short)current;
            if(nodes[next].heap==INVALID_NODE){nodes[next].heap=(unsigned short)heap_count;heap[heap_count++]=(unsigned short)next;}
            heap_up(nodes[next].heap);
        }
    }
    nav_stats.expanded_nodes+=expanded;
    if(expanded>=max_expanded && nodes[best].cell!=goal)++nav_stats.budget_exhaustions;
    if(best==start)return 0;
    /* Reverse parent links in the existing node arena; no second path array. */
    {unsigned child=INVALID_NODE,at=best;
     while(at!=INVALID_NODE){unsigned parent=nodes[at].parent;nodes[at].parent=(unsigned short)child;child=at;at=parent;}}
    current=nodes[start].parent;
    while(current!=INVALID_NODE && count<capacity){
        unsigned selected=current,next=nodes[current].parent;int sx,sy;
        while(next!=INVALID_NODE){int nx,ny;dota_nav_cell_center(nodes[next].cell,&nx,&ny);if(!segment_current(px,py,nx,ny))break;selected=next;next=nodes[next].parent;}
        dota_nav_cell_center(nodes[selected].cell,&sx,&sy);out[count].x=sx;out[count++].y=sy;px=sx;py=sy;current=nodes[selected].parent;
    }
    if(current==INVALID_NODE && nodes[best].cell==goal){
        int previous_x=count==1?x:out[count-2].x,previous_y=count==1?y:out[count-2].y;
        if(segment_current(previous_x,previous_y,gx,gy)){out[count-1].x=gx;out[count-1].y=gy;}
        else if(count<capacity){out[count].x=gx;out[count++].y=gy;}
        if(out[count-1].x==gx && out[count-1].y==gy){if(complete)*complete=1;++nav_stats.paths_completed;}
    }
    return count;
}
int dota_nav_walkable(int x,int y){refresh_item_trees();return walkable_current(x,y);}
int dota_nav_segment(int x,int y,int xx,int yy){refresh_item_trees();return segment_current(x,y,xx,yy);}
int dota_nav_nearest(int x,int y,int radius,int *out_x,int *out_y){refresh_item_trees();return nearest_current(x,y,radius,out_x,out_y);}
const DotaNavStats *dota_nav_stats(void){return &nav_stats;}
void dota_nav_reset_stats(void){unsigned char *p=(unsigned char *)&nav_stats;unsigned n=sizeof nav_stats;while(n--)*p++=0;}
unsigned dota_nav_static_bytes(void){return sizeof(nodes)+sizeof(lookup)+sizeof(heap)+sizeof(nav_stats)+sizeof(goal_cell_x)+sizeof(goal_cell_y)+sizeof(node_count)+sizeof(heap_count)+sizeof(dota_nav_walk_bits)+sizeof(dota_nav_right_bits)+sizeof(dota_nav_down_bits)+sizeof(dota_nav_tower_offsets)+sizeof(dota_nav_tower_cells)+sizeof(tower_mask)+sizeof(filler_mask)+sizeof(barracks_mask)+sizeof(dota_barracks_nav_cells)+sizeof(dota_barracks_nav_edges)+sizeof(dota_barracks_passage_owners)+sizeof(dota_nav_passage_cells)+sizeof(dota_nav_passage_circles)+sizeof(item_trees)+sizeof(item_tree_count)+sizeof(active_tree_indices)+sizeof(active_tree_count)+sizeof(exit_cache_cells)+sizeof(exit_cache_values)+sizeof(static_tree_game)+sizeof(static_tree_cached_game)+sizeof(static_tree_revision)+sizeof(static_tree_geometry)+sizeof(static_tree_valid)+sizeof(static_tree_bits)+sizeof(dota_nav_source_tree_bits);}
