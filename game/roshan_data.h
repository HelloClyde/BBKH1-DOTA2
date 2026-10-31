/* Generated from installed Valve definitions by tools/bake_roshan_visibility.py.
 * Source manifest SHA256 4b445d239c09c9889445c50c8d83db69cc12a2cb3755de4b359976c9ebc54c0f
 * damage_avg is the H1 integer floor of original min/max; both endpoints remain.
 * Regeneration retains hundredths/sec, without runtime floating point.
 * Native source blocker convex hull is an H1 approximation, not recovered native FOW.
 */
#ifndef H1_DOTA_ROSHAN_DATA_H
#define H1_DOTA_ROSHAN_DATA_H
#define DOTA_WILD_SOURCE_COUNT 11
#define DOTA_ANCIENT_SPECIES_COUNT 10
#define DOTA_ANCIENT_CAMP_COUNT 2
#define DOTA_ANCIENT_GROUP_COUNT 5
typedef struct {
    int model_id,hp,armor,damage_avg,damage_min,damage_max,range_source,acquire_source,speed_source,bat_ticks,point_ms,base_attack_speed,projectile_source,magic_resist_pct,xp,gold_min,gold_max,regen_per_sec_x100,hull_source;
} DotaWildSourceDef;
/* index0=Roshan/model48; index1..10=Ancient species0..9/models49..58. */
static const DotaWildSourceDef dota_wild_source_defs[DOTA_WILD_SOURCE_COUNT]={
    {48,6000,30,75,75,75,150,150,270,40,600,100,1000,55,400,200,290,2000,24}, /* roshan */
    {49,2000,4,65,62,68,300,300,300,40,500,135,1500,30,124,76,80,200,24}, /* black_dragon */
    {50,950,2,21,20,22,300,300,350,40,500,120,900,25,95,37,43,50,24}, /* black_drake */
    {51,1500,8,82,80,84,128,500,270,40,300,150,0,30,124,76,80,150,24}, /* granite_golem */
    {52,800,4,23,22,24,100,500,270,40,300,150,0,30,95,37,43,50,24}, /* rock_golem */
    {53,1700,3,62,60,65,300,300,270,40,300,120,1500,30,124,76,80,50,24}, /* big_thunder_lizard */
    {54,800,3,33,32,34,300,800,270,36,500,100,1500,50,95,42,49,50,24}, /* small_thunder_lizard */
    {55,1500,3,60,58,62,500,500,290,40,700,135,1500,30,124,76,80,50,24}, /* ice_shaman */
    {56,900,7,30,29,31,100,500,300,40,300,150,0,30,95,37,43,200,24}, /* frostbitten_golem */
    {57,1400,12,62,59,65,100,500,300,40,300,135,0,0,124,76,80,50,24}, /* prowler_shaman */
    {58,750,11,35,33,38,100,500,270,40,300,135,0,0,95,37,43,50,24}, /* prowler_acolyte */
};
static const short dota_ancient_camp_positions[DOTA_ANCIENT_CAMP_COUNT][2]={
    {794,2072}, /* [PR#]neutralcamp_good_8 */
    {3136,2036}, /* [PR#]neutralcamp_evil_8 */
};
/* Native NeutralType3 camps: Min1 Max5 Forced-1 selects rows0..4.
 * ForcedSubType0 also randomizes; only positive forced values are 1-based.
 * A camp rerolls a random result equal to its immediately previous subtype.
 * Native table order: two lesser creatures, then their leader. Values are species0..9. */
static const unsigned char dota_ancient_spawn_species[DOTA_ANCIENT_GROUP_COUNT][3]={
    {1,1,0},{3,3,2},{5,5,4},{7,7,6},{9,9,8}
};
/* Top/Bottom original Source anchors, half-up world rounding. */
static const short dota_roshan_pit_positions[2][2]={{1262,1448},{2756,2733}};
/* Native default60-second growth interval; preserve current HP/maxHP fraction. */
#define DOTA_ROSHAN_HEALTH_PER_MINUTE 130
#define DOTA_ROSHAN_SLAM_RADIUS_SOURCE 350
#define DOTA_ROSHAN_SLAM_BASE_DAMAGE 70
#define DOTA_ROSHAN_SLAM_DAMAGE_PER_MINUTE 10
#define DOTA_ROSHAN_SLAM_POINT_MS 470
#define DOTA_ROSHAN_SLAM_COOLDOWN_TICKS 200
#define DOTA_ROSHAN_SLAM_SLOW_TICKS 80
#define DOTA_ROSHAN_SLAM_SLOW_PERCENT 50
#define DOTA_ROSHAN_BASH_CHANCE_PERCENT 15
#define DOTA_ROSHAN_BASH_BONUS_DAMAGE 50
#define DOTA_ROSHAN_BASH_STUN_TICKS 33
#define DOTA_ROSHAN_SPELL_BLOCK_TICKS 300
#define DOTA_ROSHAN_STATUS_RESIST_PERCENT 25
#define DOTA_ROSHAN_ROAR_RADIUS_SOURCE 600
#define DOTA_ROSHAN_ROAR_BASE_DAMAGE 50
#define DOTA_ROSHAN_ROAR_DAMAGE_PER_MINUTE 10
#define DOTA_ROSHAN_ROAR_COOLDOWN_TICKS 1200
#define DOTA_ROSHAN_ROAR_DISARM_TICKS 40
#define DOTA_ROSHAN_ROAR_VULNERABLE_TICKS 240
#define DOTA_ROSHAN_ROAR_EXTRA_DAMAGE_PERCENT 25
#define DOTA_ROSHAN_FIRST_MOVE_TICKS 18000
#define DOTA_ROSHAN_RESPAWN_MIN_TICKS 9600
#define DOTA_ROSHAN_RESPAWN_MAX_TICKS 13200
#define DOTA_AEGIS_LIFE_TICKS 6000
#define DOTA_AEGIS_REINCARNATE_TICKS 100
/* Exact derived hull coordinates are Q8 world units. */
static const int dota_roshan_polygon_q8[][2]={
    {295427,377446},
    {297993,369589},
    {302092,358081},
    {312504,353001},
    {317710,350461},
    {321583,351794},
    {344821,359792},
    {357585,385639},
    {356189,389695},
    {354856,393568},
    {350900,396655},
    {342708,400751},
    {323620,408808},
    {319747,407475},
    {315874,406142},
    {312001,404809},
    {305587,398270},
    {297967,382652},
    {670471,694990},
    {671280,690777},
    {675260,685248},
    {676847,683505},
    {696061,670321},
    {700821,667253},
    {704843,668025},
    {708866,668797},
    {712888,669570},
    {720161,675137},
    {723411,679931},
    {726662,684726},
    {729912,689521},
    {733162,694316},
    {731732,702457},
    {729301,714429},
    {719712,720929},
    {714917,724179},
    {710894,723407},
    {706872,722635},
    {690782,719546},
    {680258,709184},
};
static const unsigned char dota_roshan_polygon_offsets[3]={0,18,40};
#endif
