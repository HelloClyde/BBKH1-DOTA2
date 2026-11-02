"""Prepare/copy raw round3 evidence. Default is a plan, never an archive write.

No build, USB, Git, documentation-summary or outcome mutation is performed.
Raw report bytes are retained with their original failed/passed/source_current
fields. CPU slots, host parity and device FPS retain their separate scopes.
"""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[3]
BASE_ROOTS=[
 'build/v030/local-fov','build/v030/sprite-pipeline',
 'build/v030/startup-io-study','build/v030/perf3-device',
 'build/v030/perf3-baseline7','build/v030/perf3-previous',
]
PRODUCTION=[
 'game/sight.c','game/sight.h','platform/baked_scene.c','platform/baked_scene.h',
 'platform/compressed_cache.h','platform/sprite_rows.h',
 'platform/resource_pack.c','platform/resource_pack.h','platform/native.c',
]
DRIVERS=[
 'tests/local_fov_test.c','tests/local_fov_cases.inc',
 'tests/verify_local_fov.py','tests/verify_local_fov_mips.py',
 'tests/mips_core_model.py','tests/build_sprite_prototype.py','tests/verify_sprite_pipeline.py',
 'tests/profile_renderer_snapshot_mips.py','tests/verify_fog_pair.py',
 'tests/resource_renderer_ui.c','tests/trace_startup_io_mips.py',
 'tests/verify_resource_read_cache_plan.py','tests/replay_resource_cache_plan.c',
 'tests/measure_resource_cache_plan.py','tests/resource_pack_test.c',
 'tests/verify_resource_pack.py','tests/verify_resource_mips.py',
 'tests/physical_resource_profile.py',
 'docs/PERFORMANCE-v0.30-round3.md',
 'build/v030/perf3-archive/compare_round3.py',
]
REFERENCE_INPUTS=[
 'build/v030/perf2-final7-aligned/fountain-paused-game.bin',
 'build/v030/perf2-final7-aligned/post-horn-paused-game.bin',
 'build/v030/perf-candidate7/DOTA2.build.json',
 'build/v030/perf-candidate7/commands.json',
 'build/v030/game-cost-study/host-finalclass/new-wrapper.c',
]
TEXT={'.json','.py','.c','.h','.inc','.txt','.md','.csv','.ld','.log'}
FORBIDDEN={'.bda','.elf','.o','.obj','.exe','.a','.lib','.dll','.so','.pdb','.pyc'}
ASSETS={'.h1r','.h1p','.h1t','.h1v','.h1c','.h1a','.raw','.ppm','.pgm','.wav','.mp3'}

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def rel(path):return Path(path).relative_to(ROOT).as_posix()
def read_json(path):return json.loads(Path(path).read_bytes())
def checked(path):
 path=Path(path);path=(path if path.is_absolute() else ROOT/path).resolve()
 assert path.is_relative_to(ROOT),('outside repository',path)
 return path
