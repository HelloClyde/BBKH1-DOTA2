from pathlib import Path
import hashlib,json,struct,sys
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[2];out=ROOT/'build/v029/device/acceptance1-completion';out.mkdir(exist_ok=False)
source=ROOT/'build/v029/device/acceptance1/verification.json';r=json.loads(source.read_text());assert r['error']=='two complete frames' and all(x['passed'] for x in r['checks'])
with (ROOT/'build/v029/candidate4/DOTA2.elf').open('rb') as f:
 e=ELFFile(f);syms={s.name:int(s['st_value']) for s in e.get_section_by_name('.symtab').iter_symbols()}
sys.path.insert(0,str(ROOT.parent/'tools'));from h1_debug import Client
with Client() as c:
 def words(n,count):return list(struct.unpack('<'+'I'*count,c.read_memory(syms[n],count*4)))
 record=dict(passed=False,physical_h1_tested=True,same_run_read_only_completion=True,game_state_ram_writes=0,original_report_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),bda_sha256=r['bda_sha256'],desktop=c.desktop_status(),resident=c.status(),tree=words('tree_stats',16),audio=words('h1_dota_audio_debug',32),cache=words('h1_dota_cache',16),debug=words('h1_dota_debug',8))
 assert record['desktop']['menu_idle']==1 and record['desktop']['state']==4 and record['desktop']['return_code']==0
 assert record['tree'][1:4]==[0,0,0] and record['tree'][14]>0 and record['tree'][15]==1
 assert record['audio'][1]==0 and record['audio'][18]==0 and record['cache'][9]==0 and record['cache'][11]==1 and record['debug'][7]==0
 assert record['resident']['state']==1 and record['resident']['usb_fault']==record['resident']['last_error']==record['resident']['bad_frames']==0
 record['screenshot']=c.screenshot(out/'desktop-after-exit.png');record['passed']=True
(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
