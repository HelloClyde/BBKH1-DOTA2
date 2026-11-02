"""Read-only exact-C3 MIPS shadow key/cache trace; never physical FPS."""
from pathlib import Path
import collections,hashlib,json,struct,sys
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_A0
ROOT=Path.cwd();OUT=ROOT/'build/v030/game-cost-study';BUILD=ROOT/'build/v030/perf-candidate3'
sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
meta=json.loads((BUILD/'DOTA2.build.json').read_bytes())
assert sha(ROOT/'game/sight.c')==meta['source_sha256']['game/sight.c']
with (BUILD/'DOTA2.elf').open('rb')as f:
 elf=ELFFile(f);dwarf=elf.get_dwarf_info();layouts={};sizes={}
 for cu in dwarf.iter_CUs():
  for die in cu.iter_DIEs():
   name=die.attributes.get('DW_AT_name')
   if die.tag=='DW_TAG_typedef' and name and name.value.decode()in('SightWork','SightSource','SightShared'):
    d=die.get_DIE_from_attribute('DW_AT_type')
    while d.tag=='DW_TAG_typedef':d=d.get_DIE_from_attribute('DW_AT_type')
    n=name.value.decode();sizes[n]=d.attributes['DW_AT_byte_size'].value
    layouts[n]={x.attributes['DW_AT_name'].value.decode():x.attributes['DW_AT_data_member_location'].value for x in d.iter_children()if x.tag=='DW_TAG_member'}
