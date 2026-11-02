"""Append separate restored-SD hardware evidence without editing old archives/docs.

Default only plans. --execute copies selected original bytes beneath round3/
hardware and writes its own RAW-MANIFEST/INDEX. Existing bytes are immutable;
the earlier offline round3 INDEX and historical C7 acceptance stay untouched.
Only completed actual-binary reports and CRC-verified deployment can establish
the narrow installed/tested fields. Performance acceptance is a root decision.
"""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[3]
PREVIOUS=ROOT/'build/perf3-hardware-previous'
ROUND3=ROOT/'provenance/verification/v0.30/performance/round3'
COMPILED={'.bda','.elf','.o','.obj','.exe','.a','.lib','.dll','.so','.pdb','.pyc'}
ASSETS={'.h1r','.h1p','.h1t','.h1v','.h1c','.h1a','.raw','.ppm','.pgm','.wav','.mp3'}
TEXT={'.json','.py','.c','.h','.inc','.txt','.md','.csv','.log'}

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_bytes())
def rel(path):return Path(path).relative_to(ROOT).as_posix()
def checked(path):
 path=Path(path);path=(path if path.is_absolute() else ROOT/path).resolve();assert path.is_relative_to(ROOT),path;return path
def snapshot_manifest():
 m=read(PREVIOUS/'manifest.json')
 for row in m['files']:
  path=ROOT/row['snapshot'];assert path.stat().st_size==row['bytes'] and sha(path)==row['sha256'],path
 return m
def classify(path):
 ext=path.suffix.lower();size=path.stat().st_size
 if ext in COMPILED:return False,'compiled-binary-object-or-library'
 if ext in ASSETS:return False,'resource-package-audio-or-canonical-render-output'
 if ext=='.map':return False,'regenerable-link-map'
 if ext=='.bin':
  if size==148 and 'view' in path.name.lower():return True,'exact-DotaView148-snapshot'
  return (True,'small-Game-or-fog-snapshot') if size<=256*1024 and any(x in path.name.lower() for x in ('game','fog')) else (False,'unapproved-binary-output')
 if ext=='.png':return (True,'raw-device-screenshot') if size<=1024*1024 else (False,'oversize-raster-output')
 if ext in TEXT:return (True,'raw-report-driver-or-metadata') if size<=8*1024*1024 else (False,'oversize-text-output')
 return False,'unselected-file-type'
def immutable_write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if path.exists():assert path.read_bytes()==data,('refuse replacing existing evidence',path)
 else:path.write_bytes(data)
