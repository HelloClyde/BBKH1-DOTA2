#ifndef H1_DOTA_SHOP_CATALOG_H
#define H1_DOTA_SHOP_CATALOG_H
#include "../game/game.h"
#define SHOP_CATEGORIES 10
#define SHOP_RECOMMENDED 8
#define SHOP_UPGRADES 9
#define SHOP_RECOMMEND_STAGES 4
void shop_catalog_reset(void);
void shop_catalog_bind(const DotaGame *game);
int shop_catalog_count(int category);
int shop_catalog_item(int category,int index);
int shop_catalog_index(int category,int item);
int shop_catalog_move(int category,int item,int delta);
void shop_catalog_recommend_set_stage(int stage);
int shop_catalog_recommend_stage(void);
const char *shop_catalog_recommend_stage_name(int stage);
void shop_catalog_upgrade_from(int item);
int shop_catalog_upgrade_base(void);
#endif
