#ifndef H1_DOTA_NAVIGATION_H
#define H1_DOTA_NAVIGATION_H
#include "game.h"

#define DOTA_NAV_PATH_NODES 4096
#define DOTA_UNIT_ROUTE_POINTS DOTA_ROUTE_POINTS
typedef struct {
    unsigned segment_checks,blocked_segments,path_queries,expanded_nodes;
    unsigned budget_exhaustions,paths_completed;
} DotaNavStats;

int dota_nav_walkable(int x,int y);
int dota_nav_segment(int x,int y,int xx,int yy);
int dota_nav_nearest(int x,int y,int radius,int *out_x,int *out_y);
int dota_nav_find_path(int x,int y,int goal_x,int goal_y,DotaPoint *out,
                       int capacity,unsigned max_expanded,int *complete);
/* Tower bits0..11=(team*3+lane)*2+tier forT1/T2;12..21 are five
 * highground towers per team. Filler bits0..13 follow original entity order.
 * Call with the active game's live structure masks before navigation queries.
 * Source terrain/tree/PHYS obstacles are never restored by these APIs. */
/* Compatibility helper sets the tower mask and clears the filler mask. */
void dota_nav_set_towers(unsigned mask);
void dota_nav_set_structures(unsigned towers,unsigned fillers);
/* Barracks bits=(team*3+lane)*2+kind-1; compatibility APIs keep all12 alive. */
void dota_nav_set_all_structures(unsigned towers,unsigned fillers,unsigned barracks);
/* The caller owns this bounded array for the active game's lifetime. Only
 * explicitly planted live trees are added; source terrain never becomes open.
 * Bind the active game's array before querying. Each query observes all direct
 * same-tick edits, including planting, removal, expiration and item callbacks. */
void dota_nav_set_item_trees(const DotaItemTree *trees,unsigned count);
const DotaNavStats *dota_nav_stats(void);
void dota_nav_reset_stats(void);
unsigned dota_nav_static_bytes(void);
int dota_nav_cell_at(int x,int y);
void dota_nav_cell_center(unsigned cell,int *x,int *y);
#endif
