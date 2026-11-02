"""Exact frozen BDA normal startup/menu/play IO trace; modeled CPU, never USB/FPS.

The BDA's normal init/tick/render APIs generate the state. Only initial input,
view and resource configuration are provided; outcomes and pixels are not
patched. Read plans are suitable for reader-cache comparisons, not H1 timing.
"""
from pathlib import Path
from collections import Counter
import argparse,hashlib,json,struct,sys,time
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_PC,UC_MIPS_REG_V0
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from mips_core_model import CoreMachine

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def layout(elf,name):
 for cu in elf.get_dwarf_info().iter_CUs():
  for die in cu.iter_DIEs():
   n=die.attributes.get('DW_AT_name')
   if die.tag=='DW_TAG_typedef' and n and n.value.decode()==name:
    target=die.get_DIE_from_attribute('DW_AT_type')
    while target.tag=='DW_TAG_typedef':target=target.get_DIE_from_attribute('DW_AT_type')
    return {x.attributes['DW_AT_name'].value.decode():x.attributes['DW_AT_data_member_location'].value for x in target.iter_children()if x.tag=='DW_TAG_member'}
 raise AssertionError(name)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--menu-frames',type=int,default=320);p.add_argument('--play-frames',type=int,default=160);p.add_argument('--reserve-read-cache',action='store_true');a=p.parse_args()
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 build=a.build.resolve();meta=json.loads((build/'DOTA2.build.json').read_text());bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';pack=ROOT/'assets/runtime/DOTA2.H1R'
 assert sha(bda)==meta['bda_sha256'] and sha(elf)==meta['elf_sha256']
 with elf.open('rb')as f:e=ELFFile(f);gl=layout(e,'DotaGame');vl=layout(e,'DotaView');ul=layout(e,'DotaUnit')
 m=CoreMachine(bda,elf,{'DOTA2.H1R':pack});gp=m.symbols['dota_game'];vp=m.symbols['view'];ip=0x81500000
 reads=[];sprites=[];phase='startup';frame=-1;hooks=[]
 def on_read(uc,address,size,user):
  fp=uc.reg_read(UC_MIPS_REG_A0);at=uc.reg_read(UC_MIPS_REG_A1);n=uc.reg_read(UC_MIPS_REG_A3)
  assert fp and m.integer(fp)==0x31465352
  section=m.integer(fp+4);assert section<4
  reads.append([phase,frame,section,at,n])
 def on_sprite(uc,address,size,user):sprites.append([phase,frame,uc.reg_read(UC_MIPS_REG_A0)])
 hooks.append(m.uc.hook_add(UC_HOOK_CODE,on_read,begin=m.symbols['resource_read_at'],end=m.symbols['resource_read_at']))
 hooks.append(m.uc.hook_add(UC_HOOK_CODE,on_sprite,begin=m.symbols['get_sprite'],end=m.symbols['get_sprite']))
 def call(name,*args):
  context=m.uc.context_save();stack,ret=0x818ff000,0x80001f00;m.write(stack,bytes(64))
  for reg,value in zip((UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3),args):m.uc.reg_write(reg,value&0xffffffff)
  for i,value in enumerate(args[4:]):m.word(stack+16+4*i,value)
  m.uc.reg_write(UC_MIPS_REG_SP,stack);m.uc.reg_write(UC_MIPS_REG_RA,ret)
  try:
   m.uc.emu_start(m.symbols[name],ret,timeout=120_000_000,count=2_000_000_000);assert m.uc.reg_read(UC_MIPS_REG_PC)==ret,(name,hex(m.uc.reg_read(UC_MIPS_REG_PC)));value=m.uc.reg_read(UC_MIPS_REG_V0);m.calls+=1
  finally:m.uc.context_restore(context)
  return value if value<0x80000000 else value-0x100000000
 def vw(name,value):m.word(vp+vl[name],value)
 def camera():
  player=m.integer(gp+gl['player']);base=gp+gl['units']+player*240;vw('camera_x',m.integer(base+ul['x']));vw('camera_y',m.integer(base+ul['y']))
 def resource_stats():return list(struct.unpack('<21I',m.read(m.symbols['resource_status'],84)))
 report=dict(passed=False,scope=__doc__,physical_h1_tested=False,USB_opened=False,actual_complete_bda=True,actual_mips32=True,bda_sha256=sha(bda),elf_sha256=sha(elf),pack_sha256=sha(pack),driver_sha256=sha(__file__),menu_frames=a.menu_frames,play_frames=a.play_frames,normal_initial_seed=0x484431,game_outcome_writes=0,limitations='Read calls come from normal APIs of the frozen BDA. No entry/LCD/audio/MXU/USB or actual H1 timing is executed. Render is at an ideal20Hz and does not reproduce physical scheduling or dropped frames. Main menu frames configure view.frames; match seed uses the requested menu-frame count. Upper renderer caches are active in trace generation; replay itself only exercises the resource reader.')
 try:
  m.write(0x81508000,b'B:\\DOTA2\\DOTA2.H1R\0');assert call('resource_mount',0x81508000)==1
  report['reserved_reader_cache_bytes']=call('resource_reserve_read_cache')if a.reserve_read_cache else 0
  assert call('bs_open_default')==1 and call('item_assets_open')==1
  call('baked_io_reserve_cache',262144);call('item_assets_reserve_rules_cache',65536);assert call('item_assets_reserve_icon_cache',64)==64
  assert call('bs_reserve_cache',14*1024*1024)==14*1024*1024
  assert call('vision_assets_open_default')==1 and call('tree_visual_open_default')==1
  report['fountain_prewarm_frames']=call('bs_prewarm_fountain',0,0)
  call('dota_init',gp,0,0x484431);call('dota_set_paused',gp,1);call('shop_catalog_bind',gp);call('shop_catalog_reset')
  m.write(vp,bytes(meta['binary']['abi']['DotaView']));vw('hero',0);vw('follow',1)
  for n in ['target_x','target_y','aim_skill','aim_item','inventory_source','inventory_hover']:vw(n,-1)
  phase='menu';vw('screen',0)
  for frame in range(a.menu_frames):
   vw('frames',frame);call('dota_render',gp,vp)
   if frame%40==0:print('MENU '+str(frame),flush=True)
  report['before_play_resource_stats']=resource_stats()
  seed=0x484431+a.menu_frames;call('dota_init',gp,0,seed);call('dota_set_paused',gp,0);call('shop_catalog_bind',gp);call('shop_catalog_reset');vw('screen',1);camera();m.write(ip,bytes(20))
  report['lineup_hero_ids']=[m.integer(gp+gl['units']+i*240+ul['hero_id'])for i in range(10)];report['match_seed']=seed
  phase='play'
  for frame in range(a.play_frames):
   m.word(ip+16,64 if frame==0 else 0);call('dota_tick',gp,ip);camera();vw('frames',a.menu_frames+frame);call('dota_render',gp,vp)
   if frame%20==0:print('PLAY '+str(frame),flush=True)
  report['after_play_resource_stats']=resource_stats();report['end_game_tick']=m.integer(gp+gl['tick'])
  (out/'end-game.bin').write_bytes(m.read(gp,meta['binary']['abi']['DotaGame']))
  report['passed']=True
 finally:
  for hook in hooks:m.uc.hook_del(hook)
  (out/'reads.json').write_text(json.dumps(reads,separators=(',',':'))+'\n')
  (out/'sprites.json').write_text(json.dumps(sprites,separators=(',',':'))+'\n')
  (out/'plan.bin').write_bytes(b''.join(struct.pack('<4I',['startup','menu','play'].index(p),s,at,n)for p,f,s,at,n in reads))
  report['logical_read_calls']=len(reads);report['read_phase_counts']=dict(Counter(x[0]for x in reads));report['sprite_calls']=len(sprites);report['unique_sprite_phase_counts']={p:len({x[2]for x in sprites if x[0]==p})for p in ['startup','menu','play']}
  report['plan_sha256']=sha(out/'plan.bin');report['reads_sha256']=sha(out/'reads.json');report['sprites_sha256']=sha(out/'sprites.json')
  try:
   call('tree_visual_close');call('vision_assets_close');call('item_assets_close');call('bs_close');assert call('resource_unmount')==1;report['after_close']=dict(allocations=len(m.allocations),handles=len(m.handles));assert not m.allocations and not m.handles
  except Exception as exc:report['cleanup_error']=str(exc);report['passed']=False
  m.dispose();(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report))
if __name__=='__main__':main()
