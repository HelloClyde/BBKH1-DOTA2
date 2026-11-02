from pathlib import Path
import sys,json,time,struct
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT.parent/'tools'))
from h1_debug import Client
with (ROOT/'build/v030/perf-candidate8/DOTA2.elf').open('rb')as f:
 e=ELFFile(f);symbols={s.name:(int(s['st_value']),int(s['st_size']))for s in e.get_section_by_name('.symtab').iter_symbols()}
out=Path(__file__).with_name('normal-exit-after-timeout.json')
r={'reason':'Startup harness30-second draw timeout; healthy menu was subsequently observed. Initial ESCAPE did not exit; native menu exits with BACK.'}
with Client()as c:
 def words(name):
  at,n=symbols[name];return list(struct.unpack('<'+'I'*(n//4),c.read_memory(at,n)))
 try:
  r['before']={'desktop':c.desktop_status(),'debug':words('h1_dota_debug')}
  assert r['before']['desktop']['state']==3 and r['before']['debug'][2]==0 and r['before']['debug'][1]>3 and r['before']['debug'][7]==0
  r['live_optional_cache_symbols']={n:words(n)for n in ['sprite_row_slots','sprite_row_heap_bytes','shadow_curve','read_cache']}
  c.press(['BACK'],ticks=20);time.sleep(.2);c.release()
  until=time.monotonic()+30
  while time.monotonic()<until:
   d=c.desktop_status()
   if d['menu_idle']:break
   time.sleep(.2)
  assert d['menu_idle']==1
  r.update(passed=True,desktop=d,status=c.status(),resources=words('resource_status'),cache=words('h1_dota_cache'),audio=words('h1_dota_audio_debug'))
 except Exception as e:r.update(passed=False,error=str(e));raise
 finally:
  c.release();c.release_touch();out.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r))
