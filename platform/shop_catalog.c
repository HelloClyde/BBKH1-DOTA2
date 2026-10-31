#include "shop_catalog.h"
#include "../game/items.h"
#include "../game/neutral.h"
static unsigned char membership[DOTA_ITEM_CATALOG_COUNT];
static int ready;
static const DotaGame *bound;
static int recommend_stage,upgrade_base=-1;
/* Role-based reference builds using the current Source item catalogue.
 * These are starting points, not a claimed live professional meta. */
static const uint16_t recommended[DOTA_HERO_TYPES][SHOP_RECOMMEND_STAGES][6]={
 {{57,23,26,18,47,58},{61,82,48,124,126,6},{6,126,113,112,111,7},{114,117,125,93,107,109}},
 {{57,25,26,50,47,58},{75,84,48,152,99,100},{153,113,101,93,107,109},{131,146,162,142,116,147}},
 {{57,17,24,26,18,58},{61,83,48,136,155,152},{136,137,135,148,113,146},{179,133,131,147,109,107}},
 {{57,55,56,50,51,58},{181,48,42,100,183,109},{183,100,107,113,170,120},{99,162,110,93,177,116}},
 {{57,17,24,26,18,58},{75,83,48,96,157,168},{157,109,107,113,148,179},{158,137,146,133,142,116}},
 {{57,25,26,50,47,58},{61,82,48,121,99,109},{109,132,107,114,144,112},{115,162,93,110,116,117}},
 {{57,23,26,18,47,58},{161,48,6,99,90,109},{6,107,112,113,114,110},{117,93,162,120,116,177}},
 {{57,24,26,18,47,58},{75,83,48,139,152,156},{101,147,113,134,142,109},{131,146,133,107,116,148}},
 {{57,17,23,26,18,58},{75,82,48,96,156,6},{184,113,134,6,111,146},{7,131,133,107,109,116}},
 {{57,17,23,26,18,58},{161,82,48,6,90,109},{6,90,177,107,114,110},{125,117,162,93,112,111}}
};
static int upgrades[64],upgrade_count;
void shop_catalog_bind(const DotaGame *game){bound=game;}
static int choosing(int category){return bound && ((category==5 && dota_neutral_phase(bound,bound->player)==1) || (category==6 && dota_neutral_phase(bound,bound->player)==2));}
void shop_catalog_reset(void){ready=0;recommend_stage=0;upgrade_base=-1;upgrade_count=0;}
void shop_catalog_recommend_set_stage(int stage){recommend_stage=stage<0?0:stage>=SHOP_RECOMMEND_STAGES?SHOP_RECOMMEND_STAGES-1:stage;}
int shop_catalog_recommend_stage(void){return recommend_stage;}
const char *shop_catalog_recommend_stage_name(int stage){static const char *names[SHOP_RECOMMEND_STAGES]={"出门","前期","核心","可选"};return (unsigned)stage<SHOP_RECOMMEND_STAGES?names[stage]:"";}
void shop_catalog_upgrade_from(int item){
    int i,j;DotaItemRecipe recipe;DotaItemRecord record;
    upgrade_base=item;upgrade_count=0;
    for(i=0;i<dota_items_recipes_count();++i){
        int uses=0,duplicate=0;
        if(!dota_items_recipe_read(i,&recipe)||recipe.result==item)continue;
        if(recipe.recipe==item)uses=1;
        for(j=0;j<recipe.count&&j<6;++j)if(recipe.component[j]==item)uses=1;
        if(!uses||!dota_items_read(recipe.result,&record)||!(record.flags&ITEM_F_BUY))continue;
        for(j=0;j<upgrade_count;++j)if(upgrades[j]==recipe.result)duplicate=1;
        if(!duplicate&&upgrade_count<64)upgrades[upgrade_count++]=recipe.result;
    }
}
int shop_catalog_upgrade_base(void){return upgrade_base;}
static int special_item(int category,int index){
    DotaItemRecord record;
    if(index<0)return -1;
    if(category==SHOP_UPGRADES)return index<upgrade_count?upgrades[index]:-1;
    if(category==SHOP_RECOMMENDED){
        int hero=bound?bound->units[bound->player].hero_id:0,id,i;
        if((unsigned)hero>=DOTA_HERO_TYPES||index>=6)return -1;
        for(i=0;i<6;++i){id=recommended[hero][recommend_stage][i];if(dota_items_read(id,&record)&&(record.flags&ITEM_F_BUY)&&!index--)return id;}
        return -1;
    }return -1;
}
static void prepare(void){
    int i;DotaItemRecord r;if(ready)return;
    for(i=0;i<DOTA_ITEM_CATALOG_COUNT;++i){unsigned mask=0;
        if(!dota_items_read(i,&r)){membership[i]=0;continue;}
        if(r.flags&ITEM_F_BUY){mask=1;if(r.flags&ITEM_F_RECIPE)mask|=1u<<4;
            else if(r.flags&ITEM_F_CONSUMABLE)mask|=1u<<3;
            else {DotaItemRecipe recipe;int j,assembled=0,limit=(int)r.recipe_first+r.recipe_count;
                if(limit<=dota_items_recipes_count())for(j=r.recipe_first;j<limit;++j)if(dota_items_recipe_read(j,&recipe)&&recipe.result==i){assembled=1;break;}
                mask|=1u<<(assembled?2:1);}
        }else if(r.flags&ITEM_F_NEUTRAL)mask=1u<<5;
        else if(r.flags&ITEM_F_ENCHANTMENT)mask=1u<<6;
        else if(!(r.flags&ITEM_F_AUTO_RECIPE))mask=1u<<7;
        membership[i]=(unsigned char)mask;
    }ready=dota_items_count()==DOTA_ITEM_CATALOG_COUNT;
}
int shop_catalog_count(int category){int i,n=0;if((unsigned)category>=SHOP_CATEGORIES)return 0;if(category>=SHOP_RECOMMENDED){for(i=0;i<64&&special_item(category,i)>=0;++i)++n;return n;}if(choosing(category))return dota_neutral_offer_count(bound,bound->player);prepare();for(i=0;i<DOTA_ITEM_CATALOG_COUNT;++i)if(membership[i]&(1u<<category))++n;return n;}
int shop_catalog_item(int category,int index){int i;if((unsigned)category>=SHOP_CATEGORIES || index<0)return -1;if(category>=SHOP_RECOMMENDED)return special_item(category,index);if(choosing(category))return dota_neutral_offer_item(bound,bound->player,index);prepare();for(i=0;i<DOTA_ITEM_CATALOG_COUNT;++i)if(membership[i]&(1u<<category)){if(!index--)return i;}return -1;}
int shop_catalog_index(int category,int item){int i,n=0;if((unsigned)category>=SHOP_CATEGORIES)return -1;if(category>=SHOP_RECOMMENDED){for(i=0;i<shop_catalog_count(category);++i)if(special_item(category,i)==item)return i;return -1;}if(choosing(category)){for(i=0;i<dota_neutral_offer_count(bound,bound->player);++i)if(dota_neutral_offer_item(bound,bound->player,i)==item)return i;return -1;}prepare();for(i=0;i<DOTA_ITEM_CATALOG_COUNT;++i)if(membership[i]&(1u<<category)){if(i==item)return n;++n;}return -1;}
int shop_catalog_move(int category,int item,int delta){int count=shop_catalog_count(category),index=shop_catalog_index(category,item);if(!count)return -1;if(index<0)index=0;index=(index+delta)%count;if(index<0)index+=count;return shop_catalog_item(category,index);}
