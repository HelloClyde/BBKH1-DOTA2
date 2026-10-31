#ifndef H1_DOTA_JUNGLE_H
#define H1_DOTA_JUNGLE_H
#include "game.h"
#include "jungle_data.h"
static inline const DotaJungleDefinition *dota_jungle_definition(const DotaUnit *u){
    return u && u->type==DOTA_NEUTRAL && (unsigned)u->tier<DOTA_JUNGLE_SPECIES?&dota_jungle_definitions[u->tier]:0;
}
static inline int dota_jungle_first(int camp){return (unsigned)camp<26?dota_jungle_camp_first[camp]:-1;}
static inline int dota_jungle_end(int camp){return (unsigned)camp<26?dota_jungle_camp_first[camp]+dota_jungle_camp_capacity[camp]:-1;}
#endif
