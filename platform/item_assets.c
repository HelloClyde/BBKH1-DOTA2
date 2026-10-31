#include "item_assets.h"
#include "baked_io.h"
#include "cache_memory.h"
#include <stddef.h>
/* Native H1 supplies this freestanding primitive; no libc headers/runtime. */
extern void *memset(void *dst,int value,size_t bytes);
#define IAR_MAGIC 0x31524149u
#define UI_MAGIC 0x31524955u
#define FONT_MAGIC 0x31544e46u
#define RECORD_SLOTS 32
#define ICON_SLOTS 8
#define ICON_MAX_SLOTS 64
#define UI_INDEX_SLOTS 128
#define GLYPH_SLOTS 64
/* A small deterministic fallback works even when the application heap is full.
 * The optional allocation contains rules only, never text, icons or glyphs. */
static unsigned char *rules_memory;
static uint32_t rules_bytes;
static int pc_complete,pc_found;
static void pc_reset(void);
static DotaItemsHeader cat;
static uint32_t catalog_base,catalog_bytes,ui_base,font_base,ui[8],font[8];
static DotaItemRecord records[RECORD_SLOTS];
static int16_t record_ids[RECORD_SLOTS];
static unsigned record_cursor;
static uint16_t icon_pixels[ICON_SLOTS][576];
static uint8_t icon_alpha[ICON_SLOTS][576];
static DotaArt arts[ICON_SLOTS];
static int32_t icon_keys[ICON_SLOTS];
static unsigned icon_cursor,icon_capacity=ICON_SLOTS,icon_heap_bytes;
static void *icon_allocation;
static uint16_t (*cached_pixels)[576]=icon_pixels;
static uint8_t (*cached_alpha)[576]=icon_alpha;
static DotaArt *cached_arts=arts;
static int32_t *cached_keys=icon_keys;
typedef struct {uint32_t key;uint16_t w,h;uint32_t offset,bytes;} UIRecord;
static UIRecord ui_index[UI_INDEX_SLOTS];
static unsigned ui_count;
typedef char UIRecord16[sizeof(UIRecord)==16?1:-1];
static void reset_icons(void){
 if(icon_allocation)cache_memory_free(icon_allocation);
 icon_allocation=0;icon_heap_bytes=0;icon_capacity=ICON_SLOTS;icon_cursor=0;
 cached_pixels=icon_pixels;cached_alpha=icon_alpha;cached_arts=arts;cached_keys=icon_keys;
 for(unsigned i=0;i<ICON_SLOTS;i++){cached_keys[i]=-1;cached_arts[i].w=cached_arts[i].h=24;cached_arts[i].pixels=cached_pixels[i];cached_arts[i].alpha=cached_alpha[i];}
}
static DotaGlyph glyphs[GLYPH_SLOTS];
static uint16_t glyph_ids[GLYPH_SLOTS];
typedef char Glyph32[(sizeof(DotaGlyph)==32)?1:-1];
static int valid_range(uint32_t offset,uint32_t bytes,uint32_t limit){return offset<=limit&&bytes<=limit-offset;}
static int catalog_read(uint32_t off,void *dst,uint32_t n){return catalog_base&&valid_range(off,n,catalog_bytes)&&baked_io_read_at(catalog_base+off,dst,n);}
void item_assets_reset(void){int i;if(rules_memory)cache_memory_free(rules_memory);rules_memory=0;rules_bytes=0;pc_reset();record_cursor=0;for(i=0;i<RECORD_SLOTS;i++)record_ids[i]=-1;reset_icons();for(i=0;i<GLYPH_SLOTS;i++)glyph_ids[i]=65535;icon_cursor=0;}
void item_assets_close(void){ui_count=0;catalog_base=catalog_bytes=ui_base=font_base=0;memset(&cat,0,sizeof(cat));memset(ui,0,sizeof(ui));memset(font,0,sizeof(font));item_assets_reset();}
int item_assets_open(void){
 uint32_t h[16],iar[8],size=baked_io_size();item_assets_close();
 if(!baked_io_is_open()||!baked_io_read_at(0,h,64)||h[0]!=0x314b4248u||h[1]<11||h[2]!=size||!valid_range(h[15],64,size)||!baked_io_read_at(h[15]+32,iar,32))return 0;
 if(iar[0]!=IAR_MAGIC||iar[1]!=1||iar[2]!=32||!valid_range(iar[3],iar[4],size)||iar[4]<64||!baked_io_read_at(iar[3],&cat,64))return 0;
 catalog_base=iar[3];catalog_bytes=iar[4];ui_base=iar[5];font_base=iar[6];
 if(cat.magic!=DOTA_ITEMS_MAGIC||cat.version!=1||cat.bytes!=catalog_bytes||cat.record_bytes!=96||cat.item_count>4096||cat.recipe_count>4096||cat.param_count>65535||!valid_range(cat.record_offset,cat.item_count*96,catalog_bytes)||!valid_range(cat.param_offset,cat.param_count*8,catalog_bytes)||!valid_range(cat.recipe_offset,cat.recipe_count*20,catalog_bytes)||!valid_range(cat.text_index_offset,cat.item_count*16,catalog_bytes)||cat.text_offset>cat.icon_index_offset||!valid_range(cat.icon_index_offset,cat.item_count*8,catalog_bytes)||cat.icon_offset>catalog_bytes)goto fail;
 if(!valid_range(ui_base,32,size)||!baked_io_read_at(ui_base,ui,32)||ui[0]!=UI_MAGIC||ui[1]!=1||ui[3]>128||ui[5]!=16||!valid_range(ui_base,ui[2],size)||!valid_range(ui[4],ui[3]*16,ui[2]))goto fail;
 ui_count=ui[3];
 if(ui_count&&!baked_io_read_at(ui_base+ui[4],ui_index,ui_count*sizeof(UIRecord)))goto fail;
 for(unsigned i=0;i<ui_count;i++){
  UIRecord value=ui_index[i];unsigned j=i;
  if(value.w!=24||value.h!=24||value.bytes!=1728||!valid_range(value.offset,value.bytes,ui[2]))goto fail;
  while(j&&ui_index[j-1].key>value.key){ui_index[j]=ui_index[j-1];j--;}
  ui_index[j]=value;
 }
 for(unsigned i=1;i<ui_count;i++)if(ui_index[i-1].key==ui_index[i].key)goto fail;
 if(!valid_range(font_base,32,size)||!baked_io_read_at(font_base,font,32)||font[0]!=FONT_MAGIC||font[1]!=1||font[3]>65535||font[5]!=32||font[6]!=14||!valid_range(font_base,font[2],size)||!valid_range(font[4],font[3]*32,font[2]))goto fail;
 return 1;
fail:item_assets_close();return 0;
}
unsigned item_assets_icon_cache_bytes(void){return icon_heap_bytes;}
unsigned item_assets_icon_cache_slots(void){return icon_capacity;}
unsigned item_assets_reserve_icon_cache(unsigned slots){
 if(!slots){reset_icons();return icon_capacity;}
 if(slots<ICON_SLOTS)slots=ICON_SLOTS;
 if(slots>ICON_MAX_SLOTS)slots=ICON_MAX_SLOTS;
 if(slots<=icon_capacity)return icon_capacity;
 unsigned bytes=slots*(1728u+sizeof(DotaArt)+sizeof(int32_t));
 unsigned char *memory=(unsigned char*)cache_memory_alloc(bytes);
 if(!memory)return icon_capacity;
 if(icon_allocation)cache_memory_free(icon_allocation);
 icon_allocation=memory;icon_heap_bytes=bytes;icon_capacity=slots;icon_cursor=0;
 cached_pixels=(uint16_t(*)[576])memory;cached_alpha=(uint8_t(*)[576])(memory+slots*1152u);
 cached_arts=(DotaArt*)(memory+slots*1728u);cached_keys=(int32_t*)(memory+slots*(1728u+sizeof(DotaArt)));
 for(unsigned i=0;i<slots;i++){cached_keys[i]=-1;cached_arts[i].w=cached_arts[i].h=24;cached_arts[i].pixels=cached_pixels[i];cached_arts[i].alpha=cached_alpha[i];}
 return icon_capacity;
}
uint32_t item_assets_rules_cache_bytes(void){return rules_bytes;}
uint32_t item_assets_reserve_rules_cache(uint32_t budget){
 uint32_t records_bytes,params_bytes,recipes_bytes,total;
 unsigned char *memory;
 if(!budget){if(rules_memory)cache_memory_free(rules_memory);rules_memory=0;rules_bytes=0;return 0;}
 if(rules_memory)return rules_bytes;
 if(!catalog_base)return 0;
 records_bytes=cat.item_count*96u;params_bytes=cat.param_count*8u;recipes_bytes=cat.recipe_count*20u;
 total=records_bytes+params_bytes+recipes_bytes;
 if(!total||total>budget)return 0;
 memory=(unsigned char*)cache_memory_alloc(total);
 if(!memory)return 0;
 /* Publish only after all three validated spans are present. Any allocation or
  * I/O failure leaves the existing bounded caches usable and leaks no memory. */
 if(!catalog_read(cat.record_offset,memory,records_bytes)||
    !catalog_read(cat.param_offset,memory+records_bytes,params_bytes)||
    !catalog_read(cat.recipe_offset,memory+records_bytes+params_bytes,recipes_bytes)){
  cache_memory_free(memory);return 0;
 }
 rules_memory=memory;rules_bytes=total;return total;
}
int dota_items_count(void){return catalog_base?(int)cat.item_count:0;}
int dota_items_recipes_count(void){return catalog_base?(int)cat.recipe_count:0;}
int dota_items_read(int item,DotaItemRecord *out){unsigned i,slot=RECORD_SLOTS;if(!out||item<0||(unsigned)item>=cat.item_count||!catalog_base)return 0;if(rules_memory){*out=((const DotaItemRecord*)rules_memory)[item];return 1;}for(i=0;i<RECORD_SLOTS;i++){if(record_ids[i]==item){*out=records[i];return 1;}if(record_ids[i]<0&&slot==RECORD_SLOTS)slot=i;}if(slot==RECORD_SLOTS)slot=record_cursor++%RECORD_SLOTS;if(!catalog_read(cat.record_offset+(uint32_t)item*96,&records[slot],96))return 0;record_ids[slot]=(int16_t)item;*out=records[slot];return 1;}
static int pc_uncached(int item,int key,int rank,int fallback){DotaItemRecord r;DotaItemParameter p;unsigned i;int best=-1,value=fallback;pc_complete=pc_found=0;if(!dota_items_read(item,&r)||key<=0)return fallback;if(rank<0)rank=r.level?r.level-1:0;if((uint32_t)r.param_first+r.param_count>cat.param_count)return fallback;for(i=0;i<r.param_count;i++){if(rules_memory)p=((const DotaItemParameter*)(rules_memory+cat.item_count*96u))[(unsigned)r.param_first+i];else if(!catalog_read(cat.param_offset+((uint32_t)r.param_first+i)*8,&p,8))return fallback;if(p.key==(unsigned)key&&p.rank<=rank&&(int)p.rank>best){best=p.rank;value=p.value_x100;}}pc_complete=1;pc_found=best>=0;return value;}
int dota_items_recipe_read(int index,DotaItemRecipe *out){if(!out||index<0||(unsigned)index>=cat.recipe_count)return 0;if(rules_memory){*out=((const DotaItemRecipe*)(rules_memory+cat.item_count*96u+cat.param_count*8u))[index];return 1;}return catalog_read(cat.recipe_offset+(uint32_t)index*20,out,20);}
static const DotaArt *load_art(int key,uint32_t absolute,uint32_t bytes){
 unsigned i,slot;if(bytes!=1728||!valid_range(absolute,bytes,baked_io_size()))return 0;
 for(i=0;i<icon_capacity;i++)if(cached_keys[i]==key)return &cached_arts[i];
 slot=icon_cursor++%icon_capacity;
 if(!baked_io_read_at(absolute,cached_pixels[slot],1152)||!baked_io_read_at(absolute+1152,cached_alpha[slot],576)){cached_keys[slot]=-1;return 0;}
 cached_keys[slot]=key;return &cached_arts[slot];
}
const DotaArt *item_assets_item_art(int item){uint32_t ix[2];DotaItemRecord r;if(!dota_items_read(item,&r)||r.icon>=cat.item_count||!catalog_read(cat.icon_index_offset+(uint32_t)r.icon*8,ix,8)||!valid_range(ix[0],ix[1],catalog_bytes-cat.icon_offset))return 0;return load_art(item,catalog_base+cat.icon_offset+ix[0],ix[1]);}
static const DotaArt *ui_art(uint32_t key){
 unsigned lo=0,hi=ui_count;if(!ui_base)return 0;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;
  if(ui_index[mid].key==key)return load_art((int)key,ui_base+ui_index[mid].offset,ui_index[mid].bytes);
  if(ui_index[mid].key<key)lo=mid+1;else hi=mid;
 }return 0;
}
const DotaArt *item_assets_hero_art(int hero){return hero>=4&&hero<=9?ui_art(0x10000u|(unsigned)hero):0;}
const DotaArt *item_assets_ability_art(int hero,int skill){return hero>=0&&hero<=9&&skill>=0&&skill<8?ui_art(0x20000u|((unsigned)hero<<8)|(unsigned)skill):0;}
const DotaGlyph *item_assets_glyph(unsigned code){int lo=0,hi;unsigned slot;DotaGlyph g;if(!font_base||code>65535)return 0;slot=code%GLYPH_SLOTS;if(glyph_ids[slot]==code)return &glyphs[slot];hi=(int)font[3]-1;while(lo<=hi){int mid=lo+(hi-lo)/2;if(!baked_io_read_at(font_base+font[4]+(uint32_t)mid*32,&g,32))return 0;if(g.code==code){if(!g.width||g.width>14)return 0;glyphs[slot]=g;glyph_ids[slot]=(uint16_t)code;return &glyphs[slot];}if(g.code<code)lo=mid+1;else hi=mid-1;}return 0;}
void item_assets_text(int item,int field,char *dst,int cap){uint32_t offset,index,limit;int n=0;if(!dst||cap<=0)return;dst[0]=0;if(!catalog_base||item<0||(unsigned)item>=cat.item_count||field<0||field>3||!catalog_read(cat.text_index_offset+(uint32_t)item*16+(uint32_t)field*4,&index,4))return;limit=cat.icon_index_offset-cat.text_offset;if(index>=limit)return;offset=cat.text_offset+index;while(n<cap-1&&offset<cat.icon_index_offset){uint32_t take=(uint32_t)(cap-1-n);unsigned i;char chunk[64];if(take>64)take=64;if(take>cat.icon_index_offset-offset)take=cat.icon_index_offset-offset;if(!catalog_read(offset,chunk,take)){dst[0]=0;return;}for(i=0;i<take;i++){if(!chunk[i]){dst[n]=0;return;}dst[n++]=chunk[i];}offset+=take;}dst[n]=0;/* Trim an incomplete UTF-8 tail rather than return a broken code point. */if(n&&((unsigned char)dst[n-1]>=128)){int start=n-1,need;while(start>0&&((unsigned char)dst[start]&192)==128)start--;need=((unsigned char)dst[start]>=240)?4:((unsigned char)dst[start]>=224)?3:((unsigned char)dst[start]>=192)?2:1;if(n-start<need)dst[start]=0;}}

 typedef struct {uint16_t item,key;int16_t rank;uint16_t flags;int32_t value;} ParamCache;
 static ParamCache pc[256];static uint8_t pc_next[64];
 static void pc_reset(void){unsigned i;for(i=0;i<256;i++)pc[i].flags=0;for(i=0;i<64;i++)pc_next[i]=0;}
 int dota_items_parameter(int item,int key,int rank,int fallback){unsigned h,base,i,slot;int value;
 if(item<0||(unsigned)item>=cat.item_count||!catalog_base||key<=0||key>65535)return fallback;
 if(rank<0)rank=-1;
 if(rank>32767)return pc_uncached(item,key,rank,fallback);
 h=(unsigned)item*0x9e3779b9u^(unsigned)key*0x85ebca6bu^(unsigned)(rank+1)*0xc2b2ae35u;h^=h>>16;base=(h&63u)*4u;
 for(i=0;i<4;i++){ParamCache*p=&pc[base+i];if((p->flags&1)&&p->item==item&&p->key==key&&p->rank==rank)return (p->flags&2)?p->value:fallback;}
 value=pc_uncached(item,key,rank,0);if(!pc_complete)return fallback;
 slot=base+pc_next[h&63u];pc_next[h&63u]=(uint8_t)((pc_next[h&63u]+1)&3u);
 pc[slot].item=(uint16_t)item;pc[slot].key=(uint16_t)key;pc[slot].rank=(int16_t)rank;pc[slot].value=value;pc[slot].flags=(uint16_t)(1|(pc_found?2:0));return pc_found?value:fallback;
 }
 
