Tree state uses the 2113 records in `../vision/trees.json`, in their existing
order. The runtime borrows their immutable uint16 world XY pairs from TREE;
it does not copy coordinates into application RAM. `DotaGame.trees` owns every
cut bit, regrow deadline and revision, so copying/restarting a Game also copies
or resets its tree state. Its O32 size is 4500 bytes; DotaGame is 178044 bytes.

Ordinary trees regrow 180 seconds after destruction (3600 fixed game ticks).
Pause stops the game tick. uint16 modular deadlines are valid because that
interval is below half the clock span; wrap and exact expiration are tested.
Temporary branch trees retain their original 20-second lifetime and do not
regrow after consumption. Tango has the original 7 HP/s for 16 seconds, doubled
when its target was a planted branch tree. Failed targeting consumes no charge
or cooldown. Quelling Blade and Battle Fury use their original tree action.
Lina Light Strike Array cuts trees in its actual ability area.
Slark Pounce clears its installed 120-Source-unit circle at natural landing or
early hero impact before resolving the final navigable point; see
`pounce-source-audit.json` for the native parameter-to-tree-destroy call chain.

Navigation uses original GNV occupancy and a new full-grid run of the original
PHYS swept-ray converter before trees are subtracted. See
`../tree-navigation/import-audit.json` and `generation.json`. The standing tree
mask reproduces all 65536 original walk cells exactly; all 88220 previously
legal PHYS edges agree. Cutting cannot restore GNV- or PHYS-blocked terrain.
Live tower/filler/barracks hulls and the existing certified narrow passages
remain independently enforced. The 22-world collision radius includes the
original 16-world tree radius and 6-world unit clearance. LOS uses the separate
16-world radius and rebuilds the union of standing IDs, preserving overlapping
trees. Both caches refresh on the same Game revision, including the cut tick.

These retain H1's audited grid/continuous-collision approximations. The native
timer and item data do not prove every Source2 tree/nearby-unit regrowth detail.
The focused host receipts are not physical-H1 or FPS measurements. Runtime
integration, final BDA outcomes and physical acceptance remain root tasks.
