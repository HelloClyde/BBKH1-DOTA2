#include "baked_scene.h"
#include "objectives.h"
#include "courier.h"
#include "baked_io.h"
#include "cache_memory.h"
#include "shadow_cull_data.h"
#include "bosses.h"
#include "jungle.h"
#include "roster_heroes.h"
#include "vision.h"
#include "sight.h"
#include "barracks.h"
#include "vision_assets.h"
#include "../private/generated/dota_map_fixtures.h"
#include "../private/generated/dota_barracks_patch.h"

#define TILE_CACHE_SLOTS 16
#define TILE_PIXELS (BS_TILE_SIZE*BS_TILE_SIZE)
#define MAX_TILES 4096
#define MAX_HEIGHT_SIZE 257
#define SPRITE_CACHE_BYTES (640u*1024u)
#define SPRITE_CACHE_SLOTS 64
#define META_CACHE_SLOTS 32
#define META_BLOCK_FRAMES 16
#define CLEAR_COLOR 0x10c3u
#define FOG_STEP 8
#define FOG_COLUMNS (BS_WIDTH/FOG_STEP)
#define FOG_ROWS (BS_HEIGHT/FOG_STEP)
static unsigned char fog_visible[FOG_COLUMNS*FOG_ROWS];
static BSFogStats fog_stats;
static const DotaGame *fog_game;
static const BSCamera *fog_camera;
static int fog_sprite_gate;
const BSFogStats *bs_fog_stats(void){return &fog_stats;}
#define FOG_BACKGROUND_BYTES (BS_WIDTH*BS_HEIGHT*sizeof(uint16_t))
#define FOG_BACKGROUND_KEYS (8+DOTA_LOTUS_POOLS+1)
static uint16_t *fog_background;
static int fog_background_key[FOG_BACKGROUND_KEYS],fog_saved_key[FOG_BACKGROUND_KEYS];
static unsigned char fog_saved_visible[FOG_COLUMNS*FOG_ROWS];
static BSFogCacheStats fog_cache_stats;
static unsigned fog_cached_dimmed,fog_cached_soft;
const BSFogCacheStats *bs_fog_cache_stats(void){return &fog_cache_stats;}


/* Only real groups are resident. The sorted key packs model/clip/direction;
 * selection is a binary search outside all pixel loops. */
typedef struct { uint16_t first,count,ticks,key; } ClipGroup;
/* The on-disk BSFrame remains32bytes. All accepted payloads fit19bits,
 * dimensions9bits and world height13bits. Explicit shifts keep the resident
 * directory16bytes with aligned MIPS loads and no implementation bitfields. */
typedef struct {
    uint32_t offset,size_ticks_flags,geometry;
    int16_t anchor_x,anchor_y;
} RuntimeFrame;
typedef char RuntimeFrame_must_be_16_bytes[(sizeof(RuntimeFrame)==16)?1:-1];
static inline unsigned rf_size(const RuntimeFrame *f){return f?f->size_ticks_flags&0x7ffffu:0;}
static inline unsigned rf_ticks(const RuntimeFrame *f){return f?(f->size_ticks_flags>>19)&255u:0;}
static inline unsigned rf_flags(const RuntimeFrame *f){return f?(f->size_ticks_flags>>27)&7u:0;}
static inline int rf_width(const RuntimeFrame *f){return f?f->geometry&511u:0;}
static inline int rf_height(const RuntimeFrame *f){return f?(f->geometry>>9)&511u:0;}
static inline int rf_world_height(const RuntimeFrame *f){return f?f->geometry>>18:0;}
typedef struct { int tile;uint32_t stamp; } TileCache;
typedef struct { int frame;uint32_t start,size,stamp; } SpriteCache;
typedef struct { int bounds[4],valid;uint16_t depth; } UnitBounds;
typedef struct {
    int x,y,z,yaw,moving,generation,active,hp,model;
    int attack_wait,cooldown[4],action,action_tick,death_tick;
    int cast_skill,cast_yaw,was_channel;
} UnitHistory;
typedef struct { DotaSkillEvent event;int valid; } SkillTrack;
typedef struct { int active,kind,x,y,to_x,to_y,ticks,start_tick,initial_ticks; } EffectHistory;

static unsigned char shadow_curve[256][96];
static int shadow_curve_ready;
static void initialize_shadow_curve(void){
    unsigned a,c;if(shadow_curve_ready)return;
    for(a=0;a<256u;++a){
        for(c=0;c<32u;++c)shadow_curve[a][c]=(unsigned char)((c*(255u-a)+127u)/255u);
        for(c=0;c<64u;++c)shadow_curve[a][32u+c]=(unsigned char)((c*(255u-a)+127u)/255u);
    }
    shadow_curve_ready=1;
}
static BSPackHeader pack;
/* Full frame metadata is streamed in small blocks. Only the timing byte is
 * always resident, so animation selection never causes a file read. */
static unsigned char frame_ticks[BS_MAX_FRAMES];
static RuntimeFrame frame_metadata[META_CACHE_SLOTS][META_BLOCK_FRAMES];
static RuntimeFrame *resident_frames;
/* Optional coordinate weights share the resident frame directory allocation.
 * Static-cache fallback computes the identical division directly. */
static uint16_t *ground_coordinates;
static TileCache metadata_cache[META_CACHE_SLOTS];
static uint32_t metadata_clock;
static BSUnitAnimation courier_animations[DOTA_HERO_COUNT];
static BSCourierStats courier_stats;
static ClipGroup groups[BS_MAX_GROUPS];
static unsigned group_count;
static uint32_t tile_offsets[MAX_TILES];
static int16_t ground[MAX_HEIGHT_SIZE*MAX_HEIGHT_SIZE];
static uint16_t tile_pixels[TILE_CACHE_SLOTS][TILE_PIXELS*2];
static TileCache tile_cache[TILE_CACHE_SLOTS];
static BSWaterHeader water;
static uint32_t water_offsets[MAX_TILES];
static uint8_t (*water_frames)[TILE_PIXELS];
static unsigned water_heap_bytes;
static uint16_t water_palette[512];
static uint8_t water_masks[TILE_CACHE_SLOTS][TILE_PIXELS];
static TileCache water_cache[TILE_CACHE_SLOTS];
static BSWaterStats water_stats;
static BSLightingStats lighting_stats;
/* uint32 backing guarantees alignment for all cache allocations. */
static uint32_t sprite_storage[SPRITE_CACHE_BYTES/4];
static SpriteCache sprite_cache[SPRITE_CACHE_SLOTS];
static unsigned char *sprite_data=(unsigned char *)sprite_storage;
static SpriteCache *sprite_entries=sprite_cache;
static uint16_t *sprite_lookup;
static void *sprite_allocation;
static unsigned sprite_capacity=SPRITE_CACHE_BYTES,sprite_slots=SPRITE_CACHE_SLOTS,sprite_heap_bytes;
static uint32_t sprite_write,cache_clock;
static uint16_t depths[BS_WIDTH*BS_HEIGHT];
static UnitBounds bounds[DOTA_MAX_UNITS];
static UnitBounds shop_bounds[2];
static UnitHistory history[DOTA_MAX_UNITS];
static SkillTrack skill_tracks[DOTA_HERO_COUNT][DOTA_SKILLS];
static BSUnitAnimation animations[DOTA_MAX_UNITS];
static BSSkillStats skill_stats;
static unsigned observed_skill_serial;
static BSProjectileStats projectile_stats;
static BSComponentStats component_stats;
static BSPassiveStats passive_stats;
static BSJungleStats jungle_stats;
static BSOmnislashStats omnislash_stats;
static BSBeamGeometry beam_geometry;
static unsigned observed_attack_serial;
static EffectHistory effect_history[DOTA_MAX_EFFECTS];
static BSStats stats;
static int ready,error_code,last_tick=-1,view_region;
static const int sine[16]={0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};

typedef char HeaderSizeCheck[sizeof(BSPackHeader)==64?1:-1];
typedef char FrameSizeCheck[sizeof(BSFrame)==32?1:-1];
typedef char RuntimeFrameSizeCheck[sizeof(RuntimeFrame)==16?1:-1];
typedef char WaterHeaderSizeCheck[sizeof(BSWaterHeader)==32?1:-1];
typedef char ClipGroupSizeCheck[sizeof(ClipGroup)==8?1:-1];

static void clear_bytes(void *data,unsigned size){unsigned char *p=data;while(size--)*p++=0;}
static int clamp(int value,int lo,int hi){return value<lo?lo:value>hi?hi:value;}
static int floor_div(int value,int divisor){int q=value/divisor,r=value%divisor;return q-(r<0);}
static int global_x(int x){return floor_div(x*BS_ZOOM,256);}
static int global_y(int y,int z){return floor_div((y-z)*(BS_PITCH*BS_ZOOM),65536);}
static int valid_range(uint32_t offset,uint32_t size){return offset>=64 && offset<=pack.file_size && size<=pack.file_size-offset;}
static int read_block(uint32_t offset,void *dst,uint32_t size){
    ++stats.read_requests;stats.bytes_requested+=size;
    if(!baked_io_read_at(offset,dst,size)){++stats.read_failures;error_code=8;return 0;}
    return 1;
}
static void decode_frame(RuntimeFrame *r,const BSFrame *f){
    r->offset=f->offset;
    r->size_ticks_flags=f->size|((uint32_t)f->duration_ticks<<19)|((uint32_t)f->flags<<27);
    r->geometry=f->width|((uint32_t)f->height<<9)|((uint32_t)f->height_world<<18);
    r->anchor_x=f->anchor_x;r->anchor_y=f->anchor_y;
}
static int frame_record_valid(const BSFrame *f){
    uint32_t body=(uint32_t)f->width*f->height*5u;
    int shadow_model=f->model<4 || (f->model>=48 && f->model<=58 && pack.version>=8u) ||
        (f->model>=59 && f->model<=72 && pack.version>=9u) ||
        (f->model>=75 && f->model<=76 && pack.version>=10u) ||
        (((f->model>=77 && f->model<=78) || (f->model>=88 && f->model<=92)) && pack.version>=11u) ||
        (((f->model>=93 && f->model<=96) || (f->model>=114 && f->model<=115) || (f->model>=128 && f->model<=157)) && pack.version>=12u);
    if(!f->width || !f->height || f->width>BS_MAX_FRAME_SIDE || f->height>BS_MAX_FRAME_SIDE ||
       (!(f->flags&BS_HAS_SHADOW) && f->size!=body) ||
       ((f->flags&BS_HAS_SHADOW) && (pack.version<7u || !shadow_model || f->size<body+9u || f->size>body+8u+BS_MAX_FRAME_SIDE*BS_MAX_FRAME_SIDE)) ||
       !valid_range(f->offset,f->size) || f->size>0x7ffffu ||
       f->model>=BS_MAX_MODELS || f->clip>=BS_CLIPS || f->direction>=BS_DIRECTIONS ||
       !f->duration_ticks || f->duration_ticks>200 || (f->flags&~7u) || f->reserved || f->height_world>4096)return 0;
    return 1;
}
static int frame_identity_valid(unsigned index,const BSFrame *f){
    unsigned lo=0,hi=group_count;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(groups[mid].first<=index)lo=mid+1;else hi=mid;}
    if(!lo)return 0;
    {const ClipGroup *g=&groups[lo-1];return index<g->first+g->count && f->frame==index-g->first &&
        g->key==(unsigned)((f->model*BS_CLIPS+f->clip)*BS_DIRECTIONS+f->direction);}
}
static const RuntimeFrame *frame_info(unsigned index){
    unsigned i,slot=0,count;uint32_t oldest=0xffffffffu;unsigned block=index/META_BLOCK_FRAMES;
    BSFrame disk[META_BLOCK_FRAMES];
    if(index>=pack.frame_count)return 0;
    if(resident_frames)return &resident_frames[index];
    for(i=0;i<META_CACHE_SLOTS;++i){
        if(metadata_cache[i].tile==(int)block){metadata_cache[i].stamp=++metadata_clock;return &frame_metadata[i][index%META_BLOCK_FRAMES];}
        if(metadata_cache[i].tile<0){slot=i;oldest=0;}
        else if(oldest && metadata_cache[i].stamp<oldest){oldest=metadata_cache[i].stamp;slot=i;}
    }
    count=pack.frame_count-block*META_BLOCK_FRAMES;if(count>META_BLOCK_FRAMES)count=META_BLOCK_FRAMES;
    if(!read_block(pack.frame_index_offset+block*META_BLOCK_FRAMES*sizeof(BSFrame),disk,count*sizeof(BSFrame)))return 0;
    metadata_cache[slot].tile=-1; /* A rejected partial reload cannot poison its previous key. */
    for(i=0;i<count;++i){const BSFrame *f=&disk[i];
        if(!frame_record_valid(f) || !frame_identity_valid(block*META_BLOCK_FRAMES+i,f) ||
           f->duration_ticks!=frame_ticks[block*META_BLOCK_FRAMES+i]){
            error_code=6;++stats.read_failures;return 0;
        }
        decode_frame(&frame_metadata[slot][i],f);
    }
    metadata_cache[slot].tile=(int)block;metadata_cache[slot].stamp=++metadata_clock;
    return &frame_metadata[slot][index%META_BLOCK_FRAMES];
}
static uint16_t read_u16(const unsigned char *p){return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
static int16_t read_i16(const unsigned char *p){return (int16_t)read_u16(p);}
/* RGB payloads start at a word-aligned cache allocation plus a4-byte-aligned
 * entry; index*2 remains even. Relative-depth/shadow headers may be odd and
 * deliberately retain byte reads. memcpy preserves C aliasing semantics. */
static inline uint16_t read_sprite_rgb16(const unsigned char *p){
    uint16_t value;__builtin_memcpy(&value,__builtin_assume_aligned(p,2),2);return value;
}
/* Exact saturated RGB565 addition, equivalent to mix(fg,bg,255,1).
 * Independent red/blue carry bits are5/16, green carry bit11. */
static inline uint16_t add_full_rgb565(uint16_t fg,uint16_t bg){
    unsigned rb=(fg&0xf81fu)+(bg&0xf81fu),g=(fg&0x07e0u)+(bg&0x07e0u),overflow;
    overflow=rb&0x10020u;rb=(rb&0xf81fu)|(overflow-(overflow>>5));
    overflow=g&0x0800u;g=(g&0x07e0u)|(overflow-(overflow>>6));
    return (uint16_t)(rb|g);
}
static unsigned group_key(int model,int clip,int direction){return (unsigned)((model*BS_CLIPS+clip)*BS_DIRECTIONS+direction);}
static const ClipGroup *exact_group(int model,int clip,int direction){
    unsigned lo=0,hi=group_count,key;
    if((unsigned)model>=BS_MAX_MODELS || (unsigned)clip>=BS_CLIPS || (unsigned)direction>=BS_DIRECTIONS)return 0;
    key=group_key(model,clip,direction);
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(groups[mid].key<key)lo=mid+1;else hi=mid;}
    return lo<group_count && groups[lo].key==key?&groups[lo]:0;
}
static uint16_t mix(uint16_t fg,uint16_t bg,unsigned alpha,int additive){
    unsigned r,g,b,inv=255-alpha;
    if(additive){
        r=((bg>>11)&31)+(((fg>>11)&31)*alpha+127)/255;
        g=((bg>>5)&63)+(((fg>>5)&63)*alpha+127)/255;
        b=(bg&31)+((fg&31)*alpha+127)/255;
        if(r>31)r=31;
        if(g>63)g=63;
        if(b>31)b=31;
    }else{
        r=(((fg>>11)&31)*alpha+((bg>>11)&31)*inv+127)/255;
        g=(((fg>>5)&63)*alpha+((bg>>5)&63)*inv+127)/255;
        b=((fg&31)*alpha+(bg&31)*inv+127)/255;
    }
    return (uint16_t)((r<<11)|(g<<5)|b);
}

#include "map_sidecar.h"
static void barracks_visual_reset(void);
static unsigned barracks_visual_static_bytes(void);
unsigned bs_reserve_fog_cache(unsigned bytes){
    cache_memory_free(fog_background);fog_background=0;
    clear_bytes(&fog_cache_stats,sizeof(fog_cache_stats));
    if(bytes>=FOG_BACKGROUND_BYTES){
        fog_background=cache_memory_alloc(FOG_BACKGROUND_BYTES);
        if(fog_background)fog_cache_stats.heap_bytes=FOG_BACKGROUND_BYTES;
    }
    return fog_cache_stats.heap_bytes;
}
static void release_cache(void){
    unsigned i;
    bs_reserve_fog_cache(0);
    map_sidecar_reset();
    barracks_visual_reset();
    cache_memory_free(resident_frames);resident_frames=0;ground_coordinates=0;
    cache_memory_free(sprite_allocation);sprite_allocation=0;sprite_lookup=0;
    sprite_data=(unsigned char *)sprite_storage;sprite_entries=sprite_cache;
    sprite_capacity=SPRITE_CACHE_BYTES;sprite_slots=SPRITE_CACHE_SLOTS;sprite_heap_bytes=0;
    sprite_write=0;cache_clock=0;
    for(i=0;i<SPRITE_CACHE_SLOTS;++i)sprite_cache[i].frame=-1;
}

