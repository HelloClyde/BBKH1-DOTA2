#ifndef H1_DOTA_TURBO_DATA_H
#define H1_DOTA_TURBO_DATA_H
/* Installed Steam server.dll af4622bd: primary CDOTATurboGameMode vtable,
 * source scripts, and exact extraction receipt in private/turbo/source-rules.
 * These alter mode rules; original movement, attack and skill ranges stay put. */
#define DOTA_TURBO_STARTING_GOLD 600
#define DOTA_TURBO_LOTUS_SECONDS 90
#define DOTA_TURBO_AEGIS_SECONDS 240
typedef struct { int last_xp_snap_tick[10]; } DotaTurboState;
#endif
