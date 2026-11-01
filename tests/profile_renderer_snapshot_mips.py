"""Full-BDA MIPS CPU-renderer workload, not physical display/FPS timing.

Initial Game comes from a read-only physical snapshot. Modeled read-only FS
and heap serve the original assets. Normal ticks warm animation/history/cache
state before measurements. No game outcomes, pixels or visibility are patched.
"""
from pathlib import Path
from collections import Counter
import argparse,bisect,hashlib,json,re,struct,sys,time
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_BLOCK
from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_PC,UC_MIPS_REG_V0
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from mips_core_model import CoreMachine
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def function_lines(text,name):
 m=re.search(r'^static[^;\n]*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M);assert m,name
 i=m.end();depth=1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text.count('\n',0,m.start())+1,text.count('\n',0,i)+1
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--snapshot',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--warm',type=int,default=24);p.add_argument('--frames',type=int,default=8);p.add_argument('--renderer-source',type=Path,help='Exact frozen compiled renderer for diagnostic A/B line attribution');p.add_argument('--camera-step',type=int,default=0,help='Test-only horizontal camera pan per frame; game state is not changed');a=p.parse_args()
 build=a.build.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);meta=json.loads((build/'DOTA2.build.json').read_text(encoding='utf8'));bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';assert sha(bda)==meta['bda_sha256'] and sha(elf)==meta['elf_sha256'];snapshot=a.snapshot.resolve();snap=snapshot.read_bytes();assert len(snap)==173544
 baked=a.renderer_source or ROOT/'platform/baked_scene.c';assert sha(baked)==meta['source_sha256']['platform/baked_scene.c'],'DWARF line attribution requires exact renderer source'
 text=baked.read_text(encoding='utf8');line_groups={name:function_lines(text,name) for name in ['draw_fog','fog_light_row','fog_world_point','draw_map','copy_map_row','draw_sprite_opacity','draw_hero_shadow','update_history']}
 if re.search(r'^static[^;\n]*\bfog_map_depth\(',text,re.M):line_groups['fog_map_depth']=function_lines(text,'fog_map_depth')
 assets={n:ROOT/'assets/runtime'/n for n in ['SCENE.H1P','MAP12.H1C','VISION.H1V']}
 if (ROOT/'assets/runtime/TREE.H1T').exists():assets['TREE.H1T']=ROOT/'assets/runtime/TREE.H1T'
 asset_hashes={n:sha(v) for n,v in assets.items()};m=CoreMachine(bda,elf,assets);gp=m.symbols['dota_game'];vp=m.symbols['view'];pixels=m.symbols['dota_pixels'];ip=0x81500000
 with elf.open('rb') as f:
  e=ELFFile(f);functions=sorted((int(s['st_value']),int(s['st_size']),s.name) for s in e.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and int(s['st_size'])>0);line_records=[]
  for cu in e.get_dwarf_info().iter_CUs():
   lp=e.get_dwarf_info().line_program_for_CU(cu)
   if not lp:continue
   files=lp.header['file_entry']
   for ent in lp.get_entries():
    s=ent.state
    if s and not s.end_sequence:line_records.append((s.address,files[s.file-1].name.decode('utf8','replace'),s.line or 0))
 functions.sort();starts=[x[0] for x in functions];line_records.sort();line_addresses=[x[0] for x in line_records]
 blockmeta={};totals=Counter();groups=Counter();entries=Counter();divisions=Counter();phase='';frame_index=-1;active=False;allslots=0;phase_slots=Counter();frames=[];warm=[];same_tick=[];hook=None
 def block(uc,address,size,user):
  nonlocal allslots
  if not active:return
  key=address,size;record=blockmeta.get(key)
  if record is None:
   fi=bisect.bisect_right(starts,address)-1;owner=functions[fi][2] if fi>=0 and address<functions[fi][0]+functions[fi][1] else 'modeled-service';categories=Counter();words=struct.unpack('<'+'I'*(size//4),m.read(address,size));divs=sum((w>>26)==0 and (w&63) in (26,27) for w in words)
   for offset in range(0,size,4):
    li=bisect.bisect_right(line_addresses,address+offset)-1;category=owner
    if li>=0:
     _,file,line=line_records[li]
     if file=='baked_scene.c':
      for label,(first,last) in line_groups.items():
       if first<=line<=last:category='baked:'+label;break
     elif file=='vision_assets.h':category='fog_rgb565_inline'
     elif file in ['cache_rle.h','compressed_cache.h']:category='sprite-compression-inline'
     elif file.endswith('_baked.h'):category='baked-header:'+file
    categories[category]+=1
   record=(owner,size//4,address in starts,divs,categories);blockmeta[key]=record
  owner,slots,entry,divs,categories=record;totals[owner]+=slots;groups.update(categories);phase_slots[phase]+=slots;allslots+=slots;divisions[owner]+=divs
  if entry:entries[owner]+=1
 def call(name,*args):
  # Same O32 call boundary as CoreMachine, with a larger diagnostic budget:
  # startup prewarm validates megabytes of original sprite payloads in MIPS.
  context=m.uc.context_save();stack,ret=0x818ff000,0x80001f00;m.write(stack,bytes(64))
  for reg,value in zip((UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3),args):m.uc.reg_write(reg,value&0xffffffff)
  for i,value in enumerate(args[4:]):m.word(stack+16+4*i,value)
  m.uc.reg_write(UC_MIPS_REG_SP,stack);m.uc.reg_write(UC_MIPS_REG_RA,ret)
  try:
   m.uc.emu_start(m.symbols[name],ret,timeout=120_000_000,count=2_000_000_000);assert m.uc.reg_read(UC_MIPS_REG_PC)==ret,(name,hex(m.uc.reg_read(UC_MIPS_REG_PC)));value=m.uc.reg_read(UC_MIPS_REG_V0);m.calls+=1
  finally:m.uc.context_restore(context)
  return value if value<0x80000000 else value-0x100000000
 def words(name):return list(struct.unpack('<'+'I'*(m.symbol_sizes[name]//4),m.read(m.symbols[name],m.symbol_sizes[name])))
 def stat():return dict(render=words('stats'),fog=words('fog_stats'),io_bytes=m.bytes_read,framebuffer_sha256=hashlib.sha256(m.read(pixels,480*272*2)).hexdigest())
 def tick():return m.integer(gp+62068)
 def update_camera():
  player=m.integer(gp+62080);px=m.integer(gp+player*240+24);py=m.integer(gp+player*240+28);m.word(vp+12,px);m.word(vp+16,py);return player,px,py
 def frame(i,record=False):
  nonlocal phase,active,frame_index
  frame_index=i;before=Counter(phase_slots);functions_before=Counter(totals);groups_before=Counter(groups);start_tick=tick();bytes_before=m.bytes_read
  phase='normal_game_ticks';active=record
  for _ in range(1+i%2):call('dota_tick',gp,ip)
  player,px,py=update_camera();m.word(vp+12,px+i*a.camera_step);current=call('dota_sight_is_current',gp)
  phase='sight_preflight';call('dota_sight_fog_state',gp,m.integer(gp+player*240+8),px,py)
  phase='full_dota_render';call('dota_render',gp,vp);active=False
  value=dict(index=i,game_ticks=tick()-start_tick,sight_current_before_preflight=current,read_bytes=m.bytes_read-bytes_before,**stat())
  if record:value.update(slots=dict(phase_slots-before),function_slots=dict(totals-functions_before),source_groups=dict(groups-groups_before))
  return value
 report=dict(passed=False,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,entry_or_present_or_audio_executed=False,game_outcome_writes_after_initialization=0,camera_pan_world_units_per_frame=a.camera_step,device_snapshot_sha256=sha(snapshot),bda_sha256=sha(bda),elf_sha256=sha(elf),metadata_sha256=sha(build/'DOTA2.build.json'),compiled_sources=meta['source_sha256'],profile_source_sha256=sha(Path(__file__)),model_source_sha256=sha(ROOT/'tests/mips_core_model.py'),assets_sha256=asset_hashes,scope=__doc__)
 try:
  assert call('bs_open_default')==1 and call('item_assets_open')==1;call('baked_io_reserve_cache',256*1024);call('item_assets_reserve_rules_cache',64*1024);assert call('item_assets_reserve_icon_cache',64)==64;assert call('bs_reserve_cache',14*1024*1024)==14*1024*1024;assert call('bs_map_sidecar_open_default')==1;assert call('vision_assets_open_default')==1
  if 'tree_visual_open_default' in m.symbols:
   assert 'TREE.H1T' in assets and call('tree_visual_open_default')==1;report['tree_visual_initial_heap_bytes']=call('tree_visual_dynamic_bytes')
  report['fountain_prewarm_frames']=call('bs_prewarm_fountain',0,0);report['vision_stats']=words('vision_stats');report['scene_heap_bytes']=call('bs_dynamic_storage_bytes');report['io_heap_bytes']=call('baked_io_dynamic_storage_bytes')
  game_bytes=m.symbol_sizes['dota_game'];assert game_bytes>=len(snap)
  report['snapshot_bytes']=len(snap);report['game_bytes']=game_bytes;report['new_appended_state_zero_bytes']=game_bytes-len(snap)
  m.write(gp,snap+bytes(game_bytes-len(snap)));m.write(ip,bytes(20));view=[0]*25;view[0]=1;view[1]=m.integer(gp+62084);view[6]=view[7]=view[8]=view[15]=-1;view[9]=1;view[20]=view[21]=-1;m.write(vp,struct.pack('<25i',*view));call('dota_sight_reset');call('dota_set_paused',gp,0);update_camera()
  for i in range(a.warm):warm.append(frame(i));print('WARM '+str(i)+' '+str(warm[-1]['render'][4:7]),flush=True)
  call('dota_set_paused',gp,1);m.word(vp,2);frozen=m.read(gp,game_bytes)
  for i in range(2):call('dota_render',gp,vp);same_tick.append(stat());assert m.read(gp,game_bytes)==frozen
  call('dota_set_paused',gp,0);m.word(vp,1);hook=m.uc.hook_add(UC_HOOK_BLOCK,block)
  # Unicorn retains translated blocks from warmup. Re-translate after adding
  # the hook so hot blocks, not only newly encountered code, are counted.
  m.uc.ctl_flush_tb()
  for i in range(a.frames):
   frames.append(frame(a.warm+i,True));(out/'partial.json').write_text(json.dumps(dict(frames=frames,phase_slots=phase_slots,functions=totals.most_common(20),source_groups=groups.most_common(20)),indent=2)+'\n',encoding='utf8');print('FRAME '+str(i)+' '+json.dumps(frames[-1]['slots']),flush=True)
  assert all(f['slots'].get('full_dota_render',0)>100000 and f['slots'].get('normal_game_ticks',0)>100000 for f in frames),'Hot JIT blocks were not fully counted'
  assert sum(phase_slots.values())==allslots and len(frames)==a.frames
  report.update(passed=True,initial_game_tick=struct.unpack_from('<i',snap,62068)[0],warm_frames=warm,same_tick_paused_frames=same_tick,measured_frames=frames,slots=allslots,phase_slots=dict(phase_slots),function_slots=totals.most_common(),source_group_slots=groups.most_common(),function_entries=dict(entries),hardware_div_instruction_counts=dict(divisions),workspace_bytes=report['vision_stats'][10],dwarf_fog_line_ranges=line_groups,asset_handles_still_open=len(m.handles),limit='Instruction slots and modeled file bytes only. No physical cache/bus/div latency, display/MXU/audio/interrupt/scheduling timing or FPS. Platform history starts new; normal warm ticks advance the captured Game, so this is a workload from a real initial state, not an exact physical frame replay.')
  assert sha(bda)==report['bda_sha256'] and sha(elf)==report['elf_sha256'] and all(sha(assets[n])==v for n,v in asset_hashes.items())
 except Exception as error:report.update(passed=False,error=str(error),warm_frames=warm,same_tick_paused_frames=same_tick,measured_frames=frames);raise
 finally:
  active=False
  if hook is not None:m.uc.hook_del(hook)
  report['profiled_process_heap_allocations']=len(m.allocations);m.dispose();(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(passed=True,phase_slots=dict(phase_slots),functions=totals.most_common(15),groups=groups.most_common(15))),flush=True)
if __name__=='__main__':main()