static void release_water(void){cache_memory_free(water_frames);water_frames=0;water_heap_bytes=0;}
static int initialize_pack(void){
    unsigned i,j,k;uint32_t bytes;int previous_key=-1;
    ready=0;error_code=0;last_tick=-1;clear_bytes(&stats,sizeof(stats));
    metadata_clock=0;for(i=0;i<META_CACHE_SLOTS;++i)metadata_cache[i].tile=-1;
    clear_bytes(courier_animations,sizeof(courier_animations));clear_bytes(&courier_stats,sizeof(courier_stats));
    for(i=0;i<DOTA_HERO_COUNT;++i)courier_animations[i].model=-1;
    clear_bytes(&water,sizeof(water));clear_bytes(&water_stats,sizeof(water_stats));
    clear_bytes(&lighting_stats,sizeof(lighting_stats));
    clear_bytes(skill_tracks,sizeof(skill_tracks));clear_bytes(&skill_stats,sizeof(skill_stats));observed_skill_serial=0;
    clear_bytes(&projectile_stats,sizeof(projectile_stats));observed_attack_serial=0;
    clear_bytes(&component_stats,sizeof(component_stats));clear_bytes(&beam_geometry,sizeof(beam_geometry));
    clear_bytes(&passive_stats,sizeof(passive_stats));
    clear_bytes(&omnislash_stats,sizeof(omnislash_stats));
    group_count=0;clear_bytes(groups,sizeof(groups));clear_bytes(bounds,sizeof(bounds));clear_bytes(history,sizeof(history));
    clear_bytes(effect_history,sizeof(effect_history));
    for(i=0;i<TILE_CACHE_SLOTS;++i){tile_cache[i].tile=-1;water_cache[i].tile=-1;}
    for(i=0;i<SPRITE_CACHE_SLOTS;++i)sprite_cache[i].frame=-1;
    sprite_write=0;cache_clock=0;
    if(baked_io_size()<sizeof(pack) || !baked_io_read_at(0,&pack,sizeof(pack))){error_code=1;return 0;}
    if(pack.magic!=BS_MAGIC || pack.version<1u || pack.version>BS_VERSION || pack.file_size!=baked_io_size() ||
       pack.file_size>0x7fffffffu || pack.tile_size!=BS_TILE_SIZE || pack.zoom!=BS_ZOOM || pack.pitch!=BS_PITCH ||
       (pack.version==1u && pack.flags)){error_code=2;return 0;}
    if(!pack.tile_columns || !pack.tile_rows || pack.tile_columns>64 || pack.tile_rows>64 ||
       pack.tile_columns*pack.tile_rows>MAX_TILES || !pack.frame_count || pack.frame_count>BS_MAX_FRAMES ||
       pack.ground_grid_size<2 || pack.ground_grid_size>MAX_HEIGHT_SIZE ||
       pack.origin_pixel_x<-8192 || pack.origin_pixel_x>8192 || pack.origin_pixel_y<-8192 || pack.origin_pixel_y>8192){error_code=3;return 0;}
    bytes=pack.tile_columns*pack.tile_rows*4;
    if(!valid_range(pack.tile_index_offset,bytes) || !read_block(pack.tile_index_offset,tile_offsets,bytes)){error_code=4;return 0;}
    for(i=0;i<pack.tile_columns*pack.tile_rows;++i)if(tile_offsets[i] && !valid_range(tile_offsets[i],TILE_PIXELS*4)){error_code=4;return 0;}
    bytes=pack.frame_count*sizeof(BSFrame);
    if(!valid_range(pack.frame_index_offset,bytes)){error_code=5;return 0;}
    /* Sequential2KiB index reads keep startup I/O bounded; only compressed
     * records survive. Never issue one seek/read for every animation frame. */
    for(i=0;i<pack.frame_count;){
        BSFrame disk[64];unsigned batch=pack.frame_count-i,entry;
        if(batch>64u)batch=64u;
        if(!read_block(pack.frame_index_offset+i*sizeof(BSFrame),disk,batch*sizeof(BSFrame))){error_code=5;return 0;}
        for(entry=0;entry<batch;++entry){
            const BSFrame *f=&disk[entry];ClipGroup *group;unsigned key;
            if(!frame_record_valid(f)){error_code=6;return 0;}
            key=group_key(f->model,f->clip,f->direction);
            if((int)key<previous_key){error_code=6;return 0;}
            if((int)key!=previous_key){
                if(group_count>=BS_MAX_GROUPS){error_code=6;return 0;}
                group=&groups[group_count++];group->key=(uint16_t)key;group->first=(uint16_t)(i+entry);
            }else group=&groups[group_count-1];
            if(f->frame!=group->count || (unsigned)group->ticks+f->duration_ticks>65535u){error_code=6;return 0;}
            if(pack.version>=7u && f->model<4 && !(f->flags&BS_HAS_SHADOW)){error_code=14;return 0;}
            if(pack.version>=8u && f->model>=48 && f->model<=58 && !(f->flags&BS_HAS_SHADOW)){error_code=14;return 0;}
            if(pack.version>=10u && f->model>=75 && f->model<=76 && !(f->flags&BS_HAS_SHADOW)){error_code=14;return 0;}
            if(pack.version>=11u && (f->model==77 || f->model==78) && !(f->flags&BS_HAS_SHADOW)){error_code=14;return 0;}
            if(pack.version>=12u && ((f->model>=93&&f->model<=96)||(f->model>=114&&f->model<=115)||(f->model>=128&&f->model<=157)) && !(f->flags&BS_HAS_SHADOW)){error_code=14;return 0;}
            ++group->count;group->ticks+=f->duration_ticks;previous_key=(int)key;
            frame_ticks[i+entry]=(unsigned char)f->duration_ticks;
        }
        i+=batch;
    }
    bytes=pack.ground_grid_size*pack.ground_grid_size*2;
    if(!valid_range(pack.ground_grid_offset,bytes) || !read_block(pack.ground_grid_offset,ground,bytes)){error_code=7;return 0;}
    if(pack.version>=2u){
        if(!valid_range(pack.flags,sizeof(water)) || !read_block(pack.flags,&water,sizeof(water)) ||
           water.magic!=BS_WATER_MAGIC || !water.frame_count || water.frame_count>BS_WATER_MAX_FRAMES ||
           !water.frame_ticks || water.frame_ticks>200u || water.tile_size!=BS_TILE_SIZE || water.frame_bytes!=TILE_PIXELS ||
           !valid_range(water.frames_offset,water.frame_count*TILE_PIXELS) ||
           !valid_range(water.palette_offset,sizeof(water_palette)) ||
           !valid_range(water.mask_index_offset,pack.tile_columns*pack.tile_rows*4u)){error_code=9;return 0;}
        water_heap_bytes=water.frame_count*TILE_PIXELS;water_frames=cache_memory_alloc(water_heap_bytes);
        if(!water_frames){water_heap_bytes=0;error_code=19;return 0;}
        if(!read_block(water.frames_offset,water_frames,water.frame_count*TILE_PIXELS) ||
           !read_block(water.palette_offset,water_palette,sizeof(water_palette)) ||
           !read_block(water.mask_index_offset,water_offsets,pack.tile_columns*pack.tile_rows*4u)){error_code=9;return 0;}
        for(i=0;i<pack.tile_columns*pack.tile_rows;++i)if(water_offsets[i] &&
           (!tile_offsets[i] || !valid_range(water_offsets[i],TILE_PIXELS))){error_code=9;return 0;}
    }
    for(i=0;i<group_count;++i){
        const ClipGroup *group=&groups[i];
        if((unsigned)group->first+group->count>pack.frame_count){error_code=6;return 0;}
    }
    if(pack.version>=3u){
        for(i=0;i<4;++i)for(j=0;j<DOTA_SKILLS;++j){
            for(k=0;k<BS_DIRECTIONS;++k)if(!exact_group(i,BS_SKILL_Q+j,k)){error_code=10;return 0;}
            if(!exact_group(16+i*4+j,0,0)){error_code=10;return 0;}
        }
        for(k=0;k<BS_DIRECTIONS;++k){
            if(!exact_group(DOTA_HERO_JUGGERNAUT,9,k) || !exact_group(DOTA_HERO_JUGGERNAUT,10,k) ||
               !exact_group(DOTA_HERO_CRYSTAL_MAIDEN,9,k) || !exact_group(DOTA_HERO_CRYSTAL_MAIDEN,10,k)){error_code=10;return 0;}
        }
    }
    if(pack.version>=4u){
        for(i=32;i<39;++i){
            for(k=0;k<BS_DIRECTIONS;++k)if(!exact_group(i,0,k)){error_code=11;return 0;}
            if(!exact_group(i,1,0) || !exact_group(i,2,0)){error_code=11;return 0;}
        }
        if(!exact_group(39,2,0)){error_code=11;return 0;}
        for(i=8;i<10;++i)for(k=0;k<BS_DIRECTIONS;++k)
            if(!exact_group(i,BS_ATTACK,k) || !exact_group(i,BS_DEATH,k)){error_code=11;return 0;}
        for(i=40;i<44;++i)for(j=0;j<5;++j)if(j!=BS_CAST){
            for(k=0;k<BS_DIRECTIONS;++k)if(!exact_group(i,j,k)){error_code=11;return 0;}
        }
    }
    if(pack.version>=5u)for(k=0;k<BS_DIRECTIONS;++k){
        if(!exact_group(44,BS_IDLE,k) || !exact_group(44,BS_RUN,k) ||
           !exact_group(44,BS_CAST,k) || !exact_group(44,BS_DEATH,k)){error_code=12;return 0;}
    }
    if(pack.version>=7u && pack.version<12u){
        for(i=0;i<2;++i){const ClipGroup *g=exact_group(6+i,BS_SKILL_Q,0);if(!g || g->count!=16 || g->ticks!=32){error_code=14;return 0;}}
    }
    if(pack.version>=8u)for(i=48;i<=58;++i)for(j=0;j<5;++j)if(j!=BS_CAST)
        for(k=0;k<16;++k)if(!exact_group((int)i,(int)j,(int)k)){error_code=15;return 0;}
    if(pack.version>=9u){
        for(i=59;i<=72;++i)if(!exact_group((int)i,BS_IDLE,0)){error_code=16;return 0;}
        for(i=73;i<=74;++i)for(j=0;j<6;++j)if(!exact_group((int)i,(int)j,0)){error_code=16;return 0;}
    }
    if(pack.version>=10u)for(i=75;i<=76;++i)for(j=0;j<2;++j)for(k=0;k<BS_DIRECTIONS;++k)
        if(!exact_group((int)i,(int)j,(int)k)){error_code=17;return 0;}
    if(pack.version>=11u)for(i=77;i<=78;++i)for(j=0;j<9;++j)for(k=0;k<BS_DIRECTIONS;++k)
        if(!exact_group((int)i,(int)j,(int)k)){error_code=18;return 0;}
    if(pack.version>=12u){
        for(i=0;i<2;++i){const ClipGroup *g=exact_group(6+i,5,0);if(!g||g->count!=16||g->ticks!=16){error_code=14;return 0;}}
        for(j=6;j<=10;++j)for(k=0;k<((j==7||j==8)?16u:1u);++k){const ClipGroup *g=exact_group(6,j,k);unsigned n=j>=9?40u:16u;if(!g||g->count!=n||g->ticks!=n){error_code=14;return 0;}}
        for(i=93;i<=96;++i)for(j=0;j<9;++j)for(k=0;k<16;++k)if(!exact_group(i,j,k)){error_code=13;return 0;}
        for(i=128;i<=157;++i)for(j=0;j<5;++j)if(j!=BS_CAST)for(k=0;k<16;++k)if(!exact_group(i,j,k)){error_code=13;return 0;}
        for(i=97;i<=119;++i)for(j=0;j<3;++j)if(!exact_group(i,j,0)){error_code=13;return 0;}
        for(k=0;k<16;++k)if(!exact_group(120,0,k)){error_code=13;return 0;}
    }
    initialize_shadow_curve();ready=1;return 1;
}
int bs_initialize(void){
    release_cache();release_water();
    if(initialize_pack())return 1;
    release_cache();release_water();return 0;
}

unsigned bs_reserve_cache(unsigned bytes){
    unsigned i,slots,heap_bytes;unsigned char *allocation=0;RuntimeFrame *frames;
    if(!ready){release_cache();release_water();return 0;}
    if(!bytes){release_cache();return sprite_capacity;}
    /* Bound all arithmetic and keep every cache allocation word aligned. */
    if(bytes>64u*1024u*1024u)bytes=64u*1024u*1024u;
    bytes&=~3u;slots=bytes/8192u;
    if(slots<SPRITE_CACHE_SLOTS)slots=SPRITE_CACHE_SLOTS;
    if(slots>2048u)slots=2048u;
    heap_bytes=bytes+slots*sizeof(SpriteCache)+pack.frame_count*sizeof(uint16_t);
    if(bytes>SPRITE_CACHE_BYTES)allocation=cache_memory_alloc(heap_bytes);
    frames=cache_memory_alloc(pack.frame_count*sizeof(RuntimeFrame)+DOTA_WORLD*sizeof(uint16_t));
    if(frames){
        for(i=0;i<pack.frame_count;){
            BSFrame disk[64];unsigned batch=pack.frame_count-i,j;
            if(batch>64u)batch=64u;
            if(!read_block(pack.frame_index_offset+i*sizeof(BSFrame),disk,batch*sizeof(BSFrame))){
                cache_memory_free(frames);cache_memory_free(allocation);release_cache();release_water();ready=0;return 0;
            }
            for(j=0;j<batch;++j){const BSFrame *f=&disk[j];
                if(!frame_record_valid(f) || !frame_identity_valid(i+j,f) || f->duration_ticks!=frame_ticks[i+j]){
                    error_code=6;++stats.read_failures;
                    cache_memory_free(frames);cache_memory_free(allocation);release_cache();release_water();ready=0;return 0;
                }
                decode_frame(&frames[i+j],f);
            }
            i+=batch;
        }
    }
    release_cache();resident_frames=frames;
    if(frames){
        ground_coordinates=(uint16_t *)(void *)(frames+pack.frame_count);
        for(i=0;i<DOTA_WORLD;++i)ground_coordinates[i]=(uint16_t)(i*(pack.ground_grid_size-1u)*256u/(DOTA_WORLD-1));
    }
    if(allocation){
        sprite_allocation=allocation;sprite_heap_bytes=heap_bytes;sprite_data=allocation;
        sprite_capacity=bytes;sprite_slots=slots;
        sprite_entries=(SpriteCache *)(allocation+bytes);
        sprite_lookup=(uint16_t *)(allocation+bytes+slots*sizeof(SpriteCache));
        for(i=0;i<slots;++i)sprite_entries[i].frame=-1;
        for(i=0;i<pack.frame_count;++i)sprite_lookup[i]=0xffffu;
    }
    if(bytes>SPRITE_CACHE_BYTES)bs_reserve_fog_cache(FOG_BACKGROUND_BYTES);
    return sprite_capacity;
}
unsigned bs_sprite_cache_bytes(void){return sprite_capacity;}
unsigned bs_resident_frame_count(void){return resident_frames?pack.frame_count:0;}
unsigned bs_dynamic_storage_bytes(void){return sprite_heap_bytes+(resident_frames?pack.frame_count*sizeof(RuntimeFrame)+DOTA_WORLD*sizeof(uint16_t):0)+water_heap_bytes+map_sidecar_stats.heap_bytes+fog_cache_stats.heap_bytes;}
int bs_open_default(void){if(!baked_io_open_default()){ready=0;error_code=1;release_cache();release_water();return 0;}if(bs_initialize())return 1;baked_io_close();return 0;}
void bs_close(void){ready=0;last_tick=-1;fog_game=0;fog_camera=0;fog_sprite_gate=0;release_cache();release_water();baked_io_close();}
int bs_ready(void){return ready;}
int bs_last_error(void){return error_code;}
const BSPackHeader *bs_pack_header(void){return ready?&pack:0;}
const BSStats *bs_stats(void){return &stats;}
const BSSkillStats *bs_skill_stats(void){return &skill_stats;}
const BSProjectileStats *bs_projectile_stats(void){return &projectile_stats;}
const BSComponentStats *bs_component_stats(void){return &component_stats;}
const BSPassiveStats *bs_passive_stats(void){return &passive_stats;}
const BSBeamGeometry *bs_beam_geometry(void){return &beam_geometry;}
const BSUnitAnimation *bs_unit_animation(int unit){return ready && unit>=0 && unit<DOTA_MAX_UNITS && animations[unit].model>=0?&animations[unit]:0;}
const BSUnitAnimation *bs_courier_animation(int owner){return ready && (unsigned)owner<DOTA_HERO_COUNT && courier_animations[owner].model>=0?&courier_animations[owner]:0;}
const BSCourierStats *bs_courier_stats(void){return &courier_stats;}
const BSWaterStats *bs_water_stats(void){return &water_stats;}
const BSWaterHeader *bs_water_header(void){return ready && water.frame_count?&water:0;}
const BSLightingStats *bs_lighting_stats(void){return &lighting_stats;}
unsigned bs_static_storage_bytes(void){
    return barracks_visual_static_bytes()+sizeof(shadow_curve)+sizeof(shadow_curve_ready)+sizeof(pack)+sizeof(frame_ticks)+sizeof(frame_metadata)+sizeof(resident_frames)+sizeof(ground_coordinates)+sizeof(metadata_cache)+sizeof(metadata_clock)+sizeof(courier_animations)+sizeof(courier_stats)+sizeof(groups)+sizeof(group_count)+sizeof(tile_offsets)+sizeof(ground)+sizeof(tile_pixels)+
        sizeof(tile_cache)+sizeof(map_sidecar_memory)+sizeof(map_sidecar_stats)+sizeof(sprite_storage)+sizeof(sprite_cache)+sizeof(sprite_data)+sizeof(sprite_entries)+sizeof(sprite_lookup)+sizeof(sprite_allocation)+sizeof(sprite_capacity)+sizeof(sprite_slots)+sizeof(sprite_heap_bytes)+sizeof(depths)+sizeof(bounds)+sizeof(history)+sizeof(effect_history)+sizeof(stats)+
        sizeof(water)+sizeof(water_offsets)+sizeof(water_frames)+sizeof(water_heap_bytes)+sizeof(water_palette)+sizeof(water_masks)+sizeof(water_cache)+sizeof(water_stats)+sizeof(lighting_stats)+
        sizeof(skill_tracks)+sizeof(animations)+sizeof(skill_stats)+sizeof(observed_skill_serial)+
        sizeof(projectile_stats)+sizeof(observed_attack_serial)+sizeof(component_stats)+sizeof(beam_geometry)+sizeof(passive_stats)+sizeof(jungle_stats)+sizeof(shop_bounds)+sizeof(omnislash_stats)+sizeof(fog_visible)+sizeof(fog_stats)+sizeof(fog_game)+sizeof(fog_camera)+sizeof(fog_sprite_gate)+sizeof(fog_background)+sizeof(fog_background_key)+sizeof(fog_saved_key)+sizeof(fog_saved_visible)+sizeof(fog_cache_stats)+sizeof(fog_cached_dimmed)+sizeof(fog_cached_soft);
}

