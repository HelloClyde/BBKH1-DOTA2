/* Exercise the actual file reader/compositor, allocation and tree state. */
#include "tree_visual.h"
#include "trees.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned live,allocations,fail_at,checks;
void *cache_memory_alloc(unsigned n){unsigned *p;if(++allocations==fail_at)return 0;p=malloc(n+sizeof(unsigned));if(!p)return 0;*p=n;live+=n;return p+1;}
void cache_memory_free(void *v){if(v){unsigned *p=(unsigned *)v-1;live-=*p;free(p);}}
void dota_sight_reset(void){}
void dota_sight_invalidate(void){}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){
 DotaGame *g=calloc(1,sizeof(*g));uint16_t *rgb=malloc(32768),*depth=malloc(32768);unsigned i,reads,rev;int mode=argc>2?atoi(argv[2]):0;
 CHECK(argc>1&&g&&rgb&&depth);
 if(mode==1){CHECK(!tree_visual_open(argv[1]));CHECK(tree_visual_error()!=0);CHECK(!live);goto done;}
 CHECK(tree_visual_open(argv[1]));CHECK(tree_visual_ready());CHECK(dota_trees_ready());CHECK(live==41196);
 if(mode==3){unsigned reserved;reads=tree_visual_stats()->read_requests;CHECK(tree_visual_reserve_work());reserved=live;CHECK(tree_visual_reserve_work()&&live==reserved);CHECK(reads==tree_visual_stats()->read_requests);fail_at=allocations+1;}
 if(mode==4||mode==5){fail_at=allocations+(mode==4?1:2);CHECK(!tree_visual_reserve_work());CHECK(tree_visual_error()!=0&&live==41196);goto close;}
 for(i=0;i<16384;++i){rgb[i]=0x1234;depth[i]=500;}
 rgb[0]=0x07e0;depth[0]=100;rgb[1]=0xffff;depth[1]=80;rgb[2]=0x001f;depth[2]=50;
 reads=tree_visual_stats()->read_requests;tree_visual_apply(rgb,depth,g,-512,-512,128,128);CHECK(reads==tree_visual_stats()->read_requests);CHECK(rgb[0]==0x07e0);
 CHECK(dota_trees_cut(g,0));rev=tree_visual_revision(g);
 if(mode==2){fail_at=allocations+1;tree_visual_apply(rgb,depth,g,-512,-512,128,128);CHECK(!tree_visual_ready());CHECK(tree_visual_error()!=0);goto close;}
 CHECK(tree_visual_pick(g,-512,-512)==1);CHECK(tree_visual_pick(g,-511,-512)==-1);CHECK(tree_visual_pick(g,-510,-512)==-1);
 tree_visual_apply(rgb,depth,g,-512,-512,128,128);CHECK(!tree_visual_error());CHECK(rgb[0]==0xf800&&depth[0]==200);CHECK(rgb[1]==0xffff&&depth[1]==80);CHECK(rgb[2]==0x001f&&depth[2]==50);CHECK(rgb[4]==0x8410&&depth[4]==150);
 CHECK(tree_visual_depth_at(g,-512,-512,100)==200);reads=tree_visual_stats()->read_requests;
 tree_visual_apply(rgb,depth,g,-512,-512,128,128);CHECK(reads==tree_visual_stats()->read_requests);
 CHECK(dota_trees_cut(g,1));CHECK(tree_visual_revision(g)!=rev);tree_visual_apply(rgb,depth,g,-512,-512,128,128);CHECK(rgb[0]==0x1234&&depth[0]==500);
 g->tick=DOTA_TREE_REGROW_TICKS-1;dota_trees_tick(g);CHECK(g->trees.cut_count==2);
 g->tick=DOTA_TREE_REGROW_TICKS;dota_trees_tick(g);CHECK(!g->trees.cut_count);
 CHECK(tree_visual_pick(g,-512,-512)==0);
 rgb[0]=0x07e0;depth[0]=100;reads=tree_visual_stats()->read_requests;tree_visual_apply(rgb,depth,g,-512,-512,128,128);CHECK(rgb[0]==0x07e0&&depth[0]==100);CHECK(reads==tree_visual_stats()->read_requests);
close:tree_visual_close();CHECK(!live);CHECK(!dota_trees_ready());CHECK(!tree_visual_dynamic_bytes());
done:free(g);free(rgb);free(depth);printf("PASS %u\n",checks);return 0;
}
