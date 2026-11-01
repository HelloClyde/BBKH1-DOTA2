"""Opt-in normal-input physical profiling of play versus paused rendering."""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--debug-workspace',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);(out/'driver.py').write_bytes(Path(__file__).read_bytes());app=a.build.resolve()
 sys.path.insert(0,str(a.debug_workspace.resolve()/'tools'));from h1_debug import Client
 with (app/'DOTA2.elf').open('rb') as f:
  e=ELFFile(f);s={x.name:(int(x['st_value']),int(x['st_size']))for x in e.get_section_by_name('.symtab').iter_symbols()};layouts={}
  for cu in e.get_dwarf_info().iter_CUs():
   for die in cu.iter_DIEs():
    name=die.attributes.get('DW_AT_name')
    if die.tag=='DW_TAG_typedef' and name and name.value==b'DotaGame':
     d=die.get_DIE_from_attribute('DW_AT_type')
     while d.tag=='DW_TAG_typedef':d=d.get_DIE_from_attribute('DW_AT_type')
     layouts['DotaGame']={x.attributes['DW_AT_name'].value.decode():x.attributes['DW_AT_data_member_location'].value for x in d.iter_children() if x.tag=='DW_TAG_member'}
 assert 'tick'in layouts['DotaGame'];g=s['dota_game'][0]
 r=dict(bda_sha256=hashlib.sha256((app/'DOTA2.bda').read_bytes()).hexdigest(),physical_h1_tested=True,normal_input_only=True,game_state_ram_writes=0,profiles=[])
 def save():(out/'verification.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 with Client()as c:
  def words(n,k):return list(struct.unpack('<'+'I'*k,c.read_memory(s[n][0],4*k)))
  def tick():return struct.unpack('<i',c.read_memory(g+layouts['DotaGame']['tick'],4))[0]
  def wait(test,label,seconds=120):
   deadline=time.monotonic()+seconds
   while time.monotonic()<deadline:
    if test():return
    time.sleep(.1)
   raise AssertionError(label)
  def key(n):c.press([n],ticks=20);time.sleep(.18);c.release();time.sleep(.2)
  def profile(n,seconds=6):
   t0=time.monotonic();b=words('h1_dota_debug',8);t1=time.monotonic();start=(t0+t1)/2
   time.sleep(seconds);t0=time.monotonic();v=words('h1_dota_debug',8);t1=time.monotonic();elapsed=(t0+t1)/2-start
   r['profiles'].append(dict(name=n,fps=(v[1]-b[1])/elapsed,seconds=elapsed,frames=v[1]-b[1],game_ticks=v[6]-b[6],debug=v,render=words('stats',16),fog=words('fog_stats',5),audio=words('h1_dota_audio_debug',32)));save();print(json.dumps(r['profiles'][-1]),flush=True)
  try:
   r['desktop_before']=c.desktop_status();assert r['desktop_before']['menu_idle']==1;r['launch']=c.desktop_run(r'A:\应用\程序\DOTA2.bda');wait(lambda:words('h1_dota_debug',8)[2]==0,'menu')
   from sys import path;path.insert(0,str(ROOT/'sdk'));from h1_bda.resources import PAYLOAD_OFFSET
   payload=(app/'DOTA2.bda').read_bytes()[PAYLOAD_OFFSET:];off=s['dota_sight_attach'][0]-0x83c00020;assert c.read_memory(s['dota_sight_attach'][0],32)==payload[off:off+32]
   key('ENTER');wait(lambda:words('h1_dota_debug',8)[2]==1,'play');key('K');save()
   wait(lambda:tick()>=1450,'natural post-horn',100);profile('post-horn-play')
   key('BACK');wait(lambda:words('h1_dota_debug',8)[2]==2,'pause');snapshot=c.read_memory(g,s['dota_game'][1]);(out/'paused-game.bin').write_bytes(snapshot);profile('same-camera-paused')
   assert c.read_memory(g,s['dota_game'][1])==snapshot
   r['screenshot']=c.screenshot(out/'paused.png');key('BACK');wait(lambda:words('h1_dota_debug',8)[2]==1,'resume');profile('resumed-play')
   key('BACK');wait(lambda:words('h1_dota_debug',8)[2]==2,'pause before exit');key('X');wait(lambda:c.desktop_status()['menu_idle']==1,'normal exit',30);r['desktop_after']=c.desktop_status();r['status_after']=c.status();r['passed']=True;save()
  except Exception as error:r['error']=str(error);save();raise
  finally:c.release();c.release_touch()
if __name__=='__main__':main()