int bs_project(const BSCamera *camera,int x,int y,int z,int *sx,int *sy,uint16_t *depth){
    int d;
    if(!camera || !sx || !sy)return 0;
    *sx=BS_WIDTH/2+global_x(x)-global_x(camera->x);
    *sy=BS_TOP+BS_HEIGHT/2+global_y(y,z)-global_y(camera->y,camera->z);
    d=32768-y-z;if(depth)*depth=(uint16_t)clamp(d,0,65534);
    return *sx>=0 && *sx<BS_WIDTH && *sy>=BS_TOP && *sy<BS_TOP+BS_HEIGHT && d>=0 && d<65535;
}
int bs_unproject_at_z(const BSCamera *camera,int sx,int sy,int z,int *x,int *y){
    int px,py;
    if(!camera || !x || !y || sx<0 || sx>=BS_WIDTH || sy<BS_TOP || sy>=BS_TOP+BS_HEIGHT)return 0;
    px=global_x(camera->x)+sx-BS_WIDTH/2;py=global_y(camera->y,camera->z)+sy-BS_TOP-BS_HEIGHT/2;
    *x=floor_div((2*px+1)*256,2*BS_ZOOM);
    *y=z+floor_div((2*py+1)*65536,2*BS_PITCH*BS_ZOOM);
    return 1;
}
int bs_ground_height(int x,int y){
    unsigned size,gx,gy,ix,iy,fx,fy;int h0,h1,a,b,c,d;
    if(!ready)return 0;
    size=pack.ground_grid_size;x=clamp(x,0,DOTA_WORLD-1);y=clamp(y,0,DOTA_WORLD-1);
    if(ground_coordinates){gx=ground_coordinates[x];gy=ground_coordinates[y];}
    else{
        gx=(unsigned)x*(size-1)*256u/(DOTA_WORLD-1);
        gy=(unsigned)y*(size-1)*256u/(DOTA_WORLD-1);
    }
    ix=gx>>8;iy=gy>>8;fx=gx&255;fy=gy&255;
    if(x==DOTA_WORLD-1){ix=size-2;fx=256;}if(y==DOTA_WORLD-1){iy=size-2;fy=256;}
    a=ground[iy*size+ix];b=ground[iy*size+ix+1];
    c=ground[(iy+1)*size+ix];d=ground[(iy+1)*size+ix+1];
    /* Original terrain plateaus need no interpolation. This also keeps signed
     * truncation exactly equivalent for non-flat and negative-height cells. */
    if(a==b && a==c && a==d)return a;
    h0=(a*(int)(256-fx)+b*(int)fx)/256;
    h1=(c*(int)(256-fx)+d*(int)fx)/256;
    return (h0*(int)(256-fy)+h1*(int)fy)/256;
}
int bs_pick_ground(const BSCamera *camera,int sx,int sy,int *x,int *y){
    int i,z=0,xx,yy;if(!x || !y)return 0;
    for(i=0;i<8;++i){if(!bs_unproject_at_z(camera,sx,sy,z,&xx,&yy))return 0;z=bs_ground_height(xx,yy);}
    if(!bs_unproject_at_z(camera,sx,sy,z,&xx,&yy))return 0;
    *x=clamp(xx,0,DOTA_WORLD-1);*y=clamp(yy,0,DOTA_WORLD-1);return 1;
}
const uint16_t *bs_depth_buffer(void){return depths;}
/* Invert the same quantized oblique projection as the baked depth. A terrain
 * sample already carries y+z, so no ray march, bilerp or unit loop is needed. */
static int fog_world_point(const BSCamera *camera,int sx,int sy,int depth,int *x,int *y){
    int py,flat_y;
    if(depth<0||depth>=65535)return 0;
    *x=floor_div((2*(global_x(camera->x)+sx-BS_WIDTH/2)+1)*256,2*BS_ZOOM);
    py=global_y(camera->y,camera->z)+sy-BS_TOP-BS_HEIGHT/2;
    flat_y=floor_div((2*py+1)*65536,2*BS_PITCH*BS_ZOOM);
    *y=floor_div(flat_y+32768-depth,2);
    return (unsigned)*x<DOTA_WORLD&&(unsigned)*y<DOTA_WORLD;
}
static int fog_point_visible(int x,int y){
    return !fog_game||dota_sight_position_visible(fog_game,fog_game->player,x,y);
}
static void fog_light_row(int y,unsigned short *out){
    int x,dx,dy;
    for(x=0;x<=FOG_COLUMNS;++x){unsigned count=0,total=0;
        for(dy=-1;dy<=0;++dy)for(dx=-1;dx<=0;++dx){int xx=x+dx,yy=y+dy;
            if((unsigned)xx<FOG_COLUMNS&&(unsigned)yy<FOG_ROWS){count+=fog_visible[yy*FOG_COLUMNS+xx];++total;}
        }
        out[x]=(unsigned short)(3u*256u+count*5u*256u/total);
    }
}
/* Cache only the shader output. Map/depth, animated shops, lotus, selection,
 * actors, shadows and effects still run normally. All world queries run each
 * frame. A stable four-frame key avoids captures during camera/vision motion. */
typedef uint32_t FogCopyWord __attribute__((__may_alias__));
static void fog_background_copy(uint16_t *dst,const uint16_t *src){
    unsigned n=BS_WIDTH*BS_HEIGHT;
    if(!(((uintptr_t)dst|(uintptr_t)src)&3u)){
        FogCopyWord *d=(FogCopyWord *)dst;const FogCopyWord *s=(const FogCopyWord *)src;
        while(n>=16u){d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];d[4]=s[4];d[5]=s[5];d[6]=s[6];d[7]=s[7];d+=8;s+=8;n-=16;}
    }else while(n--)*dst++=*src++;
}
static int fog_background_hit(uint16_t *pixels){
    unsigned i;int same=fog_cache_stats.streak!=0;
    if(!fog_background)return 0;
    /* A failed resource read must never replace the actual fallback image
     * with a prior successful background. The next good frame starts fresh. */
    if(stats.read_failures||stats.missing_frames){fog_cache_stats.streak=fog_cache_stats.ready=0;return 0;}
    for(i=0;same&&i<FOG_BACKGROUND_KEYS;++i)if(fog_saved_key[i]!=fog_background_key[i])same=0;
    for(i=0;same&&i<sizeof(fog_saved_visible);++i)if(fog_saved_visible[i]!=fog_visible[i])same=0;
    if(!same){
        fog_cache_stats.streak=1;fog_cache_stats.ready=0;
        for(i=0;i<FOG_BACKGROUND_KEYS;++i)fog_saved_key[i]=fog_background_key[i];
        for(i=0;i<sizeof(fog_saved_visible);++i)fog_saved_visible[i]=fog_visible[i];
    }else if(fog_cache_stats.streak<4u)++fog_cache_stats.streak;
    if(!fog_cache_stats.ready){++fog_cache_stats.misses;return 0;}
    fog_background_copy(pixels+BS_TOP*BS_WIDTH,fog_background);
    fog_stats.dimmed_pixels=fog_cached_dimmed;fog_stats.soft_cells=fog_cached_soft;
    ++fog_cache_stats.hits;return 1;
}
static void fog_background_capture(const uint16_t *pixels){
    if(!fog_background||fog_cache_stats.ready||fog_cache_stats.streak<4u||stats.read_failures||stats.missing_frames)return;
    fog_background_copy(fog_background,pixels+BS_TOP*BS_WIDTH);
    fog_cached_dimmed=fog_stats.dimmed_pixels;fog_cached_soft=fog_stats.soft_cells;
    fog_cache_stats.ready=1;++fog_cache_stats.captures;
}
static void draw_fog(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    int tx,ty,team=game->units[game->player].team;unsigned short top[FOG_COLUMNS+1],bottom[FOG_COLUMNS+1];
    for(ty=0;ty<FOG_ROWS;++ty)for(tx=0;tx<FOG_COLUMNS;++tx){
        int sx=tx*FOG_STEP+FOG_STEP/2,sy=BS_TOP+ty*FOG_STEP+FOG_STEP/2,x,y;
        int valid=fog_world_point(camera,sx,sy,depths[(sy-BS_TOP)*BS_WIDTH+sx],&x,&y);
        fog_visible[ty*FOG_COLUMNS+tx]=(unsigned char)(valid&&dota_sight_fog_state(game,team,x,y)==2);
        ++fog_stats.cells;if(valid)++fog_stats.queries;
    }
    fog_background_key[FOG_BACKGROUND_KEYS-1]=water_stats.water_pixels?(int)water_stats.frame:-1;
    if(fog_background_hit(pixels))return;
    /* Bilinear vertex light makes the eight-pixel sampling grid disappear.
     * Only two small light rows live on the stack; all per-pixel math is
     * add/shift RGB565 shading, with no terrain or unit query in that loop. */
    fog_light_row(0,top);
    for(ty=0;ty<FOG_ROWS;++ty)for(tx=0;tx<FOG_COLUMNS;++tx){
        int x,y,ta,tb,ba,bb;
        if(tx==0)fog_light_row(ty+1,bottom);
        ta=top[tx];tb=top[tx+1];ba=bottom[tx];bb=bottom[tx+1];
        if(ta==2048&&tb==2048&&ba==2048&&bb==2048){if(tx==FOG_COLUMNS-1)for(x=0;x<=FOG_COLUMNS;++x)top[x]=bottom[x];continue;}
        if(ta==768&&tb==768&&ba==768&&bb==768){
            /* Most off-camera terrain is uniformly dim. Its old bilinear
             * result is exactly light3: no gradient or per-pixel dispatch. */
            for(y=0;y<FOG_STEP;++y){uint16_t *row=pixels+(BS_TOP+ty*FOG_STEP+y)*BS_WIDTH+tx*FOG_STEP;
                row[0]=vision_assets_fog_color(row[0],3);row[1]=vision_assets_fog_color(row[1],3);
                row[2]=vision_assets_fog_color(row[2],3);row[3]=vision_assets_fog_color(row[3],3);
                row[4]=vision_assets_fog_color(row[4],3);row[5]=vision_assets_fog_color(row[5],3);
                row[6]=vision_assets_fog_color(row[6],3);row[7]=vision_assets_fog_color(row[7],3);
            }
            fog_stats.dimmed_pixels+=FOG_STEP*FOG_STEP;
            if(tx==FOG_COLUMNS-1)for(x=0;x<=FOG_COLUMNS;++x)top[x]=bottom[x];
            continue;
        }
        if(ta!=768||tb!=768||ba!=768||bb!=768)++fog_stats.soft_cells;
        for(y=0;y<FOG_STEP;++y){uint16_t *row=pixels+(BS_TOP+ty*FOG_STEP+y)*BS_WIDTH+tx*FOG_STEP;
            int left=ta+(ba-ta)*y/FOG_STEP,right=tb+(bb-tb)*y/FOG_STEP,light=left*FOG_STEP,step=right-left;
            for(x=0;x<FOG_STEP;++x,light+=step)row[x]=vision_assets_fog_color(row[x],(unsigned)(light+1024)>>11);
        }
        fog_stats.dimmed_pixels+=FOG_STEP*FOG_STEP;
        if(tx==FOG_COLUMNS-1)for(x=0;x<=FOG_COLUMNS;++x)top[x]=bottom[x];
    }
    fog_background_capture(pixels);
}
int bs_unit_screen_bounds(int unit,int out[4],uint16_t *nearest){
    int i;if(unit<0 || unit>=DOTA_MAX_UNITS || !out || !bounds[unit].valid)return 0;
    for(i=0;i<4;++i)out[i]=bounds[unit].bounds[i];
    if(nearest)*nearest=bounds[unit].depth;
    return 1;
}
int bs_shop_screen_bounds(int shop,int out[4],uint16_t *nearest){
    int i;if((unsigned)shop>=2 || !out || !shop_bounds[shop].valid)return 0;
    for(i=0;i<4;++i)out[i]=shop_bounds[shop].bounds[i];
    if(nearest)*nearest=shop_bounds[shop].depth;
    return 1;
}
const BSJungleStats *bs_jungle_stats(void){return &jungle_stats;}
const BSOmnislashStats *bs_omnislash_stats(void){return &omnislash_stats;}

