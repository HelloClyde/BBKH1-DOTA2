"""Original shadow implementation versus selected cache implementation."""
from pathlib import Path
root=Path.cwd();study=root/'build/v030/game-cost-study/host-reference';study.mkdir();old=root/'build/v030/static-sight-study'
(study/'sight-before.c').write_bytes((old/'sight-before.c').read_bytes())
fixture=(root/'tests/static_sight_union_test.c').read_text().replace('"../game/','"'+(root/'game').as_posix()+'/').replace('"../platform/','"'+(root/'platform').as_posix()+'/')
extra=r'''
static void height_edges(void){
 static const short values[]={-32768,-32752,-32751,-80,-49,-48,-47,-17,-16,-15,-1,0,15,16,17,31,32,47,48,32751,32766,32767};
 int pass,c,i,team;unsigned count=sizeof(values)/sizeof(values[0]);
 dota_trees_detach();memset(tree_bits,0,sizeof(tree_bits));tree_bits[0]=0x81;tree_bits[8191]=0x80;
 for(pass=0;pass<(int)count;++pass){
  for(c=0;c<65536;++c)terrain[c]=values[(c%256+c/256+pass)%count];
  attach_both(terrain,tree_bits);dota_init(&game,DOTA_HERO_AXE,81);game.tick=2000;old_dota_sight_reset();
  for(i=1;i<DOTA_MAX_UNITS;++i)game.units[i].active=0;
  game.units[0].x=2008;game.units[0].y=2008;
  game.roster.field[0].source=0;game.roster.field[0].source_generation=game.units[0].generation;game.roster.field[0].x=2008;game.roster.field[0].y=2008;game.roster.field[0].starts_at=game.tick;game.roster.field[0].until=game.tick+100;game.roster.field[0].radius=1000;game.roster.field_mask|=1u;
  compare("signed-height-rounding-unknown-flying-fallback",1);
 }
 for(team=0;team<2;++team){const uint32_t*p=dota_sight_current_bits(&game,team);CHECK(p);for(c=0;c<65536;++c)CHECK(((p[(unsigned)c>>5]>>(c&31))&1u)==(unsigned)(dota_sight_fog_state(&game,team,(c%256)*16+8,(c/256)*16+8)==2));}
 CHECK(!dota_sight_current_bits(0,0));CHECK(!dota_sight_current_bits(&game,2));CHECK(!dota_sight_current_bits(&game,-1));
}
'''
fixture=fixture.replace('int main(int argc,char**argv){',extra+'\nint main(int argc,char**argv){').replace('mutations();fine_radius();slark_and_failure','mutations();fine_radius();height_edges();slark_and_failure')
(study/'enhanced-test.c').write_text(fixture,newline='\n')
text=(old/'run-host.py').read_text().replace("base=root/'build/v030/static-sight-study';out=base/'host-run3'","base=root/'build/v030/game-cost-study/host-reference';out=base/'host-run1'")
text=text.replace("root/'tests/static_sight_union_test.c'","base/'enhanced-test.c'")
text=text.replace('#include "../../../game/sight.c"','#include "'+(root/'game/sight.c').as_posix()+'"')
exec(compile(text,str(study/'run-host.py'),'exec'))