def classification(path):
 ext=path.suffix.lower();n=path.name.lower();size=path.stat().st_size
 if ext in FORBIDDEN:return False,'compiled-executable-object-or-library'
 if ext in ASSETS:return False,'regenerable-asset-or-canonical-render-output'
 if ext=='.map':return False,'regenerable-linker-map'
 if ext=='.bin':
  if ('game' in n or 'fog' in n) and size<=256*1024:return True,'small-Game-or-fog-snapshot'
  return False,'binary-not-an-approved-small-Game-or-fog-snapshot'
 if ext=='.png':
  if size<=1024*1024:return True,'small-device-screenshot'
  return False,'oversize-raster-output'
 if ext in TEXT:
  if size<=8*1024*1024:return True,'raw-report-driver-or-source-snapshot'
  return False,'oversize-generated-text'
 return False,'unselected-file-type'

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--include-root',action='append',default=[],help='Additional final hardware/check folder under build/')
 p.add_argument('--final-build',help='Final combined build: metadata/commands copied; executable hashes checked but binaries excluded')
 p.add_argument('--destination',default='provenance/verification/v0.30/performance/round3')
 p.add_argument('--plan-output',help='Optional derived selection plan under build/; never modifies provenance')
 p.add_argument('--execute',action='store_true',help='Explicitly copy selected bytes and write raw manifest; root decides when to run')
 a=p.parse_args();destination=checked(a.destination)
 assert destination.is_relative_to(ROOT/'provenance/verification/v0.30/performance/round3'),destination
 roots=[checked(x) for x in BASE_ROOTS+a.include_root]
 for root in roots:assert root.is_relative_to(ROOT/'build'),root
 selected={};excluded={};missing=[]
 def consider(path,category):
  path=checked(path)
  if not path.is_file():missing.append(rel(path));return
  ok,reason=classification(path)
  row=dict(source=rel(path),bytes=path.stat().st_size,category=category,selection_reason=reason)
  if ok:
   row.update(path=rel(destination/'raw'/rel(path)),sha256=sha(path),byte_identical_at_capture=True);selected[row['source']]=row
  else:excluded[row['source']]=dict(**row,exclusion_reason=reason)
 for root in roots:
  if not root.exists():missing.append(rel(root));continue
  for path in sorted(root.rglob('*')):
   if path.is_file():consider(path,'raw-study-or-device-evidence')
 for name in PRODUCTION:consider(ROOT/name,'selected-production-source-snapshot')
 for name in DRIVERS:consider(ROOT/name,'test-and-measurement-source')
 for name in REFERENCE_INPUTS:consider(ROOT/name,'C7-reference-or-small-physical-Game-input')
 consider(ROOT/'docs/IPU-v0.30.md','root-managed-IPU-assessment')
 consider(Path(__file__),'archive-script')
 for name in ['independent-review.json','write_independent_review.py']:
  consider(Path(__file__).with_name(name),'independent-source-review')
 final=None
 if a.final_build:
  build=checked(a.final_build);assert build.is_relative_to(ROOT/'build')
  meta=read_json(build/'DOTA2.build.json')
  assert sha(build/'DOTA2.bda')==meta['bda_sha256'] and sha(build/'DOTA2.elf')==meta['elf_sha256']
  checked_sources={}
  for name in PRODUCTION:
   path=ROOT/name
   if path.exists() and name in meta['source_sha256']:
    assert sha(path)==meta['source_sha256'][name],('selected source drift',name)
    checked_sources[name]=sha(path)
  for name in ['DOTA2.build.json','commands.json','DOTA2.bda','DOTA2.elf']:consider(build/name,'final-combined-build')
  final=dict(source=rel(build),bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],selected_source_hash_checks=checked_sources)
 rows=sorted(selected.values(),key=lambda x:x['source'])
 manifest=dict(schema_version=1,version='0.30',round=3,scope=__doc__,executed=a.execute,
  physical_acceptance_inferred=False,summary_docs_modified=False,gameplay_or_measurement_fields_rewritten=False,
  final_combined_build=final,files=rows,selected_files=len(rows),selected_raw_bytes=sum(x['bytes'] for x in rows),
  explicit_exclusion_policy=dict(compiled_binaries=sorted(FORBIDDEN),assets_and_canonical_rasters=sorted(ASSETS),linker_maps=True,
   arbitrary_binary_outputs=True,per_binary_snapshot_limit_bytes=256*1024,small_snapshot_name_requires='game or fog',
   text_limit_bytes=8*1024*1024,screenshot_limit_bytes=1024*1024),
  excluded_files=sorted(excluded.values(),key=lambda x:x['source']),missing_optional_inputs=sorted(set(missing)),
  limitations=['This manifest records raw bytes, not acceptance or physical FPS conclusions.',
   'Failed attempts remain failed; raw source_current fields retain their original meaning.',
   'BDA/ELF, objects/libraries, asset containers and large canonical renderer outputs are intentionally omitted.',
   'Restore archived drivers to original source paths in an isolated checkout and obtain retained toolchain/assets/build binaries to reproduce.'])
 if a.execute:
  destination.mkdir(parents=True,exist_ok=True)
  for row in rows:
   source=ROOT/row['source'];target=ROOT/row['path'];data=source.read_bytes();assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
   target.parent.mkdir(parents=True,exist_ok=True)
   if target.exists():assert target.read_bytes()==data,('refuse replacing archived bytes',target)
   else:target.write_bytes(data)
   assert sha(target)==row['sha256']
  index=destination/'RAW-MANIFEST.json';data=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf8')
  if index.exists():assert index.read_bytes()==data,'Existing manifest differs; select a new archive destination'
  else:index.write_bytes(data)
 if a.plan_output:
  path=checked(a.plan_output);assert path.is_relative_to(ROOT/'build'),path;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(executed=a.execute,selected_files=len(rows),selected_raw_bytes=manifest['selected_raw_bytes'],excluded_files=len(excluded),missing_optional_inputs=manifest['missing_optional_inputs'],destination=rel(destination))))
if __name__=='__main__':main()