static const uint16_t *get_tile(unsigned tile){
    unsigned i,slot=0;uint32_t oldest=0xffffffffu;
    if(tile>=pack.tile_columns*pack.tile_rows || !tile_offsets[tile])return 0;
    for(i=0;i<TILE_CACHE_SLOTS;++i){
        if(tile_cache[i].tile==(int)tile){tile_cache[i].stamp=++cache_clock;++stats.tile_hits;return tile_pixels[i];}
        if(tile_cache[i].tile<0){slot=i;oldest=0;}
        else if(oldest && tile_cache[i].stamp<oldest){oldest=tile_cache[i].stamp;slot=i;}
    }
    ++stats.tile_misses;tile_cache[slot].tile=-1;
    if(!map_sidecar_tile(tile,(unsigned char*)tile_pixels[slot]) && !read_block(tile_offsets[tile],tile_pixels[slot],TILE_PIXELS*4))return 0;
    tile_cache[slot].tile=(int)tile;tile_cache[slot].stamp=++cache_clock;return tile_pixels[slot];
}
static const uint8_t *get_water_mask(unsigned tile){
    unsigned i,slot=0;uint32_t oldest=0xffffffffu;
    if(!water.frame_count || tile>=pack.tile_columns*pack.tile_rows || !water_offsets[tile])return 0;
    for(i=0;i<TILE_CACHE_SLOTS;++i){
        if(water_cache[i].tile==(int)tile){water_cache[i].stamp=++cache_clock;++water_stats.mask_hits;return water_masks[i];}
        if(water_cache[i].tile<0){slot=i;oldest=0;}
        else if(oldest && water_cache[i].stamp<oldest){oldest=water_cache[i].stamp;slot=i;}
    }
    ++water_stats.mask_misses;water_cache[slot].tile=-1;
    if(!read_block(water_offsets[tile],water_masks[slot],TILE_PIXELS))return 0;
    water_cache[slot].tile=(int)tile;water_cache[slot].stamp=++cache_clock;return water_masks[slot];
}
typedef uint32_t MapCopyWord __attribute__((__may_alias__));
static inline void copy_map_row(uint16_t *dst,uint16_t *zbuf,const uint16_t *color,const uint16_t *depth,unsigned n){
    if(!n)return;
    if(!((((uintptr_t)color^(uintptr_t)dst)|((uintptr_t)color^(uintptr_t)depth)|((uintptr_t)color^(uintptr_t)zbuf))&3u)){
        const MapCopyWord *c,*d;MapCopyWord *p,*z;
        if(((uintptr_t)color&3u) && n){*dst++=*color++;*zbuf++=*depth++;--n;}
        c=(const MapCopyWord *)color;d=(const MapCopyWord *)depth;p=(MapCopyWord *)dst;z=(MapCopyWord *)zbuf;
        while(n>=8u){
            p[0]=c[0];z[0]=d[0];p[1]=c[1];z[1]=d[1];
            p[2]=c[2];z[2]=d[2];p[3]=c[3];z[3]=d[3];
            c+=4;d+=4;p+=4;z+=4;n-=8u;
        }
        while(n>=2u){*p++=*c++;*z++=*d++;n-=2u;}
        if(n){*(uint16_t *)p=*(const uint16_t *)c;*(uint16_t *)z=*(const uint16_t *)d;}
    }else{
        while(n>=8u){
            dst[0]=color[0];zbuf[0]=depth[0];dst[1]=color[1];zbuf[1]=depth[1];
            dst[2]=color[2];zbuf[2]=depth[2];dst[3]=color[3];zbuf[3]=depth[3];
            dst[4]=color[4];zbuf[4]=depth[4];dst[5]=color[5];zbuf[5]=depth[5];
            dst[6]=color[6];zbuf[6]=depth[6];dst[7]=color[7];zbuf[7]=depth[7];
            dst+=8;zbuf+=8;color+=8;depth+=8;n-=8u;
        }
        while(n--){*dst++=*color++;*zbuf++=*depth++;}
    }
}
static void draw_map(uint16_t *pixels,const BSCamera *camera,unsigned tick){
    int ox=global_x(camera->x)-BS_WIDTH/2-pack.origin_pixel_x;
    int oy=global_y(camera->y,camera->z)-BS_HEIGHT/2-pack.origin_pixel_y;
    int tx0=floor_div(ox,BS_TILE_SIZE),ty0=floor_div(oy,BS_TILE_SIZE),tx,ty;
    int tx1=floor_div(ox+BS_WIDTH-1,BS_TILE_SIZE),ty1=floor_div(oy+BS_HEIGHT-1,BS_TILE_SIZE);
    const uint8_t *wave=0;
    if(water.frame_count){
        water_stats.enabled=1;water_stats.frame=(tick/water.frame_ticks)%water.frame_count;
        wave=water_frames[water_stats.frame];
    }
    for(ty=ty0;ty<=ty1;++ty)for(tx=tx0;tx<=tx1;++tx){
        const uint16_t *data;const uint8_t *mask;int x,y,left=tx*BS_TILE_SIZE-ox,top=ty*BS_TILE_SIZE-oy;
        int x0=clamp(left,0,BS_WIDTH),x1=clamp(left+BS_TILE_SIZE,0,BS_WIDTH);
        int y0=clamp(top,0,BS_HEIGHT),y1=clamp(top+BS_TILE_SIZE,0,BS_HEIGHT);
        data=(tx<0 || ty<0 || tx>=(int)pack.tile_columns || ty>=(int)pack.tile_rows)?0:get_tile((unsigned)ty*pack.tile_columns+(unsigned)tx);
        if(!data){
            for(y=y0;y<y1;++y){
                uint16_t *dst=pixels+(y+BS_TOP)*BS_WIDTH+x0,*zbuf=depths+y*BS_WIDTH+x0;
                for(x=x0;x<x1;++x){*dst++=CLEAR_COLOR;*zbuf++=65535;}
            }
            continue;
        }
        mask=wave?get_water_mask((unsigned)ty*pack.tile_columns+(unsigned)tx):0;
        for(y=y0;y<y1;++y){
            const uint16_t *color=data+(y-top)*BS_TILE_SIZE+(x0-left);
            const uint16_t *depth=color+TILE_PIXELS;uint16_t *dst=pixels+(y+BS_TOP)*BS_WIDTH+x0;
            uint16_t *zbuf=depths+y*BS_WIDTH+x0;
            if(mask){
                const uint8_t *coverage=mask+(y-top)*BS_TILE_SIZE+(x0-left);
                unsigned wave_row=((unsigned)(oy+y+pack.origin_pixel_y)&127u)*BS_TILE_SIZE;
                for(x=x0;x<x1;++x){
                    unsigned m=*coverage++,a=(m&3u)*85u;uint16_t c=*color++;
                    if(a){
                        unsigned index=wave[wave_row+((unsigned)(ox+x+pack.origin_pixel_x)&127u)],shore=m>>2;
                        uint16_t w=water_palette[index];
                        if(shore){w=mix(water_palette[256+index],w,shore*255u/63u,0);++water_stats.shore_pixels;}
                        c=a==255u?w:mix(w,c,a,0);++water_stats.water_pixels;
                    }
                    *dst++=c;*zbuf++=*depth++;
                }
            }else copy_map_row(dst,zbuf,color,depth,(unsigned)(x1-x0));
            stats.map_pixels+=(uint32_t)(x1-x0);
        }
    }
}
#include "compressed_cache.h"
static const unsigned char *get_sprite_fallback(unsigned index){
    unsigned i,slot=sprite_slots;uint32_t size,oldest=0xffffffffu,start,end;
    unsigned char *data=sprite_data;
    const RuntimeFrame *frame;
    if(index>=pack.frame_count)return 0;
    frame=frame_info(index);if(!frame){++stats.missing_frames;return 0;}
    if(sprite_lookup){
        i=sprite_lookup[index];
        if(i<sprite_slots && sprite_entries[i].frame==(int)index){
            sprite_entries[i].stamp=++cache_clock;++stats.sprite_hits;return data+sprite_entries[i].start;
        }
    }else for(i=0;i<sprite_slots;++i)if(sprite_entries[i].frame==(int)index){
        sprite_entries[i].stamp=++cache_clock;++stats.sprite_hits;return data+sprite_entries[i].start;
    }
    ++stats.sprite_misses;size=(rf_size(frame)+3)&~3u;
    if(size>sprite_capacity){++stats.missing_frames;return 0;}
    start=sprite_write;if(start>sprite_capacity-size)start=0;end=start+size;
    for(i=0;i<sprite_slots;++i){
        SpriteCache *entry=&sprite_entries[i];
        if(entry->frame>=0 && start<entry->start+cache_payload_allocation(entry) && entry->start<end)entry->frame=-1;
        if(entry->frame<0){slot=i;oldest=0;}
        else if(oldest && entry->stamp<oldest){oldest=entry->stamp;slot=i;}
    }
    if(slot==sprite_slots)return 0;
    if(sprite_lookup && sprite_entries[slot].frame>=0)sprite_lookup[sprite_entries[slot].frame]=0xffffu;
    sprite_entries[slot].frame=-1;
    if(!read_block(frame->offset,data+start,rf_size(frame)))return 0;
    if(rf_flags(frame)&BS_HAS_SHADOW){
        const unsigned char *header=data+start+(unsigned)rf_width(frame)*rf_height(frame)*5u;
        unsigned w=read_u16(header),h=read_u16(header+2);int ax=read_i16(header+4),ay=read_i16(header+6);
        if(!w || !h || w>BS_MAX_FRAME_SIDE || h>BS_MAX_FRAME_SIDE ||
           ax<-1024 || ax>1024 || ay<-1024 || ay>1024 ||
           rf_size(frame)!=(uint32_t)rf_width(frame)*rf_height(frame)*5u+8u+w*h){
            ++stats.missing_frames;error_code=14;return 0;
        }
    }
    sprite_entries[slot].frame=(int)index;sprite_entries[slot].start=start;sprite_entries[slot].size=size;
    sprite_entries[slot].stamp=++cache_clock;
    if(sprite_lookup)sprite_lookup[index]=(uint16_t)slot;
    sprite_write=end;return data+start;
}
static const unsigned char *get_sprite(unsigned index){
    if(sprite_allocation){
        const RuntimeFrame *frame=frame_info(index);
        if(frame && (rf_flags(frame)&BS_ADDITIVE))return compressed_get_sprite(index);
    }
    return get_sprite_fallback(index);
}
unsigned bs_prewarm_fountain(unsigned team,int (*poll)(void)){
    unsigned model=6u+team,g,i,count=0,slot=0;
    uint32_t begin=0,end=0,span,rounded,packed=0;
    if(team>1u || !ready || pack.version<12u || !resident_frames ||
       !sprite_lookup || !sprite_allocation || sprite_write)return 0;
    /* Bulk loading may only own empty sprite storage. Existing body/menu
     * payloads are never silently invalidated to make a warmup look complete. */
    for(i=0;i<sprite_slots;++i)if(sprite_entries[i].frame>=0)return 0;
    for(i=0;i<SPRITE_CACHE_SLOTS;++i)if(sprite_cache[i].frame>=0)return 0;
    for(g=0;g<group_count;++g){const ClipGroup *group=&groups[g];
        if(group->key/(BS_CLIPS*BS_DIRECTIONS)!=model ||
           (group->key/BS_DIRECTIONS)%BS_CLIPS<BS_SKILL_Q)continue;
        for(i=group->first;i<(unsigned)group->first+group->count;++i){
            const RuntimeFrame *f=&resident_frames[i];unsigned n=rf_size(f);
            if((f->offset&3u) || !valid_range(f->offset,n) ||
               (count && (f->offset<end || f->offset-end>3u)))return 0;
            if(!count)begin=f->offset;
            end=f->offset+n;++count;
        }
    }
    if(!count || count>sprite_slots)return 0;
    span=end-begin;rounded=(span+3u)&~3u;
    if(rounded>sprite_capacity)return 0;
    /* Keep one sequential seek, but let the native caller pump BACK between
     * bounded reads. No partial work is published as a cache hit. */
    for(i=0;i<span;){unsigned n=span-i;if(n>65536u)n=65536u;
        if(poll&&poll())return 0;
        if(!read_block(begin+i,sprite_data+i,n))return 0;
        i+=n;
    }
    if(poll&&poll())return 0;
    for(g=0;g<group_count;++g){const ClipGroup *group=&groups[g];
        if(group->key/(BS_CLIPS*BS_DIRECTIONS)!=model ||
           (group->key/BS_DIRECTIONS)%BS_CLIPS<BS_SKILL_Q)continue;
        for(i=group->first;i<(unsigned)group->first+group->count;++i){
            const RuntimeFrame *f=&resident_frames[i];
            if(rf_flags(f)&BS_HAS_SHADOW){
                const unsigned char *header=sprite_data+f->offset-begin+(unsigned)rf_width(f)*rf_height(f)*5u;
                unsigned w=read_u16(header),h=read_u16(header+2);int ax=read_i16(header+4),ay=read_i16(header+6);
                if(!w || !h || w>BS_MAX_FRAME_SIDE || h>BS_MAX_FRAME_SIDE ||
                   ax<-1024 || ax>1024 || ay<-1024 || ay>1024 ||
                   rf_size(f)!=(uint32_t)rf_width(f)*rf_height(f)*5u+8u+w*h){
                    ++stats.missing_frames;error_code=14;return 0;
                }
            }
        }
    }
    for(g=0;g<group_count;++g){const ClipGroup *group=&groups[g];
        if(group->key/(BS_CLIPS*BS_DIRECTIONS)!=model ||
           (group->key/BS_DIRECTIONS)%BS_CLIPS<BS_SKILL_Q)continue;
        for(i=group->first;i<(unsigned)group->first+group->count;++i){
            const RuntimeFrame *f=&resident_frames[i];SpriteCache *entry=&sprite_entries[slot++];
            if(poll&&poll())return 0;
            if(!compressed_warm_stage(f,f->offset-begin,&packed,entry))return 0;
        }
    }
    if(poll&&poll())return 0;
    slot=0;
    for(g=0;g<group_count;++g){const ClipGroup *group=&groups[g];
        if(group->key/(BS_CLIPS*BS_DIRECTIONS)!=model ||
           (group->key/BS_DIRECTIONS)%BS_CLIPS<BS_SKILL_Q)continue;
        for(i=group->first;i<(unsigned)group->first+group->count;++i){
            SpriteCache *entry=&sprite_entries[slot];entry->frame=(int)i;
            entry->stamp=++cache_clock;sprite_lookup[i]=(uint16_t)slot++;
        }
    }
    for(i=0;i<SPRITE_CACHE_SLOTS;++i)sprite_cache[i].frame=-1;
    sprite_write=packed;return count;
}
static const ClipGroup *find_group(int model,int clip,int direction,int fallback){
    const ClipGroup *group;
    if(model<0 || model>=BS_MAX_MODELS || clip<0 || clip>=BS_CLIPS)return 0;
    direction&=15;group=exact_group(model,clip,direction);if(group)return group;
    group=exact_group(model,clip,0);if(group)return group;
    if(fallback && clip!=BS_IDLE)return find_group(model,BS_IDLE,direction,0);
    return 0;
}
static int frame_at(const ClipGroup *group,unsigned tick,int loop){
    unsigned i;if(!group || !group->count || !group->ticks)return -1;
    if(loop)tick%=group->ticks;else if(tick>=group->ticks)tick=group->ticks-1;
    for(i=0;i<group->count;++i){unsigned duration=frame_ticks[group->first+i];if(tick<duration)return group->first+(int)i;tick-=duration;}
    return group->first+group->count-1;
}
static int hero_model(int hero){static const unsigned char models[10]={0,1,2,3,77,78,93,94,95,96};return (unsigned)hero<10u?models[hero]:-1;}
static int unit_model(const DotaUnit *unit){
    if(!unit)return -1;
    if(unit->type==DOTA_ANCIENT || unit->type==DOTA_ROSHAN){int species=dota_boss_species(unit);return species<0?-1:48+species;}
    if(unit->type==DOTA_HERO && (unsigned)unit->hero_id<DOTA_HERO_TYPES)return hero_model(unit->hero_id);
    if(unit->type==DOTA_TOWER)return 4+(unit->team&1);
    if(unit->type==DOTA_BUILDING)return pack.version>=9u && (unsigned)unit->hero_id<14u?59+unit->hero_id:-1;
    if(unit->type==DOTA_BASE)return 6+(unit->team&1);
    if(unit->type==DOTA_CREEP)return (dota_attack_style(unit)!=DOTA_ATTACK_MELEE && pack.version>=4u?40:8)+(unit->team&1);
    if(unit->type==DOTA_NEUTRAL){const DotaJungleDefinition *d=dota_jungle_definition(unit);return pack.version>=12u && d?d->model:(dota_attack_style(unit)==DOTA_ATTACK_NEUTRAL?42:43);}
    if(unit->type==DOTA_SUMMON){
        if(unit->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && unit->tier==-1)return hero_model(DOTA_HERO_CRYSTAL_MAIDEN);
        return pack.version>=5u?44:-1;
    }
    if(unit->type==DOTA_TORMENTOR)return pack.version>=6u?47:-1;
    return -1;
}
int bs_unit_height(const DotaUnit *unit){
    if(unit && dota_barracks_kind(unit)){
        int bit=dota_barracks_ordinal(unit->team,unit->lane,dota_barracks_kind(unit));
        if((unsigned)bit<12u)return unit->y-bs_ground_height(unit->x,unit->y)-dota_barracks_bar_floor[bit];
    }
    const ClipGroup *group=find_group(unit_model(unit),BS_IDLE,0,0);return group?rf_world_height(frame_info(group->first)):80;
}
static int game_unit_model(const DotaGame *game,int index){
    const DotaUnit *u=&game->units[index];
    if(index>=DOTA_ITEM_UNIT_FIRST && index<DOTA_MAX_UNITS){const DotaItemWorldUnit *s=&game->item_world.unit[index-DOTA_ITEM_UNIT_FIRST];
        if(s->kind==ITEM_UNIT_ILLUSION)return hero_model(u->hero_id);
        if(s->kind==ITEM_UNIT_OBSERVER)return 88;
        if(s->kind==ITEM_UNIT_SENTRY)return 89;
        if(s->kind==ITEM_UNIT_SUMMON)return u->tier?91:90;
        if(s->kind==ITEM_UNIT_DOMINATED){DotaUnit source=*u;source.type=s->source_type;source.team=s->source_team;return unit_model(&source);}
        if(s->kind==ITEM_UNIT_ROSTER_FISH)return 115;
        if(s->kind==ITEM_UNIT_ROSTER_ANCHOR)return 114;
    }return unit_model(u);
}
int bs_game_unit_height(const DotaGame *game,int index){
    const ClipGroup *group;if(!game || (unsigned)index>=DOTA_MAX_UNITS)return 80;
    if(dota_barracks_kind(&game->units[index]))return bs_unit_height(&game->units[index]);
    group=find_group(game_unit_model(game,index),BS_IDLE,0,0);return group?rf_world_height(frame_info(group->first)):80;
}
static int direction_yaw(int dx,int dy,int old){
    int i,best=old,dot,best_dot=-0x7fffffff;if(!dx && !dy)return old;
    for(i=0;i<16;++i){dot=-dx*sine[i]+dy*sine[(i+4)&15];if(dot>best_dot){best=i;best_dot=dot;}}return best;
}
static unsigned integer_sqrt(unsigned value){
    unsigned result=0,bit=1u<<30;
    while(bit>value)bit>>=2;
    while(bit){if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return result;
}
/* Returns visible, depth-tested pixels. Bounds describe actually drawn pixels,
 * so a fully hidden hero cannot be selected through the foreground scenery. */
static unsigned draw_sprite_opacity(uint16_t *pixels,int frame_index,int feet_x,int feet_y,int base_depth,int scale_q8,int unit,unsigned opacity){
    const RuntimeFrame *frame;const unsigned char *data,*alpha,*relative;
    int x,y,left,top,width,height,x0,y0,x1,y1,additive,write_depth,fw,fh;unsigned drawn=0,n;
    unsigned short source_x[BS_WIDTH];UnitBounds *selection=0;
    if(fog_sprite_gate&&fog_game&&fog_camera&&!(unit>=0&&unit<DOTA_MAX_UNITS&&fog_game->units[unit].team==fog_game->units[fog_game->player].team)){int wx,wy;
        if(!fog_world_point(fog_camera,feet_x,feet_y,base_depth,&wx,&wy)||!fog_point_visible(wx,wy)){++fog_stats.sprites_rejected;return 0;}
    }
    if(frame_index<0 || (unsigned)frame_index>=pack.frame_count){++stats.missing_frames;return 0;}
    frame=frame_info((unsigned)frame_index);if(!frame || scale_q8<1)return 0;
    fw=rf_width(frame);fh=rf_height(frame);
    width=(fw*scale_q8+255)/256;height=(fh*scale_q8+255)/256;
    left=feet_x-(int)frame->anchor_x*scale_q8/256;top=feet_y-(int)frame->anchor_y*scale_q8/256;
    x0=clamp(left,0,BS_WIDTH);x1=clamp(left+width,0,BS_WIDTH);
    y0=clamp(top,BS_TOP,BS_TOP+BS_HEIGHT);y1=clamp(top+height,BS_TOP,BS_TOP+BS_HEIGHT);
    if(x0>=x1 || y0>=y1){++stats.culled_sprites;return 0;}
    data=get_sprite((unsigned)frame_index);if(!data)return 0;
    n=(unsigned)fw*fh;alpha=data+n*2;relative=alpha+n;
    additive=(rf_flags(frame)&BS_ADDITIVE)!=0;write_depth=!(rf_flags(frame)&BS_NO_DEPTH_WRITE) && !additive && opacity>=128;
    if(unit>=0 && unit<DOTA_MAX_UNITS)selection=&bounds[unit];
    else if(unit==-2 || unit==-3)selection=&shop_bounds[-unit-2];
    if(scale_q8!=256)for(x=x0;x<x1;++x)source_x[x-x0]=(unsigned short)((x-left)*256/scale_q8);
    /* Fountain/ordinary additive FX use unit scale, full actor opacity and
     * no selectable bounds. Their depth test and every public statistic are
     * unchanged, while generic actor-only branches leave the inner loop. */
    if(scale_q8==256 && opacity==255u && additive && !selection){
        for(y=y0;y<y1;++y){
            int sy=y-top;unsigned row=(unsigned)sy*fw;
            uint16_t *dst=pixels+y*BS_WIDTH,*zrow=depths+(y-BS_TOP)*BS_WIDTH;
            if(sy>=fh)continue;
            for(x=x0;x<x1;++x){
                unsigned index=row+(unsigned)(x-left),a=alpha[index];int d;uint16_t color;
                if(!a)continue;
                d=base_depth+read_i16(relative+index*2);
                if(d<0 || d>=65535 || d>(int)zrow[x]+1){++stats.depth_rejects;continue;}
                color=read_sprite_rgb16(data+index*2);
                dst[x]=a==255u?add_full_rgb565(color,dst[x]):mix(color,dst[x],a,1);
                ++stats.blended_pixels;++drawn;++stats.sprite_pixels;
            }
        }
        return drawn;
    }
    /* Unit-size sprites have no resampling or depth rescale. The generic
     * path computes each source column once, outside the destination rows. */
#define SPRITE_PIXEL(DELTA) { \
    unsigned a=alpha[index];int d;uint16_t color; \
    if(opacity!=255u)a=a*opacity/255u; \
    if(!a)continue; \
    d=base_depth+(DELTA); \
    if(d<0 || d>=65535 || d>(int)zrow[x]+1){++stats.depth_rejects;continue;} \
    color=read_sprite_rgb16(data+index*2); \
    if(a==255 && !additive)dst[x]=color; \
    else{dst[x]=mix(color,dst[x],a,additive);++stats.blended_pixels;} \
    if(write_depth && a>=128)zrow[x]=(uint16_t)d; \
    ++drawn;++stats.sprite_pixels; \
    if(selection && a>=128){ \
        UnitBounds *b=selection; \
        if(!b->valid){b->bounds[0]=b->bounds[2]=x;b->bounds[1]=b->bounds[3]=y;b->depth=(uint16_t)d;b->valid=1;} \
        else{if(x<b->bounds[0])b->bounds[0]=x;if(x>b->bounds[2])b->bounds[2]=x;if(y<b->bounds[1])b->bounds[1]=y;if(y>b->bounds[3])b->bounds[3]=y;if(d<b->depth)b->depth=(uint16_t)d;} \
    } \
}
    for(y=y0;y<y1;++y){
        int sy=(y-top)*256/scale_q8;unsigned row=(unsigned)sy*fw;
        uint16_t *dst=pixels+y*BS_WIDTH,*zrow=depths+(y-BS_TOP)*BS_WIDTH;
        if(sy>=fh)continue;
        if(scale_q8==256){
            for(x=x0;x<x1;++x){unsigned index=row+(unsigned)(x-left);SPRITE_PIXEL(read_i16(relative+index*2));}
        }else{
            for(x=x0;x<x1;++x){unsigned sx=source_x[x-x0],index=row+sx;
                if(sx>=(unsigned)fw)continue;
                SPRITE_PIXEL(read_i16(relative+index*2)*scale_q8/256);
            }
        }
    }
#undef SPRITE_PIXEL
    return drawn;
}

