"""Export and bake four more original Steam heroes for a unique ten-hero match."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,subprocess,sys
from source2_vpk import VpkArchive
from bake_extra_heroes import kvparse

ROOT=Path(__file__).resolve().parents[1]
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SOURCE=ROOT/'private/source2/exports/roster-heroes'
OUT=ROOT/'private/baked/roster-heroes'
HEROES=[dict(id=93,hero=6,name='pudge'),dict(id=94,hero=7,name='sniper'),dict(id=95,hero=8,name='sven'),dict(id=96,hero=9,name='tidehunter')]
CLIPS={
 'pudge':['pudge_idle','pudge_walkN','pudge_attack1','pudge_meathook_start','pudge_death','pudge_meathook_start','pudge_rot','pudge_idle','pudge_dismember_mid','pudge_dismember_start','pudge_idle'],
 'sniper':['sniper_idle','sniper_run','sniper_attack','sniper_attack_schrapnel_cast1_aggressive','sniper_death','sniper_attack_schrapnel_cast1_aggressive','sniper_attack','sniper_idle_aggressive','sniper_assassinate_cast4_aggressive'],
 'sven':['idle','runN','attack01','storm_bolt','death','storm_bolt','attack01','warcry','gods_strength'],
 'tidehunter':['idle','run','attack1','gush','death','gush','idle','anchor_smash','ravage'],
}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,d):Path(p).write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def export_models():
 SOURCE.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
 v=VpkArchive(STEAM/'pak01_dir.vpk');raw=v.read('scripts/items/items_game.txt');(SOURCE/'items_game.txt').write_bytes(raw)
 items=kvparse(raw.decode('utf-8-sig'))['items_game']['items'];defaults={}
 for spec in HEROES:
  slug=spec['name'];entry=f'scripts/npc/heroes/npc_dota_hero_{slug}.txt';raw=v.read(entry);(SOURCE/(slug+'-unit.txt')).write_bytes(raw)
  unit=kvparse(raw.decode('utf-8-sig'))['DOTAHeroes']['npc_dota_hero_'+slug]
  candidates=[dict(id=k,item=i) for k,i in items.items() if isinstance(i,dict) and i.get('prefab')=='default_item' and 'npc_dota_hero_'+slug in i.get('used_by_heroes',{}) and i.get('model_player')]
  parts=[p for p in candidates if '_persona_' not in p['item'].get('item_slot','')]
  spec['scale']=float(unit['ModelScale']);defaults[slug]=dict(unit=unit,defaults=parts,excluded_persona_defaults=[p['id'] for p in candidates if p not in parts],source_unit=v.identity(entry),model_id=spec['id'],hero_id=spec['hero'])
 write(SOURCE/'hero-defaults.json',defaults)
 def one(spec):
  slug=spec['name'];d=defaults[slug]
  for part,model in [('body',d['unit']['Model']),*[(Path(i['item']['model_player']).stem,i['item']['model_player']) for i in d['defaults']]]:
   path=SOURCE/slug/(part+'.gltf')
   if not path.exists():
    process=subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name',f'roster_{slug}_{part}','--entry',model+'_c','--output',str(path),'--skeleton'],capture_output=True)
    if process.returncode:raise RuntimeError(process.stdout.decode('utf8',errors='replace')+process.stderr.decode('utf8',errors='replace'))
  body=json.loads((SOURCE/slug/'body.gltf').read_text(encoding='utf8'))
  result=dict(**spec,default_wearables=len(d['defaults']),animations=[a['name'] for a in body.get('animations',[])],body_sha256=sha(SOURCE/slug/'body.gltf'))
  print(json.dumps({k:v for k,v in result.items() if k!='animations'},ensure_ascii=False),flush=True);return result
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:reports=list(pool.map(one,HEROES))
 write(SOURCE/'exports.json',dict(complete=True,heroes=reports,source_items=v.identity('scripts/items/items_game.txt'),generator_sha256=sha(__file__)))

def main():
 p=argparse.ArgumentParser();p.add_argument('--extract-only',action='store_true');p.add_argument('--hero',choices=CLIPS);p.add_argument('--ss',type=int,default=2);a=p.parse_args()
 export_models()
 if a.extract_only:return
 import bake_extra_heroes as original
 original.SOURCE=SOURCE;original.OUT=OUT
 exports=json.loads((SOURCE/'exports.json').read_text(encoding='utf8'))['heroes']
 reports=[];records=[];motions=[]
 for row in exports:
  if a.hero and a.hero!=row['name']:continue
  spec=dict(row,clips=CLIPS[row['name']],idle_clip=CLIPS[row['name']][0],loop_clips=[8] if row['name']=='pudge' else [])
  report,frames,clips=original.bake_body((spec,a.ss));reports.append(report);records.extend(frames);motions.extend(clips)
  write(OUT/(row['name']+'-manifest.json'),dict(complete=True,hero=row['hero'],body_model=row['id'],frames=frames,models=[report],clips=clips,source_sha256={str(Path(__file__).relative_to(ROOT)):sha(__file__),'tools/bake_extra_heroes.py':sha(ROOT/'tools/bake_extra_heroes.py')}))
 write(OUT/('body-manifest.json' if not a.hero else a.hero+'-body-summary.json'),dict(complete=True,frames=records,models=reports,clips=motions))

if __name__=='__main__':main()
