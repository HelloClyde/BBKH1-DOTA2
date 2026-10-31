#ifndef H1_DOTA_NEUTRAL_STATE_H
#define H1_DOTA_NEUTRAL_STATE_H
typedef struct {int active,x,y,primary,allies;} DotaMadstoneDrop;
typedef struct {
 int stones[10],earned[10],crafted[10],recrafts[10];
 short enchantment[10],pending[10],offers[10][5];
 unsigned char phase[10],next_tier[10],offer_count[10];
 DotaMadstoneDrop drops[16];
} DotaNeutralState;
#endif
