"""Normal-input physical H1 frame/resource profiling; no live Game RAM writes."""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
RESOURCE='mounted version file_bytes open_files heap_bytes last_error mount_attempts open_attempts read_requests bytes_returned physical_reads physical_bytes seeks index_hits index_misses chunk_hits chunk_misses decoded_chunks decoded_bytes service_calls errors'.split()
SCENE='tile_hits tile_misses sprite_hits sprite_misses read_requests bytes_requested read_failures map_pixels sprite_pixels blended_pixels depth_rejects units_drawn effects_drawn culled_sprites missing_frames rendered_triangles'.split()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--debug-workspace',type=Path,required=True);p.add_argument('--seconds',type=float,default=6);p.add_argument('--menu-frame',type=int,default=320,help='Normal ENTER at this menu frame to align initial game seed/lineup');p.add_argument('--post-horn',action='store_true',help='Also walk toward mid and sample naturally after the first wave');p.add_argument('--expected-lineup',help='Comma-separated hero IDs from the baseline; reject an unmatched run');a=p.parse_args()
 build=a.build.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 meta=json.loads((build/'DOTA2.build.json').read_text());assert sha(build/'DOTA2.bda')==meta['bda_sha256'];assert sha(build/'DOTA2.elf')==meta['elf_sha256']
 with(build/'DOTA2.elf').open('rb')as f:
  e=ELFFile(f);symbols={x.name:(int(x['st_value']),int(x['st_size']))for x in e.get_section_by_name('.symtab').iter_symbols()}
 (out/'driver.py').write_bytes(Path(__file__).read_bytes())
 report=dict(passed=False,physical_h1_tested=True,normal_inputs_only=True,game_state_ram_writes=0,bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],driver_sha256=sha(__file__),post_horn_requested=a.post_horn,profiles=[])
 def save():(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 sys.path.insert(0,str(a.debug_workspace.resolve()/'tools'));from h1_debug import Client
 with Client()as c:
  def words(name,count=None):
   address,size=symbols[name];count=count or size//4;return list(struct.unpack('<'+'I'*count,c.read_memory(address,count*4)))
  def wait(predicate,label,seconds=120):
   end=time.monotonic()+seconds
   while time.monotonic()<end:
    if predicate():return
    time.sleep(.1)
   raise AssertionError(label)
  def key(name):c.press([name],ticks=20);time.sleep(.18);c.release();time.sleep(.2)
  def screen(n):wait(lambda:words('h1_dota_debug',8)[2]==n,'screen '+str(n))
  def sample():
   r=dict(debug=words('h1_dota_debug',8),scene=dict(zip(SCENE,words('stats',16))),resources=dict(zip(RESOURCE,words('resource_status',21))),cache=words('h1_dota_cache',16),audio=words('h1_dota_audio_debug',32))
   for name in ('io_stats','fog_stats','fog_cache_stats','lighting_stats','water_stats','tree_stats'):
    if name in symbols:r[name]=words(name)
   return r
  def profile(name):
   before=sample();t0=time.monotonic();b=words('h1_dota_debug',8);t1=time.monotonic();start=(t0+t1)/2
   time.sleep(a.seconds);t0=time.monotonic();v=words('h1_dota_debug',8);t1=time.monotonic();elapsed=(t0+t1)/2-start;after=sample()
   entry=dict(name=name,fps=(v[1]-b[1])/elapsed,frames=v[1]-b[1],seconds=elapsed,game_ticks=v[6]-b[6],before=before,after=after,resource_delta={k:after['resources'][k]-before['resources'][k]for k in RESOURCE if k not in ('mounted','version','file_bytes','open_files','heap_bytes','last_error')})
   report['profiles'].append(entry);save();print(json.dumps(dict(name=name,fps=entry['fps'],frames=entry['frames'],game_ticks=entry['game_ticks'],resource_delta=entry['resource_delta'],scene=after['scene'],audio=after['audio'])),flush=True)
  try:
   report['status_before']=c.status();report['desktop_before']=c.desktop_status();assert report['desktop_before']['menu_idle']==1
   report['launch']=c.desktop_run(r'A:\应用\程序\DOTA2.bda');save();print('LAUNCHED',flush=True)
   def ready():
    state=words('h1_dota_debug',8)
    if state[7]:
     report['startup_debug']=state;report['startup_resources']=dict(zip(RESOURCE,words('resource_status',21)));save()
     raise AssertionError('Startup error '+str(state[7]))
    return state[1]>=3
   wait(ready,'three fully presented frames',150);screen(0)
   sys.path.insert(0,str(ROOT/'sdk'));from h1_bda.resources import PAYLOAD_OFFSET
   payload=(build/'DOTA2.bda').read_bytes()[PAYLOAD_OFFSET:]
   for name in ('resource_read_at','tree_visual_open','dota_cast_order_tick','dota_match_capture'):
    at=symbols[name][0];assert c.read_memory(at,32)==payload[at-0x83c00020:at-0x83c00020+32],name
   profile('menu')
   # Poll below one LCD frame and submit ENTER immediately after that same
   # observed counter; a second read and 100ms sleep can skip menu frames.
   deadline=time.monotonic()+90
   while True:
    frame=words('h1_dota_debug',8)[1]
    if frame>=a.menu_frame:break
    assert time.monotonic()<deadline,'fixed menu frame timeout'
    time.sleep(.003)
   report['menu_frame_before_enter']=frame
   key('ENTER');screen(1);key('K')
   units=c.read_memory(symbols['dota_game'][0],10*240);report['lineup_hero_ids']=[struct.unpack_from('<i',units,i*240+12)[0]for i in range(10)];save()
   if a.expected_lineup and report['lineup_hero_ids']!=[int(x)for x in a.expected_lineup.split(',')]:
    key('BACK');screen(2);c.tap(390,174,hold_seconds=.18);wait(lambda:c.desktop_status()['menu_idle']==1,'normal exit after unmatched lineup',30)
    raise AssertionError('Unmatched lineup; no gameplay comparison performed')
   profile('fresh-fountain-play')
   key('BACK');screen(2);profile('same-fountain-paused')
   (out/'fountain-paused-game.bin').write_bytes(c.read_memory(symbols['dota_game'][0],symbols['dota_game'][1]))
   report['fountain_screenshot']=c.screenshot(out/'fountain-paused.png');save()
   key('BACK');screen(1);profile('fountain-resumed')
   c.tap(36,236,hold_seconds=.18);time.sleep(.2);profile('mid-camera-first');profile('mid-camera-warm')
   key('BACK');screen(2);profile('same-mid-paused')
   (out/'mid-paused-game.bin').write_bytes(c.read_memory(symbols['dota_game'][0],symbols['dota_game'][1]));report['mid_screenshot']=c.screenshot(out/'mid-paused.png');save()
   if a.post_horn:
    key('BACK');screen(1);c.tap(240,96,hold_seconds=.18);time.sleep(.2);profile('mid-approach-play')
    print('WAITING_FOR_NATURAL_FIRST_WAVE',flush=True)
    wait(lambda:words('h1_dota_debug',8)[6]>=1800,'natural first wave',110)
    profile('post-horn-mid-play')
    c.tap(110,238,hold_seconds=.18);time.sleep(.2);profile('post-horn-hero-play')
    key('BACK');screen(2);profile('post-horn-hero-paused')
    (out/'post-horn-paused-game.bin').write_bytes(c.read_memory(symbols['dota_game'][0],symbols['dota_game'][1]));report['post_horn_screenshot']=c.screenshot(out/'post-horn-paused.png');save()
   c.tap(390,174,hold_seconds=.18);wait(lambda:c.desktop_status()['menu_idle']==1,'normal exit',30)
   report['after_exit']=sample();report['status_after']=c.status();report['desktop_after']=c.desktop_status();report['passed']=True;save()
  except Exception as exc:report['error']=str(exc);save();raise
  finally:c.release();c.release_touch()
if __name__=='__main__':main()