static unsigned draw_sprite(uint16_t *pixels,int frame_index,int feet_x,int feet_y,int base_depth,int scale_q8,int unit){
    return draw_sprite_opacity(pixels,frame_index,feet_x,feet_y,base_depth,scale_q8,unit,255);
}
/* The silhouette and penumbra are evaluated from skinned triangles on the PC.
 * H1 only relocates those baked mask samples to the existing ground grid.
 * Never change depth or selection bounds: scenery and later bodies cover it. */
static inline uint16_t mix_shadow(uint16_t bg,unsigned alpha){
    const unsigned char *curve=shadow_curve[alpha];
    return (uint16_t)(((unsigned)curve[bg>>11]<<11)|((unsigned)curve[32u+((bg>>5)&63u)]<<5)|curve[bg&31u]);
}
static void draw_hero_shadow(uint16_t *pixels,int frame_index,const UnitHistory *h,const BSCamera *camera){
    const RuntimeFrame *f;const unsigned char *data,*mask;unsigned n,drawn=0;
    int x,y,sx,sy,left,top,w,hh,ax,ay,x0,x1,y0,y1,gx0,gy0,gx1,gy1,min_z,max_z;
    int world_x[BS_MAX_FRAME_SIDE];
    int camera_gx=global_x(camera->x),camera_gy=global_y(camera->y,camera->z),base_gy=global_y(h->y,h->z);
    unsigned size=pack.ground_grid_size;
    if(frame_index<0 || (unsigned)frame_index>=pack.frame_count)return;
    f=frame_info((unsigned)frame_index);if(!(rf_flags(f)&BS_HAS_SHADOW))return;
    bs_project(camera,h->x,h->y,h->z,&sx,&sy,0);
    if(sx<-BS_MAX_FRAME_SIDE || sx>=BS_WIDTH+BS_MAX_FRAME_SIDE || sy<-512 || sy>=BS_TOP+BS_HEIGHT+512)return;
    /* The audited mask/terrain envelope encloses every actual shadow in this
     * asset pack. Reject it before loading the body plus shadow payload;
     * the body rectangle alone cannot bound a shadow at a screen edge. */
    if(pack.version==BS_SHADOW_CULL_PACK_VERSION && pack.file_size==BS_SHADOW_CULL_PACK_BYTES &&
       pack.frame_count==BS_SHADOW_CULL_PACK_FRAMES && pack.ground_grid_size==BS_SHADOW_CULL_GROUND_GRID &&
       pack.zoom==BS_SHADOW_CULL_PROJECTION_ZOOM && pack.pitch==BS_SHADOW_CULL_PROJECTION_PITCH){
        int low=sy+BS_SHADOW_CULL_TOP_MIN+floor_div((h->z-BS_SHADOW_CULL_GROUND_MAX-1)*BS_PITCH*BS_ZOOM,65536)-2;
        int high=sy+BS_SHADOW_CULL_BOTTOM_MAX+floor_div((h->z-BS_SHADOW_CULL_GROUND_MIN-1)*BS_PITCH*BS_ZOOM,65536)+2;
        if(sx+BS_SHADOW_CULL_RIGHT_MAX<=0 || sx+BS_SHADOW_CULL_LEFT_MIN>=BS_WIDTH || low>=BS_TOP+BS_HEIGHT || high<=BS_TOP)return;
    }
    data=get_sprite((unsigned)frame_index);if(!data){++lighting_stats.missing_lighting_frames;return;}
    n=rf_width(f)*rf_height(f);mask=data+n*5;w=read_u16(mask);hh=read_u16(mask+2);ax=read_i16(mask+4);ay=read_i16(mask+6);mask+=8;
    left=sx-ax;top=sy-ay;
    if(left>=BS_WIDTH || left+w<=0)return;
    /* Bilinear terrain within this footprint is bounded by its grid corners.
     * Find a conservative screen rectangle without a second resident buffer. */
    x0=clamp(h->x+floor_div(-ax*256,BS_ZOOM)-2,0,DOTA_WORLD-1);
    x1=clamp(h->x+floor_div((w-ax)*256,BS_ZOOM)+2,0,DOTA_WORLD-1);
    y0=clamp(h->y+floor_div(-ay*65536,BS_PITCH*BS_ZOOM)-2,0,DOTA_WORLD-1);
    y1=clamp(h->y+floor_div((hh-ay)*65536,BS_PITCH*BS_ZOOM)+2,0,DOTA_WORLD-1);
    gx0=x0*(int)(size-1)/(DOTA_WORLD-1);gx1=clamp(x1*(int)(size-1)/(DOTA_WORLD-1)+1,0,(int)size-1);
    gy0=y0*(int)(size-1)/(DOTA_WORLD-1);gy1=clamp(y1*(int)(size-1)/(DOTA_WORLD-1)+1,0,(int)size-1);
    min_z=max_z=ground[gy0*size+gx0];
    for(y=gy0;y<=gy1;++y)for(x=gx0;x<=gx1;++x){int z=ground[y*size+x];if(z<min_z)min_z=z;if(z>max_z)max_z=z;}
    x0=clamp(left,0,BS_WIDTH);x1=clamp(left+w,0,BS_WIDTH);
    y0=clamp(top+floor_div((h->z-max_z-1)*BS_PITCH*BS_ZOOM,65536)-2,BS_TOP,BS_TOP+BS_HEIGHT);
    y1=clamp(top+hh+floor_div((h->z-min_z-1)*BS_PITCH*BS_ZOOM,65536)+2,BS_TOP,BS_TOP+BS_HEIGHT);
    /* Gather from the already rendered surface. Scatter through integer world
     * coordinates left empty columns and blended some pixels2/4 times even on
     * a flat floor. Each destination is now sampled and blended at most once.
     * Map depth gives y+z; the camera gives y-z, so no iterative raycast is
     * needed. Higher foreground scenery still fails the ground-depth test. */
    if(x0>=x1 || y0>=y1)return;
    for(x=x0;x<x1;++x)world_x[x-x0]=floor_div((2*(camera_gx+x-BS_WIDTH/2)+1)*256,2*BS_ZOOM);
    for(y=y0;y<y1;++y){
        int gy=camera_gy+y-BS_TOP-BS_HEIGHT/2;
        int64_t row=(int64_t)(2*gy+1)*65536-(int64_t)4*h->z*BS_PITCH*BS_ZOOM;
        int flat_y=floor_div((2*gy+1)*65536,2*BS_PITCH*BS_ZOOM);
        int previous_depth=-1,cached_my=0,cached_wy=0;
        for(x=x0;x<x1;++x){
            int p=(y-BS_TOP)*BS_WIDTH+x,wx,wy,z,d,my;int64_t sample;unsigned a;
            if(depths[p]==65535)continue;
            /* row/terrain projection depends on destination depth only. Exact
             * equal-depth runs may reuse it without changing sample order. */
            if((int)depths[p]!=previous_depth){
                previous_depth=(int)depths[p];
                sample=row+(int64_t)2*(32768-previous_depth)*BS_PITCH*BS_ZOOM;
                cached_my=(int)(sample>>18)-base_gy+ay;
                cached_wy=(flat_y+32768-previous_depth)>>1;
            }
            my=cached_my;
            if((unsigned)my>=(unsigned)hh)continue;
            a=mask[(unsigned)my*w+(unsigned)(x-left)];if(!a)continue;
            wx=world_x[x-x0];
            wy=cached_wy;
            if((unsigned)wx>=DOTA_WORLD || (unsigned)wy>=DOTA_WORLD)continue;
            if(pack.version>=8u && dota_roshan_region_at(wx,wy)!=view_region)continue;
            z=bs_ground_height(wx,wy)+1;
            d=32768-wy-z;
            if(d<0 || d>=65535 || d>(int)depths[p]+1){++lighting_stats.shadow_depth_rejects;continue;}
            pixels[y*BS_WIDTH+x]=mix_shadow(pixels[y*BS_WIDTH+x],a);++drawn;
        }
    }
    if(drawn){++lighting_stats.shadow_instances;lighting_stats.shadow_pixels+=drawn;}
}
static void consume_skill_events(const DotaGame *game,int reset){
    unsigned count=reset?game->skill_event_count:game->skill_event_serial-observed_skill_serial,i;
    if(count>game->skill_event_count)count=game->skill_event_count;
    if(count>DOTA_MAX_SKILL_EVENTS)count=DOTA_MAX_SKILL_EVENTS;
    if(reset)clear_bytes(skill_tracks,sizeof(skill_tracks));
    for(i=0;i<count;++i){
        const DotaSkillEvent *e=dota_get_skill_event(game,game->skill_event_serial-count+1u+i);
        UnitHistory *h;const DotaUnit *u;
        if(!e || e->caster<0 || e->caster>=DOTA_HERO_COUNT ||
           (unsigned)e->hero_id>=DOTA_HERO_TYPES || (unsigned)e->skill>=DOTA_SKILLS)continue;
        u=&game->units[e->caster];if(u->generation!=e->caster_generation || !u->active || u->hp<=0)continue;
        if(e->kind==DOTA_SKILL_EVENT_STRIKE && e->hero_id==DOTA_HERO_JUGGERNAUT && e->skill==3){
            ++omnislash_stats.strike_events;continue;
        }
        if(e->kind!=DOTA_SKILL_EVENT_CAST && e->kind!=DOTA_SKILL_EVENT_PASSIVE)continue;
        if(e->kind==DOTA_SKILL_EVENT_PASSIVE && (e->hero_id>=6 || e->skill!=2))continue;
        skill_tracks[e->caster][e->skill].event=*e;skill_tracks[e->caster][e->skill].valid=1;
        if(e->kind==DOTA_SKILL_EVENT_PASSIVE){
            ++passive_stats.trigger_events;
            if(e->hero_id==DOTA_HERO_LINA || e->hero_id==DOTA_HERO_CRYSTAL_MAIDEN ||
               (e->hero_id==DOTA_HERO_JUGGERNAUT && (e->step<0 || u->skill_ticks[0]>0)))continue;
        }
        h=&history[e->caster];h->action=BS_SKILL_Q+e->skill;h->action_tick=e->started_tick;h->cast_skill=e->skill;
        if(e->hero_id>=4 && e->step==-4){h->action=BS_CAST;h->cast_skill=-1;}
        h->cast_yaw=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,h->yaw);
        if(e->kind==DOTA_SKILL_EVENT_CAST){
            ++skill_stats.cast_events;
            if(e->hero_id==DOTA_HERO_JUGGERNAUT && e->skill==3)++omnislash_stats.cast_events;
        }
    }
    observed_skill_serial=game->skill_event_serial;
}
static void consume_attack_events(const DotaGame *game,int reset){
    unsigned count=reset?game->attack_event_count:game->attack_event_serial-observed_attack_serial,i;
    if(count>game->attack_event_count)count=game->attack_event_count;
    if(count>DOTA_MAX_ATTACK_EVENTS)count=DOTA_MAX_ATTACK_EVENTS;
    for(i=0;i<count;++i){
        const DotaAttackEvent *e=dota_get_attack_event(game,game->attack_event_serial-count+1u+i);
        UnitHistory *h;const DotaUnit *u;
        if(!e)continue;
        if(e->kind==DOTA_ATTACK_EVENT_RELEASE)++projectile_stats.release_events;
        if(e->kind==DOTA_ATTACK_EVENT_CANCEL)++projectile_stats.cancel_events;
        if(e->source<0 || e->source>=DOTA_MAX_UNITS)continue;
        u=&game->units[e->source];h=&history[e->source];
        if(u->generation!=e->source_generation || !u->active || u->hp<=0)continue;
        if(e->kind==DOTA_ATTACK_EVENT_START && (h->action<BS_SKILL_Q ||
            (u->hero_id==DOTA_HERO_JUGGERNAUT && h->action==BS_SKILL_E))){
            h->action=BS_ATTACK;h->cast_skill=-1;h->action_tick=e->started_tick;
            h->yaw=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,h->yaw);
        }else if(e->kind==DOTA_ATTACK_EVENT_CANCEL && h->action==BS_ATTACK &&
                 (u->stun_ticks || u->channel_ticks || u->target<0 || u->x!=e->origin_x || u->y!=e->origin_y)){
            h->action=-1;
        }
    }
    observed_attack_serial=game->attack_event_serial;
}
static void update_history(const DotaGame *game){
    int i,j,reset=last_tick<0 || game->tick<last_tick || game->tick==0;
    if(!reset && game->tick==last_tick && observed_skill_serial==game->skill_event_serial && observed_attack_serial==game->attack_event_serial)return;
    if(reset)clear_bytes(effect_history,sizeof(effect_history));
    for(i=0;i<DOTA_MAX_UNITS;++i){
        const DotaUnit *unit=&game->units[i];UnitHistory *h=&history[i];int dx=unit->x-h->x,dy=unit->y-h->y;
        if(reset || unit->generation!=h->generation){clear_bytes(h,sizeof(*h));h->death_tick=-1;h->action=-1;h->cast_skill=-1;h->model=game_unit_model(game,i);dx=dy=0;}
        else if(h->active && h->hp>0 && (!unit->active || unit->hp<=0)){h->death_tick=game->tick;h->action=-1;}
        if(unit->active && unit->hp>0){
            h->moving=dx!=0 || dy!=0;h->model=game_unit_model(game,i);
            if(pack.version<4u && !reset && unit->attack_wait>h->attack_wait && h->action<BS_SKILL_Q){h->action=BS_ATTACK;h->cast_skill=-1;h->action_tick=game->tick;}
            for(j=0;j<4;++j)if(pack.version<3u && !reset && unit->cooldown[j]>h->cooldown[j]){h->action=BS_CAST;h->cast_skill=-1;h->action_tick=game->tick;}
            if(unit->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && h->was_channel && !unit->channel_ticks){
                h->action=unit->stun_ticks || unit->taunt_ticks?-1:BS_SKILL_PHASE_END;
                h->cast_skill=3;h->action_tick=game->tick;
            }
            if(unit->target>=0 && unit->target<DOTA_MAX_UNITS && game->units[unit->target].active && game->units[unit->target].hp>0){dx=game->units[unit->target].x-unit->x;dy=game->units[unit->target].y-unit->y;}
            else if(unit->move_order){dx=unit->move_x-unit->x;dy=unit->move_y-unit->y;}
            h->yaw=direction_yaw(dx,dy,h->yaw);
            h->x=unit->x;h->y=unit->y;h->z=bs_ground_height(unit->x,unit->y);
            if(unit->type==DOTA_BUILDING && (unsigned)unit->hero_id<14u)h->z=dota_filler_source_height[unit->hero_id];
            if(unit->type==DOTA_TOWER && unit->tier>=2)h->z=64;
        }else if(reset){h->x=unit->x;h->y=unit->y;h->z=bs_ground_height(unit->x,unit->y);}
        h->generation=unit->generation;h->active=unit->active;h->hp=unit->hp;h->attack_wait=unit->attack_wait;
        h->was_channel=unit->channel_ticks>0;
        for(j=0;j<4;++j)h->cooldown[j]=unit->cooldown[j];
    }
    consume_attack_events(game,reset);consume_skill_events(game,reset);last_tick=game->tick;
}
static const DotaSkillEvent *latest_omnislash_strike(const DotaGame *game,int caster){
    unsigned i,count=game->skill_event_count;
    if(count>DOTA_MAX_SKILL_EVENTS)count=DOTA_MAX_SKILL_EVENTS;
    for(i=0;i<count;++i){const DotaSkillEvent *e=dota_get_skill_event(game,game->skill_event_serial-i);
        if(e && e->kind==DOTA_SKILL_EVENT_STRIKE && e->hero_id==DOTA_HERO_JUGGERNAUT && e->skill==3 &&
           e->caster==caster && e->caster_generation==game->units[caster].generation)return e;
    }
    return 0;
}
static const ClipGroup *skill_body(const DotaGame *game,const DotaUnit *u,UnitHistory *h,int *clip,int *yaw,unsigned *phase,int *loop){
    const ClipGroup *group,*start;unsigned age=(unsigned)(game->tick-h->action_tick),start_ticks=0;
    int main=h->action;
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && u->type==DOTA_HERO){
        int caster=(int)(u-game->units),stage=dota_omnislash_phase(game,caster);
        if(stage){
            const DotaSkillEvent *strike=stage==2?latest_omnislash_strike(game,caster):0;
            *clip=stage==1?BS_SKILL_PHASE_END:BS_SKILL_R;*yaw=h->yaw;*loop=stage==2;
            if(strike)*yaw=direction_yaw(strike->target_x-strike->origin_x,strike->target_y-strike->origin_y,h->yaw);
            group=find_group(h->model,*clip,*yaw,0);
            if(!group){++omnislash_stats.missing_frames;return 0;}
            if(stage==1)*phase=(unsigned)clamp(6-game->omnislash[caster].phase_ticks,0,5)*(group->ticks-1u)/5u;
            else if(strike)*phase=(unsigned)(game->tick-strike->started_tick)*group->ticks/(unsigned)(strike->duration_ticks>0?strike->duration_ticks:1);
            else *phase=0;
            h->cast_skill=3;return group;
        }
        if(h->cast_skill==3){h->action=-1;h->cast_skill=-1;}
    }
    if(u->type==DOTA_HERO && u->hero_id==6){
        int caster=(int)(u-game->units);const DotaRosterHero *r=&game->roster.hero[caster];
        if(r->generation==u->generation && ((r->cast_at>game->tick && r->cast_skill==3) || r->channel_until>game->tick)){
            *clip=r->cast_at>game->tick?BS_SKILL_PHASE_START:BS_SKILL_R;*yaw=h->cast_yaw;*loop=*clip==BS_SKILL_R;
            group=find_group(h->model,*clip,*yaw,0);if(!group)return 0;
            *phase=*loop?(unsigned)game->tick:(unsigned)clamp(6-(r->cast_at-game->tick),0,5)*(group->ticks-1u)/5u;return group;
        }
    }
    if(h->action<BS_SKILL_Q)return 0;
    *yaw=h->cast_yaw;*loop=0;*phase=age;
    if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && h->action==BS_SKILL_PHASE_END){
        *clip=BS_SKILL_PHASE_END;group=find_group(h->model,*clip,*yaw,0);
        if(group && age<group->ticks)return group;
        h->action=-1;return 0;
    }
    if(u->stun_ticks || u->taunt_ticks){
        int axe_reaction=u->hero_id==DOTA_HERO_AXE && h->cast_skill==2 &&
            skill_tracks[u-game->units][2].valid && skill_tracks[u-game->units][2].event.kind==DOTA_SKILL_EVENT_PASSIVE;
        if(!axe_reaction && !(u->hero_id==DOTA_HERO_JUGGERNAUT && h->cast_skill==0 && u->skill_ticks[0])){h->action=-1;return 0;}
    }
    if(u->hero_id==DOTA_HERO_JUGGERNAUT && h->cast_skill==0){
        int opening=BS_SKILL_PHASE_START;
        start=find_group(h->model,opening,*yaw,0);start_ticks=start?start->ticks:0;
        if(start && age<start_ticks){*clip=opening;return start;}
        *phase=age-start_ticks;
        if(h->cast_skill==0){
            if(!u->skill_ticks[0]){h->action=-1;return 0;}
            *loop=1;
        }
    }else if(u->hero_id==DOTA_HERO_CRYSTAL_MAIDEN && h->cast_skill==3){
        start=find_group(h->model,BS_SKILL_R,*yaw,0);start_ticks=start?start->ticks:0;
        if(!u->channel_ticks || !u->skill_ticks[3]){h->action=-1;return 0;}
        if(start && age<start_ticks){*clip=BS_SKILL_R;return start;}
        main=BS_SKILL_PHASE_START;*phase=age-start_ticks;*loop=1;
    }
    *clip=main;group=find_group(h->model,main,*yaw,0);
    if(group && (*loop || *phase<group->ticks))return group;
    if(pack.version<3u){*clip=BS_CAST;group=find_group(h->model,BS_CAST,*yaw,0);if(group && age<group->ticks){*phase=age;return group;}}
    h->action=-1;return 0;
}
static int courier_frame(const DotaGame *game,int owner,UnitHistory *position,int *yaw){
    DotaCourierView v;const ClipGroup *group;int model,clip,frame;
    if(pack.version<10u || !dota_courier_view(game,owner,&v))return -1;
    if(!game->units[owner].active || game->units[owner].type!=DOTA_HERO || (unsigned)game->units[owner].team>=2)return -1;
    if(dota_roshan_region_at(v.x,v.y)!=view_region)return -1;
    if(game->units[owner].team!=game->units[game->player].team&&!dota_sight_position_visible(game,game->player,v.x,v.y))return -1;
    model=75+game->units[owner].team;clip=v.moving?BS_RUN:BS_IDLE;
    *yaw=direction_yaw(v.target_x-v.x,v.target_y-v.y,game->units[owner].team?12:4);
    group=exact_group(model,clip,*yaw);
    if(!group){++courier_stats.missing_frames;return -1;}
    frame=frame_at(group,(unsigned)(game->tick+owner*3),1);
    position->x=v.x;position->y=v.y;position->z=bs_ground_height(v.x,v.y);position->model=model;
    courier_animations[owner].model=model;courier_animations[owner].clip=clip;courier_animations[owner].direction=*yaw;
    courier_animations[owner].frame=frame-group->first;courier_animations[owner].skill=-1;courier_animations[owner].phase=game->tick;
    return frame;
}
static void render_item_trees(uint16_t *pixels,const DotaGame *game,const BSCamera *camera,int shadow){
    int i;const ClipGroup *group=find_group(92,BS_IDLE,0,0);if(!group)return;
    for(i=0;i<DOTA_ITEM_TREES;++i){const DotaItemTree *tree=&game->item_world.tree[i];UnitHistory position;int frame,sx,sy;uint16_t depth;
        if(tree->until<=game->tick || tree->removed || !dota_sight_position_visible(game,game->player,tree->x,tree->y) || (pack.version>=8u && dota_roshan_region_at(tree->x,tree->y)!=view_region))continue;
        clear_bytes(&position,sizeof(position));position.x=tree->x;position.y=tree->y;position.z=bs_ground_height(tree->x,tree->y);frame=frame_at(group,0,0);
        if(shadow){draw_hero_shadow(pixels,frame,&position,camera);continue;}
        bs_project(camera,position.x,position.y,position.z,&sx,&sy,&depth);draw_sprite(pixels,frame,sx,sy,depth,256,-1);
    }
}
#include "fountain_baked.h"
#include "barracks_baked.h"
void bs_render_game(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    unsigned i;int selected_frames[DOTA_MAX_UNITS];clear_bytes(&stats,sizeof(stats));clear_bytes(bounds,sizeof(bounds));
    clear_bytes(&fog_stats,sizeof(fog_stats));fog_game=game;fog_camera=camera;fog_sprite_gate=0;
    clear_bytes(&courier_stats,sizeof(courier_stats));
    for(i=0;i<DOTA_HERO_COUNT;++i)courier_animations[i].model=-1;
    clear_bytes(shop_bounds,sizeof(shop_bounds));clear_bytes(&jungle_stats,sizeof(jungle_stats));
    clear_bytes(&omnislash_stats,sizeof(omnislash_stats));
    clear_bytes(&water_stats,sizeof(water_stats));clear_bytes(&skill_stats,sizeof(skill_stats));
    clear_bytes(&lighting_stats,sizeof(lighting_stats));
    clear_bytes(&projectile_stats,sizeof(projectile_stats));
    clear_bytes(&component_stats,sizeof(component_stats));clear_bytes(&beam_geometry,sizeof(beam_geometry));
    clear_bytes(&passive_stats,sizeof(passive_stats));
    for(i=0;i<DOTA_MAX_UNITS;++i){animations[i].model=-1;selected_frames[i]=-1;}
    if(!ready || !pixels || !game || !camera){
        for(i=0;i<BS_WIDTH*BS_HEIGHT;++i){depths[i]=65535;if(pixels)pixels[BS_TOP*BS_WIDTH+i]=CLEAR_COLOR;}
        return;
    }
    view_region=pack.version>=8u?dota_roshan_region_at(game->units[game->player].x,game->units[game->player].y):0;
    fog_background_key[0]=camera->x;fog_background_key[1]=camera->y;fog_background_key[2]=camera->z;
    fog_background_key[3]=view_region;fog_background_key[4]=(int)dota_barracks_alive_mask(game);
    fog_background_key[5]=game->units[game->player].team;
    for(i=6;i<FOG_BACKGROUND_KEYS;++i)fog_background_key[i]=-1;
    draw_map(pixels,camera,(unsigned)(game->tick<0?0:game->tick));
    draw_barracks_ground(pixels,game,camera);update_history(game);
    if(pack.version>=8u){
        int gx=global_x(camera->x)-BS_WIDTH/2,gy=global_y(camera->y,camera->z)-BS_HEIGHT/2;
        dota_roshan_mask_pixels(pixels+BS_TOP*BS_WIDTH,BS_WIDTH,BS_WIDTH,BS_HEIGHT,gx,gy,view_region,0x0841u);
    }
    register_barracks_bounds(game,camera);
    /* Original pit pedestals are static scenery, present before spawn and
     * after death. Their source matrices use z=0, not the floating cube feet. */
    if(pack.version>=6u)for(i=0;i<2;++i){
        const ClipGroup *group=find_group(47,BS_SKILL_Q+(int)i,0,0);int sx,sy,index;uint16_t depth;
        if(!group){++jungle_stats.missing_jungle_frames;continue;}
        if(pack.version>=8u && dota_roshan_region_at(dota_tormentor_locations[i].x,dota_tormentor_locations[i].y)!=view_region)continue;
        index=frame_at(group,0,0);
        bs_project(camera,dota_tormentor_locations[i].x,dota_tormentor_locations[i].y,0,&sx,&sy,&depth);
        draw_sprite(pixels,index,sx,sy,depth,256,-1);
    }
    if(pack.version>=6u)for(i=0;i<2;++i){
        const ClipGroup *group=find_group(45+(int)i,BS_IDLE,0,0);int sx,sy,index;uint16_t depth;
        if(!group){++jungle_stats.missing_jungle_frames;continue;}
        if(pack.version>=8u && dota_roshan_region_at(dota_secret_shops[i].x,dota_secret_shops[i].y)!=view_region)continue;
        index=frame_at(group,(unsigned)game->tick,1);
        bs_project(camera,dota_secret_shops[i].x,dota_secret_shops[i].y,31,&sx,&sy,&depth);
        if(draw_sprite(pixels,index,sx,sy,depth,256,-2-(int)i)){fog_background_key[6+i]=index;++jungle_stats.shop_instances;}
    }
    if(pack.version>=9u)for(i=0;i<DOTA_LOTUS_POOLS;++i){
        int id=DOTA_OBJECTIVE_LOTUS_FIRST+(int)i,count=clamp(game->objectives.lotus_count[i],0,6),j;
        int sx,sy;uint16_t depth;const DotaPoint *p=&dota_objective_positions[id];
        if(dota_roshan_region_at(p->x,p->y)!=view_region||!dota_sight_position_visible(game,game->player,p->x,p->y))continue;
        fog_background_key[8+i]=count;
        bs_project(camera,p->x,p->y,dota_objective_heights[id],&sx,&sy,&depth);
        for(j=0;j<count;++j){const ClipGroup *group=exact_group(73+(int)i,j,0);
            if(group)draw_sprite(pixels,frame_at(group,0,0),sx,sy,depth,256,-1);
        }
    }
    draw_fog(pixels,game,camera);fog_sprite_gate=1;
    render_item_trees(pixels,game,camera,1);
    for(i=0;i<DOTA_MAX_UNITS;++i){
        const DotaUnit *unit=&game->units[i];UnitHistory *h=&history[i];const ClipGroup *group;
        int clip=h->moving?BS_RUN:BS_IDLE,sx,sy,index,loop=1,yaw=h->yaw,skill=-1;unsigned phase=(unsigned)(game->tick+(int)i*3);uint16_t depth;
        if(h->model<0)continue;
        if(i<DOTA_HERO_COUNT && game->extra.hero[i].seeker_at>0 && game->tick>=game->extra.hero[i].seeker_at)continue;
        if(pack.version>=8u && !dota_unit_visible(game,game->player,(int)i))continue;
        if(!unit->active || unit->hp<=0){
            if(h->death_tick<0)continue;
            clip=BS_DEATH;group=find_group(h->model,BS_DEATH,yaw,0);phase=(unsigned)(game->tick-h->death_tick);
            if(!group || phase>=group->ticks)continue;
            loop=0;
        }else{
            group=skill_body(game,unit,h,&clip,&yaw,&phase,&loop);
            if(!group && unit->hero_id==DOTA_HERO_JUGGERNAUT && unit->skill_ticks[0] && i<DOTA_HERO_COUNT){
                const SkillTrack *spin=&skill_tracks[i][0];
                if(spin->valid && spin->event.caster_generation==unit->generation){
                    h->action=BS_SKILL_Q;h->cast_skill=0;h->action_tick=spin->event.started_tick;
                    h->cast_yaw=direction_yaw(spin->event.target_x-spin->event.origin_x,spin->event.target_y-spin->event.origin_y,h->yaw);
                    group=skill_body(game,unit,h,&clip,&yaw,&phase,&loop);
                }
            }
            if(group){skill=h->cast_skill;++skill_stats.body_skill_actors;}
            if(!group && h->action>=0){int action_clip=h->action;const ClipGroup *action;
                action=find_group(h->model,action_clip,yaw,0);
                if(action && (unsigned)(game->tick-h->action_tick)<action->ticks){
                    group=action;clip=action_clip;phase=(unsigned)(game->tick-h->action_tick);loop=0;
                    if(action_clip==BS_SKILL_E){skill=2;++skill_stats.body_skill_actors;}
                }
                else h->action=-1;
            }
            if(!group){clip=h->moving?BS_RUN:BS_IDLE;yaw=h->yaw;loop=1;phase=(unsigned)(game->tick+(int)i*3);group=find_group(h->model,clip,yaw,1);}
            if(unit->type==DOTA_SUMMON && h->model==44 && i>=DOTA_SUMMON_FIRST && i<DOTA_SUMMON_FIRST+DOTA_MAX_SUMMONS && !h->moving){
                const ClipGroup *opening=find_group(44,BS_CAST,yaw,0);
                int lifetime=dota_healing_ward_lifetime(game,game->summon_owner[i-DOTA_SUMMON_FIRST]);
                unsigned age=(unsigned)clamp(lifetime-game->summon_life[i-DOTA_SUMMON_FIRST],0,lifetime);
                if(opening && age<opening->ticks){group=opening;clip=BS_CAST;phase=age;loop=0;}
            }
            if(pack.version>=12u && i>=DOTA_ITEM_UNIT_FIRST && (h->model==114 || h->model==115)){
                clip=1;loop=1;phase=(unsigned)game->tick;group=find_group(h->model,1,yaw,0);
            }
            if(unit->type==DOTA_TORMENTOR){
                unsigned age=(unsigned)(game->tick-unit->skill_y);yaw=0;clip=BS_IDLE;loop=1;phase=(unsigned)game->tick;
                group=find_group(47,BS_CAST,0,0);
                if(group && age<group->ticks){clip=BS_CAST;phase=age;loop=0;}
                else if(unit->skill_x>0 && game->tick-unit->skill_x<16){
                    clip=BS_ATTACK;phase=(unsigned)(game->tick-unit->skill_x);loop=0;
                }else if(game->tormentor.last_damage_tick>0 && game->tick-game->tormentor.last_damage_tick<16){
                    clip=BS_RUN;
                    phase=(unsigned)(game->tick-game->tormentor.last_damage_tick);loop=0;
                }
                group=find_group(47,clip,0,0);
                if(!group)++jungle_stats.missing_jungle_frames;
            }
            if(unit->type==DOTA_ROSHAN && unit->skill_ticks[0]>0 && unit->skill_x>=1 && unit->skill_x<=3){
                int total=unit->skill_x==1?20:(unit->skill_x==2?37:40);
                clip=unit->skill_x==1?BS_CAST:(unit->skill_x==2?BS_SKILL_Q:BS_SKILL_W);
                group=find_group(48,clip,yaw,0);loop=0;
                phase=(unsigned)clamp(total-unit->skill_ticks[0],0,total-1)*(group?group->ticks:1u)/(unsigned)total;
            }
        }
        index=frame_at(group,phase,loop);bs_project(camera,h->x,h->y,h->z,&sx,&sy,&depth);
        if(index>=0){animations[i].model=h->model;animations[i].clip=clip;animations[i].direction=yaw;animations[i].frame=index-group->first;animations[i].skill=skill;animations[i].phase=(int)phase;}
        selected_frames[i]=index;
        if(index>=0 && (rf_flags(frame_info((unsigned)index))&BS_HAS_SHADOW))draw_hero_shadow(pixels,index,h,camera);
    }
    for(i=0;i<DOTA_HERO_COUNT;++i){
        UnitHistory h;int yaw,index=courier_frame(game,(int)i,&h,&yaw);unsigned before=lighting_stats.shadow_instances;
        if(index<0)continue;
        draw_hero_shadow(pixels,index,&h,camera);
        courier_stats.shadow_instances+=lighting_stats.shadow_instances-before;
    }
    render_item_trees(pixels,game,camera,0);
    for(i=0;i<DOTA_MAX_UNITS;++i){
        const DotaUnit *unit=&game->units[i];const UnitHistory *h=&history[i];
        int index=selected_frames[i],sx,sy,yaw=animations[i].direction,skill=animations[i].skill;uint16_t depth;
        if(index<0)continue;
        bs_project(camera,h->x,h->y,h->z,&sx,&sy,&depth);
        if(dota_omnislash_phase(game,(int)i)==2){
            const DotaSkillEvent *strike=latest_omnislash_strike(game,(int)i);
            if(strike && game->tick-strike->started_tick<4){
                int old_x,old_y,old_frame;uint16_t old_depth;
                const ClipGroup *old_group=find_group(h->model,BS_SKILL_R,yaw,0);
                unsigned age=(unsigned)(game->tick-strike->started_tick);
                old_frame=frame_at(old_group,0,0);
                bs_project(camera,strike->origin_x,strike->origin_y,bs_ground_height(strike->origin_x,strike->origin_y),&old_x,&old_y,&old_depth);
                if(draw_sprite_opacity(pixels,old_frame,old_x,old_y,old_depth,256,-1,75u-age*18u))++omnislash_stats.afterimages;
            }
        }
        if(draw_sprite_opacity(pixels,index,sx,sy,depth,256,unit->active && unit->hp>0?(int)i:-1,dota_omnislash_phase(game,(int)i)==2?176u:255u)){
            ++stats.units_drawn;if(h->model==44)++component_stats.ward_bodies;
            if(h->model==47){++jungle_stats.tormentor_instances;jungle_stats.tormentor_clip=(unsigned)animations[i].clip;}
            if(dota_omnislash_phase(game,(int)i)>0)++omnislash_stats.body_instances;
            if(skill==2)++passive_stats.body_reactions;
        }
    }
    for(i=0;i<DOTA_HERO_COUNT;++i){
        UnitHistory h;int yaw,index=courier_frame(game,(int)i,&h,&yaw),sx,sy;uint16_t depth;
        if(index<0)continue;
        bs_project(camera,h.x,h.y,h.z,&sx,&sy,&depth);
        if(draw_sprite(pixels,index,sx,sy,depth,256,-1)){
            ++courier_stats.bodies;courier_stats.model_mask|=1u<<(unsigned)(h.model-75);
            if(courier_animations[i].clip==BS_RUN)++courier_stats.moving_bodies;
        }
    }
    if(pack.version>=12u)draw_fountains_v022(pixels,game,camera,view_region);
    else if(pack.version>=7u)for(i=0;i<2;++i){
        const ClipGroup *group=find_group(6+(int)i,BS_SKILL_Q,0,0);int sx,sy,index;uint16_t depth;unsigned drawn;
        if(!group){++lighting_stats.missing_lighting_frames;continue;}
        if(pack.version>=8u && dota_roshan_region_at(dota_fountains[i].x,dota_fountains[i].y)!=view_region)continue;
        index=frame_at(group,(unsigned)(game->tick<0?0:game->tick),1);
        if(index<0){++lighting_stats.missing_lighting_frames;continue;}
        lighting_stats.fountain_frame[i]=(unsigned)(index-group->first);
        bs_project(camera,dota_fountains[i].x,dota_fountains[i].y,0,&sx,&sy,&depth);
        drawn=draw_sprite(pixels,index,sx,sy,depth,256,-1);
        if(drawn){++lighting_stats.fountain_instances;lighting_stats.fountain_pixels+=drawn;}
    }
}
static int skill_fx(uint16_t *pixels,const BSCamera *camera,int model,int clip,int direction,unsigned phase,int loop,int x,int y,int radius){
    const ClipGroup *group=find_group(model,clip,direction,0);int index,sx,sy,scale=256;uint16_t depth;
    if(!fog_point_visible(x,y))return 0;
    if(!group){++skill_stats.missing_skill_frames;return 0;}
    index=frame_at(group,phase,loop);if(index<0)return 0;
    if(radius>0 && rf_world_height(frame_info((unsigned)index)))scale=clamp(radius*256/rf_world_height(frame_info((unsigned)index)),16,2048);
    bs_project(camera,x,y,bs_ground_height(x,y)+3,&sx,&sy,&depth);
    if(draw_sprite(pixels,index,sx,sy,depth,scale,-1)){
        ++skill_stats.effect_instances;++stats.effects_drawn;
        if((unsigned)model<32u)skill_stats.model_mask|=1u<<(unsigned)model;
        if(model==18 || model==22 || model==26 || model==30){
            ++passive_stats.effect_instances;passive_stats.model_mask|=1u<<(unsigned)model;
        }
        if(model==25)++component_stats.ward_auras;
        if(model==17)++component_stats.skull_instances;
        if(model==20)++component_stats.dragon_instances;
        if(clip==1)++skill_stats.loop_instances;
        if(clip==2)++skill_stats.impact_instances;
        return 1;
    }
    return 0;
}
static int event_caster_valid(const DotaGame *game,const DotaSkillEvent *e){
    return e->caster>=0 && e->caster<DOTA_HERO_COUNT && game->units[e->caster].active &&
        game->units[e->caster].type==DOTA_HERO && game->units[e->caster].generation==e->caster_generation &&
        game->units[e->caster].hero_id==e->hero_id;
}
static const DotaUnit *event_target(const DotaGame *game,const DotaSkillEvent *e){
    const DotaUnit *u;
    if(e->target<0 || e->target>=DOTA_MAX_UNITS)return 0;
    u=&game->units[e->target];return u->active && u->hp>0 && u->generation==e->target_generation?u:0;
}
static int persistent_skill(const DotaGame *game,const DotaSkillEvent *e,unsigned age){
    const DotaUnit *u=&game->units[e->caster],*target=event_target(game,e);int id=e->hero_id*4+e->skill;
    switch(id){
        case 0:return u->hp>0 && u->skill_ticks[0]>0;
        case 1:return target && target->dot_ticks>0 && target->dot_source==e->caster;
        case 6:return u->hp>0 && u->skill_ticks[2]>0;
        case 8:return u->hp>0 && u->skill_ticks[0]>0;
        case 9:return u->hp>0 && u->skill_ticks[1]>0;
        case 10:return u->hp>0 && u->skill_ticks[2]>0;
        case 13:return target && target->stun_ticks>0 && age<(unsigned)(e->duration_ticks>0?e->duration_ticks:35);
        case 14:return u->hp>0; /* Intrinsic aura lasts for the current life. */
        case 15:return u->hp>0 && u->channel_ticks>0 && u->skill_ticks[3]>0 && !u->stun_ticks && !u->taunt_ticks;
        default:return -1;
    }
}
static void skill_beam(uint16_t *pixels,const BSCamera *camera,int model,int clip,unsigned phase,int ax,int ay,int bx,int by,int radius){
    int dx=bx-ax,dy=by-ay,length=(int)integer_sqrt((unsigned)(dx*dx+dy*dy)),segments,i,dir=direction_yaw(dx,dy,0);
    if(length<1){skill_fx(pixels,camera,model,clip,dir,phase,0,bx,by,radius);return;}
    segments=(length+179)/180;if(segments>8)segments=8;
    for(i=0;i<segments;++i){
        int x=ax+dx*(2*i+1)/(2*segments),y=ay+dy*(2*i+1)/(2*segments);
        skill_fx(pixels,camera,model,clip,dir,phase,0,x,y,radius);
    }
}
static void beam_endpoint(uint16_t *pixels,const BSCamera *camera,int clip,unsigned phase,int x,int y,int z){
    const ClipGroup *group=find_group(23,clip,0,0);int sx,sy,index;uint16_t depth;
    if(!group){++component_stats.missing_component_frames;return;}
    if(phase>=group->ticks)return;
    index=frame_at(group,phase,0);bs_project(camera,x,y,z,&sx,&sy,&depth);
    if(draw_sprite(pixels,index,sx,sy,depth,256,-1)){
        ++stats.effects_drawn;++skill_stats.effect_instances;skill_stats.model_mask|=1u<<23;
        if(clip==2)++skill_stats.impact_instances;
    }
}
/* Stretch one canonical200-world beam between its actual raised endpoints.
 * The affine inverse is calculated once; the inner loop uses integer steps.
 * Transverse width stays constant, so long casts remain a sharp white bolt. */
