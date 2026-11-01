"""Exact complete-BDA tree/item/spell/regrow outcomes; modeled MIPS, no USB."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,struct,subprocess,sys,traceback
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--prepare-only',action='store_true');p.add_argument('--expected-bda-sha256');p.add_argument('--expected-elf-sha256');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build/tests')and not out.exists();out.mkdir(parents=True);(out/'objects').mkdir();(out/'fixtures').mkdir()
 production={};meta={};bda=elf=metadata=None
 if not a.prepare_only:
  assert a.build;build=a.build.resolve();bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';metadata=build/'DOTA2.build.json';meta=json.loads(metadata.read_text());assert sha(bda)==meta['bda_sha256']and sha(elf)==meta['elf_sha256']and not meta.get('diagnostic_only',False);production=meta['source_sha256']
  if a.expected_bda_sha256:assert sha(bda)==a.expected_bda_sha256
  if a.expected_elf_sha256:assert sha(elf)==a.expected_elf_sha256
  for n,v in production.items():assert sha(ROOT/n)==v,('Stale compiled input',n)
 inputs=[Path(__file__).resolve(),ROOT/'tests/trees_mips_fixtures.c',ROOT/'tests/mips_core_model.py',ROOT/'provenance/vision/trees.json',*sorted((ROOT/'game').glob('*.c')),*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'private/generated').glob('*nav.h'))];before={x.relative_to(ROOT).as_posix():sha(x)for x in inputs}
 records=json.loads((ROOT/'provenance/vision/trees.json').read_bytes())['trees'];xy=b''.join(struct.pack('<HH',t['x_q8']//256,t['y_q8']//256)for t in records);assert len(xy)==2113*4;(out/'natural-trees.xy').write_bytes(xy)
 assets={n:ROOT/'assets/runtime'/n for n in ('SCENE.H1P','VISION.H1V')}
 tree_asset=ROOT/'assets/runtime/TREE.H1T'
 if tree_asset.is_file():
  assets['TREE.H1T']=tree_asset;data=tree_asset.read_bytes();header=struct.unpack('<32I',data[:128]);assert header[11:14]==(2113,128,8580)and data[128:8580]==xy,'Final runtime tree geometry differs from canonical IDs'
 asset_hashes={n:sha(v)for n,v in assets.items()}
 for n,r in meta.get('runtime_assets',{}).items():
  if n in assets and isinstance(r,dict)and 'sha256'in r:assert sha(assets[n])==r['sha256']
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');logs=[];cc='C:/msys64/ucrt64/bin/gcc.exe'
 def run(cmd):
  r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True,timeout=180);logs.append(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr;return r.stdout
 sources=[*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/item_assets.c',ROOT/'platform/baked_io.c',ROOT/'tests/trees_mips_fixtures.c']
 def compile_(s):
  o=out/'objects'/(s.stem+'.o');run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-I',ROOT/'game','-I',ROOT/'platform','-c',s,'-o',o]);return o
 with ThreadPoolExecutor(max_workers=4)as pool:objects=list(pool.map(compile_,sources))
 exe=out/'fixtures.exe';run([cc,*objects,'-o',exe]);layout=json.loads(run([exe,out/'fixtures',assets['SCENE.H1P'],out/'natural-trees.xy',assets['VISION.H1V']]));(out/'host-layout.json').write_text(json.dumps(layout,indent=2)+'\n');(out/'host-compile.txt').write_text(''.join(logs));assert [layout[k]for k in ('game_size','unit_size','view_size','input_size','tree_size')]==[178044,240,112,20,4500]
 if a.prepare_only:
  (out/'prepared.json').write_text(json.dumps(dict(prepared=True,actual_mips32=False,source_current=before=={x.relative_to(ROOT).as_posix():sha(x)for x in inputs},source_sha256=before,host_layout_sha256=sha(out/'host-layout.json')),indent=2)+'\n');print('Prepared twelve full-host tree outcome configurations; actual BDA not executed.');return
 deps=ROOT.parent/'.tools/dota3d-deps'
 if deps.is_dir():sys.path.insert(0,str(deps))
 sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
 m=CoreMachine(bda,elf,assets);gp=m.symbols['dota_game'];ip=0x81500000;assert m.symbol_sizes['dota_game']==layout['game_size']and m.symbol_sizes['view']==layout['view_size'];checks=writes=0;cases=[];expect=layout['expected'];target=layout['target']
 def check(value,label):
  nonlocal checks;checks+=1
  if not value:raise AssertionError((label,cases[-1:]))
 def call(n,*args):return m.call(n,*args)
 def half(addr):return struct.unpack('<H',m.read(addr,2))[0]
 def setup(n):
  nonlocal writes
  data=(out/'fixtures'/(n+'.bin')).read_bytes();check(len(data)==layout['game_size'],'Initial complete Game ABI');m.write(gp,data);m.write(ip,bytes(layout['input_size']));writes+=2;call('dota_sight_reset');call('dota_nav_reset_static_trees',gp);call('dota_nav_set_item_trees',gp+layout['temporary_tree_offset'],32);call('dota_nav_set_all_structures',0,0,0)
 def regen():
  base=gp+layout['effects'];return max((half(base+i*layout['effect_size']+layout['effect_ticks'])for i in range(128)if half(base+i*layout['effect_size']+layout['effect_tag'])==layout['tango_id']),default=0)
 def state_values(a,b):return[a,b,call('dota_trees_is_cut',gp,400),half(gp+layout['charges']),half(gp+layout['cooldown']),regen(),half(gp+layout['tree_offset']+layout['tree_count']),m.integer(gp+layout['tree_offset']+layout['tree_revision'])]
 def passed(n,values):
  check(values==expect[n],(n,values,expect[n]));check(m.read(gp+layout['tree_offset'],4500)==(out/'fixtures'/(n+'.trees')).read_bytes(),'Complete tree state equals independent host outcome');cases.append(dict(case=n,passed=True,actual=values,host_expected=expect[n]));print('CASE '+n,flush=True)
 try:
  check(call('baked_io_open_default')==1 and call('item_assets_open')==1,'Original item provider opens');call('item_assets_reserve_rules_cache',65536);xy_ptr=m.alloc(len(xy));m.write(xy_ptr,xy);writes+=1;check(call('dota_trees_attach',xy_ptr,2113)==1,'Borrowed canonical original natural-tree geometry')
  sidecar=assets['VISION.H1V'].read_bytes();terrain=m.alloc(len(sidecar)-64);m.write(terrain,sidecar[64:]);writes+=1;work_bytes=call('dota_sight_workspace_bytes');work=m.alloc(work_bytes);check(call('dota_sight_attach',terrain,terrain+131072,work,work_bytes)==1,'Real terrain and original tree blocker sidecar')
  for n in ('tango','quelling','battlefury'):
   setup(n);a1=call('dota_use_item_at',gp,0,*target,-1);a2=call('dota_use_item_at',gp,0,*target,-1);passed(n,state_values(a1,a2))
  setup('branch');a1=call('dota_use_item_at',gp,0,2016,2000,-1);check(m.integer(gp+layout['temporary_tree_offset']+12)==1,'Branch tree consumed');passed('branch',state_values(a1,0))
  setup('out_of_range');a1=call('dota_use_item_at',gp,0,target[0]+8,target[1],-1);passed('out_of_range',state_values(a1,0))
  for n in ('lina_no_mana','lina'):
   setup(n);a1=call('dota_cast',gp,1,*target);passed(n,state_values(a1,0))
  for n in ('regrow','wrap_regrow'):
   setup(n);a1=call('dota_trees_is_cut',gp,400);before_pause=m.read(gp,layout['game_size']);call('dota_tick',gp,ip);check(m.read(gp,layout['game_size'])==before_pause,'Pause freezes complete Game before regrowth');call('dota_set_paused',gp,0);call('dota_tick',gp,ip);a2=call('dota_trees_is_cut',gp,400);passed(n,state_values(a1,a2))
  for n in ('pounce_landing','pounce_impact'):
   setup(n);a1=call('dota_cast',gp,1,*target)
   for _ in range(18):call('dota_tick',gp,ip)
   passed(n,state_values(a1,call('dota_extra_moving',gp,0)))
  setup('all_cut')
  for i in range(2113):check(call('dota_trees_cut',gp,i)==1,'Each source tree ID cuts once')
  vals=[call('dota_nav_walkable',*target),call('dota_sight_team_position_visible',gp,0,*target),half(gp+layout['tree_offset']+layout['tree_count'])];passed('all_cut',vals)
  check(len(cases)==12,'Twelve focused full-BDA cases');check(before=={x.relative_to(ROOT).as_posix():sha(x)for x in inputs},'Test inputs current');check(all(sha(ROOT/n)==v for n,v in production.items()),'Actual compiled production inputs current');check(sha(bda)==meta['bda_sha256']and sha(elf)==meta['elf_sha256'],'Exact final BDA and ELF unchanged');check(asset_hashes=={n:sha(v)for n,v in assets.items()},'Runtime asset evidence unchanged')
  report=dict(passed=True,actual_complete_bda=True,actual_core=True,actual_mips32=True,physical_H1_tested=False,USB_opened=False,entry_executed=False,checks=checks,cases=cases,public_ELF_calls=m.calls,source_current=True,compiled_source_sha256=production,source_sha256=before,bda_sha256=sha(bda),elf_sha256=sha(elf),metadata_sha256=sha(metadata),initial_setup_writes=writes,outcome_state_writes_after_setup=0,tree_geometry_sha256=hashlib.sha256(xy).hexdigest(),tree_count=2113,workspace_bytes=work_bytes,assets_sha256=asset_hashes,fixture_sha256={x.name:sha(x)for x in(out/'fixtures').iterdir()},scope=__doc__);(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,cases=len(cases),checks=checks,public_ELF_calls=m.calls)))
 except Exception as e:
  (out/'failure.json').write_text(json.dumps(dict(passed=False,error=str(e),cases=cases,bda_sha256=sha(bda),elf_sha256=sha(elf),USB_opened=False),indent=2)+'\n');(out/'failure.txt').write_text(traceback.format_exc());raise
 finally:m.dispose()
if __name__=='__main__':main()
