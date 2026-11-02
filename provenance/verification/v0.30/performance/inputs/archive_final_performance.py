"""Offline evidence curation only; no device, build, Git, or production writes."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'provenance/verification/v0.30/performance'
DOC = ROOT / 'docs/verification/v0.30-h1.json'
BASE = ROOT / 'build/v030/perf-baseline-aligned'
FINAL = ROOT / 'build/v030/perf-union-aligned'
FINAL_SHA = '33a650a25ef78ce16250cd3875f9be2ec6e7d831e6bd4d0ba775b5f1f83be857'
ELF_SHA = '06a527609df12e8ff6a7d8e5588039446b0dc04b31b4e4556a5b43df4d9ff47e'
DRIVER = 'b63ad9d1c2756b7371bc076f9a430dc07cd48ea067ecdd8844d35f7a5caaf950'

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def read(p):
    return json.loads(Path(p).read_text(encoding='utf-8'))

def write(p, value):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')

def relative(p):
    return Path(p).relative_to(ROOT).as_posix()

old_index = read(OUT / 'INDEX.json')
old_summary = read(DOC)
files = list(old_index['files'])
for row in files:
    assert sha(ROOT / row['path']) == row['sha256'], row['path']

def copy(source, target):
    source = ROOT / source
    target = OUT / target
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        assert sha(target) == sha(source), target
    else:
        shutil.copyfile(source, target)
    assert source.read_bytes() == target.read_bytes()
    row = dict(path=relative(target), source=relative(source), bytes=target.stat().st_size,
               sha256=sha(target), byte_identical=True)
    if not any(x['path'] == row['path'] for x in files):
        files.append(row)
    return dict(path=row['path'], sha256=row['sha256'])

copy('provenance/verification/v0.30/performance/INDEX.json', 'historical/INDEX-before-final-performance.json')
copy('docs/verification/v0.30-h1.json', 'historical/v0.30-h1-before-final-performance.json')
for source, target in [('perf-baseline-aligned', 'aligned/baseline'),
                       ('perf-union-aligned', 'aligned/final'),
                       ('perf-baseline-rebooted', 'historical/baseline-rebooted'),
                       ('perf-optimized-rebooted', 'historical/io-only-rebooted'),
                       ('perf-union-cpu', 'cpu-union')]:
    for p in sorted((ROOT / 'build/v030' / source).rglob('*')):
        if p.is_file():
            copy(relative(p), target + '/' + p.relative_to(ROOT / 'build/v030' / source).as_posix())

for source in ['after-full-reboot.json', 'retry-after-user.json', 'retry-system-check.json']:
    copy('build/v030/perf-inspect/' + source, 'storage/' + source)
for source in ['perf-union-deploy.json', 'perf-union-deploy.json.upload.json', 'perf-union-deploy-tool.json',
               'perf-baseline-aligned-deploy.json', 'perf-baseline-aligned-deploy.json.upload.json']:
    copy('build/v030/' + source, 'deployment/' + source)
for source, prefix in [('features-candidate2', 'baseline'), ('perf-candidate2', 'io-only'), ('perf-candidate3', 'final')]:
    copy('build/v030/' + source + '/DOTA2.build.json', 'build/' + prefix + '-DOTA2.build.json')
copy('build/v030/perf-candidate3/commands.json', 'build/final-commands.json')

for source, target, extras in [
    ('perf-union-vision-mips2', 'checks/vision-mips', ['layout-and-host-results.json', 'compile.txt']),
    ('perf-union-trees-mips2', 'checks/trees-mips', ['host-layout.json', 'host-compile.txt']),
    ('static-sight-study/host-run3', 'checks/host-union', ['compile.txt', 'output.txt'])]:
    for name in ['verification.json'] + extras:
        copy('build/v030/' + source + '/' + name, target + '/' + name)

inputs = [
    'build/v030/static-sight-study/sight-before.c', 'build/v030/static-sight-study/old-wrapper.c',
    'build/v030/static-sight-study/new-wrapper.c', 'build/v030/static-sight-study/run-host.py',
    'build/v030/perf-baseline-source/render.c', 'build/v030/perf-baseline-source/resource_pack.c',
    'tests/static_sight_union_test.c', 'tests/verify_vision_mips.py', 'tests/vision_mips_fixtures.c',
    'tests/verify_trees_mips.py', 'tests/trees_mips_fixtures.c', 'tests/mips_core_model.py',
    'tests/profile_actual_vision.py', 'tools/requirements-test.txt', 'provenance/vision/trees.json',
    'game/sight.c', 'platform/render.c', 'platform/resource_pack.c']
for name in inputs:
    copy(name, 'inputs/' + name)
copy('build/v030/archive_final_performance.py', 'inputs/archive_final_performance.py')

base = read(BASE / 'verification.json')
final = read(FINAL / 'verification.json')
meta = read(ROOT / 'build/v030/perf-candidate3/DOTA2.build.json')
assert meta['bda_sha256'] == FINAL_SHA and meta['elf_sha256'] == ELF_SHA
assert sha(ROOT / 'build/v030/perf-candidate3/DOTA2.bda') == FINAL_SHA
assert sha(ROOT / 'build/v030/perf-candidate3/DOTA2.elf') == ELF_SHA
assert final['bda_sha256'] == FINAL_SHA and final['elf_sha256'] == ELF_SHA
assert base['driver_sha256'] == final['driver_sha256'] == DRIVER
assert sha(BASE / 'driver.py') == sha(FINAL / 'driver.py') == DRIVER
assert base['menu_frame_before_enter'] == final['menu_frame_before_enter'] == 320
assert base['lineup_hero_ids'] == final['lineup_hero_ids'] == [0, 3, 6, 8, 4, 1, 2, 7, 5, 9]
assert len(base['profiles']) == len(final['profiles']) == 11
for report in (base, final):
    assert report['passed'] and report['physical_h1_tested'] and report['normal_inputs_only']
    assert report['game_state_ram_writes'] == 0
    for p in report['profiles']:
        assert 8 <= p['seconds'] < 8.05
        for edge in ('before', 'after'):
            assert p[edge]['resources']['errors'] == p[edge]['resources']['last_error'] == 0
            assert p[edge]['audio'][2] == p[edge]['audio'][6] == 0
            assert p[edge]['audio'][23:26] == [0, 0, 0]
    exit = report['after_exit']
    assert all(exit['resources'][n] == 0 for n in ('mounted', 'open_files', 'heap_bytes', 'last_error', 'errors'))
    assert exit['cache'][9] == 0 and exit['audio'][18] == 0
    assert exit['audio'][16] == exit['audio'][17] == 9
    assert report['desktop_after']['menu_idle'] == 1 and report['desktop_after']['return_code'] == 0

rows = []
for a, b in zip(base['profiles'], final['profiles']):
    assert a['name'] == b['name']
    rows.append(dict(name=a['name'], baseline={k:a[k] for k in ('fps','frames','seconds','game_ticks','resource_delta')},
                     final={k:b[k] for k in ('fps','frames','seconds','game_ticks','resource_delta')},
                     observed_fps_change_percent=(b['fps']/a['fps']-1)*100))
positions = {}
with (ROOT/'build/v030/perf-candidate3/DOTA2.elf').open('rb') as f:
    dwarf = ELFFile(f).get_dwarf_info()
    members = None
    for cu in dwarf.iter_CUs():
        for die in cu.iter_DIEs():
            name = die.attributes.get('DW_AT_name')
            if die.tag == 'DW_TAG_typedef' and name and name.value == b'DotaUnit':
                struct_die = die.get_DIE_from_attribute('DW_AT_type')
                assert struct_die.attributes['DW_AT_byte_size'].value == 240
                members = {x.attributes['DW_AT_name'].value.decode(): x.attributes['DW_AT_data_member_location'].value
                           for x in struct_die.iter_children() if x.tag == 'DW_TAG_member'}
                break
        if members is not None:
            break
    assert members and {n:members[n] for n in ('x','y','kills','deaths')} == dict(x=24,y=28,kills=208,deaths=212)
for folder in (BASE, FINAL):
    positions[folder.name] = {}
    for p in sorted(folder.glob('*-game.bin')):
        values = {n:struct.unpack_from('<i', p.read_bytes(), members[n])[0] for n in ('x','y','kills','deaths')}
        assert [values['x'],values['y']] == [373,3723]
        assert values['kills'] == 0 and values['deaths'] == (1 if p.name.startswith('post-horn') else 0)
        positions[folder.name][p.name] = values
write(OUT/'SNAPSHOT-FIELDS.json', dict(passed=True, readonly_analysis=True, elf_sha256=ELF_SHA,
    offsets_derived_from_elf_DWARF={n:members[n] for n in ('x','y','kills','deaths')}, unit_bytes=240,
    snapshots=positions, scope='Pause snapshots only. Both runs have one death before the last snapshot; these samples do not prove that the hero never moved.'))

cpu = read(ROOT / 'build/v030/perf-union-cpu/verification.json')
assert cpu['passed'] and cpu['actual_mips32'] and not cpu['physical_h1_tested']
for pair in cpu['results']:
    for kind in ('original', 'union'):
        assert sha(ROOT / pair[kind + '_report']) == pair[kind + '_report_sha256']
    assert pair['all_game_fog_unit_visibility_byte_identical']
    left = (ROOT / pair['original_report']).parent
    right = (ROOT / pair['union_report']).parent
    for name, passed in pair['compared_outputs'].items():
        assert passed and (left / name).read_bytes() == (right / name).read_bytes()
for name in ('vision', 'trees'):
    report = read(ROOT / ('build/v030/perf-union-' + name + '-mips2/verification.json'))
    assert report['passed'] and report['actual_mips32'] and report['source_current']
    assert report['bda_sha256'] == FINAL_SHA and report['elf_sha256'] == ELF_SHA
    assert report['metadata_sha256'] == sha(ROOT / 'build/v030/perf-candidate3/DOTA2.build.json')
    assert report['outcome_state_writes_after_setup'] == 0

comparison = dict(schema_version=1, version='0.30', performance_acceptance_passed=True,
    baseline=copy(relative(BASE/'verification.json'), 'aligned/baseline/verification.json'),
    final=copy(relative(FINAL/'verification.json'), 'aligned/final/verification.json'),
    driver_sha256=DRIVER, menu_frame=320, lineup_hero_ids=base['lineup_hero_ids'], profiles=rows,
    hero_snapshot_positions=positions, audio_errors_underruns_restarts=0, resource_errors=0,
    normal_exit_and_cleanup_passed=True, game_state_ram_writes=0,
    limitations=['One normal-input run per binary, not repeated statistical trials.',
      'Equal driver/menu frame/lineup; AI and elapsed ticks are not RAM-forced identical.',
      'mid-approach is only a fixed-coordinate click, without a continuous position trace; it is not an independent walking benchmark.',
      'Pause snapshots show fountain coordinates(373,3723), and the last snapshot shows one death in both runs; movement between snapshots is not excluded.',
      'Post-horn hero window is reported as the sampled fountain camera view.',
      'Mid-camera is a camera view, not hero walking or a team-fight benchmark.',
      'Natural first-wave windows only; no late-game or all-scenes >=15FPS claim.',
      'Resource counters bracket wider sample RPCs than exact FPS counter-read timing.',
      'Scene render counters reset per frame and are not cumulative I/O deltas.',
      'RAW sound streaming is included in resource physical-read counters.'],
    cpu_slot_comparison=dict(path=relative(OUT/'cpu-union/verification.json'),
      sha256=sha(OUT/'cpu-union/verification.json'), physical_fps=False,
      warm_ticks=2, measured_ticks=2, byte_identical_game_fog_unit_visibility=True,
      fountain_reduction_percent=cpu['results'][0]['slot_reduction_percent'],
      post_horn_reduction_percent=cpu['results'][1]['slot_reduction_percent']))
write(OUT / 'ALIGNED-COMPARISON.json', comparison)

readme = '''# v0.30 performance evidence archive

Raw copies preserve original bytes, paths and claims. INDEX.json records every
archive/source pair and SHA-256; historical failures remain failures.

- aligned/baseline and aligned/final contain the two normal-input H1 runs,
  execution-time driver snapshots, unmodified480x272 PNGs and Game snapshots.
  SNAPSHOT-FIELDS.json derives X/Y/K/D offsets from the final ELF DWARF. The
  last pause snapshot has one death in both runs. Fountain coordinates at
  snapshot times do not prove that the hero stayed there between captures.
- historical/baseline-rebooted and io-only-rebooted have different menu frame /
  lineup; they are independent observations, not a matched FPS comparison.
- deployment contains device CRC-verified final installation receipts;
  build contains exact metadata, including historical source drift.
- cpu-union is an offline complete-BDA MIPS instruction-slot comparison:
  warm2/ticks2, same captured Game per pair, full Game/fog/visibility byte equality.
  Its slots are not physical FPS or a full match benchmark.
- checks contains final-BDA vision/tree MIPS reports and host union regression.
  MIPS fixtures are regenerated by archived test sources; host/SDK/toolchain and
  asset prerequisites remain documented in docs/TESTING.md.
- inputs preserves the old sight implementation, wrappers, runners, fixture
  sources and the old render/resource source snapshots. Current production
  source dependencies remain bound by original build/report SHA maps in Git;
  the archive does not pretend that historical sources are all current.

To reproduce outside this archive, restore each copied input to its recorded
source path in an isolated checkout; never overwrite an active release. Use the
recorded baseline/final build metadata and retained build artifacts. The CPU
profile expects original BDA/ELF paths build/v030/features-candidate2 and
build/v030/perf-candidate3, original aligned snapshots and legacy VISION/TREE
from build/v030/legacy. It needs the pinned SDK/toolchain, pyelftools and Unicorn.
The H1R is unchanged and stays in assets/runtime, not duplicated here.
No test is rerun by this curation step; no USB, RAM, Git or release-pin action.

Acceptance scope is loading, short frame windows, pause/resume, camera, audio
and normal exit. Full v0.30 feature acceptance and release/GitHub publication
are pending. No all-scenes15FPS, walking, late-game or team-fight claim.
'''
(OUT/'README.md').write_text(readme, encoding='utf-8', newline='\n')
derived = []
for name in ('ALIGNED-COMPARISON.json', 'README.md', 'SNAPSHOT-FIELDS.json'):
    derived.append(dict(path=relative(OUT/name), sha256=sha(OUT/name)))
write(OUT / 'ARCHIVE-VERIFICATION.json', dict(passed=True, offline_curation_only=True,
    USB_opened=False, production_files_modified=False, originals_modified=False,
    copied_files=len(files), copied_bytes=sum((ROOT/x['path']).stat().st_size for x in files),
    all_copies_byte_identical=True, original_sd_failure_receipts_preserved=True,
    aligned_identity_checks_passed=True, cpu_compared_binary_outputs_equal=True,
    full_feature_acceptance_complete=False, release_pin_created=False, github_updated=False,
    copied_files_sha256={x['path']:x['sha256'] for x in files}))
derived.append(dict(path=relative(OUT/'ARCHIVE-VERIFICATION.json'), sha256=sha(OUT/'ARCHIVE-VERIFICATION.json')))
index = dict(schema_version=2, version='0.30',
    baseline_installed_bda_sha256=base['bda_sha256'], optimization_candidate_bda_sha256=FINAL_SHA,
    optimization_installed=True, performance_acceptance_passed=True,
    full_feature_acceptance_complete=False, release_pin_created=False, github_updated=False,
    hardware_state='Full reboot restored B filesystem recognition; exact final candidate was CRC-verified installed, measured using normal inputs, and exited to healthy idle with owned resources released.',
    physical_fps_samples=rows, comparison=dict(path=relative(OUT/'ALIGNED-COMPARISON.json'),sha256=sha(OUT/'ALIGNED-COMPARISON.json')),
    historical_observations=['baseline-startup-failed.json retains SD-registration startup failure, not0FPS.',
      'historical/baseline-rebooted versus historical/io-only-rebooted have different lineups and are not causal FPS evidence.'],
    files=files, derived_files=derived)
write(OUT / 'INDEX.json', index)

summary = dict(old_summary)
for key in ('startup_error', 'storage_drive_count_readonly', 'storage_diagnosis', 'optimization_candidate'):
    summary.pop(key, None)
summary.update(bda_sha256=FINAL_SHA, bda_bytes=meta['bda_bytes'], elf_sha256=ELF_SHA,
    binary=meta['binary'], physical_h1_game_tested=True, functional_h1_passed=None,
    performance_acceptance_passed=True, full_feature_acceptance_complete=False,
    hardware_deployment_verified=True, game_state_ram_writes_to_device=0,
    runtime_acceptance='Passed normal-input loading,11 short FPS windows, pause/resume, camera, audio and normal exit/cleanup. Full feature acceptance is not claimed.',
    installation_report=dict(path=relative(OUT/'deployment/perf-union-deploy.json'),sha256=sha(OUT/'deployment/perf-union-deploy.json')),
    performance_evidence_index=dict(path=relative(OUT/'INDEX.json'),sha256=sha(OUT/'INDEX.json')),
    physical_fps=dict(scope='One aligned normal-input run per binary,11 windows about8s each, natural first-wave only; no walking/team-fight/all-scenes15FPS claim.',
      comparison=dict(path=relative(OUT/'ALIGNED-COMPARISON.json'),sha256=sha(OUT/'ALIGNED-COMPARISON.json')),
      final_active_window_fps_range=[min(x['fps'] for x in final['profiles'] if x['game_ticks']>0),max(x['fps'] for x in final['profiles'] if x['game_ticks']>0)],
      final_post_horn_active_fps=[x['fps'] for x in final['profiles'] if x['name'] in ('post-horn-mid-play','post-horn-hero-play')]),
    historical_startup_failure=dict(path=relative(OUT/'baseline-startup-failed.json'),sha256=sha(OUT/'baseline-startup-failed.json'),asset_error=17,debug_error=37,B_volume_errno=9,drive_count_at_failure=1,resolved_by_full_reboot=True),
    full_reboot_receipt=dict(path=relative(OUT/'storage/after-full-reboot.json'),sha256=sha(OUT/'storage/after-full-reboot.json')),
    release_pin_created=False, github_updated=False)
summary['all_current_offline_checks_passed'] = None
summary['offline_verification_scope'] = 'Original broad feature checks are historical/inherited. Fresh final-BDA vision/tree MIPS, host union and exact CPU-output comparison are archived; no claim of rerunning every feature suite.'
summary['historical_feature_evidence_index'] = summary.pop('evidence_index')
write(DOC, summary)
print(json.dumps(dict(index=relative(OUT/'INDEX.json'),index_sha256=sha(OUT/'INDEX.json'),
    archive_receipt_sha256=sha(OUT/'ARCHIVE-VERIFICATION.json'),comparison_sha256=sha(OUT/'ALIGNED-COMPARISON.json'),
    summary_sha256=sha(DOC),copied_files=len(files),archive_bytes=sum((ROOT/x['path']).stat().st_size for x in files)),indent=2))
