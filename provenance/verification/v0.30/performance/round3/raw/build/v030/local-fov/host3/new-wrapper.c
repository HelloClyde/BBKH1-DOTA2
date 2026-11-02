#include "C:/Users/ASUS/Documents/ChatGPT/bbk H1/BBKH1-DOTA2/game/sight.c"

#include <string.h>
void new_copy_masks(void *out){memcpy(out,work->current,sizeof(work->current));memcpy((unsigned char*)out+sizeof(work->current),work->explored,sizeof(work->explored));}
int new_static_changed(void){return (int)work->static_changed;}
void new_source_fixture(const DotaGame*g,int x,int y,int radius){work->active_count=0;dynamic_trees(g);work->static_changed=!work->static_valid || work->static_tree_stamp!=work->tree_stamp;zero(work->next_static_members,sizeof(work->next_static_members));source_add(10,0,x,y,radius,0,0,0,0,1);merge_sources();}
void new_mode_source(const DotaGame*g,int x,int y,int r,int f){work->active_count=0;dynamic_trees(g);work->static_changed=!work->static_valid || work->static_tree_stamp!=work->tree_stamp;zero(work->next_static_members,sizeof(work->next_static_members));source_add(10,0,x,y,r,0,0,0,f,1);merge_sources();}
unsigned new_obstacle_count(void){unsigned n=0;int i;for(i=0;i<OBSTACLE_PLANES;++i)n+=work->obstacle_planes[i].valid!=0;return n;}
int new_source_is_cached(void){return work->source[10].obstacle_plane!=0;}
int new_source_has_cached_class(void){SightSource*s=&work->source[10];int i;for(i=0;i<OBSTACLE_PLANES;++i){SightObstaclePlane*p=&work->obstacle_planes[i];if(p->valid&&p->height==s->height&&p->region==s->region&&p->flying==s->flying)return 1;}return 0;}
unsigned new_obstacle_hash(void){unsigned h=2166136261u;int i,c;for(i=0;i<OBSTACLE_PLANES;++i){SightObstaclePlane*p=&work->obstacle_planes[i];h=mix(h,(unsigned)p->height);h=mix(h,(unsigned)p->region);h=mix(h,(unsigned)p->flying);for(c=0;c<GRID*GRID;++c)h=mix(h,p->cells[c]);}return h;}

int new_tree_cell(int x,int y){return work->tree_cells[work->active_tree_plane][(y/CELL)*GRID+x/CELL];}
int new_cached_cell(int h,int r,int f,int x,int y){int i;for(i=0;i<OBSTACLE_PLANES;++i){SightObstaclePlane*p=&work->obstacle_planes[i];if(p->valid&&p->height==h&&p->region==r&&p->flying==f)return p->cells[(y/CELL)*GRID+x/CELL];}return -1;}

int new_source_tree_current(int i){return work->source[i].valid&&work->source[i].tree_stamp==work->tree_stamp;}
