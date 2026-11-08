"""Bounded complete-BDA MIPS native item-input acceptance, with modeled IO.

Runs unchanged use_inventory, confirm_item and pump code from the actual ELF.
The full process_ui chain is optional when that static function is retained.
Only initial native UI/input fixtures and expected GUI
timer/key-event callbacks are supplied; all Game changes come from real APIs.
Does not execute entry, display conversion, audio device IO or USB, and does
not measure physical FPS. The timer callback returns controlled milliseconds.
"""
from pathlib import Path
import argparse,collections,hashlib,json,re,struct,sys,time,traceback
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
RESOURCE_FIELDS='mounted version file_bytes open_files heap_bytes last_error mount_attempts open_attempts read_requests bytes_returned physical_reads physical_bytes seeks index_hits index_misses chunk_hits chunk_misses decoded_chunks decoded_bytes service_calls errors'.split()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def layouts_of(elf,wanted):
 layouts={};sizes={}
 for cu in elf.get_dwarf_info().iter_CUs():
  for die in cu.iter_DIEs():
   name=die.attributes.get('DW_AT_name')
   if die.tag!='DW_TAG_typedef'or not name or name.value.decode()not in wanted:continue
   target=die.get_DIE_from_attribute('DW_AT_type')
   while target.tag in ['DW_TAG_typedef','DW_TAG_const_type','DW_TAG_volatile_type']:target=target.get_DIE_from_attribute('DW_AT_type')
   if 'DW_AT_byte_size'not in target.attributes:continue
   n=name.value.decode();sizes[n]=target.attributes['DW_AT_byte_size'].value;layouts[n]={d.attributes['DW_AT_name'].value.decode():d.attributes['DW_AT_data_member_location'].value for d in target.iter_children()if d.tag=='DW_TAG_member'}
 assert wanted==set(layouts),(wanted-set(layouts));return layouts,sizes
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path);p.add_argument('--bundle',type=Path,default=ROOT/'assets/runtime/DOTA2.H1R');p.add_argument('--output',type=Path);p.add_argument('--check-interface',action='store_true');a=p.parse_args()
 if a.check_interface:print(json.dumps(dict(prepared=True,USB_opened=False,entry_executed=False,game_state_ram_writes_by_test=0,native_UI_fixture_writes=True,actual_native_input_test=True)));return
 assert a.build and a.output;build=a.build.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True);(out/'driver-executed.py').write_bytes(Path(__file__).read_bytes());bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';meta=json.loads((build/'DOTA2.build.json').read_text());assert sha(bda)==meta['bda_sha256']and sha(elf)==meta['elf_sha256'];assert not meta.get('diagnostic_only');bundle_sha=sha(a.bundle);assert meta['runtime_assets']['DOTA2.H1R']['sha256']==bundle_sha
 compiled={n:h for n,h in meta['source_sha256'].items()if Path(n).suffix.lower()in ['.c','.h','.s','.ld']};assert all(sha(ROOT/n)==h for n,h in compiled.items()),'Candidate compiled sources are stale'
 noncompiled_changes=[n for n,h in meta['source_sha256'].items()if n not in compiled and(not(ROOT/n).is_file()or sha(ROOT/n)!=h)]
 inputs=[Path(__file__).resolve(),ROOT/'tests/mips_core_model.py',ROOT/'sdk/sdk/include/h1_input.h'];test_sources={str(p):sha(p)for p in inputs};constants={m[1]:int(m[2])for m in re.finditer(r'^#define\s+(H1_KEY_\w+)\s+(\d+)',inputs[-1].read_text(),re.M)}
 with elf.open('rb')as stream:layouts,sizes=layouts_of(ELFFile(stream),{'DotaGame','DotaUnit','DotaView','DotaItemWorldState','DotaItemChannel','DotaExtraHero','DotaExtraHeroesState'})
 sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
 m=CoreMachine(bda,elf,{'DOTA2.H1R':a.bundle});required=['dota_game','view','use_inventory','confirm_item','pump','held','pressed','item_tap','resource_status'];assert all(n in m.symbols for n in required),[n for n in required if n not in m.symbols];has_process_ui='process_ui'in m.symbols
 gp=m.symbols['dota_game'];vp=m.symbols['view'];up=gp+layouts['DotaGame']['units'];calls=[];checks=0;cases=[];native_ms=1000;events=collections.deque();event_receipts=[];fixture_writes=[];start=time.monotonic()
 report=dict(passed=False,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,entry_executed=False,video_present_executed=False,audio_device_opened=False,game_state_ram_writes_by_test=0,optional_chain=dict(process_ui_symbol_retained=has_process_ui,executed=has_process_ui,note='If optimized into entry, entry and guessed inline regions are not run; pump edge counts and direct bounded native activations are tested separately.'),scope=__doc__,bda_sha256=sha(bda),elf_sha256=sha(elf),metadata_sha256=sha(build/'DOTA2.build.json'),bundle_sha256=bundle_sha,compiled_source_sha256=compiled,test_source_sha256=test_sources,noncompiled_build_inputs_changed=noncompiled_changes,layouts=layouts,sizes=sizes,cases=cases,API_calls=calls,fixture_writes=fixture_writes,modeled_key_events=event_receipts)
 def save(name='verification.json'):(out/name).write_text(json.dumps(report,indent=2)+'\n')
 def check(ok,label):
  nonlocal checks;checks+=1
  if not ok:raise AssertionError(label)
 def call(name,*args):
  at=time.monotonic();value=m.call(name,*args);calls.append(dict(api=name,args=list(args),seconds=round(time.monotonic()-at,6)));return value
 def field(base,typ,name):return m.integer(base+layouts[typ][name])
 def view(name):return field(vp,'DotaView',name)
 def ufield(name):return field(up,'DotaUnit',name)
 def game():return m.read(gp,sizes['DotaGame'])
 def resources():return dict(zip(RESOURCE_FIELDS,struct.unpack('<21I',m.read(m.symbols['resource_status'],84))))
 def fixture(symbol,data):
  check(len(data)==m.symbol_sizes[symbol],symbol+' fixture ABI');address=m.symbols[symbol];check(address+len(data)<=gp or address>=gp+sizes['DotaGame'],symbol+' fixture outside Game');m.write(address,data);fixture_writes.append(dict(symbol=symbol,bytes=len(data),scope='Initial native UI/input fixture only; outside DotaGame'))
 def fixture_word(symbol,value):fixture(symbol,struct.pack('<I',value))
 # Register only documented GUI services used by these controller APIs. The
 # normal CoreMachine hook dispatches them through the actual GUI table.
 def timer(*_):return native_ms
 def fetch(code,value,*_):
  event=events.popleft()if events else(-1,-1);m.word(code,event[0]);m.word(value,event[1]);return 0
 for address,offset,callback in [(0x800011e0,0x71c,timer),(0x80001200,0x750,fetch)]:
  check(address not in m.callbacks,'Unused modeled callback slot');m.callbacks[address]=callback;m.write(address,struct.pack('<II',0x03e00008,0));m.word(0x80002000+offset,address)
 def setup(hero=0):
  nonlocal native_ms
  call('dota_init',gp,hero,73);native_ms=1000;events.clear();data=bytearray(sizes['DotaView'])
  for n,v in dict(screen=1,hero=hero,aim_item=-1,aim_skill=-1,follow=1,camera_x=ufield('x'),camera_y=ufield('y'),target_x=ufield('x'),target_y=ufield('y'),inventory_source=-1,inventory_hover=-1).items():struct.pack_into('<i',data,layouts['DotaView'][n],v)
  fixture('view',bytes(data))
  for name in ['held','pressed','item_tap','inventory_gesture','inventory_action','battlefield_gesture','battlefield_action']:
   if name in m.symbols:fixture(name,bytes(m.symbol_sizes[name]))
  for name in ['game_clock','clock_native_previous','clock_audio_previous','clock_ready','clock_has_audio','touch_pending','touch_down','minimap_drag','pending_actions','target_controls_frame']:
   if name in m.symbols:fixture_word(name,0)
  check(call('dota_audio_clock_ms',0x81501000)==0,'Unopened audio clock uses no device and returns false')
 def receive(item,count=1):
  for _ in range(count):check(call('dota_items_receive',gp,0,item,201)==1,'Actual item delivery '+str(item))
 def activation(slot,when):
  nonlocal native_ms;native_ms=when;call('use_inventory',slot)
 def event(code,key,when):
  nonlocal native_ms;native_ms=when;check(not events,'Prior events consumed');events.append((code,key));event_receipts.append(dict(code=code,key=key,native_ms=when));call('pump');check(not events,'Expected event consumed')
  if has_process_ui:call('process_ui')
 try:
  m.write(0x81500000,b'B:\\DOTA2\\DOTA2.H1R\0');check(call('resource_mount',0x81500000)==1,'Mount actual H1R');check(call('bs_open_default')==1 and call('item_assets_open')==1,'Open original virtual SCENE/item provider');call('item_assets_reserve_rules_cache',65536)
  for label,item in [('salve',5),('lotus-orb',117),('force-staff',100),('euls',97)]:
   setup();receive(item);before=game();mana=ufield('mana');activation(0,1000);check(view('aim_item')==0,label+' first activation aims');check(game()==before,label+' first activation consumes nothing');activation(0,1400);check(view('aim_item')==-1,label+' 400ms second activation self casts');check(game()!=before,label+' actual rules changed Game');check(call('dota_inventory_item',gp,0,0)==(-1 if item==5 else item),label+' correct item ownership after cast');check(ufield('mana')<=mana,label+' mana not increased by test');check(call('dota_items_cooldown',gp,0,0)>=0,label+' valid cooldown');after=game();activation(0,1450);check(game()==after,label+' third activation neither consumes nor bypasses cooldown');cases.append(dict(case=label+'-native-double-tap',passed=True,first_activation_game_unchanged=True,second_interval_ms=400))
   print('CASE '+label,flush=True)
  setup();receive(58,2);before=game();activation(7,1000);check(view('aim_item')==7 and game()==before,'Dedicated TP first activation only aims');activation(7,1400);check(view('aim_item')==-1,'Dedicated TP second activation confirms self');check(call('dota_items_teleport_charges',gp,0)==1,'Dedicated TP consumes exactly one charge');check(call('dota_items_teleport_channel',gp,0,0,0,0,0)==1,'Dedicated TP channel exists');channel=gp+layouts['DotaGame']['item_world']+layouts['DotaItemWorldState']['channel'];target=field(channel,'DotaItemChannel','target');team=ufield('team')
  # Fountain handles use DOTA_MAX_UNITS + team. Derive MAX_UNITS from the
  # retained units array's DWARF adjacency (effects immediately follows it).
  max_units=layouts['DotaGame']['effects']//sizes['DotaUnit'];check(target==max_units+team,'Dedicated TP self targets own-team fountain');after=game();activation(7,1450);check(game()==after and call('dota_items_teleport_charges',gp,0)==1,'TP third activation cannot consume again');cases.append(dict(case='tp7-native-double-tap-own-fountain',passed=True,team=team,channel_target=target,expected_fountain_target=max_units+team,remaining_charges=1));print('CASE TP',flush=True)
  setup();receive(5,2);activation(0,1000);call('confirm_item',ufield('x'),ufield('y'),0);check(view('aim_item')==-1 and call('dota_items_charges',gp,0,0)==1,'Actual confirm_item uses explicit friendly target once');cases.append(dict(case='native-confirm-item-explicit-self',passed=True))
  setup();receive(102);before=game();activation(0,1000);activation(0,1400);check(view('aim_item')==0 and game()==before,'Enemy-only Dagon double tap never self casts or consumes');cases.append(dict(case='enemy-only-native-double-tap-refused',passed=True))
  setup();receive(31,7);receive(58,2);check(call('dota_inventory_item',gp,0,6)==31,'UI slot6 is first backpack item');check(call('dota_items_cast_item',gp,0,6)==-1,'Legacy slot6 remains empty neutral, not backpack');check(call('dota_items_cast_item',gp,0,7)==58 and call('dota_inventory_item',gp,0,9)==58,'Legacy7 and UI9 refer to same dedicated TP');before=game();activation(6,1000);check(view('aim_item')==-1 and game()==before,'Neutral activation does not use backpack or TP');cases.append(dict(case='main-backpack-neutral-tp-mapping',passed=True,backpack0=31,neutral=-1,TP_legacy_slot=7,TP_UI_slot=9,nonempty_neutral_also_covered_by_host_tp_slot_test=True))
  setup();receive(58,2);gkey=constants['H1_KEY_G'];keys_before=m.integer(m.symbols['h1_dota_debug']+20);event(9,gkey,1000)
  if not has_process_ui:check(m.read(m.symbols['pressed']+gkey,1)==b'\1','Initial G edge latched by actual pump');activation(7,1000)
  check(view('aim_item')==7 and call('dota_items_teleport_charges',gp,0)==2,'First G edge/native activation enters TP aim');tap_state=m.read(m.symbols['item_tap'],m.symbol_sizes['item_tap']);before=game()
  for now in [1050,1100,1150,1200]:event(9,gkey,now);check(game()==before and m.read(m.symbols['item_tap'],m.symbol_sizes['item_tap'])==tap_state,'Repeated held KEY_DOWN produces no second activation')
  check(m.integer(m.symbols['h1_dota_debug']+20)-keys_before==1 and m.read(m.symbols['held']+gkey,1)==b'\1','Repeated down messages retain held state and create only first edge');event(10,gkey,1250);check(m.read(m.symbols['held']+gkey,1)==b'\0','Actual key-up releases held latch');event(9,gkey,1400)
  if not has_process_ui:activation(7,1400)
  check(view('aim_item')==-1 and call('dota_items_teleport_charges',gp,0)==1,'Release plus second G activation self casts exactly once');before=game();tap_state=m.read(m.symbols['item_tap'],m.symbol_sizes['item_tap'])
  for now in [1450,1500,1550]:
   event(9,gkey,now);check(game()==before and m.read(m.symbols['item_tap'],m.symbol_sizes['item_tap'])==tap_state,'Held G after cast never reactivates')
   if has_process_ui:check(m.read(m.symbols['pressed']+gkey,1)==b'\0','process_ui clears edge latch')
  keys_after=m.integer(m.symbols['h1_dota_debug']+20);check(keys_after-keys_before==2,'Only two real key-down edges reach native counter');cases.append(dict(case='actual-pump-held-G-edge-filter',passed=True,modeled_key_down_messages=9,actual_new_edges=keys_after-keys_before,process_ui_executed=has_process_ui,direct_native_activations_without_process_ui=0 if has_process_ui else 2));print('CASE held edge',flush=True)
  for label,item in [('faerie-fire',18),('mango',51),('bottle',54),('lotus',403),('great-lotus',404),('greater-lotus',405),('mana-draught',318)]:
   setup();receive(item);slot=next(s for s in range(7)if call('dota_items_cast_item',gp,0,s)==item);charges=call('dota_items_charges',gp,0,slot);activation(slot,1000);check(view('aim_item')==-1,label+' first activation does not aim');check(call('dota_items_cooldown',gp,0,slot)>0 if item==318 else call('dota_items_charges',gp,0,slot)==charges-1,label+' applies cooldown or consumes one charge immediately');after=game();activation(slot,1400);check(game()==after,label+' second tap cannot consume twice');cases.append(dict(case=label+'-native-no-target-first-tap',passed=True));print('CASE '+label+' immediate',flush=True)
  setup();receive(51);fixture('held',bytes([1 if i==constants['H1_KEY_SHIFT'] else 0 for i in range(m.symbol_sizes['held'])]));before=game();activation(0,1000);check(view('aim_item')==0 and game()==before,'Shift mango keeps explicit ally aiming without consumption');cases.append(dict(case='optional-mango-native-shift-target',passed=True))
  setup(4);check(call('dota_progression_learn',gp,0,1)==1,'Learn Pounce with actual available level1 skill point');input_at=0x81502000
  # Scratch is a modeled external input struct, never a Game/Unit write.
  m.write(input_at,struct.pack('<4iI',1,0,0,0,0));old_x=ufield('x');call('dota_tick',gp,input_at);check(ufield('x')>old_x,'Actual Slark right movement establishes east facing');m.write(input_at,struct.pack('<4iI',0,0,0,0,64));call('dota_tick',gp,input_at);old_x,old_y=ufield('x'),ufield('y');call('begin_skill',1);check(view('aim_skill')==-1 and m.integer(m.symbols['pending_actions'])==4,'Native Pounce activation immediately queues W without aiming');m.write(input_at,struct.pack('<4iI',0,0,0,4095,m.integer(m.symbols['pending_actions'])));call('dota_tick',gp,input_at);extra_hero=gp+layouts['DotaGame']['extra']+layouts['DotaExtraHeroesState']['hero']
  check(field(extra_hero,'DotaExtraHero','leap_at')>0 and field(extra_hero,'DotaExtraHero','leap_to_x')>old_x and field(extra_hero,'DotaExtraHero','leap_to_y')==old_y,'Actual MIPS Pounce ignores opposite cursor and jumps east');cases.append(dict(case='slark-native-pounce-no-target-current-facing',passed=True));print('CASE Slark Pounce',flush=True)
  report['resource_before_close']=resources();check(report['resource_before_close']['mounted']==1 and report['resource_before_close']['last_error']==0,'Actual resources healthy throughout controller tests');call('item_assets_close');call('bs_close');check(call('resource_unmount')==1,'Unmount controller resources');report['resource_after_close']=resources();check(not m.handles and not m.allocations and report['resource_after_close']['heap_bytes']==0 and report['resource_after_close']['open_files']==0,'All modeled file handles and heap released');check(all(sha(ROOT/n)==h for n,h in compiled.items()),'Candidate compiled sources unchanged');check(all(sha(Path(n))==h for n,h in test_sources.items()),'Test inputs unchanged');check(sha(bda)==meta['bda_sha256']and sha(elf)==meta['elf_sha256']and sha(a.bundle)==bundle_sha,'Frozen BDA/ELF/bundle unchanged');report.update(passed=True,checks=checks,compiled_source_current=True,source_current=True,elapsed_seconds=time.monotonic()-start,modeled_physical_bytes_read=m.bytes_read,all_build_inputs_current=not noncompiled_changes);save();print(json.dumps(dict(passed=True,checks=checks,cases=len(cases),actual_mips32=True,physical_h1_tested=False,report=str(out/'verification.json'))))
 except Exception as e:report.update(error=str(e),checks=checks);save('failure.json');(out/'failure.txt').write_text(traceback.format_exc());raise
 finally:m.dispose()
if __name__=='__main__':main()