static void original_beam(uint16_t *pixels,const BSCamera *camera,const DotaSkillEvent *e,unsigned age,int model,int clip,int loop){
    const ClipGroup *group;const RuntimeFrame *frame;const unsigned char *data,*alpha;
    int dir=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,0),index;
    int ax,ay,bx,by,az=bs_ground_height(e->origin_x,e->origin_y)+48,bz=bs_ground_height(e->target_x,e->target_y)+40;
    int wx,wy,len2,len,vx,vy,canonical,margin=2,x0,x1,y0,y1,x,y,a,b,c,d,da,db;uint16_t ad,bd;unsigned n,drawn=0;
    if(!fog_point_visible(e->origin_x,e->origin_y)||!fog_point_visible(e->target_x,e->target_y))return;
    group=find_group(model,clip,dir,0);if(!group){++component_stats.missing_component_frames;return;}
    if(!loop && age>=group->ticks)return;
    index=frame_at(group,age,loop);frame=frame_info((unsigned)index);data=get_sprite((unsigned)index);if(!data)return;
    n=(unsigned)rf_width(frame)*rf_height(frame);alpha=data+n*2;
    bs_project(camera,e->origin_x,e->origin_y,az,&ax,&ay,&ad);bs_project(camera,e->target_x,e->target_y,bz,&bx,&by,&bd);
    beam_geometry.active=1;beam_geometry.start_x=ax;beam_geometry.start_y=ay;beam_geometry.end_x=bx;beam_geometry.end_y=by;
    beam_geometry.start_z=az;beam_geometry.end_z=bz;
    wx=bx-ax;wy=by-ay;len2=wx*wx+wy*wy;len=(int)integer_sqrt((unsigned)len2);
    if(len<1)return;
    vx=-sine[dir]*200*BS_ZOOM/256;vy=sine[(dir+4)&15]*200*BS_PITCH*BS_ZOOM/65536;
    canonical=(int)integer_sqrt((unsigned)(vx*vx+vy*vy));
    if(!canonical)return;
    a=(int)((long long)vx*256*wx/len2+(long long)vy*wy*65536/((long long)canonical*len));
    b=(int)((long long)vx*256*wy/len2-(long long)vy*wx*65536/((long long)canonical*len));
    c=(int)((long long)vy*256*wx/len2-(long long)vx*wy*65536/((long long)canonical*len));
    d=(int)((long long)vy*256*wy/len2+(long long)vx*wx*65536/((long long)canonical*len));
    for(y=0;y<2;++y)for(x=0;x<2;++x){
        int qx=(x?(int)rf_width(frame):0)-frame->anchor_x,qy=(y?(int)rf_height(frame):0)-frame->anchor_y;
        int across=(-qx*vy+qy*vx)/canonical;if(across<0)across=-across;
        if(across+3>margin)margin=across+3;
    }
    x0=clamp((ax<bx?ax:bx)-margin,0,BS_WIDTH);x1=clamp((ax>bx?ax:bx)+margin+1,0,BS_WIDTH);
    y0=clamp((ay<by?ay:by)-margin,BS_TOP,BS_TOP+BS_HEIGHT);y1=clamp((ay>by?ay:by)+margin+1,BS_TOP,BS_TOP+BS_HEIGHT);
    if(x0>=x1 || y0>=y1)return;
    da=(int)ad;db=(int)bd;
    for(y=y0;y<y1;++y){
        int u=frame->anchor_x*65536+(a*(2*x0-ax-bx)+b*(2*y-ay-by))/2;
        int v=frame->anchor_y*65536+(c*(2*x0-ax-bx)+d*(2*y-ay-by))/2;
        for(x=x0;x<x1;++x,u+=a,v+=c){
            int sx=floor_div(u,65536),sy=floor_div(v,65536),pixel,depth,t;unsigned q,opacity;uint16_t color;
            if(sx<0 || sx>=rf_width(frame) || sy<0 || sy>=rf_height(frame))continue;
            q=(unsigned)sy*rf_width(frame)+(unsigned)sx;opacity=alpha[q];if(!opacity)continue;
            t=clamp(((x-ax)*wx+(y-ay)*wy)*256/len2,0,256);depth=da+(db-da)*t/256;pixel=(y-BS_TOP)*BS_WIDTH+x;
            if(depth>(int)depths[pixel]+1){++stats.depth_rejects;continue;}
            color=read_u16(data+q*2);pixels[y*BS_WIDTH+x]=mix(color,pixels[y*BS_WIDTH+x],opacity,(rf_flags(frame)&BS_ADDITIVE)!=0);
            ++stats.sprite_pixels;++stats.blended_pixels;++drawn;
            if(opacity>=160 && ((color>>11)&31)>=26 && ((color>>5)&63)>=52 && (color&31)>=26)++component_stats.white_core_pixels;
        }
    }
    if(drawn){++component_stats.beam_instances;component_stats.beam_pixels+=drawn;++stats.effects_drawn;
        ++skill_stats.effect_instances;if(model<32)skill_stats.model_mask|=1u<<(unsigned)model;}
    if(model!=23)return;
    beam_endpoint(pixels,camera,1,age,e->origin_x,e->origin_y,az);
    beam_endpoint(pixels,camera,2,age,e->target_x,e->target_y,bz);
    beam_endpoint(pixels,camera,3,age,e->target_x,e->target_y,bs_ground_height(e->target_x,e->target_y)+3);
}
static void laguna_beam(uint16_t *pixels,const BSCamera *camera,const DotaSkillEvent *e,unsigned age){original_beam(pixels,camera,e,age,23,0,0);}
static void render_skill_effects(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    static const int visual_radii[16]={79,45,69,90,69,63,60,25,65,100,55,45,107,60,60,203};
    int caster,skill;
    for(caster=0;caster<DOTA_HERO_COUNT;++caster)for(skill=0;skill<DOTA_SKILLS;++skill){
        const SkillTrack *track=&skill_tracks[caster][skill];const DotaSkillEvent *e=&track->event;
        const DotaUnit *u,*target;const ClipGroup *start,*repeat,*impact;unsigned age,duration,impact_delay;
        int id,model,dir,x,y,persistent,radius,source_radius;
        if(!track->valid || !event_caster_valid(game,e) || game->tick<e->started_tick)continue;
        if(pack.version>=8u && !dota_unit_visible(game,game->player,caster))continue;
        if(e->hero_id>=4)continue; /* New heroes have state/impact based FX below. */
        id=e->hero_id*4+skill;model=16+id;dir=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,0);
        radius=visual_radii[id];source_radius=dota_skill_effect_radius_source(game,caster,skill);
        /* Logical AoEs scale with the same Source distances as damage. Small
         * target-bound skull/cage/crit art keeps its original body size. */
        if(source_radius>0 && (id==0 || id==2 || id==4 || id==5 || id==8 || id==9 || id==12 || id==15))radius=(source_radius+3)/4;
        if(pack.version>=5u && id==9)continue; /* A real summon owns this aura. */
        if(pack.version>=5u && id==7){laguna_beam(pixels,camera,e,(unsigned)(game->tick-e->started_tick));continue;}
        if(id==11)continue; /* Every Omnislash streak is tied to a real STRIKE. */
        start=find_group(model,0,dir,0);if(!start)continue;
        repeat=find_group(model,1,dir,0);impact=find_group(model,2,dir,0);
        age=(unsigned)(game->tick-e->started_tick);duration=start->ticks;u=&game->units[caster];target=event_target(game,e);
        persistent=persistent_skill(game,e,age);
        if(persistent==0){
            int lethal_frostbite=id==13 && e->target>=0 && e->target<DOTA_MAX_UNITS && !target &&
                game->units[e->target].generation==e->target_generation && game->units[e->target].hp<=0 && age<duration;
            if(!lethal_frostbite)continue;
            persistent=-1; /* Finish the hit at its snapshot; never hold a dead target's cage. */
        }
        x=u->x;y=u->y;
        if(id==1 || id==3 || id==5 || id==9 || id==12 || id==13){
            x=e->target_x;y=e->target_y;
            if((id==1 || id==13) && target){x=target->x;y=target->y;}
        }
        if(pack.version>=8u && dota_roshan_region_at(x,y)!=view_region)continue;
        if(id==4){
            if(age<duration){
                unsigned denom=duration>1?duration-1:1;
                x=e->origin_x+(int)((e->target_x-e->origin_x)*(int)age/(int)denom);
                y=e->origin_y+(int)((e->target_y-e->origin_y)*(int)age/(int)denom);
                if(pack.version>=8u && dota_roshan_region_at(x,y)!=view_region)continue;
                skill_fx(pixels,camera,model,0,dir,age,0,x,y,radius);
            }
        }else if(id==7 || id==11){
            if(age<duration)skill_beam(pixels,camera,model,0,age,e->origin_x,e->origin_y,e->target_x,e->target_y,radius);
        }else if(persistent>0 && age>=duration && repeat){
            skill_fx(pixels,camera,model,1,dir,age-duration,1,x,y,radius);
        }else if(age<duration){
            skill_fx(pixels,camera,model,0,dir,age,0,x,y,radius);
        }
        impact_delay=id==4?(duration>2?duration-2:0):duration/2;
        if(impact && id!=11 && age>=impact_delay && age-impact_delay<impact->ticks &&
           (pack.version<8u || dota_roshan_region_at(e->target_x,e->target_y)==view_region))
            skill_fx(pixels,camera,model,2,dir,age-impact_delay,0,e->target_x,e->target_y,id==7?90:radius);
        if(id==14 && (unsigned)(game->tick+caster*3)%20u<4u){
            int ally;for(ally=0;ally<DOTA_HERO_COUNT;++ally){const DotaUnit *v=&game->units[ally];int dx=v->x-e->origin_x,dy=v->y-e->origin_y;
                dx=v->x-u->x;dy=v->y-u->y;
                if(ally!=caster && v->active && v->hp>0 && v->team==u->team && dx*dx+dy*dy<300*300)
                    skill_fx(pixels,camera,model,1,0,age,1,v->x,v->y,30);
            }
        }
    }
    if(pack.version>=5u)for(caster=DOTA_SUMMON_FIRST;caster<DOTA_SUMMON_FIRST+DOTA_MAX_SUMMONS;++caster){
        const DotaUnit *ward=&game->units[caster];const ClipGroup *start;unsigned age;
        if(!ward->active || ward->hp<=0 || ward->type!=DOTA_SUMMON || game->summon_life[caster-DOTA_SUMMON_FIRST]<=0)continue;
        if(pack.version>=8u && !dota_unit_visible(game,game->player,caster))continue;
        {int lifetime=dota_healing_ward_lifetime(game,game->summon_owner[caster-DOTA_SUMMON_FIRST]);
            age=(unsigned)clamp(lifetime-game->summon_life[caster-DOTA_SUMMON_FIRST],0,lifetime);}
        start=find_group(25,0,0,0);
        if(!start){++component_stats.missing_component_frames;continue;}
        if(age<start->ticks)skill_fx(pixels,camera,25,0,0,age,0,ward->x,ward->y,(dota_healing_ward_radius_source()+3)/4);
        else skill_fx(pixels,camera,25,1,0,age-start->ticks,1,ward->x,ward->y,(dota_healing_ward_radius_source()+3)/4);
    }
    /* Omnislash impacts follow actual jumps, including targets killed on hit. */
    {unsigned i,count=game->skill_event_count;
        if(count>DOTA_MAX_SKILL_EVENTS)count=DOTA_MAX_SKILL_EVENTS;
        for(i=0;i<count;++i){const DotaSkillEvent *e=dota_get_skill_event(game,game->skill_event_serial-count+1u+i);const ClipGroup *hit;int age,dir;
            if(!e || e->kind!=DOTA_SKILL_EVENT_STRIKE || e->hero_id!=DOTA_HERO_JUGGERNAUT || e->skill!=3 || !event_caster_valid(game,e))continue;
            if(pack.version>=8u && !dota_unit_visible(game,game->player,e->caster))continue;
            age=game->tick-e->started_tick;hit=find_group(27,2,0,0);
            if(!hit){++omnislash_stats.missing_frames;continue;}
            if(age<0 || (unsigned)age>=hit->ticks)continue;
            dir=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,0);
            if(age<4 && dota_omnislash_phase(game,e->caster)==2){
                unsigned before=skill_stats.effect_instances;
                skill_beam(pixels,camera,27,0,(unsigned)age,e->origin_x,e->origin_y,e->target_x,e->target_y,80);
                if(skill_stats.effect_instances>before)++omnislash_stats.trail_instances;
            }
            if(skill_fx(pixels,camera,27,2,dir,(unsigned)age,0,e->target_x,e->target_y,90))++omnislash_stats.impact_instances;
        }
    }
}
/* Models32..38 hold the seven ranged styles;39 is the neutral melee hit.
 * The flight anchor moves in rules, with depth sampled at the two endpoints.
 * This keeps a shot above river slopes instead of sticking it to the ground. */
