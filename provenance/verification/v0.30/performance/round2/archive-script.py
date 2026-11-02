"""Preserve measured bytes for round2; no device or executable operations."""
from pathlib import Path
import hashlib,json
ROOT=Path.cwd();OUT=ROOT/'provenance/verification/v0.30/performance/round2';OUT.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
index_path=OUT/'INDEX.json'
if index_path.exists():index=json.loads(index_path.read_bytes())
else:index=dict(schema_version=1,version='0.30',round=2,baseline_bda_sha256='33a650a25ef78ce16250cd3875f9be2ec6e7d831e6bd4d0ba775b5f1f83be857',candidate_bda_sha256='b7afd86515b0206d8a4ce53217fc761cfafdbba2a4f8f9337a94db55c5c54fcb',candidate_elf_sha256='757558cd332f739396063abbcb275684cea676666e828a35591d0240103680b8',installation_verified=None,performance_acceptance_passed=None,full_feature_acceptance_complete=False,release_pin_created=False,github_updated=False,physical_fps_samples=[],files=[],derived_files=[],scope='Round2 archive. Raw report fields and recorded source_current are preserved; diagnostic/MIPS/host evidence is not physical FPS. Final aligned physical acceptance is pending root confirmation.')
files={f['path']:f for f in index['files']}
def copy(src,dst=None,category='offline-evidence',snapshot=False):
 src=ROOT/src;assert src.is_file(),src
 dst=OUT/(dst or src.relative_to(ROOT));dst.parent.mkdir(parents=True,exist_ok=True);data=src.read_bytes()
 if dst.exists():assert dst.read_bytes()==data,('refuse changed archive',dst)
 else:dst.write_bytes(data)
 row=dict(path=dst.relative_to(ROOT).as_posix(),source=src.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),byte_identical_at_capture=True,category=category)
 if snapshot:row['snapshot_before_round2_update']=True
 files[row['path']]=row
def tree(folder,extensions,category='offline-evidence',predicate=None):
 base=ROOT/folder
 for p in sorted(base.rglob('*')):
  if p.is_file()and p.suffix.lower()in extensions and(not predicate or predicate(p)):copy(p.relative_to(ROOT),category=category)
for src,name in [('provenance/verification/v0.30/performance/INDEX.json','round1-INDEX-before.json'),('docs/verification/v0.30-h1.json','round1-h1-before.json'),('docs/PERFORMANCE-v0.30.md','round1-performance-before.md')]:
 target=OUT/'history'/name
 if not target.exists():copy(src,'history/'+name,'historical-snapshot',True)
 else:
  prior=files.get(target.relative_to(ROOT).as_posix());assert prior and sha(target)==prior['sha256']
meta=json.loads((ROOT/'build/v030/perf-candidate5/DOTA2.build.json').read_bytes());assert meta['bda_sha256']==index['candidate_bda_sha256']
for n in ['game/sight.c','game/sight.h','platform/tree_visual.c','platform/tree_visual.h','platform/render.c','platform/baked_scene.c']:
 assert sha(ROOT/n)==meta['source_sha256'][n],n;copy(n,'selected-source/'+n,'selected-production-source')
for n in ['tests/verify_sight_bits_mips.py','tests/verify_tree_visual_cache.py','tests/tree_visual_cache_test.c','tests/verify_minimap_bits.py','tests/verify_fog_pair.py','tests/resource_renderer_ui.c','tests/verify_vision_mips.py','tests/verify_trees_mips.py','tests/vision_mips_fixtures.c','tests/trees_mips_fixtures.c']:
 copy(n,'test-source/'+n,'test-source')
for n in ['DOTA2.build.json','commands.json']:copy('build/v030/perf-candidate5/'+n,'candidate5/'+n,'final-candidate-build')
g='build/v030/game-cost-study/'
for n in ['game-cost-summary.json','shadow-trace-summary.json','fountain-paused-game-shadow-trace.json','post-horn-paused-game-shadow-trace.json','treebyte-summary.json','base-plane-summary.json','sight-c3-before-height.c','sight-base8-treebyte.c','shadow-trace.py','build-height-variants.py','profile-height-variants.py','build-base-planes.py','profile-base-planes.py','build-treebyte.py','profile-treebyte.py','run-host-enhanced.py','summarize.py']:
 copy(g+n,category='isolated-cpu-study')
