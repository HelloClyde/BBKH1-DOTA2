#ifndef H1_DOTA_TREE_VISUAL_H
#define H1_DOTA_TREE_VISUAL_H
#include <stdint.h>
#include "../game/game.h"
typedef struct {
 uint32_t open_attempts,ready,error,heap_bytes,metadata_bytes,cache_bytes,scratch_bytes;
 uint32_t read_requests,read_bytes,tile_hits,tile_misses,decoded_tiles,patched_pixels;
 uint32_t depth_queries,closes,released;
} TreeVisualStats;
/* TREE.H1T owns the immutable natural-tree xy provider and sparse replacement
 * pixels. No game state is stored in this module; Game copies own cut bits. */
int tree_visual_open(const char *path);
int tree_visual_open_default(void);
void tree_visual_close(void);
unsigned tree_visual_dynamic_bytes(void);
int tree_visual_ready(void);
int tree_visual_error(void);
const TreeVisualStats *tree_visual_stats(void);
/* pixels/depths point at the battlefield's first pixel (not screen row0).
 * Coordinates are global projection pixels, before subtracting pack origin. */
void tree_visual_apply(uint16_t *pixels,uint16_t *depths,const DotaGame *game,
                       int global_x,int global_y,int width,int height);
uint16_t tree_visual_depth_at(const DotaGame *game,int global_x,int global_y,
                             uint16_t original_depth);
unsigned tree_visual_revision(const DotaGame *game);
/* Hit the original rendered crown, returning its exact natural-tree ID. */
int tree_visual_pick(const DotaGame *game,int global_x,int global_y);
#endif
