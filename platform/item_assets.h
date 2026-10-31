#ifndef H1_DOTA_ITEM_ASSETS_H
#define H1_DOTA_ITEM_ASSETS_H
#include "art_types.h"
#include "../game/items_data.h"
/* Shared baked_io file must already be open. Rules have a bounded fallback;
 * text, icons and glyphs always remain streamed. */
int item_assets_open(void);
void item_assets_close(void);
void item_assets_reset(void);
/* Optional heap budget for all records/parameters/recipes (57944B in v20).
 * Atomic failure keeps the small caches. budget0 releases the allocation;
 * reset/close also release it. An existing complete cache is reused. */
uint32_t item_assets_reserve_rules_cache(uint32_t max_bytes);
uint32_t item_assets_rules_cache_bytes(void);
/* Optional24x24 image working set.64 slots consume111616B on MIPS; allocation
 * failure retains8 static slots.0/reset/close release. UI keys stay resident. */
unsigned item_assets_reserve_icon_cache(unsigned slots);
unsigned item_assets_icon_cache_slots(void);
unsigned item_assets_icon_cache_bytes(void);
const DotaArt *item_assets_item_art(int item);
const DotaArt *item_assets_hero_art(int hero);
const DotaArt *item_assets_ability_art(int hero,int skill);
const DotaGlyph *item_assets_glyph(unsigned code);
/* field0 Chinese name,1 description,2 source slug,3 English name; UTF-8. */
void item_assets_text(int item,int field,char *dst,int cap);
int dota_items_read(int item,DotaItemRecord *record);
int dota_items_parameter(int item,int key,int rank,int fallback_x100);
int dota_items_recipe_read(int index,DotaItemRecipe *recipe);
int dota_items_count(void);
int dota_items_recipes_count(void);
#endif
