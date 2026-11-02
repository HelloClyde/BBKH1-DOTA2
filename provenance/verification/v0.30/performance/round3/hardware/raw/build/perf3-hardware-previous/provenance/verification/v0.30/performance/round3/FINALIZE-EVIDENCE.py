"""Verify archived raw bytes, then link offline C8 without changing C7 acceptance."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3]
BASE='provenance/verification/v0.30/performance/round3'
def read(name):return json.loads((ROOT/name).read_bytes())
def sha(name):return hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
def write(name,value):
    path=ROOT/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def ref(name):return dict(path=name,sha256=sha(name))

script_target=ROOT/BASE/'FINALIZE-EVIDENCE.py'
script_target.write_bytes(Path(__file__).read_bytes())
manifest=read(BASE+'/RAW-MANIFEST.json')
for row in manifest['files']:
    assert sha(row['path'])==row['sha256']
    assert (ROOT/row['path']).stat().st_size==row['bytes']
assert sum(x['bytes'] for x in manifest['files'])==manifest['selected_raw_bytes']
selected={x['source']:x for x in manifest['files']}
def raw(name):
    row=selected[name]
    return dict(path=row['path'],sha256=row['sha256'])

reports={
 'resources':'build/v030/perf3-resource-mips/verification.json',
 'vision':'build/v030/perf3-vision-mips-executed/verification.json',
 'trees':'build/v030/perf3-trees-mips/verification.json',
 'sight_bitplanes':'build/v030/perf3-sight-bits-mips/verification.json',
 'local_tree_FOV':'build/v030/perf3-local-fov-final/verification.json',
 'fountain_render':'build/v030/perf3-render-fountain/verification.json',
 'posthorn_render':'build/v030/perf3-render-posthorn/verification.json',
}
for name in reports.values():assert read(name)['passed'],name
comparison=read('build/v030/perf3-comparison/verification.json')
meta=read('build/v030/perf-candidate8/DOTA2.build.json')
assert comparison['bda_sha256']==meta['bda_sha256']
for name,digest in meta['source_sha256'].items():
    if (ROOT/name).is_file():assert sha(name)==digest,('production drift',name)
write(BASE+'/COMPARISON.json',comparison)
verification=dict(passed=True,verified_files=len(manifest['files']),
    verified_raw_bytes=manifest['selected_raw_bytes'],all_sha256_and_sizes_equal=True,
    game_or_measurement_fields_rewritten=False,raw_manifest=ref(BASE+'/RAW-MANIFEST.json'))
write(BASE+'/ARCHIVE-VERIFICATION.json',verification)
index=dict(schema_version=1,version='0.30',round=3,
    scope='Offline candidate only. Preserves rejected experiments and current physical SD-card blocker. No new H1 FPS or deployment claim.',
    candidate_bda_sha256=meta['bda_sha256'],candidate_elf_sha256=meta['elf_sha256'],
    candidate_bytes=meta['bda_bytes'],binary=meta['binary'],
    candidate_installed=False,physical_candidate_tested=False,performance_acceptance_passed=None,
    full_feature_acceptance_complete=False,release_pin_created=False,github_updated=False,
    raw_manifest=ref(BASE+'/RAW-MANIFEST.json'),archive_verification=ref(BASE+'/ARCHIVE-VERIFICATION.json'),
    derived_evidence_driver=ref(BASE+'/FINALIZE-EVIDENCE.py'),
    offline_comparison=ref(BASE+'/COMPARISON.json'),
    final_build=raw('build/v030/perf-candidate8/DOTA2.build.json'),
    final_mips_checks={k:raw(v) for k,v in reports.items()},
    sprite_study=raw('build/v030/sprite-pipeline/study-summary.json'),
    independent_source_review=raw('build/v030/perf3-archive/independent-review.json'),
    reader_replay=raw('build/v030/startup-io-study/selected-replay/verification.json'),
    reader_all_streams=raw('build/v030/startup-io-study/reader-selected-tests/verification.json'),
    targeted_prewarm=raw('build/v030/startup-io-study/prewarm-trial2/verification.json'),
    IPU_assessment=raw('docs/IPU-v0.30.md'),
    hardware_observations=dict(
        initial=raw('build/v030/perf3-device/initial.json'),
        resource_manager_error=raw('build/v030/perf3-device/initial-screen.png'),
        native_desktop_and_B_stat=raw('build/v030/perf3-device/ready.json'),
        baseline_preflight=raw('build/v030/perf3-baseline7/verification.json'),
        note='Baseline preflight failed because native resource manager was foreground. B card error separately observed via screenshot and stat. No game launch, no zero-FPS inference. Await user physical SD/reboot step.'),
    installed_binary_unchanged='C7 d8d75e2c459735fad358c53c58e2d461c52fb8e348e2846b113f8fcded734d53',
    current_physical_acceptance=ref('provenance/verification/v0.30/performance/round2/INDEX.json'))
write(BASE+'/INDEX.json',index)

candidate=dict(version='0.30',candidate='C8',bda_sha256=meta['bda_sha256'],
    elf_sha256=meta['elf_sha256'],bda_bytes=meta['bda_bytes'],binary=meta['binary'],
    standard_build_verified=True,listed_offline_checks_passed=True,
    physical_h1_game_tested=False,hardware_deployment_verified=False,
    performance_acceptance_passed=None,full_feature_acceptance_complete=False,
    resource_bundle_unchanged=True,game_state_ram_writes_to_device=0,
    hardware_blocker=index['hardware_observations']['note'],
    evidence=ref(BASE+'/INDEX.json'),performance_document='docs/PERFORMANCE-v0.30-round3.md')
write('docs/verification/v0.30-candidate8.json',candidate)
h1=read('docs/verification/v0.30-h1.json')
assert h1['bda_sha256']=='d8d75e2c459735fad358c53c58e2d461c52fb8e348e2846b113f8fcded734d53'
h1['pending_offline_candidate']=ref('docs/verification/v0.30-candidate8.json')
h1['latest_observed_storage_issue']=dict(resource='B:\\DOTA2\\DOTA2.H1R',
    native_error='Cannot recognize card',stat_command_status=4,
    evidence=raw('build/v030/perf3-device/ready.json'),
    previous_C7_performance_acceptance_remains_historical=True)
write('docs/verification/v0.30-h1.json',h1)
main=read('provenance/verification/v0.30/performance/INDEX.json')
main.update(latest_round=3,latest_candidate_bda_sha256=meta['bda_sha256'],
    latest_evidence_index=ref(BASE+'/INDEX.json'),latest_candidate_installed=False,
    latest_candidate_physical_performance_accepted=False,
    latest_physical_acceptance_round=2,
    scope='Round1 D569/C3 acceptance fields and raw receipts retained below. Round2 is the latest installed C7 physical acceptance. Round3 is an offline C8 candidate awaiting B-card recovery, not a new physical FPS result.')
write('provenance/verification/v0.30/performance/INDEX.json',main)
print(json.dumps(dict(passed=True,verified_files=len(manifest['files']),
    verified_raw_bytes=manifest['selected_raw_bytes'],candidate_installed=False,
    candidate=meta['bda_sha256'],round3_index=ref(BASE+'/INDEX.json'))))
