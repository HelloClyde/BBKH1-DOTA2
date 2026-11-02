/* Input-only cases for local tree invalidation. Full mask/visibility output
 * still goes through the frozen C7 comparison, never a rewritten result. */
extern volatile unsigned h1_shared_shadow_profile[5],old_h1_shared_shadow_profile[5];
int new_source_tree_current(int index);
static unsigned local_cases,remote_casts_saved;
static void local_compare(const char *label,int static_changed,unsigned expected_casts){
 unsigned before=h1_shared_shadow_profile[3],old_before=old_h1_shared_shadow_profile[3],delta,old_delta;
 compare(label,static_changed);delta=h1_shared_shadow_profile[3]-before;old_delta=old_h1_shared_shadow_profile[3]-old_before;
 CHECK(delta==expected_casts);CHECK(old_delta>=delta);remote_casts_saved+=old_delta-delta;++local_cases;
 printf("PERF %s %u %u\n",label,old_delta,delta);
}
static void local_setup(void){
 int i;dota_init(&game,DOTA_HERO_AXE,113);game.tick=2000;
 for(i=1;i<DOTA_MAX_UNITS;++i)game.units[i].active=0;
 game.units[0].x=2008;game.units[0].y=2008;
 game.units[10]=game.units[0];game.units[10].team=1;game.units[10].type=DOTA_TOWER;game.units[10].x=700;game.units[10].y=700;
 game.roster.field[0].source=0;game.roster.field[0].source_generation=game.units[0].generation;
 game.roster.field[0].x=2008;game.roster.field[0].y=2008;game.roster.field[0].radius=1000;
 game.roster.field[0].starts_at=game.tick;game.roster.field[0].until=game.tick+12000;game.roster.field_mask|=1u;
 attach_both(terrain,tree_bits);old_dota_sight_reset();compare("local-fixture-warm",1);
}
static void local_tree_invalidation(void){
 uint16_t saved[DOTA_STATIC_TREE_COUNT*2];int i;
 memcpy(saved,coordinates,sizeof(saved));dota_trees_detach();
 for(i=0;i<DOTA_STATIC_TREE_COUNT;++i){coordinates[i*2]=3500;coordinates[i*2+1]=1200;}
 coordinates[0]=3208;coordinates[1]=2208; /* Outside every cached source. */
 coordinates[2]=2024;coordinates[3]=2008; /* Ground hero + flying field. */
 coordinates[4]=728;coordinates[5]=700;   /* Only the enemy static tower. */
 coordinates[6]=coordinates[8]=2056;coordinates[7]=coordinates[9]=2008;
 CHECK(dota_trees_attach(coordinates,DOTA_STATIC_TREE_COUNT));
 memset(terrain,0,sizeof(terrain));memset(tree_bits,0,sizeof(tree_bits));
 local_setup();CHECK(dota_trees_cut(&game,0));old_dota_sight_invalidate();local_compare("remote-cut-reuses-all-masks-and-union",0,0);
 game.tick+=DOTA_TREE_REGROW_TICKS;dota_trees_tick(&game);old_dota_sight_invalidate();local_compare("remote-natural-regrow-reuses-all-masks-and-union",0,0);
 local_setup();CHECK(dota_trees_cut(&game,3));old_dota_sight_invalidate();local_compare("overlap-cut-no-effective-change",0,0);
 CHECK(dota_trees_cut(&game,1));old_dota_sight_invalidate();local_compare("near-hero-cut-keeps-flying-and-static",0,1);
 CHECK(dota_trees_cut(&game,2));old_dota_sight_invalidate();local_compare("near-static-tower-cut-rebuilds-only-tower",1,1);
 local_setup();game.units[10].active=0;invalidate();compare("inactive-static-source-leaves-union",1);
 CHECK(dota_trees_cut(&game,2));old_dota_sight_invalidate();local_compare("inactive-source-affected-mask-stays-stale",0,0);CHECK(!new_source_tree_current(10));
 CHECK(dota_trees_cut(&game,0));old_dota_sight_invalidate();local_compare("second-remote-change-cannot-promote-older-stale-mask",0,0);CHECK(!new_source_tree_current(10));
 game.units[10].active=1;invalidate();local_compare("reactivated-stale-source-rebuilds",1,1);CHECK(new_source_tree_current(10));
 local_setup();game.units[0].x=3208;game.units[0].y=2208;
 CHECK(dota_trees_cut(&game,0));old_dota_sight_invalidate();local_compare("moved-source-key-still-forces-cast",0,1);
 local_setup();game.item_world.tree[0].x=3208;game.item_world.tree[0].y=2208;game.item_world.tree[0].until=game.tick+100;
 invalidate();local_compare("remote-clone-overlapping-tree-keeps-effective-mask",0,0);
 game.item_world.tree[0].x=3240;invalidate();local_compare("remote-clone-new-cell-reuses-masks",0,0);
 game.item_world.tree[0].removed=1;invalidate();local_compare("remote-clone-removed-reuses-masks",0,0);
 local_setup();game.item_world.tree[0].x=2024;game.item_world.tree[0].y=2056;game.item_world.tree[0].until=game.tick+100;
 invalidate();local_compare("near-clone-new-cell-rebuilds-ground-only",0,1);
 game.item_world.tree[0].removed=1;invalidate();local_compare("near-clone-removed-rebuilds-ground-only",0,1);
 /* Geometry/fallback transitions use effective bytes too. All fallback tree
  * bytes are zero here, so detaching real trees must clear near sources. */
 dota_trees_detach();old_dota_sight_reset();invalidate();compare("geometry-detach-fallback",-1);
 CHECK(dota_trees_attach(coordinates,DOTA_STATIC_TREE_COUNT));old_dota_sight_reset();invalidate();compare("geometry-reattach-real-trees",-1);
 dota_trees_detach();memcpy(coordinates,saved,sizeof(saved));CHECK(dota_trees_attach(coordinates,DOTA_STATIC_TREE_COUNT));
 printf("LOCAL_RESULT %u %u\n",local_cases,remote_casts_saved);
}
