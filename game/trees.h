#ifndef H1_DOTA_TREES_H
#define H1_DOTA_TREES_H
#include "game.h"
#define DOTA_TREE_REGROW_TICKS (180*DOTA_HZ)
#define DOTA_TREE_RADIUS_WORLD 16
/* Borrowed immutable xy pairs, owned by the runtime TREE asset allocation.
 * No Game state or mutation is stored in this provider. Invalid attach
 * detaches the provider. Count and canonical ID order must match the asset. */
int dota_trees_attach(const uint16_t *xy,unsigned count);
void dota_trees_detach(void);
int dota_trees_ready(void);
unsigned dota_trees_geometry_revision(void);
int dota_trees_position(unsigned index,int *x,int *y);
int dota_trees_is_cut(const DotaGame *g,unsigned index);
unsigned dota_trees_revision(const DotaGame *g);
/* Nearest standing tree inside Source-unit radius, stable index tie break. */
int dota_trees_pick(const DotaGame *g,int x,int y,int radius_source);
int dota_trees_cut(DotaGame *g,unsigned index);
int dota_trees_cut_radius(DotaGame *g,int x,int y,int radius_source);
void dota_trees_tick(DotaGame *g);
#endif
