"""Isolated C7/local-FOV actual MIPS API comparison; instruction slots are not FPS.

The complete BDA reuses every C7 object except sight.o. Initial Game snapshots
come from read-only H1 captures. Tree outcomes are produced by real tree APIs;
the observer never patches gameplay, visibility, masks, or counter results.
"""
from pathlib import Path
from collections import Counter
import argparse,bisect,hashlib,json,struct,subprocess,sys
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_BLOCK
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from mips_core_model import CoreMachine
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'sdk')]
from common import BUILD_DATE,binary_check
from h1_bda.header import HeaderFields,encode_header
from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def layout(elf,name):
 for cu in elf.get_dwarf_info().iter_CUs():
  for die in cu.iter_DIEs():
   attr=die.attributes.get('DW_AT_name')
   if die.tag=='DW_TAG_typedef' and attr and attr.value.decode()==name:
    target=die.get_DIE_from_attribute('DW_AT_type')
    while target.tag=='DW_TAG_typedef':target=target.get_DIE_from_attribute('DW_AT_type')
    return int(target.attributes['DW_AT_byte_size'].value),{d.attributes['DW_AT_name'].value.decode():int(d.attributes['DW_AT_data_member_location'].value) for d in target.iter_children() if d.tag=='DW_TAG_member'}
 raise AssertionError(name)

