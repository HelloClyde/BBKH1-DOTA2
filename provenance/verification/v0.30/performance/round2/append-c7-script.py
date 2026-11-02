"""Preserve selected C7 correctness and intermediate C6 physical results."""
from pathlib import Path
import hashlib,json
ROOT=Path.cwd();OUT=ROOT/'provenance/verification/v0.30/performance/round2';ip=OUT/'INDEX.json';d=json.loads(ip.read_bytes());rows={f['path']:f for f in d['files']};sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def copy(src,dst=None,category='final-c7-correctness'):
 src=ROOT/src;target=OUT/(dst or src.relative_to(ROOT));target.parent.mkdir(parents=True,exist_ok=True);data=src.read_bytes()
 if target.exists():assert target.read_bytes()==data,target
 else:target.write_bytes(data)
 row=dict(path=target.relative_to(ROOT).as_posix(),source=src.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),byte_identical_at_capture=True,category=category);rows[row['path']]=row
def tree(folder,ext,category):
 for p in sorted((ROOT/folder).rglob('*')):
  if p.is_file()and p.suffix.lower()in ext:copy(p.relative_to(ROOT),category=category)
for f in rows.values():
 if f['category']=='final-c6-production-source':f['category']='intermediate-c6-production-source';f['snapshot_before_final_obstruction_class_cache']=True
m=json.loads((ROOT/'build/v030/perf-candidate7/DOTA2.build.json').read_bytes())
for f in ['game/sight.c','game/sight.h','platform/tree_visual.c','platform/tree_visual.h','platform/render.c','platform/baked_scene.c']:
 assert sha(ROOT/f)==m['source_sha256'][f];copy(f,'selected-source-c7/'+f,'final-c7-production-source')
for f in ['DOTA2.build.json','commands.json']:copy('build/v030/perf-candidate7/'+f,'candidate7/'+f,'final-c7-build')
for f in ['build-finalclass.py','profile-finalclass.py','run-host-finalclass.py','finalclass-summary.json','sight-c6-before-finalclass.c']:copy('build/v030/game-cost-study/'+f,category='final-obstruction-class-study')
tree('build/v030/game-cost-study/host-finalclass',{'.c','.txt','.json'},'final-obstruction-class-host')
for variant in ['baseC6','finalclass']:
 tree('build/v030/game-cost-study/'+variant,{'.json','.c'},'final-obstruction-class-model')
 for case in ['fountain-paused-game','post-horn-paused-game']:tree('build/v030/game-cost-study/'+variant+'/'+case,{'.bin'},'final-obstruction-class-model')
for folder in ['perf2-bounded-aligned2','perf2-bounded-trees-cut']:
 p=ROOT/'build/v030'/folder/'verification.json';r=json.loads(p.read_bytes());assert r['passed']and r['bda_sha256']=='b79b98ce7d6389b060cc5b586d8631178bc28bea5e886bf74e0e86762d0bd81f';tree('build/v030/'+folder,{'.json','.py','.bin','.png'},'intermediate-c6-physical')
copy('build/v030/game-cost-study/append-c7-archive.py','append-c7-script.py','archive-script')
tree('build/v030/perf2-bounded-aligned',{'.json','.py'},'excluded-lineup-mismatch')
d['intermediate_candidate6']=dict(bda_sha256=d['candidate_bda_sha256'],elf_sha256=d['candidate_elf_sha256'],physical_fps_passed=True,physical_cut_only_passed=True,profile='build/v030/perf2-bounded-aligned2/verification.json',cut_only='build/v030/perf2-bounded-trees-cut/verification.json',superseded_reason='Final0/1/2 obstruction cache removes additional ordinary per-cell loads while retaining bounded fallback and exact tree updates.')
d.update(candidate_bda_sha256=m['bda_sha256'],candidate_elf_sha256=m['elf_sha256'],candidate_role='final-c7-bounded-final-obstruction-classes',pending_final_candidate='C7 physical profile/cut-only confirmation',installation_verified=None,performance_acceptance_passed=None)
d['prototype_cpu_payload_binding']=dict(prototype_bda_sha256=json.loads((ROOT/'build/v030/game-cost-study/finalclass/DOTA2.build.json').read_bytes())['bda_sha256'],standard_candidate7_bda_sha256=m['bda_sha256'],payload_bda_bytes_identical=True,prototype_elf_is_distinct_debug_build=True,scope='Isolated C6-to-finalclass8tick CPU/Game/fog comparison executes the same final BDA payload. No fresh final-ELF CPU rerun or physical FPS conversion claimed.')
assert d['prototype_cpu_payload_binding']['prototype_bda_sha256']==m['bda_sha256']
d['verification_inheritance']['fog_renderer']='C5 shader97-frame report retains original6dcc core identity. C7 shader source55df is identical; final core host105 masks/dirty-tree parity is composed evidence, not a fresh C7 full97-frame renderer execution.'
d['files']=sorted(rows.values(),key=lambda f:f['path']);d['archived_files']=len(rows);d['archived_raw_bytes']=sum(f['bytes']for f in rows.values());ip.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8');assert all(sha(ROOT/f['path'])==f['sha256']for f in rows.values());print(len(rows),d['archived_raw_bytes'],sha(ip))
