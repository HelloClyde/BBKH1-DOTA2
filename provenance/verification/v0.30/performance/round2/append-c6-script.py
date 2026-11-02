"""Archive final C6 source/correctness while physical acceptance is pending."""
from pathlib import Path
import hashlib,json
ROOT=Path.cwd();OUT=ROOT/'provenance/verification/v0.30/performance/round2';ip=OUT/'INDEX.json';d=json.loads(ip.read_bytes());rows={f['path']:f for f in d['files']};sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def copy(src,dst=None,category='final-c6-correctness'):
 src=ROOT/src;dst=OUT/(dst or src.relative_to(ROOT));dst.parent.mkdir(parents=True,exist_ok=True);data=src.read_bytes()
 if dst.exists():assert dst.read_bytes()==data,dst
 else:dst.write_bytes(data)
 row=dict(path=dst.relative_to(ROOT).as_posix(),source=src.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),byte_identical_at_capture=True,category=category);rows[row['path']]=row
def tree(folder,ext,category):
 for p in sorted((ROOT/folder).rglob('*')):
  if p.is_file()and p.suffix.lower()in ext:copy(p.relative_to(ROOT),category=category)
m=json.loads((ROOT/'build/v030/perf-candidate6/DOTA2.build.json').read_bytes());assert m['bda_sha256']=='b79b98ce7d6389b060cc5b586d8631178bc28bea5e886bf74e0e86762d0bd81f'
for p in ['game/sight.c','game/sight.h','platform/tree_visual.c','platform/tree_visual.h','platform/render.c','platform/baked_scene.c']:
 assert sha(ROOT/p)==m['source_sha256'][p];copy(p,'selected-source-c6/'+p,'final-c6-production-source')
for f in ['DOTA2.build.json','commands.json']:copy('build/v030/perf-candidate6/'+f,'candidate6/'+f,'final-c6-build')
for f in ['perf2-bounded-vision-mips','perf2-bounded-trees-mips','perf2-bounded-sight-bits-mips']:
 p=ROOT/'build/v030'/f/'verification.json';r=json.loads(p.read_bytes());assert r['passed']and r['bda_sha256']==m['bda_sha256'];tree('build/v030/'+f,{'.json','.txt','.c','.ld'},'final-c6-mips-correctness')
 if f.endswith('sight-bits-mips'):copy('build/v030/'+f+'/caller.bin',category='final-c6-mips-correctness')
tree('build/v030/tree-geometry-mask',{'.json','.py','.bin'},'original-vision-tree-geometry-equivalence')
copy('build/v030/game-cost-study/append-c6-archive.py','append-c6-script.py','archive-script')
d.update(candidate_bda_sha256=m['bda_sha256'],candidate_elf_sha256=m['elf_sha256'],candidate_role='final-c6-bounded-cache',pending_final_candidate='C6 physical profile/cut-only confirmation',installation_verified=None,performance_acceptance_passed=None)
d['verification_inheritance']=dict(resource_reader='Fresh C5 actual-MIPS resource100 checks inherited for unchanged resource source/interface; no C6 resource-MIPS rerun claimed.',fog_renderer='C5 host97-frame proof retains original6dcc core source identity. C6 baked_scene55df source is identical; shader arithmetic plus C6 guard host98-case masks are composed evidence, not a fresh C6 full97-frame run.',physical_tree_natural_regrow='C3 physical full180s regrow proof is historical. Current C6 cut-only physical acceptance remains pending.')
d['files']=sorted(rows.values(),key=lambda f:f['path']);d['archived_files']=len(rows);d['archived_raw_bytes']=sum(f['bytes']for f in rows.values());ip.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8');assert all(sha(ROOT/f['path'])==f['sha256']for f in rows.values());print(len(rows),d['archived_raw_bytes'],sha(ip))
