#ifndef H1_DOTA_BAKED_SCENE_H
#define H1_DOTA_BAKED_SCENE_H

#include <stdint.h>
#include "../game/game.h"

#define BS_WIDTH 480
#define BS_TOP 24
#define BS_HEIGHT 192
#define BS_TILE_SIZE 128
#define BS_ZOOM 180
#define BS_PITCH 181
#define BS_MAGIC 0x314b4248u /* little endian HBK1 */
#define BS_VERSION 12u
#define BS_WATER_MAGIC 0x31525457u /* little endian WTR1 */
#define BS_WATER_MAX_FRAMES 32u
#define BS_MAX_MODELS 192
#define BS_CLIPS 11
#define BS_DIRECTIONS 16
#define BS_MAX_FRAMES 65535
#define BS_MAX_GROUPS 8192
#define BS_MAX_FRAME_SIDE 256

enum { BS_IDLE=0,BS_RUN=1,BS_ATTACK=2,BS_CAST=3,BS_DEATH=4 };
enum { BS_SKILL_Q=5,BS_SKILL_W=6,BS_SKILL_E=7,BS_SKILL_R=8,
       BS_SKILL_PHASE_START=9,BS_SKILL_PHASE_END=10 };
enum { BS_ADDITIVE=1,BS_NO_DEPTH_WRITE=2,BS_HAS_SHADOW=4 };
/* File structs are little endian, with exactly these fixed-width members.
 * Header = 64 bytes; frame index entry = 32 bytes. No native pointers on disk.
 * A tile is 16384 RGB565 values followed by 16384 absolute uint16 depths.
 * A sprite is w*h RGB565, w*h alpha8, then w*h signed int16 relative depths.
 * HBK7 hero frames flagged BS_HAS_SHADOW append an8-byte <HHhh> header
 * (shadow width,height,foot anchor x,y), then width*height shadow alpha8.
 * This independent compact rectangle is projected onto the local ground.
 * Absolute depth = 32768 - (world_y + world_z); 65535 means empty.
 * Relative sprite depth = -(local_y + local_z), after source yaw/scale.
 * Sprite top-left = projected unit feet minus the signed frame anchor.
 */
typedef struct {
    uint32_t magic,version,file_size,tile_size,tile_columns,tile_rows;
    int32_t origin_pixel_x,origin_pixel_y;
    uint32_t tile_index_offset,frame_index_offset,frame_count;
    uint32_t ground_grid_offset,ground_grid_size,zoom,pitch,flags;
} BSPackHeader;

/* Version 2 uses BSPackHeader.flags as this extension's file offset.
 * Version 1 requires flags=0 and has no animated water. Frames contain 128²
 * palette indices. Palette entries0..255 are deep water,256..511 the same
 * wave phase shaded as shoreline foam. Mask bytes pack shore strength in the
 * high6 bits and coverage in the low2 bits (0/85/170/255 alpha).
 */
typedef struct {
    uint32_t magic,frame_count,frame_ticks,tile_size;
    uint32_t frames_offset,mask_index_offset,palette_offset,frame_bytes;
} BSWaterHeader;
typedef struct {
    uint32_t offset,size;
    uint16_t width,height;
    int16_t anchor_x,anchor_y;
    uint16_t model,clip,direction,frame,duration_ticks,flags,height_world,reserved;
} BSFrame;
typedef struct { int x,y,z; } BSCamera;
typedef struct { unsigned patched_pairs,patched_pixels,selectable_buildings,errors; } BSBarracksStats;
typedef struct { int model,clip,direction,frame,skill,phase; } BSUnitAnimation;
typedef struct { uint32_t bodies,moving_bodies,shadow_instances,model_mask,missing_frames; } BSCourierStats;
typedef struct {
    uint32_t cast_events,body_skill_actors,effect_instances,loop_instances;
    uint32_t impact_instances,model_mask,missing_skill_frames;
} BSSkillStats;
typedef struct {
    uint32_t release_events,flight_instances,muzzle_instances,impact_instances;
    uint32_t style_mask,missing_projectile_frames,cancel_events;
} BSProjectileStats;
typedef struct {
    uint32_t ward_bodies,ward_auras,beam_instances,beam_pixels,white_core_pixels;
    uint32_t skull_instances,dragon_instances,missing_component_frames;
} BSComponentStats;
typedef struct { uint32_t trigger_events,body_reactions,effect_instances,model_mask; } BSPassiveStats;
typedef struct { uint32_t shop_instances,tormentor_instances,tormentor_clip,missing_jungle_frames; } BSJungleStats;
typedef struct {
    uint32_t cast_events,strike_events,body_instances,trail_instances;
    uint32_t impact_instances,afterimages,missing_frames;
} BSOmnislashStats;
typedef struct { int active,start_x,start_y,end_x,end_y,start_z,end_z; } BSBeamGeometry;
typedef struct {
    uint32_t tile_hits,tile_misses,sprite_hits,sprite_misses;
    uint32_t read_requests,bytes_requested,read_failures;
    uint32_t map_pixels,sprite_pixels,blended_pixels,depth_rejects;
    uint32_t units_drawn,effects_drawn,culled_sprites,missing_frames;
    uint32_t rendered_triangles; /* Always zero: no mesh processing on H1. */
} BSStats;

