#ifndef DOTA_NATIVE_AUDIO_H
#define DOTA_NATIVE_AUDIO_H
#include <stdint.h>
#include "../game/game.h"
enum { DOTA_AUDIO_CLICK, DOTA_AUDIO_CONFIRM, DOTA_AUDIO_GOLD, DOTA_AUDIO_KILL,
       DOTA_AUDIO_SELECT, DOTA_AUDIO_WATER };
typedef struct {
 uint32_t ready,error,music_asset,queued,played_frames,underruns,read_calls,read_bytes;
 uint32_t music_blocks,fx_loads,fx_hits,fx_dropped,events,stale_events,loops;
 uint32_t initialized,destroyed,heap_bytes,fx_cache_bytes,active_effects,closes,encoded_cache_bytes;
 uint32_t music_restarts,voice_restarts,device_restarts,retry_attempts,last_runtime_error;
 uint32_t music_changes,silent_groups_skipped;
} DotaAudioStats;
/* Optional service: failure leaves the application playable and silent.
 * update must run every UI iteration, including menu/pause, not only logic ticks.
 * Screen numbers match DotaView. Neither update nor feedback mutates Game/RNG. */
int dota_audio_open(void);
void dota_audio_close(void);
void dota_audio_update(const DotaGame *game,int screen,int selected_hero);
unsigned dota_audio_preload_hero(unsigned hero);
/* Optional resident compressed SFX (size reported by reserve/stats). A failed request keeps
 * bounded streaming/PCM-LRU fallback.0 releases; close also releases. */
unsigned dota_audio_reserve_effects(unsigned max_bytes);
void dota_audio_ui(unsigned event);
void dota_audio_purchase(int succeeded);
void dota_audio_set_volume(unsigned music_percent,unsigned effects_percent);
/* Audio-consumer clock for the native caller's optional pacing calibration.
 * Returns0 if unsupported/stalled; never silently replaces the game's clock. */
int dota_audio_clock_ms(uint32_t *milliseconds);
const DotaAudioStats *dota_audio_stats(void);
unsigned dota_audio_decode_block(const uint8_t *encoded,unsigned bytes,int16_t *pcm,unsigned frames);
extern volatile uint32_t h1_dota_audio_debug[32];

#ifdef DOTA_AUDIO_HOST
/* Host tests inject a real file/allocator plus a descriptor-consuming firmware
 * model. Native builds resolve only the verified V1.41 service instructions. */
typedef struct {
 void *(*file_open)(const char *);uint32_t (*file_size)(void *);
 int (*file_read_at)(void *,uint32_t,void *,unsigned);void (*file_close)(void *);
 void *(*alloc)(unsigned);void (*free)(void *);
 int (*descriptor_init)(void *);void (*descriptor_destroy)(void *);
 int (*device_open)(const uint32_t *);int (*device_start)(void);
 void (*device_stop)(void);void (*device_close)(void);
 int (*submit)(unsigned,void *,int,int);
 int (*position)(unsigned,void *,unsigned *);uint32_t (*tick80)(void);
} DotaAudioHostServices;
void dota_audio_host_services(const DotaAudioHostServices *services);
#endif
#endif
