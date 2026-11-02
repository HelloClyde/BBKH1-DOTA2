"""Derive limited CPU/IO comparisons without converting them to H1 FPS."""
from pathlib import Path
import hashlib,json

ROOT=Path(__file__).resolve().parents[3]
def read(name):
    path=ROOT/name
    return json.loads(path.read_bytes())
def sha(name):return hashlib.sha256((ROOT/name).read_bytes()).hexdigest()

pairs=[]
for label,old,new in [
    ('fountain','build/v030/sprite-pipeline/c7-profile/verification.json','build/v030/perf3-render-fountain/verification.json'),
    ('post-horn','build/v030/perf3-render-posthorn-C7/verification.json','build/v030/perf3-render-posthorn/verification.json')]:
    left,right=read(old),read(new)
    assert left['passed'] and right['passed']
    assert left['device_snapshot_sha256']==right['device_snapshot_sha256']
    a,b=left['measured_frames'],right['measured_frames']
    assert len(a)==len(b)==2
    assert [x['framebuffer_sha256'] for x in a]==[x['framebuffer_sha256'] for x in b]
    assert [x['slots']['normal_game_ticks'] for x in a]==[x['slots']['normal_game_ticks'] for x in b]
    before=sum(x['slots']['full_dota_render'] for x in a)
    after=sum(x['slots']['full_dota_render'] for x in b)
    assert left['after_close']==right['after_close']==dict(allocations=0,handles=0)
    pairs.append(dict(label=label,baseline_report=old,baseline_report_sha256=sha(old),
        candidate_report=new,candidate_report_sha256=sha(new),
        baseline_render_instruction_slots=before,candidate_render_instruction_slots=after,
        render_instruction_reduction_percent=100*(before-after)/before,
        measured_frames=2,warm_frames=4,normal_game_instruction_counts_equal=True,
        framebuffer_hashes_equal=True,cleanup_allocations=0,cleanup_handles=0))

meta=read('build/v030/perf-candidate8/DOTA2.build.json')
assert sha('build/v030/perf-candidate8/DOTA2.bda')==meta['bda_sha256']
assert sha('build/v030/perf-candidate8/DOTA2.elf')==meta['elf_sha256']
result=dict(passed=True,scope='Offline MIPS render work from two physical initial Game snapshots. No USB, display, audio scheduling, CPU cache or physical FPS measurement.',
    bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],binary=meta['binary'],
    physical_candidate_tested=False,physical_candidate_installed=False,
    physical_fps_comparison_available=False,comparisons=pairs,
    retained=['optional reader index8/decoded4 cache','alpha row extents128','flat-ground shadow height reuse','local tree FOV invalidation'],
    rejected=['2MiB decoded sprite hot pool: small MIPS regression at warm4 and warm24'],
    deferred=['targeted sprite prewarm: optimistic IO replay benefit, no native startup-wait or net physical FPS proof'],
    IPU='JZ4740 YUV scaling/CSC has no direct RGB565+alpha+depth sprite compositor interface; no MMIO experiment attempted',
    hardware_blocker='Current native resource manager could not recognize B card; B:\\DOTA2\\DOTA2.H1R stat failed. Baseline preflight stopped before game launch because the resource manager was foreground. No zero-FPS inference.',
    installed_binary_remains='C7 d8d75e2c459735fad358c53c58e2d461c52fb8e348e2846b113f8fcded734d53')
out=ROOT/'build/v030/perf3-comparison'
out.mkdir(parents=True,exist_ok=True)
(out/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps(dict(passed=True,comparisons=pairs),ensure_ascii=False))
