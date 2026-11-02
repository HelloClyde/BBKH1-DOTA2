"""Selected final-class prototype versus frozen original, including dirty planes."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib,json,os,re,subprocess
root=Path.cwd();base=root/'build/v030/game-cost-study/host-finalclass';base.mkdir();out=base/'host-run1';out.mkdir();(out/'objects').mkdir();sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
previous=root/'build/v030/game-cost-study/host-bounded2';variant=root/'build/v030/game-cost-study/finalclass/sight-variant.c'
(base/'sight-before.c').write_bytes((previous/'sight-before.c').read_bytes())
wrapper=base/'old-wrapper.c';wrapper.write_bytes((previous/'old-wrapper.c').read_bytes())
newwrapper=base/'new-wrapper.c';newwrapper.write_text((previous/'new-wrapper.c').read_text().replace((root/'game/sight.c').as_posix(),variant.as_posix())+'''\nint new_tree_cell(int x,int y){return work->tree_cells[work->active_tree_plane][(y/CELL)*GRID+x/CELL];}
int new_cached_cell(int h,int r,int f,int x,int y){int i;for(i=0;i<OBSTACLE_PLANES;++i){SightObstaclePlane*p=&work->obstacle_planes[i];if(p->valid&&p->height==h&&p->region==r&&p->flying==f)return p->cells[(y/CELL)*GRID+x/CELL];}return -1;}
''',newline='\n')
dirty=r'''
int new_tree_cell(int,int),new_cached_cell(int,int,int,int,int);
static void dirty_overlapping_trees(void){
 uint16_t saved[4];int i;memcpy(saved,coordinates,sizeof(saved));dota_trees_detach();
 coordinates[0]=coordinates[2]=2008;coordinates[1]=coordinates[3]=2008;CHECK(dota_trees_attach(coordinates,DOTA_STATIC_TREE_COUNT));
 memset(terrain,0,sizeof(terrain));memset(tree_bits,0,sizeof(tree_bits));terrain[(2008/16)*256+2024/16]=64;attach_both(terrain,tree_bits);
 dota_init(&game,DOTA_HERO_AXE,93);game.tick=2000;old_dota_sight_reset();for(i=1;i<DOTA_MAX_UNITS;++i)game.units[i].active=0;game.units[0].x=2008;game.units[0].y=2008;
 game.roster.field[0].source=0;game.roster.field[0].source_generation=game.units[0].generation;game.roster.field[0].x=2008;game.roster.field[0].y=2008;game.roster.field[0].starts_at=game.tick;game.roster.field[0].until=game.tick+DOTA_TREE_REGROW_TICKS+20;game.roster.field[0].radius=1000;game.roster.field_mask|=1u;
 compare("dirty-overlapping-two-trees-initial",1);CHECK(new_tree_cell(2008,2008)==1);CHECK(new_cached_cell(0,0,0,2008,2008)==1);CHECK(new_cached_cell(0,0,1,2008,2008)==0);CHECK(new_cached_cell(0,0,0,2024,2008)==2);
 CHECK(dota_trees_cut(&game,0));old_dota_sight_invalidate();compare("dirty-overlap-first-cut-still-blocked",1);CHECK(new_tree_cell(2008,2008)==1);CHECK(new_cached_cell(0,0,0,2008,2008)==1);
 ++game.tick;CHECK(dota_trees_cut(&game,1));old_dota_sight_invalidate();compare("dirty-overlap-last-cut-opens",1);CHECK(new_tree_cell(2008,2008)==0);CHECK(new_cached_cell(0,0,0,2008,2008)==0);CHECK(new_cached_cell(0,0,1,2008,2008)==0);
 game.item_world.tree[0].x=2024;game.item_world.tree[0].y=2008;game.item_world.tree[0].until=game.tick+100;invalidate();compare("dirty-tree-at-terrain-wall-keeps-wall2",1);CHECK(new_tree_cell(2024,2008)==1);CHECK(new_cached_cell(0,0,0,2024,2008)==2);CHECK(new_cached_cell(0,0,1,2024,2008)==0);
 game.item_world.tree[0].removed=1;invalidate();compare("dirty-remove-tree-at-wall-keeps-wall2",1);CHECK(new_tree_cell(2024,2008)==0);CHECK(new_cached_cell(0,0,0,2024,2008)==2);
 game.tick=2000+DOTA_TREE_REGROW_TICKS;dota_trees_tick(&game);old_dota_sight_invalidate();compare("dirty-overlap-first-natural-regrow-blocks",1);CHECK(!dota_trees_is_cut(&game,0)&&dota_trees_is_cut(&game,1));CHECK(new_cached_cell(0,0,0,2008,2008)==1);CHECK(new_cached_cell(0,0,1,2008,2008)==0);
 ++game.tick;dota_trees_tick(&game);old_dota_sight_invalidate();compare("dirty-overlap-second-natural-regrow",1);CHECK(!dota_trees_is_cut(&game,1));CHECK(new_cached_cell(0,0,0,2008,2008)==1);
 dota_trees_detach();memcpy(coordinates,saved,sizeof(saved));CHECK(dota_trees_attach(coordinates,DOTA_STATIC_TREE_COUNT));
}
'''
fixture=base/'enhanced-test.c';body=(previous/'enhanced-test.c').read_text().replace('int main(int argc,char**argv){',dirty+'\nint main(int argc,char**argv){').replace('height_edges();bounded_classes();slark_and_failure','height_edges();bounded_classes();dirty_overlapping_trees();slark_and_failure');fixture.write_text(body,newline='\n')
rename=['dota_sight_workspace_bytes','dota_sight_invalidate','dota_sight_reset','dota_sight_detach','dota_sight_attach','dota_sight_is_current','dota_rules_unit_vision_source','dota_sight_team_position_visible','dota_sight_position_visible','dota_sight_team_visible','dota_sight_fog_state','h1_shared_shadow_profile']
sources=[fixture,*[p for p in sorted((root/'game').glob('*.c'))if p.name!='sight.c'],root/'platform/baked_io.c',root/'platform/item_assets.c',root/'platform/vision_assets.c',root/'platform/map_format.c',wrapper,newwrapper];inputs=[*sources,variant,base/'sight-before.c',*sorted((root/'game').glob('*.h'))];identity=lambda:{str(p.relative_to(root)):sha(p)for p in inputs};before=identity();env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cc='C:/msys64/ucrt64/bin/gcc.exe';logs=[]
def run(cmd):
 r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True);logs.append(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
def compile(p):
 obj=out/'objects'/(p.stem+'.o');defs=['-D'+n+'=old_'+n for n in rename]if p==wrapper else[];run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-Igame','-Iplatform',*defs,'-c',p,'-o',obj]);return obj
with ThreadPoolExecutor(max_workers=4)as pool:objects=list(pool.map(compile,sources))
exe=out/'host.exe';run([cc,*objects,'-o',exe]);(out/'compile.txt').write_text(''.join(logs));r=subprocess.run([str(exe),str(root/'build/v030/legacy/SCENE.H1P'),str(root/'build/v030/legacy/VISION.H1V'),str(root/'build/v030/legacy/TREE.H1T')],env=env,capture_output=True,text=True);(out/'output.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert not r.returncode
d=json.loads(re.search(r'^RESULT (.*)$',r.stdout,re.M)[1]);d.update(diagnostic_only=True,source_current=identity()==before,source_sha256=before,prototype_sight_sha256=sha(variant),binary_sha256=sha(exe),USB_opened=False);(out/'verification.json').write_text(json.dumps(d,indent=2)+'\n')