assert all(n in layouts for n in ('SightWork','SightSource','SightShared'))
vision=(ROOT/'build/v030/legacy/VISION.H1V').read_bytes();xy=(ROOT/'build/v030/legacy/TREE.H1T').read_bytes()[128:128+2113*4]
fields=['key_cx','key_cy','key_radius','key_height','key_region','key_flying','tree_stamp']
def trace(snapshot):
 m=CoreMachine(BUILD/'DOTA2.bda',BUILD/'DOTA2.elf',{'DOTA2.H1R':ROOT/'assets/runtime/DOTA2.H1R'})
 events=[];pending=None;tickno=-2;source=layouts['SightSource'];work=layouts['SightWork']
 def readint(p,field):return m.integer(p+source[field])
 def finalize():
  nonlocal pending
  if pending is None:return
  p=pending['pointer'];newkey=[readint(p,n)for n in fields]
  pending['new_key']=newkey;pending['new_xy']=[readint(p,'x'),readint(p,'y')]
  pending['own_hit']=bool(pending['old_valid'] and pending['old_key']==newkey)
  pending['reason']=['invalid']if not pending['old_valid']else[n for n,a,b in zip(fields,pending['old_key'],newkey)if a!=b]
  assert not(pending['own_hit']and pending['cast'])
  events.append(pending);pending=None
 def enter(uc,address,size,user):
  nonlocal pending
  finalize();index=uc.reg_read(UC_MIPS_REG_A0)
  wp=struct.unpack('<I',m.read(m.symbols['work'],4))[0];p=wp+work['source']+index*sizes['SightSource']
  pending=dict(tick=tickno,index=index,pointer=p,old_key=[readint(p,n)for n in fields],old_xy=[readint(p,'x'),readint(p,'y')],old_valid=readint(p,'valid'),cast=False,work_tree_stamp=m.integer(wp+work['tree_stamp']))
  if index<255:
   u=m.symbols['dota_game']+index*240;pending['unit']=dict(zip(['active','type','team','hero','lane','tier','x','y','hp'],struct.unpack('<9i',m.read(u,36))))
 def cast(uc,address,size,user):
  if pending is not None:
   assert uc.reg_read(UC_MIPS_REG_A0)==pending['pointer'];pending['cast']=True
 h1=m.uc.hook_add(UC_HOOK_CODE,enter,begin=m.symbols['source_add'],end=m.symbols['source_add'])
 h2=m.uc.hook_add(UC_HOOK_CODE,cast,begin=m.symbols['cast'],end=m.symbols['cast'])
 try:
  assert m.call('baked_io_open_default')==1 and m.call('item_assets_open')==1;m.call('item_assets_reserve_rules_cache',65536)
  terrain=m.alloc(len(vision)-64);m.write(terrain,vision[64:]);wb=m.call('dota_sight_workspace_bytes');ws=m.alloc(wb);assert m.call('dota_sight_attach',terrain,terrain+131072,ws,wb)==1
  tp=m.alloc(len(xy));m.write(tp,xy);assert m.call('dota_trees_attach',tp,2113)==1
  m.write(m.symbols['dota_game'],snapshot.read_bytes());ip=0x81500000;m.write(ip,bytes(20));m.call('dota_sight_reset');m.call('dota_set_paused',m.symbols['dota_game'],0)
  counters=[]
  for tickno in range(-2,16):
   before=list(struct.unpack('<5I',m.read(m.symbols['h1_shared_shadow_profile'],20)));reads=m.bytes_read
   m.call('dota_tick',m.symbols['dota_game'],ip);m.call('dota_sight_fog_state',m.symbols['dota_game'],0,2008,2008);finalize()
   counters.append(dict(tick=tickno,counter_delta=[x-y for x,y in zip(struct.unpack('<5I',m.read(m.symbols['h1_shared_shadow_profile'],20)),before)],modeled_read_bytes=m.bytes_read-reads))
  m.call('dota_trees_detach');m.call('dota_sight_detach');m.free(tp);m.free(ws);m.free(terrain);m.call('item_assets_close');m.call('baked_io_close');assert m.call('resource_unmount')==1 and not m.allocations and not m.handles
 finally:m.uc.hook_del(h1);m.uc.hook_del(h2);m.dispose()
 def simulate(size,ways=1):
  buckets=[[]for _ in range(size//ways)];counts=collections.Counter();per=[]
  for e in events:
   if e['own_hit']:continue
   cx,cy,radius,height,region,flying,stamp=e['new_key'];slot=(cx*17^cy*131^radius*23^height*7^region*31^flying*61^stamp)&(len(buckets)-1);b=buckets[slot];key=tuple(e['new_key'])
   hit=key in b
   if key in b:b.remove(key)
   elif len(b)==ways:b.pop(0)
   b.append(key)
   if e['tick']>=0:counts['lookups']+=1;counts['hits'if hit else'misses']+=1
   per.append(dict(tick=e['tick'],index=e['index'],hit=hit,key=key))
  return dict(entries=size,ways=ways,counts=dict(counts),lookup_outcomes=per)
 simulations=[simulate(n,w)for n,w in [(128,1),(128,2),(128,4),(256,1),(256,2),(512,1),(512,2),(1024,1)]]
 measured=[e for e in events if e['tick']>=0];lookups=[e for e in measured if not e['own_hit']];casts=[e for e in lookups if e['cast']]
 assert simulations[0]['counts']['misses']==len(casts)
 assert len(lookups)==sum(x['counter_delta'][1]for x in counters if x['tick']>=0)
 report=dict(passed=True,bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],snapshot_sha256=sha(snapshot),sight_source_sha256=sha(ROOT/'game/sight.c'),physical_h1_tested=False,USB_opened=False,warm_ticks=2,measured_ticks=16,layout=layouts,sizes=sizes,counters=counters,casts=len(casts),lookups=len(lookups),own_hits=sum(e['own_hit']for e in measured),cast_causes=dict(collections.Counter(n for e in casts for n in e['reason'])),cast_unit_types=dict(collections.Counter(str(e.get('unit',{}).get('type','special'))for e in casts)),events=events,simulations=simulations,scope=__doc__)
 out=OUT/(snapshot.stem+'-shadow-trace.json');out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k in('casts','lookups','own_hits','cast_causes','cast_unit_types')}));print([(x['entries'],x['ways'],x['counts'])for x in simulations]);return out
paths=[trace(ROOT/'build/v030/perf-baseline-aligned'/n)for n in ('fountain-paused-game.bin','post-horn-paused-game.bin')]
(OUT/'shadow-trace-summary.json').write_text(json.dumps(dict(passed=True,source_sha256=sha(__file__),reports=[dict(path=str(p.relative_to(ROOT)),sha256=sha(p))for p in paths],physical_h1_tested=False),indent=2)+'\n')