tree(g+'host-reference',{'.c','.json','.txt'},'host-sight-parity')
for variant in ['base','base8treebyte']:
 for n in ['sight-variant.c','commands.json','DOTA2.build.json']:copy(g+variant+'/'+n,category='isolated-cpu-study')
 for case in ['fountain-paused-game','post-horn-paused-game']:
  tree(g+variant+'/'+case,{'.json','.bin'},'isolated-cpu-study')
tree(g+'getter-mips-loop',{'.json','.c','.ld','.bin'},'diagnostic-getter-mips')
copy('build/v030/tree-local-cache/tree-visual-before.c',category='tree-cache-reference')
copy('build/v030/tree-local-cache/mips-object.json',category='tree-cache-object-size')
for folder in ['host1','existing-host','canonical']:
 tree('build/v030/tree-local-cache/'+folder,{'.json','.c','.txt'},'tree-cache-parity')
copy('build/v030/minimap-study/render-before.c',category='minimap-reference')
tree('build/v030/minimap-study/proof3',{'.json','.c','.txt'},'minimap-parity')
copy('build/v030/fog-pair-study/baked-before.c',category='fog-pair-reference')
tree('build/v030/fog-pair-study/parity-final2',{'.json','.c','.txt','.csv'},'fog-pair-final-parity')
for folder in ['h1-mix-benchmark','h1-mix-benchmark-v2']:
 tree('build/v030/fog-pair-study/'+folder,{'.json','.c','.ld'},'excluded-slower-kernel-benchmark')
 for p in sorted((ROOT/'build/v030/fog-pair-study'/folder).glob('*.bin')):copy(p.relative_to(ROOT),category='excluded-slower-kernel-benchmark')
tree('build/v030/perf2-trees-before',{'.json','.py','.png'},'historical-c3-physical-tree-functional')
for folder in ['perf2-final-resource-mips','perf2-final-vision-mips','perf2-final-trees-mips','perf2-final-sight-bits-mips']:
 tree('build/v030/'+folder,{'.json','.txt','.c','.ld'},'final-c5-mips-correctness')
 if folder.endswith('sight-bits-mips'):copy('build/v030/'+folder+'/caller.bin',category='final-c5-mips-correctness')
copy('build/v030/perf-union-aligned/verification.json','baseline-c3/verification.json','aligned-physical-baseline')
copy('build/v030/perf-union-aligned/driver.py','baseline-c3/driver.py','aligned-physical-baseline')
if (ROOT/'build/v030/perf2-final-aligned/verification.json').exists():
 d=json.loads((ROOT/'build/v030/perf2-final-aligned/verification.json').read_bytes())
 if not d.get('passed')and 'Unmatched lineup'in d.get('error',''):tree('build/v030/perf2-final-aligned',{'.json','.py'},'excluded-lineup-mismatch')
for n in ['perf2-final-deploy-tool.json','perf2-final-deploy.json','perf2-final-deploy.json.upload.json']:
 p=ROOT/'build/v030'/n
 if p.exists():
  d=json.loads(p.read_bytes())
  if d.get('completed')or d.get('ok')or d.get('phase')=='done':copy(p.relative_to(ROOT),category='candidate5-deployment-receipt')
copy('build/v030/game-cost-study/prepare-round2-archive.py','archive-script.py','archive-script')
index['files']=sorted(files.values(),key=lambda x:x['path']);index['archived_files']=len(files);index['archived_raw_bytes']=sum(x['bytes']for x in files.values())
index_path.write_text(json.dumps(index,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
assert all(sha(ROOT/f['path'])==f['sha256']for f in index['files'])
print(json.dumps(dict(files=len(files),bytes=index['archived_raw_bytes'],index_sha256=sha(index_path))))
