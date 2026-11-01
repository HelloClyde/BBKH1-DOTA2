#ifndef H1_DOTA_TREES_STATE_H
#define H1_DOTA_TREES_STATE_H
#include <stdint.h>
#define DOTA_STATIC_TREE_COUNT 2113
#define DOTA_STATIC_TREE_WORDS ((DOTA_STATIC_TREE_COUNT+31)/32)
/* IDs are the immutable order in provenance/vision/trees.json. Deadlines
 * wrap modulo 65536; the 3600-tick regrow interval is below half that span. */
typedef struct {
    uint32_t cut[DOTA_STATIC_TREE_WORDS];
    uint16_t regrow_at[DOTA_STATIC_TREE_COUNT];
    uint16_t cut_count;
    uint32_t revision;
} DotaTreesState;
#endif
