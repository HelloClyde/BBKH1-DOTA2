"""Freeze read-only independent source review and examined proof identities."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3]
expected={
 'platform/baked_scene.c':'974c2a18bb04e861f5608b14c5deb0ea16daa00aa1d19bf080af022f370c8c14',
 'platform/baked_scene.h':'5aa1aac3f7cde2b4bdd32c3d95352fb188849f923ea6d350a3edd7c333d10a9f',
 'platform/sprite_rows.h':'3352abb5549b974d5b0928b5c52edf711731e8bac96d52e572fe465defb28ae4',
 'platform/compressed_cache.h':'f2f111a8ef5e52e9c724e22831b4bf815c842ee6aa7ba81b2442df44c65f5f8c',
 'platform/native.c':'504942924da354131d8207eb45bdcff34c7234af8ed5ac8649c2544679364270',
 'platform/resource_pack.c':'1f6055feaed69e98be1a0dbfb1ce70c59dde1e0104e7b93312d63edfa52e8aac',
 'platform/resource_pack.h':'68338a7cc10ffdab1a48a5a94dc3a621aef0d683d568a63c5914b101e0301459',
}
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
assert all(sha(ROOT/name)==value for name,value in expected.items())
proofs=[]
for name in [
 'build/v030/startup-io-study/reader-selected-tests/verification.json',
 'build/v030/sprite-pipeline/final-stress-fallback/verification.json',
 'build/v030/sprite-pipeline/final-parity/verification.json',
 'build/v030/sprite-pipeline/final-profile/verification.json',
]:
 path=ROOT/name;d=json.loads(path.read_bytes());assert d['passed'];proofs.append(dict(path=name,bytes=path.stat().st_size,sha256=sha(path),original_passed=d['passed'],method='Read-only inspection of agent-produced report and corresponding test/source code; no independent rerun claimed'))
report=dict(passed=True,reviewer='independent-local-fov-agent',source_review_only=True,USB_opened=False,production_files_modified=False,
 reviewed_source_sha256=expected,blocking_findings=[],examined_reports=proofs,
 conclusions=[
  'Optional read cache owns one extra allocation including all added descriptors/data. Late reserve copies validated base descriptors without changing their payload pointers; callback/busy contexts cannot migrate it.',
  'Read-cache allocation failure preserves the original 2/2 cache and does not alter error fields. clear_mount returns cache pointers/slot counts to base arrays before freeing mandatory workspace and clearing descriptors. RAW audio bypasses these caches.',
  'Row cache stores only immutable frame alpha extents, never payload-ring pointers. Frame validation bounds dimensions and indices. Reserve/eviction/close clears lookup ownership; optional allocation failure uses the original loops.',
  'Scaled horizontal bounds use ceil(first*scale/256) and ceil(end*scale/256), exactly preserving the original floor source-coordinate sampling. Only pixels whose original alpha is zero are omitted.',
  'Flat shadow height reuse requires every covering ground-grid corner to equal and each reconstructed world point to remain inside the checked footprint. Other points and slopes retain the original sampler and pit visibility checks.',
  'Shadow curve is optional heap; full init/close/open failure/fatal reserve failure release it, while ordinary cache resize retains it. A null curve uses identical per-channel integer rounding.',
  'Final native changes add optional reader reserve before required scene loads and row reserve after required audio/history allocations. No start_game prewarm, simulation, seed or frame logic was added. Frame clock is sampled again after startup work.',
  'Final compressed_cache.h is byte-identical to the frozen C7 version; the rejected hot dense-cache prototype is absent from selected production.',
 ],
 qualifications=[
  '353-frame final renderer proof compares original/final/fallback RGB565, depth, selection bounds and draw counters. The row allocation failure is injected; shadow scalar output is exercised after a test ownership operation frees the curve, not by injecting its initialization allocation failure.',
  'Host parity and MIPS instruction slots do not establish physical H1 frame rate. The root owns combined-build acceptance and hardware testing.',
 ])
out=Path(__file__).with_name('independent-review.json');out.write_text(json.dumps(report,indent=2)+'\n');print(sha(out))
