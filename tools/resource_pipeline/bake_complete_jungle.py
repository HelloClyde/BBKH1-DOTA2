"""Bake every species actually selected by the installed ordinary-camp pools."""
from pathlib import Path
import argparse,concurrent.futures,json,re,subprocess,sys
from source2_vpk import VpkArchive
import bake_roshan_ancients as baker
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'private/source2/exports/jungle-complete'
OUT=ROOT/'private/baked/jungle-complete'
OVERRIDES={
 'froglet':['tadpole_idle_var1','tadpole_run_2','tadpole_attack_var_5','tadpole_death_alt_5'],
 'froglet_mage':['tadpole_ranged_idle','tadpole_run_2','tadpole_ranged_attack','tadpole_death_alt_5'],
 'ghost':['ghost_a_idle','ghost_a_run','ghost_a_hit','ghost_a_death'],
 'tadpole':['tadpole_c_idle_loop','tadpole_c_retarget_run_2','tadpole_c_attack_var_1','tadpole_c_death_a'],
}

def choose(names,kind):
 choices={'idle':['idle','idle_anim','idle01','idle1'],'run':['run','run_anim','walk','walk_anim','runN'],'attack':['attack','attack_anim','attack01','attack1','attack01_anim','attack1_anim','attack_melee','hit'],'death':['death','die','dieAlt','death_anim','die1','death1']}[kind]
 for value in choices:
  if value in names:return value
 token='(?:death|die)' if kind=='death' else kind
 matches=[n for n in names if not n.startswith('@') and re.search(r'(?:^|_)'+token+r'(?:[0-9]*|_[a-z0-9]+)?$',n,re.I)]
 if not matches:raise ValueError((kind,names))
 return min(matches,key=lambda n:(sum(s in n.lower() for s in ('channel','flap','headshake','alt','rare')),len(n),n))

def main():
 p=argparse.ArgumentParser();p.add_argument('--extract-only',action='store_true');p.add_argument('--ss',type=int,default=2);p.add_argument('--workers',type=int,default=3);a=p.parse_args()
 SOURCE.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True);manifest=json.loads((ROOT/'private/jungle-complete/manifest.json').read_text(encoding='utf8'))
 units=manifest['units'];v=VpkArchive(baker.STEAM/'pak01_dir.vpk')
 def export(unit):
  slug=unit['name'].removeprefix('npc_dota_neutral_');source=unit['source'];stats=source['effective_stats_with_script_defaults'];path=SOURCE/slug/(slug+'.gltf')
  if not path.exists():
   r=subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name','jungle_complete_'+slug,'--entry',source['model_resource'],'--output',str(path),'--skeleton'],capture_output=True)
   if r.returncode:raise RuntimeError(r.stdout.decode('utf8',errors='replace')+r.stderr.decode('utf8',errors='replace'))
  names=[x['name'] for x in json.loads(path.read_text(encoding='utf8')).get('animations',[])]
  try:clips=OVERRIDES[slug] if slug in OVERRIDES else [choose(names,k) for k in ('idle','run','attack','death')]
  except ValueError as exc:return dict(error=str(exc),name=slug,animations=names)
  assert all(c in names for c in clips),(slug,clips,names)
  spec=dict(id=unit['model_id'],name=slug,unit=unit['name'],model=source['model_resource'],scale=float(stats['ModelScale']),clips=clips)
  print(json.dumps(spec),flush=True);return spec
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:specs=list(pool.map(export,units))
 errors=[s for s in specs if 'error' in s]
 if errors:
  (SOURCE/'animation-selection-errors.json').write_text(json.dumps(errors,indent=2)+'\n',encoding='utf8')
  raise ValueError(errors)
 (SOURCE/'model-specs.json').write_text(json.dumps(specs,indent=2)+'\n',encoding='utf8')
 if a.extract_only:return
 baker.SOURCE=SOURCE;identities={s['name']:v.identity(s['model']) for s in specs}
 # Thread workers share the explicitly bound original source root; each owns
 # separate model buffers and output directories. The C rasterizer releases GIL.
 tasks=[(s,OUT,a.ss,8,False,identities) for s in specs]
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:results=list(pool.map(baker.bake_model,tasks))
 report=dict(complete=True,models=[r[0] for r in results],frames=[f for r in results for f in r[1]],motions=[m for r in results for m in r[2]],source_manifest_sha256=baker.sha(ROOT/'private/jungle-complete/manifest.json'),source_sha256={str(Path(__file__).relative_to(ROOT)):baker.sha(__file__),'tools/bake_roshan_ancients.py':baker.sha(ROOT/'tools/bake_roshan_ancients.py')})
 (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,models=len(report['models']),frames=len(report['frames']))))
if __name__=='__main__':main()