static void projectile_fx(uint16_t *pixels,const BSCamera *camera,int style,int clip,int direction,unsigned phase,int x,int y,int z){
    int model=style==DOTA_ATTACK_SNIPER?116:style==DOTA_ATTACK_NECROPHOS?87:(style?31+style:39),index,sx,sy;uint16_t depth;
    const ClipGroup *group=find_group(model,clip,direction,0);
    if(!fog_point_visible(x,y))return;
    if(!group){++projectile_stats.missing_projectile_frames;return;}
    index=frame_at(group,phase,clip==0);bs_project(camera,x,y,z,&sx,&sy,&depth);
    if(draw_sprite(pixels,index,sx,sy,depth,256,-1)){
        ++stats.effects_drawn;projectile_stats.style_mask|=1u<<(unsigned)style;
        if(clip==0)++projectile_stats.flight_instances;
        else if(clip==1)++projectile_stats.muzzle_instances;
        else ++projectile_stats.impact_instances;
    }
}
static void render_projectiles(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    unsigned i,count=game->attack_event_count;
    if(pack.version<4u)return;
    for(i=0;i<DOTA_MAX_PROJECTILES;++i){
        const DotaProjectile *p=&game->projectiles[i];int from_z,to_z,z,progress,direction,duration;
        if(p->state!=DOTA_PROJECTILE_FLYING || p->style<1 || p->style>DOTA_ATTACK_SNIPER)continue;
        if(pack.version>=8u && dota_roshan_region_at(p->x,p->y)!=view_region)continue;
        duration=p->initial_flight_ticks>0?p->initial_flight_ticks:1;
        progress=clamp(p->age,0,duration);
        from_z=bs_ground_height(p->origin_x,p->origin_y)+p->launch_z;
        to_z=bs_ground_height(p->target_x,p->target_y)+p->aim_z;
        z=from_z+(to_z-from_z)*progress/duration;
        if(p->style==DOTA_ATTACK_DIRE_TOWER)z+=128*progress*(duration-progress)/(duration*duration);
        direction=direction_yaw(p->target_x-p->x,p->target_y-p->y,direction_yaw(p->x-p->previous_x,p->y-p->previous_y,0));
        projectile_fx(pixels,camera,p->style,0,direction,(unsigned)p->age,p->x,p->y,z);
    }
    if(count>DOTA_MAX_ATTACK_EVENTS)count=DOTA_MAX_ATTACK_EVENTS;
    for(i=0;i<count;++i){
        const DotaAttackEvent *e=dota_get_attack_event(game,game->attack_event_serial-count+1u+i);
        const ClipGroup *group;int age,clip,direction,x,y,z,model;
        if(!e || e->style<0 || e->style>DOTA_ATTACK_SNIPER || (!e->style && e->source_type!=DOTA_NEUTRAL && e->source_type!=DOTA_ANCIENT && e->source_type!=DOTA_ROSHAN))continue;
        if(pack.version>=8u && dota_roshan_region_at(e->target_x,e->target_y)!=view_region)continue;
        if(e->kind==DOTA_ATTACK_EVENT_RELEASE)clip=1;
        else if(e->kind==DOTA_ATTACK_EVENT_IMPACT)clip=2;
        else continue;
        if(!e->style && clip!=2)continue;
        age=game->tick-e->started_tick;if(age<0)continue;
        model=e->style==DOTA_ATTACK_SNIPER?116:e->style==DOTA_ATTACK_NECROPHOS?87:(e->style?31+e->style:39);group=find_group(model,clip,0,0);
        if(!group){++projectile_stats.missing_projectile_frames;continue;}
        if((unsigned)age>=group->ticks)continue;
        direction=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,0);
        x=clip==1?e->origin_x:e->target_x;y=clip==1?e->origin_y:e->target_y;
        z=bs_ground_height(x,y)+(clip==1?e->launch_z:e->aim_z);
        projectile_fx(pixels,camera,e->style,clip,direction,(unsigned)age,x,y,z);
    }
}
/* Models79..87 are baked from the original two heroes' particles, skeletons
 * and meshes. Persistent clouds/leashes follow real rule state; the skulls
 * below follow Death Pulse's actual selected targets and flight clock. */
