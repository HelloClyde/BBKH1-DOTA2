"""Exact full-BDA CPU workload from a read-only physical-device Game snapshot.

No physical FPS claim: interpreter instruction slots exclude cache/bus latency,
MIPS division timing, firmware/audio/video, native rendering, and scheduling.
"""
from pathlib import Path
import argparse,bisect,hashlib,json,struct,sys
from collections import Counter
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_BLOCK
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--snapshot',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--warm',type=int,default=8);p.add_argument('--ticks',type=int,default=32);p.add_argument('--stationary',action='store_true');p.add_argument('--allow-legacy-uncut-tail',action='store_true',help='Explicitly append zero tree state to a pre-tree Game; adapted workload, not byte-identical device state');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);build=a.build.resolve();metadata=build/'DOTA2.build.json';meta=json.loads(metadata.read_text(encoding='utf-8'));bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';assert sha(bda)==meta['bda_sha256'] and sha(elf)==meta['elf_sha256'];snapshot=a.snapshot.resolve();snap=snapshot.read_bytes();assert len(snap)in(173544,178044)
 core_sources={n:v for n,v in meta['source_sha256'].items() if n.startswith('game/') or n.startswith(('platform/item_assets','platform/baked_io','platform/cache_','platform/compressed_cache','platform/baked_aux_io','private/generated/dota_nav','private/generated/dota_roshan_vision'))}
 for n,v in meta.get('source_overrides',{}).items():core_sources.pop(n,None);core_sources[v['path']]=v['sha256']
 for n,v in core_sources.items():assert sha(ROOT/n)==v,('Core source stale',n)
 sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
 assets={n:ROOT/'assets/runtime'/n for n in ('SCENE.H1P','VISION.H1V')};m=CoreMachine(bda,elf,assets);gp=m.symbols['dota_game'];ip=0x81500000;legacy_tail_bytes=0
 if m.symbol_sizes['dota_game']==178044 and len(snap)==173544:
  assert a.allow_legacy_uncut_tail,'New Game needs an explicit legacy-uncut-tail adaptation flag';legacy_tail_bytes=4500;snap+=bytes(legacy_tail_bytes)
 assert m.symbol_sizes['dota_game']==len(snap)
 with elf.open('rb') as f:
  symbols=[(int(s['st_value']),int(s['st_size']),s.name) for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and int(s['st_size'])>0]
 symbols.sort();starts=[r[0] for r in symbols];startset=set(starts);blockmeta={};totals=Counter();entries=Counter();division_functions=Counter();per_tick=[];recording=False;allslots=0;divisions=0
 def block(uc,address,size,user):
  nonlocal allslots,divisions
  if not recording:return
  key=(address,size);data=blockmeta.get(key)
  if data is None:
   i=bisect.bisect_right(starts,address)-1;owner=symbols[i][2] if i>=0 and address<symbols[i][0]+symbols[i][1] else 'outside-symbol'
   words=struct.unpack('<'+'I'*(size//4),m.read(address,size));divs=sum((w>>26)==0 and (w&63) in (26,27) for w in words);data=(owner,size//4,address in startset,divs);blockmeta[key]=data
  owner,slots,entry,divs=data;totals[owner]+=slots;allslots+=slots;divisions+=divs;division_functions[owner]+=divs
  if entry:entries[owner]+=1
 hook=m.uc.hook_add(UC_HOOK_BLOCK,block)
 def call(n,*args):return m.call(n,*args)
 try:
  assert call('baked_io_open_default')==1 and call('item_assets_open')==1;call('item_assets_reserve_rules_cache',65536)
  data=assets['VISION.H1V'].read_bytes();payload=data[64:];terrain=m.alloc(len(payload));m.write(terrain,payload);workbytes=call('dota_sight_workspace_bytes');workspace=m.alloc(workbytes);assert call('dota_sight_attach',terrain,terrain+131072,workspace,workbytes)==1
  if 'dota_trees_attach'in m.symbols:
   records=json.loads((ROOT/'provenance/vision/trees.json').read_bytes())['trees'];xy=b''.join(struct.pack('<HH',t['x_q8']//256,t['y_q8']//256)for t in records);xy_ptr=m.alloc(len(xy));m.write(xy_ptr,xy);assert call('dota_trees_attach',xy_ptr,len(records))==1
  m.write(gp,snap);m.write(ip,bytes(20));call('dota_sight_reset');call('dota_set_paused',gp,0)
  for _ in range(a.warm):call('dota_tick',gp,ip)
  if a.stationary:call('dota_sight_invalidate');call('dota_sight_fog_state',gp,0,2008,2008)
  startgame=m.read(gp,len(snap))
  recording=True
  for i in range(a.ticks):
   before=allslots;before_div=divisions;previous=Counter(totals);previousentries=Counter(entries)
   if a.stationary:call('dota_sight_invalidate')
   else:call('dota_tick',gp,ip)
   call('dota_sight_fog_state',gp,0,2008,2008);game=m.read(gp,len(snap));fog=m.read(workspace+4,32768);vis=m.read(m.symbols['visible'],510);(out/f'game-{i:03}.bin').write_bytes(game);(out/f'fog-{i:03}.bin').write_bytes(fog+vis);per_tick.append(dict(index=i,slots=allslots-before,divisions=divisions-before_div,game_sha256=hashlib.sha256(game).hexdigest(),fog_and_unit_visibility_sha256=hashlib.sha256(fog+vis).hexdigest(),functions=dict(totals-previous),entries=dict(entries-previousentries)))
  recording=False
  endgame=m.read(gp,len(snap));(out/'start-game.bin').write_bytes(startgame);(out/'end-game.bin').write_bytes(endgame)
  report=dict(passed=True,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,device_snapshot_sha256=sha(snapshot),bda_sha256=sha(bda),elf_sha256=sha(elf),metadata_sha256=sha(metadata),warm_ticks=a.warm,measured_ticks=a.ticks,stationary_refresh_only=a.stationary,slots=allslots,mean_slots=allslots/a.ticks,divisions=divisions,mean_divisions=divisions/a.ticks,division_functions=division_functions.most_common(),functions=totals.most_common(),entries=dict(entries),per_tick=per_tick,workspace_bytes=workbytes,source_current=all(sha(ROOT/n)==v for n,v in core_sources.items()),bound_core_sources=core_sources,profile_source_sha256=sha(Path(__file__)),start_game_sha256=sha(out/'start-game.bin'),end_game_sha256=sha(out/'end-game.bin'),scope=__doc__)
  report['legacy_uncut_tree_tail_bytes']=legacy_tail_bytes;report['adapted_workload']=bool(legacy_tail_bytes)
  (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(dict(mean_slots=report['mean_slots'],top_functions=totals.most_common(18),entries={n:v for n,v in entries.items() if any(t in n for t in ('refresh','cast','source','sight'))})),flush=True)
 finally:m.uc.hook_del(hook);m.dispose()
if __name__=='__main__':main()
