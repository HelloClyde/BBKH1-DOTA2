/* Generated and verified against original Steam VPK by extract_item_world_rules.py. */
#ifndef DOTA_ITEM_NPC_DATA_H
#define DOTA_ITEM_NPC_DATA_H
typedef struct { short mana,hp_regen100,mp_regen100,level,xp,gold_min,gold_max,mr,bat_ticks,point_ms,base_as,projectile; } DotaNpcSourceDef;
static const DotaNpcSourceDef npc_source[17]={
 {0},
 {0,50,100,2,32,16,18,0,40,300,150,0}, /* npc_dota_neutral_centaur_outrunner */
 {500,50,100,2,24,12,14,0,40,300,125,1500}, /* npc_dota_neutral_satyr_trickster */
 {500,200,100,6,124,76,80,30,40,500,135,1500}, /* npc_dota_neutral_black_dragon */
 {0,50,100,3,95,37,43,25,40,500,120,900}, /* npc_dota_neutral_black_drake */
 {600,150,150,6,124,76,80,30,40,300,150,0}, /* npc_dota_neutral_granite_golem */
 {400,50,100,5,95,37,43,30,40,300,150,0}, /* npc_dota_neutral_rock_golem */
 {400,50,100,6,124,76,80,30,40,300,120,1500}, /* npc_dota_neutral_big_thunder_lizard */
 {400,50,100,5,95,42,49,50,36,500,100,1500}, /* npc_dota_neutral_small_thunder_lizard */
 {400,50,100,6,124,76,80,30,40,700,135,1500}, /* npc_dota_neutral_ice_shaman */
 {0,200,100,5,95,37,43,30,40,300,150,0}, /* npc_dota_neutral_frostbitten_golem */
 {400,50,100,6,124,76,80,0,40,300,135,0}, /* npc_dota_neutral_prowler_shaman */
 {400,50,100,3,95,37,43,0,40,300,135,0}, /* npc_dota_neutral_prowler_acolyte */
 {0,50,0,3,57,34,39,0,20,467,100,0}, /* npc_dota_creep_goodguys_melee */
 {500,200,75,5,69,43,52,0,20,500,100,900}, /* npc_dota_creep_goodguys_ranged */
 {0,50,0,3,57,34,39,0,20,467,100,0}, /* npc_dota_creep_badguys_melee */
 {500,200,75,5,69,43,52,0,20,500,100,900}, /* npc_dota_creep_badguys_ranged */
};
enum { NPC_PURGE=1,NPC_FIREBALL,NPC_SLAM,NPC_FRENZY,NPC_ICEFIRE,NPC_DESECRATE,NPC_PETRIFY };
typedef struct { short range[4],mana[4],cooldown[4],cast[4],radius[4],value[4],duration[4],second[4]; } DotaNpcAbilityDef;
static const DotaNpcAbilityDef npc_abilities[8]={ {{0},{0},{0},{0},{0},{0},{0},{0}},
 {{350,400,450,550},{120,115,110,100},{60,60,60,60},{4,4,4,4},{0,0,0,0},{400,400,400,400},{100,100,100,100},{5,5,5,5}}, /* satyr_trickster_purge */
 {{1000,1000,1000,1000},{200,200,200,200},{300,300,300,300},{6,6,6,6},{300,300,300,300},{85,85,85,85},{160,180,200,240},{10,10,10,10}}, /* black_dragon_fireball */
 {{0,0,0,0},{150,150,150,150},{120,120,120,120},{6,6,6,6},{350,350,350,350},{250,250,250,250},{40,45,50,60},{80,80,80,80}}, /* big_thunder_lizard_slam */
 {{900,900,900,900},{75,75,75,75},{160,160,160,160},{0,0,0,0},{0,0,0,0},{75,75,75,75},{160,160,160,160},{0,0,0,0}}, /* big_thunder_lizard_frenzy */
 {{700,725,750,800},{100,95,90,80},{600,600,600,600},{7,7,7,7},{0,0,0,0},{50,50,50,50},{160,160,160,160},{25,25,25,25}}, /* ice_shaman_incendiary_bomb */
 {{0,0,0,0},{100,100,100,100},{240,240,240,240},{8,8,8,8},{300,300,300,300},{200,200,200,200},{120,120,120,120},{50,50,50,50}}, /* spawnlord_master_stomp */
 {{150,150,150,150},{0,0,0,0},{300,300,300,300},{0,0,0,0},{0,0,0,0},{100,100,100,100},{24,26,28,30},{90,90,90,90}}, /* spawnlord_master_freeze */
};
enum { NPC_AURA_MAGIC=1,NPC_AURA_ARMOR,NPC_AURA_SPELL,NPC_AURA_HEALTH,NPC_AURA_WEAK,NPC_AURA_DRUMS,NPC_AURA_TIME,NPC_AURA_PROWLER };
typedef struct { short kind,radius,first[4],second[4]; } DotaNpcAuraDef;
static const DotaNpcAuraDef npc_auras[13]={ {0},
 {1,1200,{10,12,14,16},{20,24,28,32}}, /* mudgolem_cloak_aura */
 {0},
 {2,1200,{3,3,3,3},{0,0,0,0}}, /* black_dragon_dragonhide_aura */
 {3,1200,{5,6,7,9},{0,0,0,0}}, /* black_drake_magic_amplification_aura */
 {4,1200,{16,17,18,19},{0,0,0,0}}, /* granite_golem_hp_aura */
 {5,1200,{3,4,5,6},{0,0,0,0}}, /* ancient_rock_golem_weakening_aura */
 {0},
 {6,1200,{25,25,25,25},{40,43,46,51}}, /* big_thunder_lizard_wardrums_aura */
 {0},
 {7,1200,{8,9,10,11},{0,0,0,0}}, /* frostbitten_golem_time_warp_aura */
 {0},
 {8,1200,{9,10,11,12},{9,10,11,12}}, /* spawnlord_aura */
};
#endif