def json_bytes(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf8')

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--baseline',type=Path,required=True,help='Completed restored-SD C7 normal-input profile folder')
 p.add_argument('--candidate',type=Path,required=True,help='Completed normal-input candidate profile folder')
 p.add_argument('--candidate-build',type=Path,required=True)
 p.add_argument('--baseline-build',type=Path,default=Path('build/v030/perf-candidate7'))
 p.add_argument('--comparison',type=Path,required=True,help='Root/startup_io comparison JSON, copied without rewriting')
 p.add_argument('--deployment',type=Path,required=True,help='Primary completed deploy JSON containing device CRC verified upload')
 p.add_argument('--tree-report',type=Path,help='Completed actual candidate cut-only report; functional FPS is never merged into profile comparison')
 p.add_argument('--final-device-report',type=Path,help='Completed root read-only desktop/stat/cleanup probe')
 p.add_argument('--include-root',action='append',default=[],help='Additional restored device/tree check folder under build/')
 p.add_argument('--include-file',action='append',default=[],help='Additional raw receipt/report under build/')
 p.add_argument('--excluded-attempt',nargs=2,action='append',default=[],metavar=('FOLDER','REASON'),help='Preserve failed/rejected attempt with explicit exclusion reason; never rewrite its report')
 p.add_argument('--destination',type=Path,default=Path('provenance/verification/v0.30/performance/round3/hardware'))
 p.add_argument('--performance-acceptance',choices=['pending','passed','failed'],default='pending',help='Explicit root decision; default preserves unknown/null')
 p.add_argument('--plan-output',type=Path,help='Derived plan under build only')
 p.add_argument('--execute',action='store_true')
 a=p.parse_args();old=snapshot_manifest();out=checked(a.destination);assert out.is_relative_to(ROUND3/'hardware'),out
 base,candidate,bb,cb,comparison,deployment=map(checked,[a.baseline,a.candidate,a.baseline_build,a.candidate_build,a.comparison,a.deployment])
 for path in [base,candidate,bb,cb,comparison,deployment,*map(checked,a.include_root),*map(checked,a.include_file),*[checked(x[0]) for x in a.excluded_attempt]]:assert path.is_relative_to(ROOT/'build'),path
 left,right=read(base/'verification.json'),read(candidate/'verification.json');lm,rm=read(bb/'DOTA2.build.json'),read(cb/'DOTA2.build.json')
 for folder,meta,report in [(bb,lm,left),(cb,rm,right)]:
  assert sha(folder/'DOTA2.bda')==meta['bda_sha256'] and sha(folder/'DOTA2.elf')==meta['elf_sha256']
  assert report['bda_sha256']==meta['bda_sha256'] and report['elf_sha256']==meta['elf_sha256']
  assert report['passed'] and report['physical_h1_tested'] and report['normal_inputs_only'] and report['game_state_ram_writes']==0 and not report.get('fixture_only')
 assert lm['bda_sha256']=='d8d75e2c459735fad358c53c58e2d461c52fb8e348e2846b113f8fcded734d53'
 assert rm['bda_sha256']==read(PREVIOUS/'provenance/verification/v0.30/performance/round3/INDEX.json')['candidate_bda_sha256']
 for folder,report in [(base,left),(candidate,right)]:assert sha(folder/'driver.py')==report['driver_sha256']
 dep=read(deployment);upload=dep['upload'];status=upload['status']
 assert dep['operation']=='deploy' and dep['completed'] and upload['completed'] and upload['phase']=='done' and upload['device_crc_verified']
 assert upload['sha256']==rm['bda_sha256'] and upload['size']==(cb/'DOTA2.bda').stat().st_size and upload['received']==upload['size']
 assert status['expected_crc']==status['actual_crc']==upload['crc32'] and status['error']==status['fs_errno']==0
 alignment=dict(driver=left['driver_sha256']==right['driver_sha256'],lineup=left.get('lineup_hero_ids')==right.get('lineup_hero_ids'),post_horn_option=left.get('post_horn_requested')==right.get('post_horn_requested'),profile_names=[x['name'] for x in left['profiles']]==[x['name'] for x in right['profiles']])
 comp=read(comparison)
 tree_path=checked(a.tree_report) if a.tree_report else None
 if tree_path:
  td=read(tree_path);assert td['passed'] and td.get('physical_H1_tested',td.get('physical_h1_tested')) and td['normal_inputs_only'] and td['game_state_ram_writes']==0
  assert td['bda_sha256']==rm['bda_sha256'] and td['elf_sha256']==rm['elf_sha256'] and sha(tree_path.with_name('driver-executed.py'))==td['driver_sha256']
 final_path=checked(a.final_device_report) if a.final_device_report else None
 if final_path:
  fd=read(final_path);assert fd['passed'] and fd['status']['state']==1 and fd['status']['last_error']==fd['status']['usb_fault']==fd['status']['keys_low']==fd['status']['keys_high']==0
  assert fd['desktop']['menu_idle']==1 and fd['desktop']['error']==fd['desktop']['return_code']==0
  assert fd['program']['size']==(cb/'DOTA2.bda').stat().st_size and fd['resource']['size']==112861448
  assert fd['resource_status'][:6]==[0]*6 and fd['resource_status'][20]==0 and fd['h1_dota_cache'][9]==0 and fd['h1_dota_cache'][11]==1 and fd['h1_dota_audio_debug'][18]==0
  assert all(fd[k]==[0] for k in ['sprite_row_slots','sprite_row_heap_bytes','shadow_curve','read_cache'])
 for label,folder,build_folder,meta,report in [('baseline',base,bb,lm,left),('candidate',candidate,cb,rm,right)]:
  identity=comp['source_reports'][label]
  assert checked(identity['report_path'])==folder/'verification.json' and identity['report_sha256']==sha(folder/'verification.json')
  assert checked(identity['driver_path'])==folder/'driver.py' and identity['driver_sha256']==sha(folder/'driver.py')==report['driver_sha256']
  assert checked(identity['build_path'])==build_folder and identity['metadata_sha256']==sha(build_folder/'DOTA2.build.json')
  for key in ['bda_sha256','elf_sha256']:assert comp[label+'_'+key]==identity[key]==report[key]==meta[key]
  assert identity['lineup_hero_ids']==report['lineup_hero_ids'] and identity['menu_frame_before_enter']==report['menu_frame_before_enter']
 assert [x['name'] for x in comp['windows']]==[x['name'] for x in left['profiles']]==[x['name'] for x in right['profiles']]
 for window,lw,rw in zip(comp['windows'],left['profiles'],right['profiles']):
  for kind,original in [('baseline',lw),('candidate',rw)]:
   for key in ['fps','frames','seconds','game_ticks','resource_delta']:assert window[kind][key]==original[key]
 assert comp['comparison_script_sha256']==sha(Path(__file__).with_name('compare_profiles.py'))
 if a.performance_acceptance=='passed':assert comp['passed'] and comp['physical_h1_tested'] and not comp['synthetic_fixture_inputs'] and all(alignment.values()),('comparison/alignment cannot establish accepted performance',alignment)
 # Guard only against this script modifying existing summaries; later root
 # documentation edits may legitimately differ from the preserved snapshot.
 protected=[ROOT/row['source'] for row in old['files']];before={rel(x):sha(x) for x in protected}
 selected={};excluded={}
 def add(path,category):
  path=checked(path);assert path.is_file(),path;ok,reason=classify(path);row=dict(source=rel(path),bytes=path.stat().st_size,category=category,selection_reason=reason)
  if ok:row.update(path=rel(out/'raw'/rel(path)),sha256=sha(path),byte_identical_at_capture=True);selected[row['source']]=row
  else:excluded[row['source']]=dict(**row,exclusion_reason=reason)
 for folder,category in [(base,'restored-SD-C7-baseline'),(candidate,'actual-candidate-H1-profile'),*[(checked(x),'additional-raw-observation-or-supporting-check') for x in a.include_root]]:
  for path in sorted(folder.rglob('*')):
   if path.is_file():add(path,category)
 for folder_name,reason in a.excluded_attempt:
  folder=checked(folder_name);assert reason.strip() and (folder/'verification.json').is_file() and read(folder/'verification.json')['passed'] is False
  for path in sorted(folder.rglob('*')):
   if path.is_file():add(path,'excluded-harness-or-lineup-attempt')
 for folder,category in [(bb,'historical-C7-build-metadata'),(cb,'actually-tested-candidate-build-metadata')]:
  for name in ['DOTA2.build.json','commands.json','DOTA2.bda','DOTA2.elf']:add(folder/name,category)
 for path,category in [(comparison,'root-comparison'),(deployment,'device-CRC-verified-candidate-deployment'),*[(checked(x),'additional-raw-receipt') for x in a.include_file]]:add(path,category)
 if tree_path:add(tree_path,'completed-candidate-cut-only-check')
 if final_path:add(final_path,'completed-readonly-device-cleanup-check')
 for path in sorted(PREVIOUS.rglob('*')):
  if path.is_file():add(path,'pre-hardware-docs-and-offline-index-snapshot')
 for name in ['archive_hardware.py','preserve_previous.py','compare_profiles.py','test_compare_profiles.py','source-audit.json','prepare_state_update.py','run_hardware_archive.py']:
  path=Path(__file__).with_name(name)
  if path.exists():add(path,'archive-comparison-or-audit-driver')
 add(ROOT/'tests/physical_trees_h1.py','current-tree-test-source')
 rows=sorted(selected.values(),key=lambda x:x['source']);excluded_rows=sorted(excluded.values(),key=lambda x:x['source'])
 manifest=dict(schema_version=1,version='0.30',round=3,phase='restored-SD-hardware',scope=__doc__,executed=a.execute,
  files=rows,archived_files=len(rows),archived_raw_bytes=sum(x['bytes'] for x in rows),
  excluded_files=excluded_rows,exclusion_policy=dict(compiled=sorted(COMPILED),assets=sorted(ASSETS),arbitrary_binary_outputs=True,maximum_Game_or_fog_snapshot_bytes=256*1024,allowed_DotaView_snapshot_bytes=148,view_snapshot_name_requires='view',maximum_PNG_bytes=1024*1024),
  old_offline_archive_or_docs_modified=False,raw_report_outcome_fields_rewritten=False)
 def ref(path):
  row=selected[rel(path)];return dict(path=row['path'],sha256=row['sha256'],bytes=row['bytes'])
 index=dict(schema_version=1,version='0.30',round=3,phase='restored-SD-hardware',
  scope='Separate append-only actual H1 follow-up. Original round3 offline/C7 historical indices remain available; acceptance does not extend to untested source or binaries.',
  baseline=dict(bda_sha256=lm['bda_sha256'],elf_sha256=lm['elf_sha256'],report=ref(base/'verification.json'),build_metadata=ref(bb/'DOTA2.build.json')),
  candidate=dict(bda_sha256=rm['bda_sha256'],elf_sha256=rm['elf_sha256'],bda_bytes=(cb/'DOTA2.bda').stat().st_size,report=ref(candidate/'verification.json'),build_metadata=ref(cb/'DOTA2.build.json')),
  candidate_installed_verified=True,physical_candidate_tested=True,normal_inputs_only=True,game_state_ram_writes=0,
  deployment_receipt=ref(deployment),comparison=ref(comparison),matched_input_metadata=alignment,
  observed_menu_frame_counters=comp['observed_menu_frame_counters'],
  physical_cut_only_acceptance=dict(passed=True,evidence=ref(tree_path),natural180s_regrow_on_current_bda_tested=False,functional_test_FPS_used_for_profile_comparison=False) if tree_path else None,
  final_readonly_device_check=ref(final_path) if final_path else None,
  excluded_attempts=[dict(source_folder=rel(checked(folder)),reason=reason,report=ref(checked(folder)/'verification.json'),original_passed=False) for folder,reason in a.excluded_attempt],
  performance_acceptance_passed={'pending':None,'passed':True,'failed':False}[a.performance_acceptance],performance_acceptance_source='Explicit --performance-acceptance root decision; never inferred from host/MIPS evidence',
  full_feature_acceptance_complete=False,release_pin_created=False,github_updated=False,
  previous_state_manifest=ref(PREVIOUS/'manifest.json'),
  prior_round3_offline_INDEX_snapshot=ref(PREVIOUS/'provenance/verification/v0.30/performance/round3/INDEX.json'),
  prior_round3_offline_RAW_MANIFEST_snapshot=ref(PREVIOUS/'provenance/verification/v0.30/performance/round3/RAW-MANIFEST.json'),
  manifest=dict(path=rel(out/'RAW-MANIFEST.json'),sha256=hashlib.sha256(json_bytes(manifest)).hexdigest()),
  limitations=['Only the candidate BDA/ELF hashes above are installed/tested claims.',
   'One restored-SD run per binary is not a repeated statistical trial or an all-scenes FPS guarantee.',
   'A baseline desktop_not_idle preflight and prior B-card stat/read failure remain distinct historical observations.',
   'No previous round3 raw report, source_current value, offline index or documentation file is rewritten by this script.'])
 if a.execute:
  for row in rows:
   data=(ROOT/row['source']).read_bytes();assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256'];immutable_write(ROOT/row['path'],data)
  immutable_write(out/'RAW-MANIFEST.json',json_bytes(manifest));immutable_write(out/'INDEX.json',json_bytes(index))
  assert all(sha(ROOT/row['path'])==row['sha256'] for row in rows)
 assert before=={rel(x):sha(x) for x in protected},'Existing summaries changed while archiving'
 if a.plan_output:
  plan=checked(a.plan_output);assert plan.is_relative_to(ROOT/'build');plan.parent.mkdir(parents=True,exist_ok=True);plan.write_bytes(json_bytes(dict(manifest=manifest,index=index)))
 print(json.dumps(dict(executed=a.execute,selected_files=len(rows),selected_raw_bytes=manifest['archived_raw_bytes'],excluded_files=len(excluded_rows),candidate_bda_sha256=rm['bda_sha256'],performance_acceptance_passed=index['performance_acceptance_passed'],destination=rel(out))))
if __name__=='__main__':main()
