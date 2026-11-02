#include "C:/Users/ASUS/Documents/ChatGPT/bbk H1/BBKH1-DOTA2/game/sight.c"
#include <string.h>
void new_copy_masks(void *out){memcpy(out,work->current,sizeof(work->current));memcpy((unsigned char*)out+sizeof(work->current),work->explored,sizeof(work->explored));}
int new_static_changed(void){return (int)work->static_changed;}
void new_source_fixture(const DotaGame*g,int x,int y,int radius){work->active_count=0;dynamic_trees(g);work->static_changed=!work->static_valid || work->static_tree_stamp!=work->tree_stamp;zero(work->next_static_members,sizeof(work->next_static_members));source_add(10,0,x,y,radius,0,0,0,0,1);merge_sources();}
