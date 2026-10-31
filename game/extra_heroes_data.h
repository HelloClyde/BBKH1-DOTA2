#ifndef DOTA_EXTRA_HEROES_DATA_H
#define DOTA_EXTRA_HEROES_DATA_H
/* Installed Steam hero abilitydefinitions; verified by extract_extra_heroes.py.
 * Rank mapping follows this H1 game's existing levels1/5/9/13 and3/6/9.
 * This table contains the base abilities; Scepter/Shard rules are in
 * extra_heroes.c. Talents and alternate facets are not selected. */
static const unsigned short extra_mana[2][4][4]={
 {{65,65,65,65},{75,75,75,75},{25,30,35,40},{100,100,100,100}},
 {{115,130,145,160},{75,75,75,75},{0,0,0,0},{250,375,500,500}}};
static const unsigned short extra_cd[2][4][4]={
 {{180,160,140,120},{480,400,320,240},{240,210,180,150},{1000,800,600,600}},
 {{160,140,120,100},{560,480,400,320},{0,0,0,0},{2000,2000,2000,2000}}};
static const unsigned short extra_ranges[2][4]={{0,0,150,0},{0,0,0,600}};
static const unsigned short extra_radii[2][4]={{325,120,0,0},{500,700,700,0}};
static const unsigned short extra_pact_damage[4]={75,150,225,300};
static const unsigned short extra_pounce_leash[4]={50,55,60,65};
static const unsigned short extra_shadow_life[3]={80,85,90};
static const unsigned short extra_pulse_damage[4]={100,160,220,280};
static const unsigned short extra_pulse_heal[4]={70,90,110,130};
static const unsigned short extra_ghost_life[4]={60,70,80,90};
static const unsigned char extra_ghost_heal[4]={45,55,65,75};
static const unsigned char extra_ghost_slow[4]={16,19,22,25};
static const unsigned short extra_heartstopper_per_mille[4]={8,13,18,23};
static const unsigned char extra_scythe_damage_pct[3]={70,80,90};
#endif
