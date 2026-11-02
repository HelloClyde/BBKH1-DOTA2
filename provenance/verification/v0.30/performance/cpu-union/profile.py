"""Complete-BDA warm2/ticks2 workload; instruction slots are not physical FPS."""
from pathlib import Path
import bisect,hashlib,json,struct,sys,time
from collections import Counter
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_BLOCK
ROOT=Path.cwd();OUT=ROOT/'build/v030/perf-union-cpu';sys.path.insert(0,str(ROOT/'tests'))
from mips_core_model import CoreMachine
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
BUNDLE=ROOT/'assets/runtime/DOTA2.H1R';VISION=ROOT/'build/v030/legacy/VISION.H1V';TREE=ROOT/'build/v030/legacy/TREE.H1T';BASE=ROOT/'build/v030/perf-baseline-aligned'
source_ext={'.c','.h','.s','.ld'}
vision=VISION.read_bytes();vh=struct.unpack('<16I',vision[:64]);assert vh[7]==131072 and vh[9]==8192
xy=TREE.read_bytes()[128:128+2113*4];assert len(xy)==8452
with BUNDLE.open('rb')as f:
 f.seek(128+96*2);vs=f.read(96);assert struct.unpack_from('<I',vs,8)[0]==len(vision) and vs[16:48].hex()==sha(VISION)
