#ifndef H1_DOTA_RUNES_STATE_H
#define H1_DOTA_RUNES_STATE_H
/* Valve rune ids; wisdom remains implemented by the existing shrine rules. */
enum {DOTA_RUNE_AMPLIFY=0,DOTA_RUNE_HASTE=1,DOTA_RUNE_ILLUSION=2,
      DOTA_RUNE_INVISIBILITY=3,DOTA_RUNE_REGENERATION=4,DOTA_RUNE_BOUNTY=5,
      DOTA_RUNE_ARCANE=6,DOTA_RUNE_WATER=7,DOTA_RUNE_XP=8,
      DOTA_RUNE_SHIELD=9,DOTA_RUNE_COUNT=10};
#define DOTA_RUNE_SPOTS 4 /* original river2, jungle bounty2 */
typedef struct {
    int generation,until[DOTA_RUNE_COUNT];
    int regeneration_reduced_until,invisibility_fade_until,reserved,shield;
} DotaRuneHero;
typedef struct {
    DotaRuneHero hero[10];
    int kind[DOTA_RUNE_SPOTS],spawn_tick[DOTA_RUNE_SPOTS]; /* kind -1 absent */
    int next_power_tick,next_bounty_tick,last_power_type,last_power_spot;
    unsigned remaining_power_types;
    int order,order_generation; /* zero none, otherwise source spot+1 */
} DotaRuneState;
#endif