/* Separate from the original16-field renderer statistics for compatibility
 * with existing controller tools. Reset on every bs_render_game call.
 */
typedef struct {
    uint32_t frame,enabled,mask_hits,mask_misses,water_pixels,shore_pixels;
} BSWaterStats;

typedef struct {
    uint32_t shadow_instances,shadow_pixels,shadow_depth_rejects;
    uint32_t fountain_instances,fountain_pixels,fountain_frame[2],missing_lighting_frames;
} BSLightingStats;
/* 63x27 world-anchored controls include the complete offscreen halo. Fresh
 * current-sight queries are bounded by1701 per render, including cache hits. */
typedef struct { uint32_t cells,queries,dimmed_pixels,soft_cells,sprites_rejected; } BSFogStats;
const BSFogStats *bs_fog_stats(void);
/* Optional exact RGB-only fog result cache; depth and scenery stay fresh. */
typedef struct { uint32_t heap_bytes,hits,misses,captures,streak,ready; } BSFogCacheStats;
const BSFogCacheStats *bs_fog_cache_stats(void);
unsigned bs_reserve_fog_cache(unsigned bytes);

/* The native wrapper opens A:\DOTA2\SCENE.H1P. Hosts can instead call
 * baked_io_open(path) followed by bs_initialize(). Static fallback is always
 * available; optional firmware-heap caches are requested after initialization. */
typedef struct {
    uint32_t open_attempts,ready,disabled,loaded_tiles,decoded_tiles,raw_tiles;
    uint32_t source_fallback_tiles,validation_errors,io_errors,heap_bytes,last_reason;
} BSMapSidecarStats;
int bs_map_sidecar_open(const char *path);
int bs_map_sidecar_open_paths(const char *primary,const char *secondary);
int bs_map_sidecar_open_default(void);
void bs_map_sidecar_close(void);
const BSMapSidecarStats *bs_map_sidecar_stats(void);

int bs_initialize(void);
/* Returns the actual sprite capacity (640KiB on allocation failure), or zero
 * for an invalid/not-ready pack. Also attempts a fully resident decoded frame
 * directory. Zero releases optional allocations and returns to static caches.
 * Calls invalidate cached pixels, never change baked pixels/animation timing. */
unsigned bs_reserve_cache(unsigned sprite_bytes);
/* Optional working sets; call after mandatory app allocations. Zero releases. */
unsigned bs_reserve_sprite_row_cache(unsigned slots);
/* Optional startup-only warmup. Returns the complete fountain frame count,
 * or0 without claiming a warm cache when storage/slots/directory are missing,
 * the cache is already populated, or the payload is not one aligned run.
 * A physical read/validation failure is reported by bs_last_error(). */
/* Optional poll returns nonzero to cancel between contiguous64KiB chunks.
 * Cancelled work publishes no cache entries and may be retried or closed. */
unsigned bs_prewarm_fountain(unsigned team,int (*poll)(void));
unsigned bs_sprite_cache_bytes(void);
unsigned bs_resident_frame_count(void);
unsigned bs_dynamic_storage_bytes(void);
int bs_open_default(void);
void bs_close(void);
int bs_ready(void);
int bs_last_error(void);
const BSPackHeader *bs_pack_header(void);
const BSStats *bs_stats(void);
const BSBarracksStats *bs_barracks_stats(void);
const BSSkillStats *bs_skill_stats(void);
const BSProjectileStats *bs_projectile_stats(void);
const BSComponentStats *bs_component_stats(void);
const BSPassiveStats *bs_passive_stats(void);
const BSJungleStats *bs_jungle_stats(void);
const BSOmnislashStats *bs_omnislash_stats(void);
const BSBeamGeometry *bs_beam_geometry(void);
const BSUnitAnimation *bs_unit_animation(int unit);
const BSUnitAnimation *bs_courier_animation(int owner);
const BSCourierStats *bs_courier_stats(void);
const BSWaterStats *bs_water_stats(void);
const BSWaterHeader *bs_water_header(void);
const BSLightingStats *bs_lighting_stats(void);
unsigned bs_static_storage_bytes(void);

/* Global integer pixel projection is rounded down BEFORE camera subtraction;
 * this is also the bake convention, so static tile seams cannot drift. */
int bs_project(const BSCamera *,int x,int y,int z,int *sx,int *sy,uint16_t *depth);
int bs_unproject_at_z(const BSCamera *,int sx,int sy,int z,int *x,int *y);
int bs_ground_height(int x,int y);
int bs_pick_ground(const BSCamera *,int sx,int sy,int *x,int *y);
const uint16_t *bs_depth_buffer(void);
int bs_unit_screen_bounds(int unit,int bounds[4],uint16_t *nearest_depth);
int bs_shop_screen_bounds(int shop,int bounds[4],uint16_t *nearest_depth);
int bs_unit_height(const DotaUnit *unit);
int bs_game_unit_height(const DotaGame *game,int index);

void bs_render_game(uint16_t *pixels,const DotaGame *,const BSCamera *);
void bs_render_effects(uint16_t *pixels,const DotaGame *,const BSCamera *);
/* Blit the original, fully rendered idle animation into a menu rectangle.
 * Does not clear pixels outside the sprite; caller draws its panel first. */
int bs_draw_menu_hero(uint16_t *pixels,int hero,int direction,unsigned tick,
                      int x,int y,int width,int height);

#endif