baseline=json.loads((BASE/'verification.json').read_bytes());assert baseline['passed'] and baseline['physical_h1_tested'] and baseline['bda_sha256']=='d569b2ae381441ea5af09f61d4b956c97f5077e81a65909734a9d18b43365920'
def run(label,build,snapshot):
 out=OUT/(snapshot.stem+'-'+label);out.mkdir();meta_path=build/'DOTA2.build.json';meta=json.loads(meta_path.read_bytes());bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';assert sha(bda)==meta['bda_sha256'] and sha(elf)==meta['elf_sha256'];snap=snapshot.read_bytes();assert len(snap)==meta['binary']['abi']['DotaGame']==179008
 compiled={n:h for n,h in meta['source_sha256'].items()if Path(n).suffix.lower()in source_ext};current={n:sha(ROOT/n)for n in compiled};drift={n:{'compiled_sha256':h,'current_sha256':current[n]}for n,h in compiled.items()if current[n]!=h}
 with elf.open('rb')as f:
  symbols=sorted((int(s['st_value']),int(s['st_size']),s.name)for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()if s['st_info']['type']=='STT_FUNC'and int(s['st_size'])>0)
 starts=[x[0]for x in symbols];startset=set(starts);blockmeta={};totals=Counter();entries=Counter();division_functions=Counter();allslots=divisions=0;recording=False;per_tick=[]
 m=CoreMachine(bda,elf,{'DOTA2.H1R':BUNDLE});gp=m.symbols['dota_game'];ip=0x81500000;assert m.symbol_sizes['dota_game']==len(snap)
 def block(uc,address,size,user):
  nonlocal allslots,divisions
  if not recording:return
  key=(address,size);data=blockmeta.get(key)
  if data is None:
   i=bisect.bisect_right(starts,address)-1;owner=symbols[i][2]if i>=0 and address<symbols[i][0]+symbols[i][1]else'outside-symbol';words=struct.unpack('<'+'I'*(size//4),m.read(address,size));divs=sum((w>>26)==0 and (w&63)in(26,27)for w in words);data=(owner,size//4,address in startset,divs);blockmeta[key]=data
  owner,slots,entry,divs=data;totals[owner]+=slots;allslots+=slots;divisions+=divs;division_functions[owner]+=divs
  if entry:entries[owner]+=1
 hook=m.uc.hook_add(UC_HOOK_BLOCK,block);m.uc.ctl_flush_tb();start=time.monotonic()
 try:
  assert m.call('baked_io_open_default')==1 and m.call('item_assets_open')==1;m.call('item_assets_reserve_rules_cache',65536)
  terrain=m.alloc(vh[10]);m.write(terrain,vision[64:]);workbytes=m.call('dota_sight_workspace_bytes');workspace=m.alloc(workbytes);assert m.call('dota_sight_attach',terrain,terrain+131072,workspace,workbytes)==1
  treeptr=m.alloc(len(xy));m.write(treeptr,xy);assert m.call('dota_trees_attach',treeptr,2113)==1
  m.write(gp,snap);m.write(ip,bytes(20));m.call('dota_sight_reset');m.call('dota_set_paused',gp,0)
  for _ in range(2):m.call('dota_tick',gp,ip)
  (out/'start-game.bin').write_bytes(m.read(gp,len(snap)));recording=True
  for i in range(2):
   before=allslots;prevdiv=divisions;prev=Counter(totals);preventry=Counter(entries);reads=m.bytes_read;profile=list(struct.unpack('<5I',m.read(m.symbols['h1_shared_shadow_profile'],20)))
   m.call('dota_tick',gp,ip);m.call('dota_sight_fog_state',gp,0,2008,2008)
   game=m.read(gp,len(snap));fog=m.read(workspace+4,32768);vis=m.read(m.symbols['visible'],510);(out/f'game-{i:03}.bin').write_bytes(game);(out/f'fog-{i:03}.bin').write_bytes(fog+vis)
   per_tick.append(dict(index=i,slots=allslots-before,divisions=divisions-prevdiv,game_sha256=sha(out/f'game-{i:03}.bin'),fog_and_unit_visibility_sha256=sha(out/f'fog-{i:03}.bin'),functions=dict(totals-prev),entries=dict(entries-preventry),modeled_read_bytes=m.bytes_read-reads,shadow_counter_delta=[v-b for v,b in zip(struct.unpack('<5I',m.read(m.symbols['h1_shared_shadow_profile'],20)),profile)]))
  recording=False;(out/'end-game.bin').write_bytes(m.read(gp,len(snap)))
  m.call('dota_trees_detach');m.call('dota_sight_detach');m.free(treeptr);m.free(workspace);m.free(terrain);m.call('item_assets_close');m.call('baked_io_close');assert m.call('resource_unmount')==1 and not m.allocations and not m.handles
  assert all(sha(ROOT/n)==h for n,h in current.items())
  report=dict(passed=True,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,bda_sha256=sha(bda),elf_sha256=sha(elf),metadata_sha256=sha(meta_path),device_snapshot=str(snapshot.relative_to(ROOT)),device_snapshot_sha256=sha(snapshot),device_capture_receipt_sha256=sha(BASE/'verification.json'),warm_ticks=2,measured_ticks=2,workspace_bytes=workbytes,slots=allslots,mean_slots=allslots/2,divisions=divisions,functions=totals.most_common(),entries=dict(entries),division_functions=division_functions.most_common(),per_tick=per_tick,source_current=not drift,bound_compiled_sources=compiled,compiled_source_drift=drift,source_files_unchanged_during_test=True,profile_source_sha256=sha(__file__),model_sha256=sha(ROOT/'tests/mips_core_model.py'),original_profiler_sha256=sha(ROOT/'tests/profile_actual_vision.py'),start_game_sha256=sha(out/'start-game.bin'),end_game_sha256=sha(out/'end-game.bin'),assets_sha256={'DOTA2.H1R':sha(BUNDLE),'VISION.H1V':sha(VISION),'TREE.H1T':sha(TREE)},cleanup={'allocations':0,'open_handles':0},emulator_wall_seconds=time.monotonic()-start,scope='Same unmodified physical Game snapshot loaded as initial state; unpause API and two empty-input warm ticks, then two measured ticks plus fog query. Exact BDA APIs with modeled read-only H1 filesystem/heap. Borrowed terrain/tree geometry is identical; workspace size queried from each BDA. No native rendering, audio, firmware scheduling, cache/bus latency, real division timing or physical FPS measurement.')
  (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(label,snapshot.stem,report['mean_slots'],[x['slots']for x in per_tick],flush=True);return report,out
 finally:m.uc.hook_del(hook);m.dispose()
results=[]
for name in ('fountain-paused-game.bin','post-horn-paused-game.bin'):
 old,op=run('original',ROOT/'build/v030/features-candidate2',BASE/name);new,np=run('union',ROOT/'build/v030/perf-candidate3',BASE/name)
 equal={p.name:p.read_bytes()==(np/p.name).read_bytes()for p in op.glob('*.bin')};assert all(equal.values()),equal
 compare=dict(snapshot=name,device_snapshot_sha256=old['device_snapshot_sha256'],original_report=str((op/'verification.json').relative_to(ROOT)),original_report_sha256=sha(op/'verification.json'),union_report=str((np/'verification.json').relative_to(ROOT)),union_report_sha256=sha(np/'verification.json'),all_game_fog_unit_visibility_byte_identical=True,compared_outputs=equal,original_mean_slots=old['mean_slots'],union_mean_slots=new['mean_slots'],saved_mean_slots=old['mean_slots']-new['mean_slots'],slot_reduction_percent=100*(old['mean_slots']-new['mean_slots'])/old['mean_slots'],original_per_tick_slots=[t['slots']for t in old['per_tick']],union_per_tick_slots=[t['slots']for t in new['per_tick']]);results.append(compare);print(json.dumps(compare),flush=True)
summary=dict(passed=True,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,warm_ticks=2,measured_ticks=2,original_bda_sha256=old['bda_sha256'],union_bda_sha256=new['bda_sha256'],original_workspace_bytes=old['workspace_bytes'],union_workspace_bytes=new['workspace_bytes'],results=results,profile_source_sha256=sha(__file__),scope='CPU instruction-slot comparison only, no FPS conversion. Each pair starts from the same original device Game snapshot. Full post-warm Game and both measured Game/current-fog/explored-fog/unit-visibility outputs are byte-identical. Original compiled source SHA is preserved with explicit current-source drift, never forced current.')
(OUT/'verification.json').write_text(json.dumps(summary,indent=2)+'\n')