def build(base,out):
 out.mkdir(parents=True);meta=json.loads((base/'DOTA2.build.json').read_bytes());commands=json.loads((base/'commands.json').read_bytes())
 assert sha(base/'DOTA2.bda')==meta['bda_sha256']=='d8d75e2c459735fad358c53c58e2d461c52fb8e348e2846b113f8fcded734d53'
 resolve=lambda x:x.replace('<REPO>',str(ROOT)).replace('<TOOLCHAIN>',str(ROOT/'.tools/toolchain/bin'))
 cmd=next([resolve(x) for x in c] for c in commands['compile'] if c[c.index('-c')+1].endswith('game\\sight.c'));cmd[cmd.index('-o')+1]=str(out/'sight.o')
 log=[]
 def run(c):
  r=subprocess.run(c,capture_output=True,text=True);log.append(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 run(cmd);link=[resolve(x) for x in commands['link']]
 link=[x.replace(str(base/'DOTA2.elf'),str(out/'DOTA2.elf')).replace(str(base/'DOTA2.map'),str(out/'DOTA2.map')) if x!=str(base/'sight.o') else str(out/'sight.o') for x in link];run(link)
 run([str(Path(cmd[0]).with_name('mipsel-none-elf-objcopy.exe')),'-O','binary',str(out/'DOTA2.elf'),str(out/'DOTA2.bin')])
 payload=(out/'DOTA2.bin').read_bytes();pad=(-(PAYLOAD_OFFSET+len(payload)))&3
 fields=HeaderFields(category=0x47,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+pad-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
 (out/'DOTA2.bda').write_bytes(encode_header(fields,title='DOTA2',build_time=BUILD_DATE)+(base/'DOTA2.bda').read_bytes()[136:PAYLOAD_OFFSET]+payload+bytes(pad))
 binary=binary_check(out/'DOTA2.bda',out/'DOTA2.elf',len(payload));meta.update(diagnostic_only=True,physical_h1_tested=False,only_recompiled_translation_unit='game/sight.c',reused_objects_from_exact_build=str(base.relative_to(ROOT)),bda_sha256=sha(out/'DOTA2.bda'),elf_sha256=sha(out/'DOTA2.elf'),binary=binary)
 meta['source_sha256']['game/sight.c']=sha(ROOT/'game/sight.c');(out/'sight-source.c').write_bytes((ROOT/'game/sight.c').read_bytes())
 (out/'DOTA2.build.json').write_text(json.dumps(meta,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(dict(compile=cmd,link=link),indent=2)+'\n');(out/'compile.txt').write_text(''.join(log))
 return meta

class Observer:
 def __init__(self,build,snapshot,out):
  out.mkdir();self.out=out;self.m=m=CoreMachine(build/'DOTA2.bda',build/'DOTA2.elf',{'DOTA2.H1R':ROOT/'assets/runtime/DOTA2.H1R'});self.record=False;self.slots=0;self.functions=Counter();self.blockmeta={}
  with (build/'DOTA2.elf').open('rb') as f:
   e=ELFFile(f);self.gs,self.gl=layout(e,'DotaGame');self.us,self.ul=layout(e,'DotaUnit');self.ss,self.sl=layout(e,'SightSource');self.ws,self.wl=layout(e,'SightWork');functions=sorted((int(s['st_value']),int(s['st_size']),s.name) for s in e.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and int(s['st_size'])>0)
  starts=[x[0] for x in functions]
  def block(uc,address,size,user):
   if not self.record:return
   key=address,size;v=self.blockmeta.get(key)
   if v is None:
    i=bisect.bisect_right(starts,address)-1;name=functions[i][2] if i>=0 and address<functions[i][0]+functions[i][1] else 'service';v=name,size//4;self.blockmeta[key]=v
   name,slots=v;self.slots+=slots;self.functions[name]+=slots
  self.hook=m.uc.hook_add(UC_HOOK_BLOCK,block);m.uc.ctl_flush_tb()
  assert m.call('baked_io_open_default')==1 and m.call('item_assets_open')==1;m.call('item_assets_reserve_rules_cache',65536)
  vision=(ROOT/'build/v030/legacy/VISION.H1V').read_bytes();xy=(ROOT/'build/v030/legacy/TREE.H1T').read_bytes()[128:128+2113*4];self.xy=list(struct.iter_unpack('<HH',xy))
  self.tp=m.alloc(len(vision)-64);m.write(self.tp,vision[64:]);self.wb=m.call('dota_sight_workspace_bytes');assert self.wb==self.ws==1727724;self.wp=m.alloc(self.wb);assert m.call('dota_sight_attach',self.tp,self.tp+131072,self.wp,self.wb)==1
  self.xp=m.alloc(len(xy));m.write(self.xp,xy);assert m.call('dota_trees_attach',self.xp,2113)==1
  self.gp=m.symbols['dota_game'];assert len(snapshot)==self.gs==179008;m.write(self.gp,snapshot);self.ip=0x81500000;m.write(self.ip,bytes(20));m.call('dota_sight_reset');m.call('dota_set_paused',self.gp,0)
  for _ in range(2):m.call('dota_tick',self.gp,self.ip)
  m.call('dota_sight_fog_state',self.gp,0,2008,2008);self.save('initial')
 def save(self,name):
  m=self.m;g=m.read(self.gp,self.gs);fog=m.read(self.wp+self.wl['current'],32768)+m.read(m.symbols['visible'],510)
  (self.out/(name+'-game.bin')).write_bytes(g);(self.out/(name+'-fog.bin')).write_bytes(fog);return dict(game_sha256=hashlib.sha256(g).hexdigest(),fog_sha256=hashlib.sha256(fog).hexdigest())
 def select(self):
  m=self.m;sources=[];count=m.integer(self.wp+self.wl['active_count'])
  for i in range(count):
   index=struct.unpack('<H',m.read(self.wp+self.wl['active']+i*2,2))[0];p=self.wp+self.wl['source']+index*self.ss
   if not m.integer(p+self.sl['key_flying']):sources.append((m.integer(p+self.sl['key_cx']),m.integer(p+self.sl['key_cy']),m.integer(p+self.sl['key_radius'])))
  standing=[i for i in range(len(self.xy)) if not m.call('dota_trees_is_cut',self.gp,i)];occupancy=Counter();cells={}
  for i in standing:
   x,y=self.xy[i];c=[(xx,yy) for yy in range(y//16-1,y//16+2) for xx in range(x//16-1,x//16+2) if 0<=xx<256 and 0<=yy<256 and (xx*16+8-x)**2+(yy*16+8-y)**2<=256];cells[i]=c;occupancy.update(c)
  ranked=[]
  for i in standing:
   changed=[c for c in cells[i] if occupancy[c]==1]
   if not changed:continue
   lx=min(c[0] for c in changed);hx=max(c[0] for c in changed);ly=min(c[1] for c in changed);hy=max(c[1] for c in changed)
   n=sum(not(hx<x-r or lx>x+r or hy<y-r or ly>y+r) for x,y,r in sources);ranked.append((n,i))
  ranked.sort();assert ranked;remote=ranked[0];local=ranked[-1];assert remote[1]!=local[1]
  return [('remote',remote[1],remote[0]),('local',local[1],local[0])]
 def cut(self,name,index):
  m=self.m;before=self.slots;f=Counter(self.functions);p=m.symbols['h1_shared_shadow_profile'];casts=m.integer(p+12);reads=m.bytes_read
  self.record=True;assert m.call('dota_trees_cut',self.gp,index)==1;m.call('dota_sight_fog_state',self.gp,0,2008,2008);self.record=False
  return dict(label=name,index=index,xy=self.xy[index],instruction_slots=self.slots-before,function_slots=dict(self.functions-f),shadowcasts=m.integer(p+12)-casts,read_bytes=m.bytes_read-reads,**self.save(name))
 def close(self):
  m=self.m;self.record=False;m.call('dota_trees_detach');m.call('dota_sight_detach');m.free(self.xp);m.free(self.wp);m.free(self.tp);m.call('item_assets_close');m.call('baked_io_close');assert m.call('resource_unmount')==1;assert not m.allocations and not m.handles;m.uc.hook_del(self.hook);m.dispose()

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 base=ROOT/'build/v030/perf-candidate7';candidate=out/'candidate';meta=build(base,candidate);results=[];source_sha=sha(ROOT/'game/sight.c')
 for name in ['fountain-paused-game.bin','post-horn-paused-game.bin']:
  snap=ROOT/'build/v030/perf2-final7-aligned'/name;data=snap.read_bytes();left=Observer(base,data,out/(snap.stem+'-C7'));right=Observer(candidate,data,out/(snap.stem+'-local'));selection=left.select();assert selection==right.select();rows=[]
  try:
   for label,index,affected in selection:
    old=left.cut(label,index);new=right.cut(label,index);assert old['game_sha256']==new['game_sha256'] and old['fog_sha256']==new['fog_sha256'];rows.append(dict(label=label,tree_id=index,predicted_intersecting_sources=affected,reference=old,candidate=new,saved_shadowcasts=old['shadowcasts']-new['shadowcasts'],slot_reduction_percent=100*(old['instruction_slots']-new['instruction_slots'])/old['instruction_slots']))
   equality={f.name:f.read_bytes()==(right.out/f.name).read_bytes() for f in left.out.glob('*.bin')};assert all(equality.values());results.append(dict(snapshot_sha256=sha(snap),snapshot=name,compared_files=equality,cuts=rows));print(name,[(r['label'],r['reference']['shadowcasts'],r['candidate']['shadowcasts'],round(r['slot_reduction_percent'],2)) for r in rows],flush=True)
  finally:left.close();right.close()
 assert source_sha==sha(ROOT/'game/sight.c');report=dict(passed=True,scope=__doc__,actual_mips32=True,complete_bda=True,physical_h1_tested=False,USB_opened=False,game_outcome_writes_after_initialization=0,baseline_bda_sha256=sha(base/'DOTA2.bda'),candidate_bda_sha256=meta['bda_sha256'],candidate_elf_sha256=meta['elf_sha256'],sight_source_sha256=source_sha,baseline_workspace_bytes=1727724,candidate_workspace_bytes=1727724,private_ram_margin_bytes=meta['binary']['ram_margin_bytes'],test_source_sha256=sha(__file__),results=results)
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,private_ram_margin_bytes=report['private_ram_margin_bytes'],saved_shadowcasts=sum(r['saved_shadowcasts'] for x in results for r in x['cuts']))))
if __name__=='__main__':main()
