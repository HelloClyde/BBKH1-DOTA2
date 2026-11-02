"""Preserve pre-hardware docs/offline evidence as raw bytes under build only."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/perf3-hardware-previous'
NAMES=[
 'provenance/verification/v0.30/performance/round3/RAW-MANIFEST.json',
 'provenance/verification/v0.30/performance/round3/INDEX.json',
 'provenance/verification/v0.30/performance/round3/COMPARISON.json',
 'provenance/verification/v0.30/performance/round3/ARCHIVE-VERIFICATION.json',
 'provenance/verification/v0.30/performance/round3/FINALIZE-EVIDENCE.py',
 'provenance/verification/v0.30/performance/INDEX.json',
 'provenance/verification/v0.30/performance/round2/INDEX.json',
 'docs/verification/v0.30-h1.json','docs/PERFORMANCE-v0.30.md',
 'docs/PERFORMANCE-v0.30-round3.md','docs/IPU-v0.30.md',
]
sha=lambda data:hashlib.sha256(data).hexdigest()
manifest=OUT/'manifest.json'
if manifest.exists():
 previous=json.loads(manifest.read_bytes())
 for row in previous['files']:
  path=ROOT/row['snapshot'];assert path.stat().st_size==row['bytes'] and sha(path.read_bytes())==row['sha256']
 print(json.dumps(dict(existing_snapshot_verified=True,files=len(previous['files']),manifest_sha256=sha(manifest.read_bytes()))))
else:
 rows=[]
 for name in NAMES:
  source=ROOT/name;data=source.read_bytes();target=OUT/name
  target.parent.mkdir(parents=True,exist_ok=True)
  if target.exists():assert target.read_bytes()==data,('refuse replacing previous bytes',target)
  else:target.write_bytes(data)
  assert target.read_bytes()==data
  rows.append(dict(source=name,snapshot=target.relative_to(ROOT).as_posix(),bytes=len(data),sha256=sha(data),byte_identical_at_capture=True))
 manifest.write_text(json.dumps(dict(schema_version=1,scope=__doc__,docs_or_provenance_modified=False,files=rows),indent=2)+'\n')
 print(json.dumps(dict(preserved=True,files=len(rows),bytes=sum(x['bytes'] for x in rows),manifest_sha256=sha(manifest.read_bytes()))))
