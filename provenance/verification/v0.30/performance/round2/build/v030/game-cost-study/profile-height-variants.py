"""Exact diagnostic-BDA height alternatives; instruction slots are not FPS."""
from pathlib import Path
import bisect,collections,hashlib,json,struct,sys,time
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_BLOCK
ROOT=Path.cwd();OUT=ROOT/'build/v030/game-cost-study';sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
vision=(ROOT/'build/v030/legacy/VISION.H1V').read_bytes();xy=(ROOT/'build/v030/legacy/TREE.H1T').read_bytes()[128:128+2113*4]
def run(label,snapshot):
 build=OUT/label;out=build/snapshot.stem
 if (out/'verification.json').exists():return json.loads((out/'verification.json').read_bytes()),out
 if out.exists():out=build/(snapshot.stem+'-retry2')
 assert not out.exists();out.mkdir();meta=json.loads((build/'DOTA2.build.json').read_bytes())
 with(build/'DOTA2.elf').open('rb')as f:syms=sorted((int(s['st_value']),int(s['st_size']),s.name)for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()if s['st_info']['type']=='STT_FUNC'and int(s['st_size'])>0)
 starts=[s[0]for s in syms];cache={};total=0;functions=collections.Counter();record=False
 m=CoreMachine(build/'DOTA2.bda',build/'DOTA2.elf',{'DOTA2.H1R':ROOT/'assets/runtime/DOTA2.H1R'})
 def block(uc,address,size,user):
  nonlocal total
  if not record:return
  key=(address,size);v=cache.get(key)
  if v is None:
   i=bisect.bisect_right(starts,address)-1;name=syms[i][2]if i>=0 and address<syms[i][0]+syms[i][1]else'outside';v=(name,size//4);cache[key]=v
  name,slots=v;total+=slots;functions[name]+=slots
 hook=m.uc.hook_add(UC_HOOK_BLOCK,block);m.uc.ctl_flush_tb();rows=[];t0=time.monotonic();getter_checks=0
 try:
  assert m.call('baked_io_open_default')==1 and m.call('item_assets_open')==1;m.call('item_assets_reserve_rules_cache',65536)
  terrain=m.alloc(len(vision)-64);m.write(terrain,vision[64:]);wb=m.call('dota_sight_workspace_bytes');ws=m.alloc(wb);assert m.call('dota_sight_attach',terrain,terrain+131072,ws,wb)==1
  tp=m.alloc(len(xy));m.write(tp,xy);assert m.call('dota_trees_attach',tp,2113)==1
  gp=m.symbols['dota_game'];ip=0x81500000;m.write(gp,snapshot.read_bytes());m.write(ip,bytes(20));m.call('dota_sight_reset');m.call('dota_set_paused',gp,0)
  for _ in range(2):m.call('dota_tick',gp,ip)
  (out/'start-game.bin').write_bytes(m.read(gp,179008));record=True
  for i in range(8):
   before=total;prev=collections.Counter(functions);reads=m.bytes_read;m.call('dota_tick',gp,ip);m.call('dota_sight_fog_state',gp,0,2008,2008)
   game=m.read(gp,179008);fog=m.read(ws+4,32768)+m.read(m.symbols['visible'],510);(out/f'game-{i:03}.bin').write_bytes(game);(out/f'fog-{i:03}.bin').write_bytes(fog)
   rows.append(dict(index=i,slots=total-before,game_sha256=sha(out/f'game-{i:03}.bin'),fog_sha256=sha(out/f'fog-{i:03}.bin'),functions=dict(functions-prev),modeled_read_bytes=m.bytes_read-reads))
  record=False
  if label=='rawlimit' and snapshot.name=='post-horn-paused-game.bin':
   assert m.call('dota_sight_current_bits',0,0)==0 and m.call('dota_sight_current_bits',gp,2)==0 and m.call('dota_sight_current_bits',gp,-1)==0;getter_checks+=3
   for team in range(2):
    p=m.call('dota_sight_current_bits',gp,team)&0xffffffff;assert p==ws+4+team*8192;data=m.read(p,8192)
    for c in range(65536):
     state=m.call('dota_sight_fog_state',gp,team,(c%256)*16+8,(c//256)*16+8);assert ((data[c>>3]>>(c&7))&1)==(state==2);getter_checks+=1
  m.call('dota_trees_detach');m.call('dota_sight_detach');assert m.call('dota_sight_current_bits',gp,0)==0;getter_checks+=1
  m.free(tp);m.free(ws);m.free(terrain);m.call('item_assets_close');m.call('baked_io_close');assert m.call('resource_unmount')==1 and not m.allocations and not m.handles
 finally:m.uc.hook_del(hook);m.dispose()
 result=dict(passed=True,label=label,bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],diagnostic_only=True,physical_h1_tested=False,USB_opened=False,snapshot_sha256=sha(snapshot),workspace_bytes=wb,warm_ticks=2,measured_ticks=8,total_slots=total,mean_slots=total/8,functions=functions.most_common(),per_tick=rows,getter_checks=getter_checks,profile_source_sha256=sha(__file__),elapsed_seconds=time.monotonic()-t0,scope=__doc__)
 (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(label,snapshot.stem,result['mean_slots'],wb,'getter',getter_checks,flush=True);return result,out
results=[]
for name in ('fountain-paused-game.bin','post-horn-paused-game.bin'):
 paths=[]
 for label in ('base','heightplane','rawlimit'):
  result,path=run(label,ROOT/'build/v030/perf-baseline-aligned'/name);paths.append((result,path))
 base,bp=paths[0]
 for test,tp in paths[1:]:
  equality={f.name:f.read_bytes()==(tp/f.name).read_bytes()for f in bp.glob('*.bin')};assert all(equality.values()),(test['label'],equality)
  row=dict(snapshot=name,label=test['label'],mean_slots=test['mean_slots'],base_mean_slots=base['mean_slots'],reduction_percent=100*(base['mean_slots']-test['mean_slots'])/base['mean_slots'],workspace_bytes=test['workspace_bytes'],all_game_fog_visibility_byte_identical=True,compared_files=equality,getter_checks=test['getter_checks']);results.append(row);print(json.dumps(row),flush=True)
(OUT/'height-variants-summary.json').write_text(json.dumps(dict(passed=True,physical_h1_tested=False,diagnostic_only=True,profile_source_sha256=sha(__file__),results=results,scope=__doc__),indent=2)+'\n')
