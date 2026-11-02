#include "sight-before.c"
#include <string.h>
void old_copy_masks(void *out){memcpy(out,work->current,sizeof(work->current));memcpy((unsigned char*)out+sizeof(work->current),work->explored,sizeof(work->explored));}
void old_source_fixture(const DotaGame*g,int x,int y,int radius){work->active_count=0;dynamic_trees(g);zero(work->current,sizeof(work->current));source_add(10,0,x,y,radius,0,0,0,0); }
void old_mode_source(const DotaGame*g,int x,int y,int r,int f){work->active_count=0;dynamic_trees(g);zero(work->current,sizeof(work->current));source_add(10,0,x,y,r,0,0,0,f);}
