/* Narrow actual-provider wrapper; only host tests include this file. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef TREE_VISUAL_IMPLEMENTATION
#define TREE_VISUAL_IMPLEMENTATION "../platform/tree_visual.c"
#endif
#include TREE_VISUAL_IMPLEMENTATION

static DotaGame test_games[2];
static unsigned test_game_index,test_live,test_peak,test_allocations,test_fail_at;
void *cache_memory_alloc(unsigned n){
 uint32_t *p;if(++test_allocations==test_fail_at)return 0;
 p=malloc(n+8u);if(!p)return 0;p[0]=n;test_live+=n;if(test_live>test_peak)test_peak=test_live;return p+2;
}
void cache_memory_free(void *v){if(v){uint32_t *p=(uint32_t *)v-2;test_live-=p[0];free(p);}}
void dota_sight_reset(void){}
void dota_sight_invalidate(void){}
int cache_test_open(const char *path){test_game_index=0;memset(test_games,0,sizeof(test_games));return tree_visual_open(path);}
void cache_test_close(void){tree_visual_close();}
unsigned cache_test_live(void){return test_live;}
unsigned cache_test_peak(void){return test_peak;}
void cache_test_fail_next(unsigned delta){test_fail_at=delta?test_allocations+delta:0;}
void cache_test_reset_same(void){memset(&test_games[test_game_index],0,sizeof(DotaGame));}
void cache_test_switch_owner(void){test_games[test_game_index^1u]=test_games[test_game_index];test_game_index^=1u;}
int cache_test_cut(unsigned id,int tick){DotaGame *g=&test_games[test_game_index];g->tick=tick;return dota_trees_cut(g,id);}
void cache_test_tick(int tick){DotaGame *g=&test_games[test_game_index];g->tick=tick;dota_trees_tick(g);}
void cache_test_apply(uint16_t *rgb,uint16_t *depth,int gx,int gy,int width,int height){tree_visual_apply(rgb,depth,&test_games[test_game_index],gx,gy,width,height);}
int cache_test_pick(int x,int y){return tree_visual_pick(&test_games[test_game_index],x,y);}
unsigned cache_test_depth(int x,int y,unsigned original){return tree_visual_depth_at(&test_games[test_game_index],x,y,(uint16_t)original);}
void cache_test_stats(uint32_t *out){memcpy(out,tree_visual_stats(),sizeof(TreeVisualStats));}

