"""Append bounded-cache regression and preserve superseded C5 physical data."""
from pathlib import Path
import hashlib,json
ROOT=Path.cwd();OUT=ROOT/'provenance/verification/v0.30/performance/round2';p=OUT/'INDEX.json';d=json.loads(p.read_bytes());files={f['path']:f for f in d['files']};sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def copy(src,dst=None,category='bounded-cache-guard'):
 src=ROOT/src;target=OUT/(dst or src.relative_to(ROOT));target.parent.mkdir(parents=True,exist_ok=True);data=src.read_bytes()
 if target.exists():assert target.read_bytes()==data,target
 else:target.write_bytes(data)
 row=dict(path=target.relative_to(ROOT).as_posix(),source=src.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),byte_identical_at_capture=True,category=category)
 files[row['path']]=row
def tree(folder,ext,category):
 for f in sorted((ROOT/folder).rglob('*')):
  if f.is_file()and f.suffix.lower()in ext:copy(f.relative_to(ROOT),category=category)
for f in files.values():
 if f['category']=='selected-production-source':f['category']='intermediate-c5-production-source';f['snapshot_before_bounded_cache_guard']=True
for src in ['game/sight.c','game/sight.h']:copy(src,'selected-source-guard/'+src)
copy('build/v030/game-cost-study/run-host-bounded.py',category='bounded-cache-regression-source')
tree('build/v030/game-cost-study/host-bounded2',{'.c','.json','.txt'},'bounded-cache-regression')
for folder in ['perf2-final-aligned','perf2-final-aligned2']:
 report=json.loads((ROOT/'build/v030'/folder/'verification.json').read_bytes())
 assert report['bda_sha256']=='b7afd86515b0206d8a4ce53217fc761cfafdbba2a4f8f9337a94db55c5c54fcb'
 category='excluded-lineup-mismatch'if not report.get('passed')else'intermediate-c5-aligned-physical'
 tree('build/v030/'+folder,{'.json','.py','.png','.bin'},category)
copy('build/v030/game-cost-study/append-guard-archive.py','append-guard-script.py','archive-script')
d['intermediate_candidate5']=dict(bda_sha256=d['candidate_bda_sha256'],elf_sha256=d['candidate_elf_sha256'],installation_verified=True,aligned_physical_passed=True,report='build/v030/perf2-final-aligned2/verification.json',superseded_reason='Add bounded obstruction-cache fallback to avoid repeated full-map fills when more than eight source classes are active. C5 FPS is retained as intermediate evidence, not final C6 acceptance.')
d['candidate_role']='superseded-intermediate-c5';d['pending_final_candidate']='C6 bounded-cache fallback';d['installation_verified']=None;d['performance_acceptance_passed']=None
d['files']=sorted(files.values(),key=lambda f:f['path']);d['archived_files']=len(files);d['archived_raw_bytes']=sum(f['bytes']for f in files.values());p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8');assert all(sha(ROOT/f['path'])==f['sha256']for f in files.values());print(len(files),d['archived_raw_bytes'],sha(p))
