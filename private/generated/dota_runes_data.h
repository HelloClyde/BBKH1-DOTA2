/* Original installed entities and audited server.dll rune constructors/switch.
 * Identity/proof: private/runes/source-rules/manifest.json. */
#ifndef H1_DOTA_RUNES_DATA_H
#define H1_DOTA_RUNES_DATA_H
#define DOTA_RUNE_POWER_INTERVAL_SECONDS 120
#define DOTA_RUNE_POWER_FIRST_SECONDS 360
#define DOTA_RUNE_WATER_FIRST_SECONDS 120
#define DOTA_RUNE_BOUNTY_INTERVAL_SECONDS 240
/* Bounded H1 interaction distance; not a claim of engine pickup collision. */
#define DOTA_RUNE_PICKUP_RADIUS_SOURCE 100
static const short dota_rune_source_positions[4][3]={{1638,1770,10},{2343,2352,14},{1799,940,42},{2197,3213,42}};
static const unsigned short dota_rune_duration_seconds[10]={45,22,0,45,30,0,50,0,0,75};
#define DOTA_RUNE_POWER_TYPES_MASK 607u /*0..4,6,9: excludes bounty/water/XP*/
#endif
