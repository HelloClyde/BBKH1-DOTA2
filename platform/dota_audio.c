/* Original Dota assets, integer IMA decoding, verified H1 V1.41 PCM services.
 * Descriptor/queue lifecycle follows the locally tested native H1 family in
 * vendor/h1-sdk/reverse/docs/audio_api.md. No audio/DMA register writes. */
#include "dota_audio.h"
#include "../private/audio/assets/audio_data.h"
#include "audio_music_windows.h"
#include <stddef.h>
#ifndef DOTA_AUDIO_HOST
#include "h1_sdk.h"
#include "cache_memory.h"
#endif
#define RATE 32000u
#define BLOCK 256u
#define BLOCK_FRAMES 505u
#define RING_FRAMES 32768u
#define TARGET_FRAMES 24576u
#define FX_BUDGET (2u*1024u*1024u)
#define FX_SLOTS 32u
#define VOICES 2u
#define NONE 0xffffffffu
typedef struct {uint32_t offset,bytes,frames,loop,gain,family,crc,source;} AudioEntry;
typedef char Entry32[sizeof(AudioEntry)==32?1:-1];
typedef struct __attribute__((aligned(32))) {int16_t *pcm;uint32_t bytes,private_words[6];} Descriptor;
#ifndef DOTA_AUDIO_HOST
typedef char Descriptor32[sizeof(Descriptor)==32?1:-1];
#endif
typedef struct {void *allocation;int16_t *pcm;uint32_t asset,gain,bytes,stamp,refs;} EffectCache;
typedef struct {Descriptor descriptor;uint32_t initialized,cache,stamp,priority;} Voice;
typedef struct {void *handle;uint32_t size,position;} AudioFile;
static AudioEntry entries[DOTA_AUDIO_ASSET_COUNT];
static EffectCache cache[FX_SLOTS];static Voice voices[VOICES];
static Descriptor music_descriptor;static AudioFile music_file,fx_file;
static void *ring_allocation;static int16_t *ring;
static uint8_t music_encoded[4096],effect_encoded[4096];
static int16_t music_decoded[BLOCK_FRAMES];
static unsigned device_owned,music_initialized,music_playing,music_wanted=NONE;
static unsigned queued,write_at,read_at,music_frame,music_block,decoded_at,decoded_count;
static unsigned encoded_at,encoded_count,music_ended;
static unsigned clock_tick,retire_tick,progress_tick,progress_at,stamp;
static unsigned music_volume=35,effects_volume=80,random_audio=0x41445531u;
static unsigned last_screen=NONE,last_game_tick=NONE,last_skill,last_attack,last_hero=NONE;
static const DotaGame *last_game;
static int last_hp[DOTA_HERO_COUNT],last_generation[DOTA_HERO_COUNT],last_gold,last_kills,last_water;
static unsigned skill_sound_at[DOTA_HERO_COUNT][5][2],cast_sound_serial[DOTA_HERO_COUNT][5],strike_sound_serial[DOTA_HERO_COUNT][5],battle_until;
static unsigned audio_enabled,retry_pending,retry_at,retry_wait=80,music_faults;
static unsigned music_resume_frame[5];
static DotaAudioStats status;
typedef char AudioDebugCapacity[(sizeof(DotaAudioStats)/4u+1u<=32u)?1:-1];
volatile uint32_t h1_dota_audio_debug[32];
static void zero(void *p,unsigned bytes){uint8_t*b=p;while(bytes--)*b++=0;}
static int clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static uint32_t crc32(const void *p,unsigned n){const uint8_t*b=p;uint32_t c=~0u;while(n--){c^=*b++;for(unsigned k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static unsigned heap_used;
static uint8_t *encoded_effects;static unsigned encoded_effect_base,encoded_effect_bytes;
static void update_debug(void){
 status.ready=device_owned;status.music_asset=music_wanted;status.queued=queued;
 status.heap_bytes=heap_used;status.active_effects=0;
 for(unsigned i=0;i<VOICES;i++)status.active_effects+=voices[i].initialized!=0;
 const uint32_t *p=(const uint32_t*)&status;h1_dota_audio_debug[0]=0x31415544u;
 for(unsigned i=0;i<sizeof(status)/4;i++)h1_dota_audio_debug[i+1]=p[i];
}
#ifdef DOTA_AUDIO_HOST
static DotaAudioHostServices host;
void dota_audio_host_services(const DotaAudioHostServices*s){if(s)host=*s;}
static void *allocate(unsigned n){void*p=host.alloc?host.alloc(n):0;if(p)heap_used+=n;return p;}
static void release(void*p,unsigned n){if(p){host.free(p);heap_used-=n;}}
static uint32_t tick80(void){return host.tick80?host.tick80():0;}
static int descriptor_init(Descriptor*d){return host.descriptor_init&&host.descriptor_init(d);}
static void descriptor_destroy(Descriptor*d){host.descriptor_destroy(d);}
static int submit(unsigned route,Descriptor*d,int repeats,int flags){return host.submit&&host.submit(route,d,repeats,flags);}
static int position(unsigned route,Descriptor*d,unsigned*p){return host.position?host.position(route,d,p):-1;}
static int open_file(AudioFile*f,const char*p){f->handle=host.file_open?host.file_open(p):0;f->size=f->handle?host.file_size(f->handle):0;return f->handle&&f->size;}
static void close_file(AudioFile*f){if(f->handle)host.file_close(f->handle);f->handle=0;f->size=0;}
static int file_read(AudioFile*f,unsigned at,void*p,unsigned n){status.read_calls++;status.read_bytes+=n;return f->handle&&at<=f->size&&n<=f->size-at&&host.file_read_at(f->handle,at,p,n);}
static int open_device(void){uint32_t c[9]={RATE,1,4096,0,0,0,0,0,0};return host.device_open&&host.device_open(c);}
static int start_device(void){return host.device_start&&host.device_start();}
static void stop_device(void){if(host.device_stop)host.device_stop();}
static void close_device(void){if(host.device_close)host.device_close();}
static int resolve_services(void){return host.alloc&&host.free&&host.file_open&&host.file_size&&host.file_close&&host.file_read_at&&host.descriptor_init&&host.descriptor_destroy&&host.device_open&&host.device_start&&host.device_stop&&host.device_close&&host.submit&&host.position&&host.tick80;}
#else
typedef int(*DescriptorFn)(Descriptor*);typedef int(*DeviceFn)(void);
typedef int(*OpenFn)(uint32_t*);typedef int(*SubmitFn)(int,Descriptor*,int,int);
static DescriptorFn initialize,destroy;static DeviceFn start,stop,close_output;
static OpenFn output_open;static SubmitFn output_submit;static volatile uint32_t *queue;
static void *service(unsigned off){return h1_runtime_entry(h1_runtime_table(H1_RUNTIME_SYS_TABLE_SLOT),off);}
static void *allocate(unsigned n){void*p=cache_memory_alloc(n);if(p)heap_used+=n;return p;}
static void release(void*p,unsigned n){if(p){cache_memory_free(p);heap_used-=n;}}
static uint32_t tick80(void){return h1_raw_tick_80hz();}
static int descriptor_init(Descriptor*d){return initialize(d);}
static void descriptor_destroy(Descriptor*d){destroy(d);}
static int submit(unsigned route,Descriptor*d,int repeats,int flags){return output_submit((int)route,d,repeats,flags);}
static int position(unsigned route,Descriptor*d,unsigned*out){
 /* The output worker can retire/replace a node between any two reads.
  * Accept only a stable owner/head snapshot; never trust a torn cursor. */
 for(unsigned attempt=0;attempt<3;++attempt){
  uint32_t address=queue[route];if(!address)return 0;
  if((address&3u)||address<0x80000000u||address>0x83fffff0u)return -1;
  volatile uint32_t *node=(volatile uint32_t*)address;
  uint32_t owner=node[1],cursor=node[2],base=(uint32_t)d->pcm;
  if(address!=queue[route]||owner!=node[1])continue;
  if(owner!=(uint32_t)d)return -1;
  if(!d->bytes||cursor<base||cursor>base+d->bytes||(cursor&1u))continue;
  *out=((cursor-base)/2u)%(d->bytes/2u);return 1;
 }return -1;
}
static int open_file(AudioFile*f,const char*p){f->handle=h1_fopen(p,"rb");if(!f->handle)return 0;
 if(h1_fseek(f->handle,0,H1_SEEK_END)<0){h1_fclose(f->handle);f->handle=0;return 0;}
 f->size=h1_ftell(f->handle);f->position=f->size;return f->size&&f->size<0x7fffffffu;}
static void close_file(AudioFile*f){if(f->handle)h1_fclose(f->handle);f->handle=0;f->size=f->position=0;}
static int file_read(AudioFile*f,unsigned at,void*p,unsigned n){status.read_calls++;status.read_bytes+=n;
 if(!f->handle||at>f->size||n>f->size-at)return 0;
 if(f->position!=at&&h1_fseek(f->handle,(int)at,H1_SEEK_SET)!=(int)at){f->position=NONE;return 0;}
 if(h1_fread(p,1,n,f->handle)!=n){f->position=NONE;return 0;}
 f->position=at+n;return 1;}
static int open_device(void){uint32_t c[9]={RATE,1,4096,0,0,0,0,0,0};return output_open(c);}
static int start_device(void){return start();}static void stop_device(void){stop();}static void close_device(void){close_output();}
static int resolve_services(void){
 initialize=(DescriptorFn)service(0x50);destroy=(DescriptorFn)service(0x54);output_open=(OpenFn)service(0x58);
 output_submit=(SubmitFn)service(0x5c);start=(DeviceFn)service(0x60);stop=(DeviceFn)service(0x64);close_output=(DeviceFn)service(0x68);
 if(!initialize||!destroy||!output_open||!output_submit||!start||!stop||!close_output)return 0;
 const uint32_t *code=(const uint32_t*)output_submit;
 if(code[1]!=0x28820008u||(code[0x70/4]&0xffff0000u)!=0x3c060000u||
    (code[0x74/4]&0xffff0000u)!=0x24c60000u||code[0x78/4]!=0x00c43021u)return 0;
 uint32_t address=((code[0x70/4]&65535u)<<16)+(int16_t)code[0x74/4];
 if((address&3u)||address<0x80000000u||address>=0x84000000u)return 0;
 queue=(volatile uint32_t*)address;return 1;
}
#endif
static const int step_table[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int index_delta[8]={-1,-1,-1,-1,2,4,6,8};
unsigned dota_audio_decode_block(const uint8_t*in,unsigned bytes,int16_t*out,unsigned n){
 if(!in||!out||bytes!=BLOCK||!n||in[2]>88||in[3])return 0;
 if(n>BLOCK_FRAMES)n=BLOCK_FRAMES;
 int value=(int16_t)((unsigned)in[0]|(unsigned)in[1]<<8),index=in[2];out[0]=(int16_t)value;
 for(unsigned i=1;i<n;i++){unsigned c=(in[4+(i-1)/2]>>(((i-1)&1)*4))&15u;
  int diff=(step_table[index]*(2*(int)(c&7)+1))>>3;value=clamp(value+((c&8)?-diff:diff),-32768,32767);
  index=clamp(index+index_delta[c&7],0,88);out[i]=(int16_t)value;
 }return n;
}
static int cancel_voice(unsigned voice){
 Voice*v=&voices[voice];if(!v->initialized)return 1;
 if(device_owned&&!submit(voice+1,0,0,0))return 0;
 descriptor_destroy(&v->descriptor);status.destroyed++;
 if(v->cache<FX_SLOTS&&cache[v->cache].refs)cache[v->cache].refs--;
 v->initialized=0;v->cache=NONE;return 1;
}
static void free_effect(unsigned slot){
 EffectCache*c=&cache[slot];if(!c->allocation)return;
 release(c->allocation,c->bytes+31u);status.fx_cache_bytes-=c->bytes;
 zero(c,sizeof(*c));c->asset=NONE;
}
static void shutdown_audio(void){
 if(music_wanted<5u)music_resume_frame[music_wanted]=music_frame;
 if(device_owned){stop_device();submit(0,0,0,0);for(unsigned i=0;i<VOICES;i++)submit(i+1,0,0,0);close_device();device_owned=0;}
 for(unsigned i=0;i<VOICES;i++){if(voices[i].initialized){descriptor_destroy(&voices[i].descriptor);status.destroyed++;}zero(&voices[i],sizeof(voices[i]));voices[i].cache=NONE;}
 if(music_initialized){descriptor_destroy(&music_descriptor);status.destroyed++;music_initialized=0;}
 for(unsigned i=0;i<FX_SLOTS;i++)free_effect(i);
 if(encoded_effects)release(encoded_effects,encoded_effect_bytes);
 encoded_effects=0;encoded_effect_bytes=encoded_effect_base=0;status.encoded_cache_bytes=0;
 if(ring_allocation)release(ring_allocation,RING_FRAMES*2u+31u);
 ring_allocation=0;ring=0;
 close_file(&music_file);close_file(&fx_file);music_playing=queued=write_at=read_at=0;music_wanted=NONE;
 last_game=0;last_game_tick=NONE;last_screen=NONE;last_hero=NONE;status.closes++;update_debug();
}
void dota_audio_close(void){audio_enabled=retry_pending=0;music_faults=0;shutdown_audio();}
static int error_stop(unsigned code){
 unsigned retry=audio_enabled&&code!=1&&code!=3&&code!=10;
 status.error=code;status.last_runtime_error=code;shutdown_audio();
 if(retry){retry_pending=1;retry_at=tick80()+retry_wait;}else{audio_enabled=retry_pending=0;}
 return 0;
}
int dota_audio_open(void){
 dota_audio_close();zero(&status,sizeof(status));zero(cache,sizeof(cache));zero(voices,sizeof(voices));
 zero(music_resume_frame,sizeof(music_resume_frame));
 audio_enabled=1;retry_wait=80;music_faults=0;
 for(unsigned i=0;i<FX_SLOTS;i++)cache[i].asset=NONE;
 for(unsigned i=0;i<VOICES;i++)voices[i].cache=NONE;
 if(!resolve_services())return error_stop(1);
 const char *path="A:\\DOTA2\\SOUND.H1A";
 if(!open_file(&music_file,path)){close_file(&music_file);path="B:\\DOTA2\\SOUND.H1A";if(!open_file(&music_file,path))return error_stop(2);}
 if(!open_file(&fx_file,path)||fx_file.size!=music_file.size)return error_stop(2);
 uint32_t h[16];if(!file_read(&music_file,0,h,64)||h[0]!=0x31414448u||h[1]!=1||h[2]!=music_file.size||h[2]!=DOTA_AUDIO_PACK_BYTES||
    h[3]!=DOTA_AUDIO_ASSET_COUNT||h[4]!=64||h[5]!=32||h[7]!=RATE||h[8]!=BLOCK||h[9]!=BLOCK_FRAMES||h[11])return error_stop(3);
 if(h[3]>512||h[6]<64+h[3]*32u||h[6]>h[2]||!file_read(&music_file,64,entries,sizeof(entries))||crc32(entries,sizeof(entries))!=h[10])return error_stop(3);
 for(unsigned i=0;i<DOTA_AUDIO_ASSET_COUNT;i++){const AudioEntry*e=&entries[i];
  if(e->offset<h[6]||e->offset>h[2]||e->bytes>h[2]-e->offset||e->offset%BLOCK||!e->bytes||e->bytes%BLOCK||
     !e->frames||e->frames>e->bytes/BLOCK*BLOCK_FRAMES||e->bytes/BLOCK*BLOCK_FRAMES-e->frames>=BLOCK_FRAMES||e->loop>1||e->gain>256||e->family>1||(e->family==1&&e->frames>FX_BUDGET/2u))return error_stop(3);
 }
 for(unsigned i=0;i<5u;i++)if(entries[i].family||audio_music_windows[i].first>=audio_music_windows[i].end||audio_music_windows[i].end>entries[i].frames)return error_stop(3);
 ring_allocation=allocate(RING_FRAMES*2u+31u);if(!ring_allocation)return error_stop(4);
 ring=(int16_t*)(((uintptr_t)ring_allocation+31u)&~(uintptr_t)31u);zero(ring,RING_FRAMES*2u);zero(&music_descriptor,sizeof(music_descriptor));
 music_descriptor.pcm=ring;music_descriptor.bytes=RING_FRAMES*2u;
 if(!descriptor_init(&music_descriptor))return error_stop(5);
 music_initialized=1;status.initialized++;
 if(!open_device())return error_stop(6);
 device_owned=1;
 if(!start_device())return error_stop(7);
 clock_tick=retire_tick=progress_tick=tick80();progress_at=0;stamp=0;last_screen=NONE;last_game_tick=NONE;music_wanted=NONE;
 update_debug();return 1;
}
static void retry_output(void){
 DotaAudioStats before;unsigned wait=retry_wait,now=tick80(),resume[5];int opened;
 if(!audio_enabled||!retry_pending||(int32_t)(now-retry_at)<0)return;
 before=status;for(unsigned i=0;i<5u;i++)resume[i]=music_resume_frame[i];retry_pending=0;opened=dota_audio_open();
 for(unsigned i=0;i<5u;i++)music_resume_frame[i]=resume[i];
 /* Preserve the public playback clock and lifetime ownership counters.
  * The native frame clock must never see playback time jump backwards. */
#define KEEP_COUNTER(field) status.field+=before.field
 KEEP_COUNTER(played_frames);KEEP_COUNTER(underruns);KEEP_COUNTER(read_calls);KEEP_COUNTER(read_bytes);
 KEEP_COUNTER(music_blocks);KEEP_COUNTER(fx_loads);KEEP_COUNTER(fx_hits);KEEP_COUNTER(fx_dropped);
 KEEP_COUNTER(events);KEEP_COUNTER(stale_events);KEEP_COUNTER(loops);KEEP_COUNTER(initialized);
 KEEP_COUNTER(destroyed);KEEP_COUNTER(closes);KEEP_COUNTER(music_restarts);KEEP_COUNTER(voice_restarts);
 KEEP_COUNTER(device_restarts);KEEP_COUNTER(retry_attempts);
 KEEP_COUNTER(music_changes);KEEP_COUNTER(silent_groups_skipped);
#undef KEEP_COUNTER
 status.retry_attempts++;
 if(!status.last_runtime_error)status.last_runtime_error=before.last_runtime_error;
 if(opened){status.device_restarts++;status.error=0;}
 else if(retry_pending){retry_wait=wait<640?wait*2:640;retry_at=now+retry_wait;}
 update_debug();
}
static void seek_music(unsigned frame){
 const DotaAudioMusicWindow*w=&audio_music_windows[music_wanted];
 if(frame<w->first||frame>=w->end)frame=w->first;
 music_frame=frame;music_block=frame/BLOCK_FRAMES;
 decoded_at=decoded_count=encoded_at=encoded_count=music_ended=0;
}
static int change_music(unsigned asset){
 if(asset==music_wanted)return 1;
 if(asset>=5u)return error_stop(3);
 if(music_wanted<5u)music_resume_frame[music_wanted]=music_frame;
 /* Let already queued samples finish. Switching the decoder never cancels
  * the live route or clears its PCM, and returning to a track resumes it. */
 music_wanted=asset;seek_music(music_resume_frame[asset]);status.music_changes++;return 1;
}
static int restart_music(unsigned code){
 status.last_runtime_error=code;
 if(++music_faults>2)return error_stop(code);
 if(!submit(0,0,0,0))return error_stop(8);
 music_playing=queued=write_at=read_at=0;zero(ring,RING_FRAMES*2u);
 retire_tick=progress_tick=tick80();progress_at=0;status.music_restarts++;return 1;
}
static int music_sample(int16_t*out){
 AudioEntry*e=&entries[music_wanted];
 if(music_frame>=audio_music_windows[music_wanted].end){if(!e->loop){music_ended=1;return 0;}seek_music(audio_music_windows[music_wanted].first);status.loops++;}
 if(decoded_at>=decoded_count){
  if(encoded_at>=encoded_count){unsigned at=music_block*BLOCK,n=e->bytes-at;if(n>sizeof(music_encoded))n=sizeof(music_encoded);
   if(!n||!file_read(&music_file,e->offset+at,music_encoded,n))return error_stop(9);
   encoded_at=0;encoded_count=n;
  }
  decoded_count=dota_audio_decode_block(music_encoded+encoded_at,BLOCK,music_decoded,BLOCK_FRAMES);
  if(decoded_count!=BLOCK_FRAMES)return error_stop(10);
  encoded_at+=BLOCK;music_block++;decoded_at=music_frame%BLOCK_FRAMES;status.music_blocks++;
 }
 *out=(int16_t)((int)music_decoded[decoded_at++]*(int)e->gain*(int)music_volume/(256*100));music_frame++;return 1;
}
static void pump_music(void){
 unsigned now=tick80();if(now==clock_tick)return;clock_tick=now;
 if(music_playing){unsigned pos=0;int r=position(0,&music_descriptor,&pos);if(r!=1){restart_music(11);return;}
  unsigned consumed=(pos+RING_FRAMES-read_at)%RING_FRAMES;
  if((unsigned)(now-retire_tick)>=RING_FRAMES*80u/RATE||consumed>queued){
   if(!music_ended)status.underruns++;
   status.played_frames+=music_ended?queued:0;
   if(!submit(0,0,0,0)){error_stop(8);return;}
   music_playing=queued=write_at=read_at=0;zero(ring,RING_FRAMES*2u);
  }else{for(unsigned i=0;i<consumed;i++)ring[(read_at+i)%RING_FRAMES]=0;read_at=pos;queued-=consumed;status.played_frames+=consumed;}
  retire_tick=now;
  if(pos!=progress_at){progress_at=pos;progress_tick=now;music_faults=0;}else if((unsigned)(now-progress_tick)>80){restart_music(12);return;}
 }
 if(music_wanted<DOTA_AUDIO_ASSET_COUNT){unsigned work=0;
  while(device_owned&&queued<TARGET_FRAMES&&!music_ended&&work++<TARGET_FRAMES){int16_t sample;if(!music_sample(&sample))break;ring[write_at]=sample;write_at=(write_at+1)%RING_FRAMES;queued++;}
  if(device_owned&&!music_playing&&queued){if(!submit(0,&music_descriptor,0,1)){error_stop(8);return;}music_playing=1;retire_tick=progress_tick=now;progress_at=read_at;}
  if(device_owned&&music_ended&&!queued&&music_playing){submit(0,0,0,0);music_playing=0;}
 }
}
unsigned dota_audio_reserve_effects(unsigned budget){
 if(!budget){if(encoded_effects)release(encoded_effects,encoded_effect_bytes);encoded_effects=0;encoded_effect_bytes=0;status.encoded_cache_bytes=0;return 0;}
 if(encoded_effects)return encoded_effect_bytes;
 if(!device_owned)return 0;
 unsigned first=NONE,end=0;
 for(unsigned i=0;i<DOTA_AUDIO_ASSET_COUNT;i++)if(entries[i].family==1){if(entries[i].offset<first)first=entries[i].offset;if(entries[i].offset+entries[i].bytes>end)end=entries[i].offset+entries[i].bytes;}
 if(first==NONE||end<=first||end-first>budget)return 0;
 unsigned n=end-first;uint8_t *memory=allocate(n);if(!memory)return 0;
 for(unsigned at=0;at<n;){unsigned take=n-at;if(take>65536u)take=65536u;
  if(!file_read(&fx_file,first+at,memory+at,take)){release(memory,n);status.error=17;return 0;}at+=take;
 }
 for(unsigned i=0;i<DOTA_AUDIO_ASSET_COUNT;i++)if(entries[i].family==1&&crc32(memory+entries[i].offset-first,entries[i].bytes)!=entries[i].crc){release(memory,n);status.error=14;return 0;}
 encoded_effects=memory;encoded_effect_base=first;encoded_effect_bytes=n;status.encoded_cache_bytes=n;update_debug();return n;
}
static int effect_cache(unsigned asset,unsigned gain,int evict){
 unsigned slot=FX_SLOTS,oldest=NONE;AudioEntry*e=&entries[asset];
 if(!device_owned||!gain||!effects_volume)return -1;
 for(unsigned i=0;i<FX_SLOTS;i++)if(cache[i].allocation&&cache[i].asset==asset&&cache[i].gain==gain){cache[i].stamp=++stamp;status.fx_hits++;return (int)i;}
 pump_music();if(!device_owned)return -1;
 unsigned bytes=e->frames*2u;if(e->family!=1||bytes>FX_BUDGET)return -1;
 while(status.fx_cache_bytes+bytes>FX_BUDGET){unsigned victim=FX_SLOTS;oldest=NONE;
  if(!evict)return -1;
  for(unsigned i=0;i<FX_SLOTS;i++)if(cache[i].allocation&&!cache[i].refs&&cache[i].stamp<oldest){oldest=cache[i].stamp;victim=i;}
  if(victim==FX_SLOTS)return -1;
  free_effect(victim);
 }
 oldest=NONE;for(unsigned i=0;i<FX_SLOTS;i++){if(!cache[i].allocation){slot=i;break;}if(!cache[i].refs&&cache[i].stamp<oldest){oldest=cache[i].stamp;slot=i;}}
 if(slot==FX_SLOTS)return -1;
 if(cache[slot].allocation){if(!evict)return -1;free_effect(slot);}
 void*allocation=allocate(bytes+31u);if(!allocation)return -1;
 int16_t*pcm=(int16_t*)(((uintptr_t)allocation+31u)&~(uintptr_t)31u);unsigned frames=0,block=0;uint32_t c=~0u;int combined=(int)(e->gain*gain*effects_volume/(256u*100u));
 unsigned encoded_pos=0,encoded_n=0;
 while(frames<e->frames){unsigned n=e->frames-frames;if(n>BLOCK_FRAMES)n=BLOCK_FRAMES;
  const uint8_t *encoded;
  if(encoded_effects)encoded=encoded_effects+e->offset-encoded_effect_base+block*BLOCK;
  else{
   if(encoded_pos>=encoded_n){encoded_n=e->bytes-block*BLOCK;if(encoded_n>sizeof(effect_encoded))encoded_n=sizeof(effect_encoded);
    int read_ok=file_read(&fx_file,e->offset+block*BLOCK,effect_encoded,encoded_n);
    pump_music();if(!device_owned){release(allocation,bytes+31u);return -1;}
    if(!read_ok){release(allocation,bytes+31u);status.error=13;return -1;}
    encoded_pos=0;
   }
   encoded=effect_encoded+encoded_pos;
  }
  if(dota_audio_decode_block(encoded,BLOCK,pcm+frames,n)!=n){release(allocation,bytes+31u);status.error=13;return -1;}
  for(unsigned k=0;k<BLOCK;k++){c^=encoded[k];for(unsigned j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
  for(unsigned k=0;k<n;k++){pcm[frames+k]=(int16_t)clamp((int)pcm[frames+k]*combined/256,-32768,32767);}
  frames+=n;block++;encoded_pos+=BLOCK;
  pump_music();if(!device_owned){release(allocation,bytes+31u);return -1;}
 }
 if((~c)!=e->crc){release(allocation,bytes+31u);status.error=14;return -1;}
 cache[slot].allocation=allocation;cache[slot].pcm=pcm;cache[slot].bytes=bytes;cache[slot].asset=asset;cache[slot].gain=gain;cache[slot].stamp=++stamp;
 status.fx_cache_bytes+=bytes;status.fx_loads++;return (int)slot;
}
static void retire_voices(void){for(unsigned i=0;i<VOICES;i++)if(voices[i].initialized){unsigned p;int r=position(i+1,&voices[i].descriptor,&p);
 if(r<=0){if(r<0){status.last_runtime_error=15;status.voice_restarts++;}if(!cancel_voice(i)){error_stop(8);return;}}
}}
static void play_group(unsigned group,unsigned priority){
 if(!device_owned||group>=DOTA_AUDIO_GROUP_COUNT||!effects_volume)return;
 if(!audio_groups[group].gain){status.silent_groups_skipped++;return;}
 unsigned n=audio_groups[group].count;if(!n)return;random_audio=random_audio*1664525u+1013904223u;
 unsigned asset=audio_group_assets[audio_groups[group].first+random_audio%n],voice=VOICES,oldest=NONE;
 retire_voices();if(!device_owned)return;
 for(unsigned i=0;i<VOICES;i++){if(!voices[i].initialized){voice=i;break;}if(voices[i].priority<=priority&&voices[i].stamp<oldest){oldest=voices[i].stamp;voice=i;}}
 if(voice==VOICES){status.fx_dropped++;return;}
 if(!cancel_voice(voice)){error_stop(8);return;}
 int slot=effect_cache(asset,audio_groups[group].gain,1);if(slot<0){status.fx_dropped++;return;}
 Voice*v=&voices[voice];zero(&v->descriptor,sizeof(v->descriptor));v->descriptor.pcm=cache[slot].pcm;v->descriptor.bytes=cache[slot].bytes;
 if(!descriptor_init(&v->descriptor)){status.error=16;return;}
 status.initialized++;v->initialized=1;v->cache=(unsigned)slot;v->priority=priority;v->stamp=++stamp;cache[slot].refs++;
 if(!submit(voice+1,&v->descriptor,1,1))error_stop(8);
}
static void stop_group(unsigned group){
 if(group>=DOTA_AUDIO_GROUP_COUNT)return;
 for(unsigned i=0;i<VOICES;i++)if(voices[i].initialized&&voices[i].cache<FX_SLOTS){
  unsigned asset=cache[voices[i].cache].asset;
  for(unsigned j=0;j<audio_groups[group].count;j++)if(audio_group_assets[audio_groups[group].first+j]==asset){if(!cancel_voice(i))error_stop(8);break;}
 }
}
unsigned dota_audio_preload_hero(unsigned hero){
 if(!device_owned||hero>=DOTA_AUDIO_HEROES)return 0;
 unsigned count=0;
 for(unsigned i=0;i<12;i++){unsigned group=audio_hero[hero][i];if(group>=DOTA_AUDIO_GROUP_COUNT)continue;
  if(!audio_groups[group].gain)continue;
  for(unsigned j=0;j<audio_groups[group].count;j++)if(effect_cache(audio_group_assets[audio_groups[group].first+j],audio_groups[group].gain,0)>=0)count++;
 }return count;
}
void dota_audio_ui(unsigned event){if(event<6)play_group(audio_generic[event],event==DOTA_AUDIO_KILL?9:3);update_debug();}
void dota_audio_purchase(int succeeded){if(succeeded)dota_audio_ui(DOTA_AUDIO_GOLD);}
void dota_audio_set_volume(unsigned music,unsigned effects){
 music_volume=music>100?100:music;if(effects>100)effects=100;
 if(effects!=effects_volume){for(unsigned i=0;i<VOICES;i++)if(!cancel_voice(i)){error_stop(8);return;}for(unsigned i=0;i<FX_SLOTS;i++)free_effect(i);effects_volume=effects;}
}
static int audible(const DotaGame*g,int x,int y){const DotaUnit*p=&g->units[g->player];int dx=x-p->x,dy=y-p->y;return (long long)dx*dx+(long long)dy*dy<=400*400;}
static void bind_game(const DotaGame*g){last_game=g;last_game_tick=(unsigned)g->tick;last_skill=g->skill_event_serial;last_attack=g->attack_event_serial;
 last_gold=g->units[g->player].gold;last_kills=g->units[g->player].kills;last_water=0;battle_until=0;zero(skill_sound_at,sizeof(skill_sound_at));zero(cast_sound_serial,sizeof(cast_sound_serial));zero(strike_sound_serial,sizeof(strike_sound_serial));
 for(unsigned i=0;i<DOTA_HERO_COUNT;i++){last_hp[i]=g->units[i].hp;last_generation[i]=g->units[i].generation;}
}
static void game_events(const DotaGame*g){
 if(g!=last_game||(unsigned)g->tick<last_game_tick){bind_game(g);return;}
 if((unsigned)g->tick-last_game_tick>20){bind_game(g);return;}
 last_game_tick=(unsigned)g->tick;
 unsigned pending=g->skill_event_serial-last_skill;if(pending>g->skill_event_count){status.stale_events+=pending-g->skill_event_count;pending=g->skill_event_count;}
 for(unsigned i=pending;i>0;i--){const DotaSkillEvent*e=dota_get_skill_event(g,g->skill_event_serial-i+1);if(!e)continue;status.events++;
  if((unsigned)e->hero_id>=10||(unsigned)e->skill>=5||(unsigned)e->caster>=DOTA_HERO_COUNT||
     g->units[e->caster].generation!=e->caster_generation||!audible(g,e->origin_x,e->origin_y))continue;
  unsigned phase=e->kind!=DOTA_SKILL_EVENT_CAST,slot=2u+(unsigned)e->skill*2u+phase;
  if(!phase)cast_sound_serial[e->caster][e->skill]=e->serial;
#ifdef H1_DOTA_ROSTER_HEROES_STATE_H
  if(e->hero_id==6&&e->skill==1&&!g->roster.hero[e->caster].rot_on){stop_group(audio_hero[6][4]);continue;}
#endif
  if(phase&&e->hero_id==4&&e->skill==0&&cast_sound_serial[e->caster][e->skill]&&strike_sound_serial[e->caster][e->skill]==cast_sound_serial[e->caster][e->skill])continue;

  unsigned last=skill_sound_at[e->caster][e->skill][phase];if(last&&(unsigned)g->tick+1-last<2)continue;
  if(audio_hero[e->hero_id][slot]!=DOTA_AUDIO_GROUP_NONE){if(phase)strike_sound_serial[e->caster][e->skill]=cast_sound_serial[e->caster][e->skill];play_group(audio_hero[e->hero_id][slot],e->caster==g->player?8:6);skill_sound_at[e->caster][e->skill][phase]=(unsigned)g->tick+1;battle_until=(unsigned)g->tick+240;}
 }last_skill=g->skill_event_serial;
 pending=g->attack_event_serial-last_attack;if(pending>g->attack_event_count){status.stale_events+=pending-g->attack_event_count;pending=g->attack_event_count;}
 for(unsigned i=pending;i>0;i--){const DotaAttackEvent*e=dota_get_attack_event(g,g->attack_event_serial-i+1);if(!e)continue;
  if(e->source_type!=DOTA_HERO||(unsigned)e->source>=DOTA_HERO_COUNT||g->units[e->source].generation!=e->source_generation||!audible(g,e->origin_x,e->origin_y))continue;
  int hero=g->units[e->source].hero_id;if((unsigned)hero>=10)continue;
  if(e->kind==DOTA_ATTACK_EVENT_RELEASE||e->kind==DOTA_ATTACK_EVENT_IMPACT){play_group(audio_hero[hero][e->kind==DOTA_ATTACK_EVENT_IMPACT],e->source==g->player?5:2);battle_until=(unsigned)g->tick+240;}
 }last_attack=g->attack_event_serial;
 for(unsigned i=0;i<DOTA_HERO_COUNT;i++)if(g->units[i].active&&g->units[i].hero_id==DOTA_HERO_CRYSTAL_MAIDEN&&g->units[i].channel_ticks<=0)stop_group(audio_hero[3][8]);
 for(unsigned i=0;i<DOTA_HERO_COUNT;i++){if(last_generation[i]==g->units[i].generation&&last_hp[i]>0&&g->units[i].hp<=0&&audible(g,g->units[i].x,g->units[i].y))dota_audio_ui(DOTA_AUDIO_KILL);last_hp[i]=g->units[i].hp;last_generation[i]=g->units[i].generation;}
 int gold=g->units[g->player].gold;if(gold-last_gold>=10)dota_audio_ui(DOTA_AUDIO_GOLD);last_gold=gold;
 int water=dota_rules_unit_in_water(g,g->player);if(water&&!last_water&&battle_until<(unsigned)g->tick)play_group(audio_generic[DOTA_AUDIO_WATER],1);last_water=water;
}
void dota_audio_update(const DotaGame*g,int screen,int selected){
 if(!device_owned){retry_output();if(!device_owned){update_debug();return;}}
 pump_music();if(!device_owned)return;retire_voices();if(!device_owned)return;
 unsigned music=audio_music[0];
 if((screen==1||screen==3)&&g){game_events(g);if(!device_owned)return;music=audio_music[battle_until>(unsigned)g->tick?2:1];}
 else if(screen==4&&g)music=audio_music[g->state==DOTA_WON?3:4];
 if(screen==2&&music_wanted<DOTA_AUDIO_ASSET_COUNT)music=music_wanted;
 if((unsigned)screen!=last_screen){if(screen==0)last_game=0;last_screen=(unsigned)screen;}
 if(selected>=0&&(unsigned)selected<DOTA_AUDIO_HEROES&&(unsigned)selected!=last_hero){if(last_hero!=NONE)dota_audio_ui(DOTA_AUDIO_SELECT);last_hero=(unsigned)selected;}
 if(!device_owned)return;
 if(change_music(music))pump_music();
 update_debug();
}
int dota_audio_clock_ms(uint32_t*out){if(!out||!device_owned||!music_playing)return 0;*out=status.played_frames/32u;return 1;}
const DotaAudioStats*dota_audio_stats(void){update_debug();return &status;}
