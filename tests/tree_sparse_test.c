/* Test-only access to the actual static decoder, never linked into the BDA. */
#include <stdlib.h>
#include <string.h>
#include "../platform/tree_visual.c"
static unsigned live_bytes;
static DotaGame game;
void *cache_memory_alloc(unsigned n){unsigned *p=malloc(n+sizeof(unsigned));if(!p)return 0;*p=n;live_bytes+=n;return p+1;}
void cache_memory_free(void *v){if(v){unsigned *p=(unsigned *)v-1;live_bytes-=*p;free(p);}}
void dota_sight_reset(void){}
void dota_sight_invalidate(void){}
int tree_test_open(const char *path){memset(&game,0,sizeof(game));return tree_visual_open(path);}
int tree_test_snapshot(unsigned tile,void *output,unsigned capacity){unsigned n;int layers;if(tile>=TV_TILES||!prepare(&game,1))return -1;layers=load_tile(tile);if(tree_visual_error())return -1;if(!layers)return 0;n=mapc_get32(directory(tile)+8);if(capacity<n)return -1;memcpy(output,scratch,n);return (int)n;}
unsigned tree_test_live(void){return live_bytes;}
unsigned tree_test_extra(void){return scratch_extra;}
unsigned tree_test_scratch(void){return tree_stats.scratch_bytes;}
/* Decoder timings deliberately bypass the production last-tile cache. */
int tree_test_repeat(unsigned tile,unsigned repeat){unsigned i;if(tile>=TV_TILES||!prepare(&game,1))return 0;for(i=0;i<repeat;++i){canonical_tile=0xffffffffu;if(!load_tile(tile)||tree_visual_error())return 0;}return 1;}
void tree_test_close(void){tree_visual_close();}