static int extra_fx_visible(int x,int y){return (pack.version<8u || dota_roshan_region_at(x,y)==view_region)&&fog_point_visible(x,y);}
static void extra_fx_sprite(uint16_t *pixels,const BSCamera *camera,int model,int clip,int dir,unsigned phase,int loop,int x,int y,int z){
    const ClipGroup *group=find_group(model,clip,dir,0);int index,sx,sy;uint16_t depth;
    if(!group){++skill_stats.missing_skill_frames;return;}index=frame_at(group,phase,loop);if(index<0)return;
    bs_project(camera,x,y,z,&sx,&sy,&depth);if(draw_sprite(pixels,index,sx,sy,depth,256,-1)){++stats.effects_drawn;++skill_stats.effect_instances;if(clip==1)++skill_stats.loop_instances;if(clip==2)++skill_stats.impact_instances;}
}
static void extra_scythe_effect(uint16_t *pixels,const DotaGame *game,const BSCamera *camera,int at,int target,int generation,int duration){
    const DotaUnit *v;if(at<=game->tick || (unsigned)target>=DOTA_MAX_UNITS)return;v=&game->units[target];
    if(v->active && v->hp>0 && v->generation==generation && extra_fx_visible(v->x,v->y)){
        /* Original particle's camera-facing plane must show its blade face. */
        int dir=4,life=duration>0?duration:30,swing_ticks=life<13?life:13;unsigned age=(unsigned)(game->tick-(at-life));
        const ClipGroup *swing=find_group(86,0,dir,0);int swing_age=game->tick-(at-swing_ticks);
        if(swing && swing_age>=0)skill_fx(pixels,camera,86,0,dir,(unsigned)swing_age*swing->ticks/(unsigned)swing_ticks,0,v->x,v->y,0);
        skill_fx(pixels,camera,86,1,0,age,1,v->x,v->y,0);
    }
}
static void render_extra_skill_effects(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    int i,j;
    if(!find_group(77,BS_IDLE,0,1))return;
    for(i=0;i<DOTA_HERO_COUNT;++i){const DotaUnit *u=&game->units[i];const DotaExtraHero *s=&game->extra.hero[i];unsigned tick=(unsigned)game->tick;
        if(!u->active || u->type!=DOTA_HERO)continue;
        if(u->hp>0 && extra_fx_visible(u->x,u->y)){
            int actor_visible=dota_unit_visible(game,game->player,i);
            if(u->hero_id==DOTA_HERO_SLARK){
                if(actor_visible&&s->pact_at>0){const SkillTrack *q=&skill_tracks[i][0];unsigned age=q->valid?(unsigned)(game->tick-q->event.started_tick):0;
                    skill_fx(pixels,camera,79,s->pact_pulse?1:0,0,age,1,u->x,u->y,(dota_skill_effect_radius_source(game,i,0)+3)/4);
                }
                if(actor_visible&&s->leap_at>0){int dir=direction_yaw(s->leap_to_x-s->leap_x,s->leap_to_y-s->leap_y,history[i].yaw);
                    skill_fx(pixels,camera,80,0,dir,tick,1,u->x,u->y,0);
                }
                /* Detection immunity hides the body from opponents. Its
                 * original dark cloud stays visible to both teams. */
                if(s->shadow_until>game->tick)skill_fx(pixels,camera,82,1,0,tick,1,u->x,u->y,55);
            }else if(u->hero_id==DOTA_HERO_NECROPHOS&&actor_visible){
                skill_fx(pixels,camera,85,1,0,tick,1,u->x,u->y,48);
                if(s->ghost_until>game->tick)skill_fx(pixels,camera,84,1,0,tick,1,u->x,u->y,60);
                if(s->seeker_at>0 && game->tick>=s->seeker_at){int dir=direction_yaw(s->seeker_to_x-s->seeker_x,s->seeker_to_y-s->seeker_y,history[i].yaw);
                    extra_fx_sprite(pixels,camera,83,1,dir,tick,1,u->x,u->y,bs_ground_height(u->x,u->y)+32);
                }
            }
        }
        if(s->shroud_until>game->tick && extra_fx_visible(s->shroud_x,s->shroud_y))skill_fx(pixels,camera,82,1,0,tick,1,s->shroud_x,s->shroud_y,(s->shroud_radius+3)/4);
        if(s->leash_until>game->tick && u->hp>0 && extra_fx_visible(s->leash_x,s->leash_y)){
            skill_fx(pixels,camera,80,1,0,tick,1,s->leash_x,s->leash_y,100);
        }
        extra_scythe_effect(pixels,game,camera,s->scythe_at,s->scythe_target,s->scythe_generation,s->scythe_duration);
        {const DotaExtraScythe *saved=&game->extra.scythe_saved[i];extra_scythe_effect(pixels,game,camera,saved->at,saved->target,saved->generation,saved->duration);}
    }
    for(i=0;i<DOTA_EXTRA_PULSES;++i){const DotaDeathPulse *p=&game->extra.pulse[i];int caster=i%10,age=game->tick-p->at;
        if(!p->rank || age<0 || age>100 || game->units[caster].generation!=p->generation)continue;
        for(j=0;j<DOTA_MAX_UNITS;++j)if((p->targets[j>>5]&(1u<<(j&31))) && !(p->hit[j>>5]&(1u<<(j&31)))){
            const DotaUnit *v=&game->units[j];int dx=v->x-p->x,dy=v->y-p->y,d=(int)integer_sqrt((unsigned)(dx*dx+dy*dy)),travel=clamp(age*5,0,d),x,y,dir;
            if(!v->active || v->hp<=0 || (unsigned short)v->generation!=p->target_generation[j] || !d)continue;
            x=p->x+dx*travel/d;y=p->y+dy*travel/d;if(!extra_fx_visible(x,y))continue;
            dir=direction_yaw(dx,dy,0);extra_fx_sprite(pixels,camera,83,1,dir,(unsigned)age,1,x,y,bs_ground_height(x,y)+30);
        }
    }
    {unsigned k,count=game->skill_event_count;if(count>DOTA_MAX_SKILL_EVENTS)count=DOTA_MAX_SKILL_EVENTS;
        for(k=0;k<count;++k){const DotaSkillEvent *e=dota_get_skill_event(game,game->skill_event_serial-count+1u+k);int age,model,clip=2,dir,x,y;const ClipGroup *group;
            if(!e || e->hero_id<4 || e->hero_id>=6 || !event_caster_valid(game,e))continue;
            age=game->tick-e->started_tick;if(age<0)continue;model=79+(e->hero_id-4)*4+e->skill;
            if(e->kind==DOTA_SKILL_EVENT_STRIKE){if(e->hero_id==4 && e->skill==0)continue;x=e->target_x;y=e->target_y;}
            else if(e->kind==DOTA_SKILL_EVENT_CAST){if(e->skill==0 || (e->hero_id==4 && e->skill==1))continue;clip=0;x=e->origin_x;y=e->origin_y;}
            else continue;
            if(!extra_fx_visible(x,y))continue;
            dir=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,0);group=find_group(model,clip,dir,0);
            if(e->hero_id==DOTA_HERO_NECROPHOS && e->skill==3){dir=4;group=find_group(model,clip,dir,0);}
            if(group && (unsigned)age<group->ticks)skill_fx(pixels,camera,model,clip,dir,(unsigned)age,0,x,y,0);
        }
    }
}
#include "roster_baked.h"
void bs_render_effects(uint16_t *pixels,const DotaGame *game,const BSCamera *camera){
    int i;if(!ready || !pixels || !game || !camera)return;
    for(i=0;i<DOTA_MAX_EFFECTS;++i){
        const DotaEffect *effect=&game->effects[i];const ClipGroup *group;const RuntimeFrame *frame;
        EffectHistory *old=&effect_history[i];
        int model,dir,sx,sy,wx,wy,z,index,scale=256;uint16_t depth;
        if(game->skill_effect_tags[i]>0 && game->skill_effect_tags[i]<=16u && find_group(15+(int)game->skill_effect_tags[i],0,0,0)){old->active=0;continue;}
        if(!effect->active || effect->kind<1 || effect->kind>6){old->active=0;continue;}
        if(pack.version>=8u && dota_roshan_region_at(effect->x,effect->y)!=view_region){old->active=0;continue;}
        if(!old->active || old->kind!=effect->kind || old->x!=effect->x || old->y!=effect->y ||
           old->to_x!=effect->to_x || old->to_y!=effect->to_y || effect->ticks>old->ticks || old->start_tick>game->tick){
            old->start_tick=game->tick;old->initial_ticks=effect->ticks>0?effect->ticks:1;
        }
        old->active=1;old->kind=effect->kind;old->x=effect->x;old->y=effect->y;
        old->to_x=effect->to_x;old->to_y=effect->to_y;old->ticks=effect->ticks;
        model=9+effect->kind;dir=direction_yaw(effect->to_x-effect->x,effect->to_y-effect->y,0);
        group=find_group(model,BS_IDLE,dir,0);if(!group)continue;
        index=frame_at(group,(unsigned)(game->tick-old->start_tick)*group->ticks/(unsigned)old->initial_ticks,0);frame=frame_info((unsigned)index);
        wx=effect->x;wy=effect->y;
        if(effect->kind==DOTA_FX_LINE){
            int dx=effect->to_x-effect->x,dy=effect->to_y-effect->y;
            wx=(effect->x+effect->to_x)/2;wy=(effect->y+effect->to_y)/2;
            if(rf_world_height(frame))scale=clamp((int)integer_sqrt((unsigned)(dx*dx+dy*dy))*128/rf_world_height(frame),32,2048);
        }else{
            if(effect->kind==DOTA_FX_ATTACK){wx=(effect->x+effect->to_x)/2;wy=(effect->y+effect->to_y)/2;}
            if(rf_world_height(frame) && effect->radius>0)scale=clamp(effect->radius*256/rf_world_height(frame),32,768);
        }
        z=bs_ground_height(wx,wy)+3;bs_project(camera,wx,wy,z,&sx,&sy,&depth);
        if(draw_sprite(pixels,index,sx,sy,depth,scale,-1))++stats.effects_drawn;
    }
    render_skill_effects(pixels,game,camera);
    render_extra_skill_effects(pixels,game,camera);
    render_roster_effects(pixels,game,camera);
    render_projectiles(pixels,game,camera);
}
int bs_draw_menu_hero(uint16_t *pixels,int hero,int direction,unsigned tick,int x,int y,int width,int height){
    const ClipGroup *group;const RuntimeFrame *frame;const unsigned char *data,*alpha;
    int index,scale,draw_w,draw_h,left,top,xx,yy;unsigned n;
    fog_sprite_gate=0;fog_game=0;fog_camera=0;
    if(!ready || !pixels || hero<0 || hero>=DOTA_HERO_TYPES || width<1 || height<1)return 0;
    group=find_group(hero_model(hero),BS_IDLE,direction,1);index=frame_at(group,tick,1);
    if(index<0)return 0;
    frame=frame_info((unsigned)index);data=get_sprite((unsigned)index);if(!data)return 0;
    n=(unsigned)rf_width(frame)*rf_height(frame);alpha=data+n*2;
    scale=width*256/rf_width(frame);if(scale>height*256/rf_height(frame))scale=height*256/rf_height(frame);
    if(scale<1)return 0;
    draw_w=rf_width(frame)*scale/256;draw_h=rf_height(frame)*scale/256;
    left=x+(width-draw_w)/2;top=y+(height-draw_h)/2;
    for(yy=0;yy<draw_h;++yy)for(xx=0;xx<draw_w;++xx){
        int dx=left+xx,dy=top+yy;unsigned source=(unsigned)(yy*256/scale)*rf_width(frame)+(unsigned)(xx*256/scale),a;
        uint16_t color;if(dx<0 || dx>=BS_WIDTH || dy<0 || dy>=272)continue;a=alpha[source];if(!a)continue;
        color=read_u16(data+source*2);pixels[dy*BS_WIDTH+dx]=a==255?color:mix(color,pixels[dy*BS_WIDTH+dx],a,0);
    }
    return 1;
}
