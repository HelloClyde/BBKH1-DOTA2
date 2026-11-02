#ifndef DOTA_MATCH_HISTORY_H
#define DOTA_MATCH_HISTORY_H
#include "game.h"
#include "match_history_state.h"
#include <stdint.h>
#define DOTA_MATCH_HISTORY_CAPACITY 50u
#define DOTA_MATCH_RECORD_BYTES 1040u
#define DOTA_MATCH_SAVE_HEADER_BYTES 64u
#define DOTA_MATCH_SAVE_BYTES (DOTA_MATCH_SAVE_HEADER_BYTES+DOTA_MATCH_HISTORY_CAPACITY*DOTA_MATCH_RECORD_BYTES)
#define DOTA_MATCH_RECORDED_ITEMS 11u /* main6, backpack3, neutral, dedicated TP */
typedef struct DotaMatchHeroRecord {
 uint32_t hero_id,team,level,kills,deaths,assists,last_hits,denies,net_worth;
 uint32_t hero_damage,building_damage,ally_healing,mvp_score;
 int16_t items[11];uint16_t charges[11];
 int16_t neutral_enchantment;uint16_t reserved;
} DotaMatchHeroRecord;
typedef struct DotaMatchRecord {
 uint32_t match_id,duration_seconds,winner_team,player,seed,team_kills[2],mvp_hero,flags,reserved;
 DotaMatchHeroRecord heroes[10];
} DotaMatchRecord;
typedef struct DotaMatchHistory {
 uint32_t count,next,sequence,next_match_id;
 DotaMatchRecord records[DOTA_MATCH_HISTORY_CAPACITY];
} DotaMatchHistory;
typedef struct {uint32_t count,next,sequence,next_match_id,payload_crc;} DotaMatchHistoryMeta;
void dota_match_stats_reset(DotaMatchStats *stats);
void dota_match_stats_damage(DotaGame *game,int source,int target,int actual_delta);
void dota_match_stats_kill(DotaGame *game,int killer,int victim);
void dota_match_record_heal(DotaGame *game,int source,int target,int actual_delta);
/* Only WON/LOST games with a nonzero assigned match_id can be captured. */
int dota_match_capture(const DotaGame *game,DotaMatchRecord *record);
int dota_match_record_valid(const DotaMatchRecord *record);
void dota_match_mvp(DotaMatchRecord *record);
const char *dota_match_mvp_formula(void);
void dota_match_history_init(DotaMatchHistory *history);
/* 1 appended,0 duplicate,-1 invalid. Newest index0; capacity remains50. */
int dota_match_history_append(DotaMatchHistory *history,const DotaMatchRecord *record);
const DotaMatchRecord *dota_match_history_get(const DotaMatchHistory *history,unsigned newest_index);
const DotaMatchRecord *dota_match_history_find(const DotaMatchHistory *history,uint32_t match_id);
uint32_t dota_match_crc_update(uint32_t crc,const void *bytes,unsigned count);
int dota_match_record_encode(const DotaMatchRecord *record,unsigned char out[DOTA_MATCH_RECORD_BYTES]);
int dota_match_record_decode(DotaMatchRecord *record,const unsigned char in[DOTA_MATCH_RECORD_BYTES]);
int dota_match_header_encode(const DotaMatchHistory *history,uint32_t payload_crc,unsigned char out[64]);
int dota_match_header_decode(DotaMatchHistoryMeta *meta,const unsigned char in[64]);
/* Full-buffer convenience for host roundtrip. Platform saves stream1KiB blocks. */
unsigned dota_match_history_encode(const DotaMatchHistory *history,void *out,unsigned capacity);
int dota_match_history_decode(DotaMatchHistory *history,const void *bytes,unsigned count);
#endif
