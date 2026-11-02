"""Final complete-BDA vision/ward/Slark outcomes; no entry, USB or FPS claim."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,struct,subprocess,sys,traceback
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path);p.add_argument('--output',type=Path);p.add_argument('--legacy',type=Path,default=ROOT/'build/v030/legacy');p.add_argument('--bundle',type=Path,default=ROOT/'assets/runtime/DOTA2.H1R');p.add_argument('--expected-bda-sha256');p.add_argument('--expected-elf-sha256');p.add_argument('--check-interface',action='store_true');a=p.parse_args()
 if a.check_interface:print(json.dumps(dict(prepared=True,cases=8,actual_bda_executed=False,outcome_state_writes_after_setup=0,USB_opened=False)));return
 assert a.build and a.output;build=a.build.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);(out/'objects').mkdir();(out/'fixtures').mkdir()
 bda,elf,metadata=build/'DOTA2.bda',build/'DOTA2.elf',build/'DOTA2.build.json';meta=json.loads(metadata.read_text(encoding='utf-8'));bda_sha,elf_sha,meta_sha=sha(bda),sha(elf),sha(metadata);assert bda_sha==meta['bda_sha256'] and elf_sha==meta['elf_sha256'] and not meta.get('diagnostic_only',False)
 if a.expected_bda_sha256:assert bda_sha==a.expected_bda_sha256
 if a.expected_elf_sha256:assert elf_sha==a.expected_elf_sha256
 production=meta['source_sha256']
 for name,value in production.items():assert sha(ROOT/name)==value,('Stale compiled input',name)
 test_inputs=[Path(__file__).resolve(),ROOT/'tests/vision_mips_fixtures.c',ROOT/'tests/mips_core_model.py'];before={x.relative_to(ROOT).as_posix():sha(x) for x in test_inputs}
 assets={'SCENE.H1P':a.legacy/'SCENE.H1P','VISION.H1V':a.legacy/'VISION.H1V'};asset_hashes={n:sha(v) for n,v in assets.items()};asset_hashes['DOTA2.H1R']=sha(a.bundle);assert asset_hashes['DOTA2.H1R']==meta['runtime_assets']['DOTA2.H1R']['sha256'];cc=Path('C:/msys64/ucrt64/bin/gcc.exe');env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','');logs=[]
 with a.bundle.open('rb')as stream:
  for sid,name in ((0,'SCENE.H1P'),(2,'VISION.H1V')):
   stream.seek(128+96*sid);section=stream.read(96);assert struct.unpack_from('<I',section,8)[0]==assets[name].stat().st_size and section[16:48].hex()==asset_hashes[name],('Host reference differs from final H1R logical stream',name)
 for name,receipt in meta.get('runtime_assets',{}).items():
  if name in assets and isinstance(receipt,dict) and 'sha256' in receipt:assert asset_hashes[name]==receipt['sha256'],('Runtime asset differs from exact final build',name)
 def run(cmd):
  r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);logs.append(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
 sources=[*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/item_assets.c',ROOT/'platform/baked_io.c',ROOT/'tests/vision_mips_fixtures.c']
 def compile_one(source):
  obj=out/'objects'/(source.stem+'.o');run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-I',ROOT/'game','-I',ROOT/'platform','-c',source,'-o',obj]);return obj
 with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,sources))
 generator=out/'fixtures.exe';run([cc,*objects,'-o',generator]);layout=json.loads(run([generator,out/'fixtures',assets['SCENE.H1P'],assets['VISION.H1V']]));(out/'layout-and-host-results.json').write_text(json.dumps(layout,indent=2)+'\n',encoding='utf-8');(out/'compile.txt').write_text(''.join(logs),encoding='utf-8');assert (layout['game_size'],layout['unit_size'],layout['input_size'])==(meta['binary']['abi']['DotaGame'],meta['binary']['abi']['DotaUnit'],20)
 deps=ROOT.parent/'.tools/dota3d-deps'
 if deps.is_dir():sys.path.insert(0,str(deps))
 sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
 m=CoreMachine(bda,elf,{'DOTA2.H1R':a.bundle});assert m.symbol_sizes['view']==meta['binary']['abi']['DotaView'];gp=m.symbols['dota_game'];ip=0x81500000;cases=[];checks=setup_writes=0;expect=layout['expected'];hx,hy=layout['hill_pair'][2:];ward=layout['item_first']
 def check(value,label):
  nonlocal checks;checks+=1
  if not value:raise AssertionError((label,cases[-1:]))
 def core(name,*args):return m.call(name,*args)
 def u(index,field):return m.integer(gp+layout['units']+index*layout['unit_size']+layout['unit_'+field])
 def charges():return struct.unpack('<H',m.read(gp+layout['charges'],2))[0]
 def world(slot,field):return m.integer(gp+layout['world_first']+slot*layout['world_stride']+layout['world_'+field])
 def setup(name):
  nonlocal setup_writes
  data=(out/'fixtures'/(name+'.bin')).read_bytes();check(len(data)==m.symbol_sizes['dota_game']==layout['game_size'],'Initial Game ABI matches actual ELF');m.write(gp,data);m.write(ip,bytes(layout['input_size']));setup_writes+=2;core('dota_sight_reset')
 def tick(count):
  for _ in range(count):core('dota_tick',gp,ip)
 def passed(name,actual,wanted):check(actual==wanted,(name,actual,wanted));cases.append(dict(case=name,passed=True,actual=actual,host_expected=wanted));print('CASE '+name,flush=True)
 try:
  check(core('baked_io_open_default')==1 and core('item_assets_open')==1,'Final original item provider opens');core('item_assets_reserve_rules_cache',65536)
  sidecar=assets['VISION.H1V'].read_bytes();header=struct.unpack('<16I',sidecar[:64]);check(header[4]==256 and header[7]==131072 and header[9]==8192,'Actual original terrain sidecar shape')
  terrain=m.alloc(header[10]);m.write(terrain,sidecar[64:]);setup_writes+=1;work_bytes=core('dota_sight_workspace_bytes');workspace=m.alloc(work_bytes);check(core('dota_sight_attach',terrain,terrain+131072,workspace,work_bytes-4)==0,'Actual MIPS rejects an undersized borrowed workspace');check(core('dota_sight_attach',terrain,terrain+131072,workspace,work_bytes)==1,'Production core borrows real sidecar and heap')
  setup('combined');values=[core('dota_items_use',gp,0,0,hx,hy,-2),charges(),core('dota_items_use',gp,0,0,hx,hy,-2),core('dota_items_use',gp,0,0,hx,hy,-1),charges(),u(0,'inventory'),world(0,'until'),world(1,'until')];check(world(0,'kind')==3 and world(1,'kind')==2,'Selected Sentry then Observer exact real unit kinds');passed('combined-sentry-selection-consumes-once-and-observer-remainder',values,expect['combined'])
  terrain_values=[]
  for fixture in ('tree','hill_low','hill_high'):setup(fixture);terrain_values.append(core('dota_unit_visible',gp,0,1))
  setup('pit_unlit');cave=[core('dota_unit_visible',gp,0,1)];no_pit_ward=core('dota_item_world_ward_placeable',gp,0,u(1,'x'),u(1,'y'));setup('pit_lit');cave.extend([core('dota_unit_visible',gp,0,1),no_pit_ward])
  passed('original-tree-highground-and-per-source-cave-shared-sight',terrain_values+cave,expect['terrain']+expect['pit'])
  setup('lifetime');values=[core('dota_unit_visible',gp,0,1)];tick(2);values.extend([core('dota_unit_visible',gp,0,1),u(ward,'active')]);passed('ward-shared-sight-is-removed-on-normal-tick-expiry',values,expect['lifetime'])
  values=[]
  for fixture in ('detected','no_ground_sight'):setup(fixture);values.append(core('dota_unit_visible',gp,0,ward))
  setup('smoke');values.append(core('dota_unit_visible',gp,0,1));core('dota_item_world_tick',gp);values.append(core('dota_unit_visible',gp,0,1));passed('true-sight-needs-ground-sight-and-does-not-pierce-smoke-dust',values,expect['detection'])
  setup('slark_cast');values=[core('dota_extra_shadow_dance_passive_active',gp,0),core('dota_items_use',gp,1,0,hx,hy,-1),core('dota_extra_shadow_dance_passive_active',gp,0),core('dota_cast',gp,3,hx,hy),core('dota_extra_shadow_dance_passive_active',gp,0),core('dota_unit_visible',gp,1,0)];passed('enemy-observer-placement-disables-passive-but-active-R-hides',values,expect['slark_cast'])
  setup('slark_delay');values=[core('dota_extra_shadow_dance_passive_active',gp,0)];tick(1);values.append(core('dota_extra_shadow_dance_passive_active',gp,0));tick(8);values.append(core('dota_extra_shadow_dance_passive_active',gp,0));tick(6);values.append(core('dota_extra_shadow_dance_passive_active',gp,0));passed('slark-recovery-waits-activation-delay-after-ward-expiry',values,expect['slark_delay'])
  values=[]
  for fixture in ('day_to_night','night_to_day'):
   setup(fixture);x,y=layout['day_night_point'];values.extend([core('dota_sight_fog_state',gp,0,x,y),core('dota_match_is_night',gp)]);tick(1);values.extend([core('dota_sight_fog_state',gp,0,x,y),core('dota_match_is_night',gp)])
  day=expect['day_night'];passed('static-tower-union-refreshes-on-natural-day-and-night-boundaries',values,[day[0],0,day[2],1,day[4],1,day[6],0])
  blocked=[]
  for entity,(x,y) in enumerate(layout['blocked_positions']):
   for team in range(2):
    setup(f'blocked_{entity}_{team}');unit_begin=gp+layout['units']+ward*layout['unit_size'];world_begin=gp+layout['world_first'];unit_before=m.read(unit_begin,32*layout['unit_size']);world_before=m.read(world_begin,32*layout['world_stride']);q=core('dota_item_world_ward_placeable',gp,0,x,y);r=core('dota_items_use',gp,0,0,x,y,-2);low=core('dota_rules_item_primitive',gp,layout['ward_primitive_encoded'],0,-1,x,y,0,7200);cd=struct.unpack('<H',m.read(gp+layout['item_cooldown'],2))[0];slots=sum(bool(u(i,'active')) for i in range(ward,ward+32));blocked.append([q,r,low,charges(),cd,slots]);check(m.read(unit_begin,32*layout['unit_size'])==unit_before and m.read(world_begin,32*layout['world_stride'])==world_before,'Forbidden placement never modifies or consumes a world slot')
  x,y,_,_=layout['cliff_pair'];setup('ward_cliff');cliff=[core('dota_item_world_ward_placeable',gp,0,x,y),core('dota_items_use',gp,0,0,x,y,-1),charges(),u(ward,'x'),u(ward,'y')];passed('all-five-native-zones-reject-atomically-and-legal-ward-cliff-remains',blocked+[cliff],expect['blocked_wards']+[expect['ward_cliff']])
  check(len(cases)==8,'Eight meaningful complete-BDA cases');check(before=={x.relative_to(ROOT).as_posix():sha(x) for x in test_inputs},'Test sources current');check(all(sha(ROOT/name)==value for name,value in production.items()),'Actual compiled production inputs current');check(sha(bda)==bda_sha and sha(elf)==elf_sha and sha(metadata)==meta_sha,'Candidate BDA ELF and metadata unchanged');check(all(asset_hashes[n]==sha(v) for n,v in assets.items()) and asset_hashes['DOTA2.H1R']==sha(a.bundle),'Real runtime assets unchanged')
  core('dota_sight_detach');m.free(workspace);m.free(terrain);core('item_assets_close');core('baked_io_close');check(core('resource_unmount')==1 and not m.allocations and not m.handles,'Detach, caller-owned workspace release and final resource close leave no allocations or handles')
  result=dict(passed=True,actual_core=True,actual_mips32=True,source_current=True,physical_h1_tested=False,entry_executed=False,USB_opened=False,outcome_state_writes_after_setup=0,initial_setup_writes=setup_writes,checks=checks,cases=cases,public_ELF_calls=m.calls,bda_sha256=bda_sha,elf_sha256=elf_sha,metadata_sha256=meta_sha,compiled_source_sha256=production,source_sha256=before,assets_sha256=asset_hashes,workspace_bytes=work_bytes,cleanup=dict(allocations=0,open_handles=0),fixture_generator_sha256=sha(generator),fixture_sha256={x.relative_to(ROOT).as_posix():sha(x) for x in (out/'fixtures').glob('*.bin')},layout_and_host_results_sha256=sha(out/'layout-and-host-results.json'),scope='Exact final complete BDA core APIs under modeled read-only H1 file and heap services, cross-checked against full host C core with actual item/terrain assets. Initial Game/input/terrain configuration only; no outcome patching, entry, native rendering, audio, MXU, physical H1 or FPS claim.')
  (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(dict(passed=True,cases=len(cases),checks=checks,public_ELF_calls=m.calls,bda_sha256=bda_sha)))
 except Exception as error:
  (out/'failure.json').write_text(json.dumps(dict(passed=False,error=str(error),cases_completed=cases,bda_sha256=bda_sha,elf_sha256=elf_sha,source_sha256=before,USB_opened=False,outcome_state_writes_after_setup=0),indent=2)+'\n',encoding='utf-8');(out/'failure.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
 finally:m.dispose()
if __name__=='__main__':main()
