"""Stage narrowly bound C8 hardware doc/state updates; --execute applies them.

Requires the completed separate hardware archive. Old round3 raw/offline INDEX
and C7 evidence remain immutable; only the main performance INDEX latest
pointers are mutable. Default writes proposals beneath build only.
No device, source-code, build, Git or release operations are performed.
"""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[3]
PREVIOUS=ROOT/'build/perf3-hardware-previous'
HARDWARE=ROOT/'provenance/verification/v0.30/performance/round3/hardware'
LABELS={
 'menu':'主菜单','fresh-fountain-play':'新开局泉水','same-fountain-paused':'同泉水暂停',
 'fountain-resumed':'泉水恢复','mid-camera-first':'中路镜头首次窗口','mid-camera-warm':'中路镜头后续窗口',
 'same-mid-paused':'同中路镜头暂停','mid-approach-play':'固定坐标点击后（mid-approach）',
 'post-horn-mid-play':'自然首波后中路镜头','post-horn-hero-play':'自然首波后英雄镜头',
 'post-horn-hero-paused':'自然首波后英雄镜头暂停',
}
PAUSED={'menu','same-fountain-paused','same-mid-paused','post-horn-hero-paused'}
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_bytes())
def rel(path):return Path(path).relative_to(ROOT).as_posix()
def checked(path):
 path=Path(path);path=(path if path.is_absolute() else ROOT/path).resolve();assert path.is_relative_to(ROOT),path;return path
def json_bytes(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf8')
def direct_ref(path):return dict(path=rel(path),sha256=sha(path))

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--hardware-index',type=Path,default=HARDWARE/'INDEX.json')
 p.add_argument('--tree-report',type=Path,required=True,help='Final actual C8 tree attempt; failed attempts remain failed')
 p.add_argument('--output',type=Path,default=Path('build/v030/perf3-hardware-followup/state-proposal'))
 p.add_argument('--execute',action='store_true',help='Apply the five staged derived docs/state/latest-pointer files; never edit original raw evidence')
 a=p.parse_args();ip=checked(a.hardware_index);assert ip.is_relative_to(HARDWARE);index=read(ip);manifest_path=ROOT/index['manifest']['path'];assert sha(manifest_path)==index['manifest']['sha256'];manifest=read(manifest_path);assert manifest['executed']
 for row in manifest['files']:
  path=ROOT/row['path'];assert path.stat().st_size==row['bytes'] and sha(path)==row['sha256']
 archived={r['source']:r for r in manifest['files']}
 def archived_ref(source):
  row=archived[rel(checked(source))];return dict(path=row['path'],sha256=row['sha256'])
 def bound_json(reference):
  path=ROOT/reference['path'];assert sha(path)==reference['sha256'];return read(path)
 comp=bound_json(index['comparison']);meta=bound_json(index['candidate']['build_metadata']);hwref=direct_ref(ip)
 recovery_path=ROOT/'build/v030/perf3-candidate8-trees-cut/normal-exit-after-timeout.json';recovery_ref=archived_ref(recovery_path);recovery=read(ROOT/recovery_ref['path'])
 symbols=recovery['live_optional_cache_symbols'];assert symbols['sprite_row_slots']==[128] and symbols['sprite_row_heap_bytes']==[220140] and symbols['shadow_curve'][0] and symbols['read_cache'][0]
 caches={label:comp['raw_samples'][label]['menu']['before']['cache'] for label in ['baseline','candidate']}
 heap_delta={key:caches['candidate'][key]-caches['baseline'][key] for key in ['scene_heap_bytes','io_heap_bytes','total_heap_bytes']}
 assert heap_delta==dict(scene_heap_bytes=244716,io_heap_bytes=168336,total_heap_bytes=413052)
 optional_runtime=dict(menu_cache_heap_baseline=caches['baseline']['total_heap_bytes'],menu_cache_heap_candidate=caches['candidate']['total_heap_bytes'],observed_heap_delta=heap_delta,row_slots=128,row_heap_bytes=220140,shadow_curve_bytes=24576,direct_live_symbols_evidence=recovery_ref,final_release_evidence=index['final_readonly_device_check'],release_basis='Application total heap0/released1; independent resource/audio/tree cleanup and optional pointers0. Per-component counters can be historical.')
 assert index['candidate_installed_verified'] and index['physical_candidate_tested'] and index['performance_acceptance_passed'] is True
 assert comp['passed'] and comp['physical_h1_tested'] and not comp['synthetic_fixture_inputs']
 assert all(comp['acceptance'][k] is True for k in ['comparison_valid','baseline_health','candidate_health'])
 assert index['candidate']['bda_sha256']==meta['bda_sha256']==comp['candidate_bda_sha256']=='782aa7e90327e82204cb913fc3440e5789f0924301cbb07105defd6116350244'
 assert index['candidate']['elf_sha256']==meta['elf_sha256']==comp['candidate_elf_sha256']
 tree_path=checked(a.tree_report);tree=read(tree_path);tree_ref=archived_ref(tree_path)
 assert tree_ref['sha256']==sha(tree_path) and tree['bda_sha256']==meta['bda_sha256'] and tree['elf_sha256']==meta['elf_sha256']
 assert tree['normal_inputs_only'] and tree['game_state_ram_writes']==0
 tree_physical=tree.get('physical_H1_tested',tree.get('physical_h1_tested')) is True
 cut_pass=tree.get('passed') is True
 if cut_pass:
  assert tree_physical
  executed=tree_path.with_name('driver-executed.py');assert sha(executed)==tree['driver_sha256'];archived_ref(executed)
 old=read(PREVIOUS/'manifest.json');old_sources={r['source']:r for r in old['files']}
 for row in old['files']:assert sha(ROOT/row['snapshot'])==row['sha256']
 addon=read(PREVIOUS/'candidate8-before-hardware-state.json');assert sha(ROOT/addon['snapshot'])==addon['sha256']
 old_sources[addon['source']]=addon
 mutable_latest='provenance/verification/v0.30/performance/INDEX.json'
 protected=[ROOT/name for name in old_sources if name.startswith('provenance/') and name!=mutable_latest];before_protected={rel(x):sha(x) for x in protected}
 def previous_bytes(name):return (ROOT/old_sources[name]['snapshot']).read_bytes()
 def previous_ref(name):return archived_ref(ROOT/old_sources[name]['snapshot'])
 windows=comp['windows'];cold=next(x for x in windows if x['name']=='fresh-fountain-play');active=[x['candidate']['fps'] for x in windows if x['name'] not in PAUSED]
 post=[x['candidate']['fps'] for x in windows if x['name'] in ['post-horn-mid-play','post-horn-hero-play']]
 lineup=comp['source_reports']['candidate']['lineup_hero_ids'];menu=comp['observed_menu_frame_counters'];driver=comp['source_reports']['candidate']['driver_sha256']
 acceptance='C8 passed normal-input loading, 11 about-8-second matched-driver/initialized-lineup FPS windows, pause/resume, camera, audio counters and normal exit/cleanup. '
 acceptance+=('The final cut-only normal-input tree test passed; natural 180-second regrow was not repeated on C8. ' if cut_pass else 'The supplied current tree attempt did not pass and is not a functional acceptance claim. ')
 acceptance+='One sequential run per binary; no repeated-run variance, late team-fight, per-second minimum or full new-feature coverage claim.'
 candidate=read(PREVIOUS/'docs/verification/v0.30-candidate8.json');assert candidate['bda_sha256']==meta['bda_sha256']
 candidate.update(physical_h1_game_tested=True,hardware_deployment_verified=True,performance_acceptance_passed=True,full_feature_acceptance_complete=False,hardware_blocker=None,
  evidence=hwref,offline_evidence=candidate['evidence'],hardware_acceptance_scope=acceptance,
  installation_report=index['deployment_receipt'],physical_comparison=index['comparison'],
  pre_hardware_candidate_state=previous_ref('docs/verification/v0.30-candidate8.json'),
  optional_cache_runtime=optional_runtime,
  physical_cut_only_acceptance=dict(passed=cut_pass,evidence=tree_ref,natural180s_regrow_on_current_bda_tested=False))
 candidate_bytes=json_bytes(candidate)
 h1=read(PREVIOUS/'docs/verification/v0.30-h1.json');historical_C7=h1['performance_evidence_index'];historical_C7_cut=h1.get('physical_cut_only_acceptance')
 h1.update(bda_sha256=meta['bda_sha256'],bda_bytes=index['candidate']['bda_bytes'],elf_sha256=meta['elf_sha256'],sdk_commit=meta['sdk_commit'],binary=meta['binary'],
  physical_h1_game_tested=True,hardware_deployment_verified=True,physical_h1_launch_attempted=True,performance_acceptance_passed=True,full_feature_acceptance_complete=False,
  functional_h1_passed=None,all_current_offline_checks_passed=None,current_performance_changes_offline_checks_passed=True,
  runtime_acceptance=acceptance,installation_report=index['deployment_receipt'],performance_evidence_index=hwref,
  physical_fps=dict(scope='One restored-SD normal-input run per binary, identical executed driver, initialized lineup and window order, about8s per window. Observed pre-ENTER menu counter is information; corresponding AI/ticks were not RAM-forced identical. No per-second minimum, repeated-run interval or late team-fight claim.',comparison=index['comparison'],observed_menu_frames=dict(baseline=menu['baseline'],final=menu['candidate']),final_active_window_fps_range=[min(active),max(active)],final_post_horn_active_fps=post),
  offline_verification_scope='Exact C8 complete-BDA resource104, vision54, trees2161, sight196608 and local-tree-FOV MIPS checks; 353-frame renderer parity and optional-row-failure/null-shadow scalar proofs are separate host evidence. These do not establish complete new-feature acceptance.',
  historical_C7_state=previous_ref('docs/verification/v0.30-h1.json'),historical_C7_performance_evidence_index=historical_C7,
  historical_C7_physical_cut_only_acceptance=historical_C7_cut,
  physical_cut_only_acceptance=dict(passed=cut_pass,evidence=tree_ref,natural180s_regrow_on_current_bda_tested=False),
  installed_candidate_state=dict(path='docs/verification/v0.30-candidate8.json',sha256=hashlib.sha256(candidate_bytes).hexdigest()))
 h1['optional_cache_runtime']=optional_runtime
 h1.pop('pending_offline_candidate',None)
 if 'latest_observed_storage_issue' in h1:
  h1['historical_pre_restore_storage_issue']=h1.pop('latest_observed_storage_issue');h1['historical_pre_restore_storage_issue']['resolved_for_recorded_C7_C8_runs']=True
 h1['restored_device_evidence']=[dict(path=r['path'],sha256=r['sha256']) for r in manifest['files'] if r['source'].startswith('build/v030/perf3-restored-device/') and r['source'].endswith('.json')]
 table='\n'.join(['| 窗口 | 恢复后 C7 FPS | C8 FPS |','| --- | ---: | ---: |']+[f"| {LABELS[x['name']]} | {x['baseline']['fps']:.2f} | {x['candidate']['fps']:.2f} |" for x in windows])
 tree_text=('C8 的最终正常输入砍树检查已通过；未在 C8 重新等待自然 180 秒长回。' if cut_pass else 'C8 当前这份砍树检查未通过；它不构成功能验收，原失败报告保持原样。')
 hardware_section=f'''## SD 恢复后的 C7 → C8 真机短窗

同一执行驱动 `{driver}`，相同实际初始化阵容 `{lineup}`、相同 11 个窗口顺序；观测菜单计数 C7/C8 为 {menu['baseline']}/{menu['candidate']}。输入由后续帧处理，观测计数本身不是种子对齐证据。每版本仅一次约 8 秒窗口，AI/tick 未强制逐字节相同。

{table}

新开局泉水由 {cold['baseline']['fps']:.2f} 提升到 {cold['candidate']['fps']:.2f} FPS；热缓存窗口大体相同。全部窗口资源/音频错误、欠载及重启计数为 0；正常退出后资源句柄/heap、应用 cache 总 heap、音频 heap 清零，桌面 idle/返回码正常。cache 各组件统计可能保留历史值，释放结论使用总 heap 和 released 标记，并独立检查资源、音频与树模块。

实际菜单 cache 总 heap 为 {caches['baseline']['total_heap_bytes']:,}→{caches['candidate']['total_heap_bytes']:,} 字节，增加 413,052 字节：reader 增加 168,336，scene 增加 244,716（128 条 row 元数据 220,140，加阴影曲线 24,576）。第一次树测试后的只读恢复观察直接读到 row slots=128、row heap=220,140，曲线和读缓存指针非空；正常 BACK 退出及最终设备检查确认总 heap 和这些指针清零。组件历史计数字段无需全部清零。

{tree_text} 第一次 C8 树测试被测试器的 30 秒菜单出帧预算拒绝，后续只读观察显示正常菜单和零资源错误；该失败按 harness 超时排除，不能称为游戏卡死。原驱动、失败报告、恢复观察和最终重试各自保留。

本次仅通过上述性能短窗和已列正常输入检查。没有逐秒最低值、多轮统计、后期团战、完整新功能覆盖或“全程 15 FPS”保证。资源计数包含独立 RAW 音频，外围 RPC 区间略宽于 FPS 计数区间，不能将全部变化归因于某一项优化。`mid-approach` 只有固定点点击，不是独立走路基准。

实际 BDA/ELF、CRC 部署、正常退出和原始测量见[第三轮真机追加索引](../provenance/verification/v0.30/performance/round3/hardware/INDEX.json)。[原离线索引](../provenance/verification/v0.30/performance/round3/INDEX.json)保留候选尚未安装时的历史状态。

'''
 round3_name='docs/PERFORMANCE-v0.30-round3.md';text=previous_bytes(round3_name).decode('utf8').replace('\r\n','\n');lines=text.split('\n');lines[0]='# v0.30 第三轮性能优化与真机限定验收';text='\n'.join(lines)
 begin=text.index('\n\n')+2;end=text.index('\n\n',begin)
 text=text[:begin]+f"SD 卡恢复后，C8 已通过设备 CRC 校验安装，并完成同驱动、同实际阵容的 C7/C8 真机短窗对照。新开局泉水 {cold['baseline']['fps']:.2f}→{cold['candidate']['fps']:.2f} FPS；后续热窗口大体相同。资源、音频和正常退出检查通过。此前 B 卡读卡错误与桌面 preflight 拒绝是独立历史观测，原始失败报告保留。完整新功能覆盖、版本封存和 GitHub 更新仍未完成。"+text[end:]
 text=text.replace('## 五项尝试',hardware_section+'## 五项尝试',1)
 tail='原始检查、失败尝试、冻结源码和范围限制见'
 at=text.index(tail);text=text[:at]+'原始离线检查、失败尝试与冻结源码见[第三轮离线历史索引](../provenance/verification/v0.30/performance/round3/INDEX.json)；当前安装 C8 的实际短窗与部署见[真机追加索引](../provenance/verification/v0.30/performance/round3/hardware/INDEX.json)。离线指令百分比仍不换算真机 FPS。\n'
 main_name='docs/PERFORMANCE-v0.30.md';main=previous_bytes(main_name).decode('utf8').replace('\r\n','\n');heading='## 第二轮：C3 → C7';start=main.index(heading)
 intro=f'''# v0.30 性能调查与限定验收

当前安装为第三轮 C8，BDA `{meta['bda_sha256']}`，{index['candidate']['bda_bytes']:,} 字节；ELF `{meta['elf_sha256']}`。SD 恢复后的同驱动、同实际阵容短窗中，新开局泉水 {cold['baseline']['fps']:.2f}→{cold['candidate']['fps']:.2f} FPS，热窗口大体相同；资源/音频错误与退出释放检查通过。{tree_text} 完整新功能覆盖、版本封存和 GitHub 更新仍未完成。

当前数据和范围见[第三轮完整记录](PERFORMANCE-v0.30-round3.md)、[C8 真机追加索引](../provenance/verification/v0.30/performance/round3/hardware/INDEX.json)及[设备摘要](verification/v0.30-h1.json)。以下第二轮 C7 和第一轮 C3 保留为历史，原算法状态与 FPS 表不改写为 C8 数据。

'''
 main=intro+main[start:].replace(heading,heading+'（历史记录）',1)
 latest=read(PREVIOUS/mutable_latest);latest_before=json.loads(json.dumps(latest));allowed_latest={'scope','latest_round','latest_candidate_bda_sha256','latest_candidate_installed','latest_candidate_physical_performance_accepted','latest_physical_acceptance_round','latest_evidence_index','latest_offline_evidence_index','latest_state_scope'}
 latest.update(scope='Primary Round1 measurements and file entries are preserved historical evidence. Latest pointers identify installed C8 and its narrowly accepted restored-SD physical follow-up; the prior round3 offline candidate INDEX/RAW-MANIFEST remain immutable history.',latest_round=3,latest_candidate_bda_sha256=meta['bda_sha256'],latest_candidate_installed=True,latest_candidate_physical_performance_accepted=True,latest_physical_acceptance_round=3,latest_evidence_index=hwref,latest_offline_evidence_index=latest['latest_evidence_index'],latest_state_scope='Latest pointers describe installed C8 and narrowly accepted restored-SD physical windows only. All non-latest primary Round1 fields, FPS tables and historical file entries remain unchanged; round3 offline INDEX/RAW-MANIFEST are historical immutable evidence.')
 assert {k:v for k,v in latest.items() if k not in allowed_latest}=={k:v for k,v in latest_before.items() if k not in allowed_latest}
 updates={
  'docs/verification/v0.30-candidate8.json':candidate_bytes,
  'docs/verification/v0.30-h1.json':json_bytes(h1),round3_name:text.encode('utf8'),main_name:main.encode('utf8'),
  mutable_latest:json_bytes(latest),
 }
 output=checked(a.output);assert output.is_relative_to(ROOT/'build');output.mkdir(parents=True,exist_ok=True);rows=[]
 for name,data in updates.items():
  previous=previous_bytes(name);target=ROOT/name;current=target.read_bytes();assert current in [previous,data],('Unexpected live docs drift; review before applying',name)
  proposed=output/name;proposed.parent.mkdir(parents=True,exist_ok=True);proposed.write_bytes(data)
  rows.append(dict(target=name,previous_sha256=hashlib.sha256(previous).hexdigest(),proposed_sha256=hashlib.sha256(data).hexdigest(),proposal=rel(proposed),bytes=len(data)))
  if a.execute:target.write_bytes(data)
 assert before_protected=={rel(x):sha(x) for x in protected},'Historical provenance changed'
 report=dict(schema_version=1,executed=a.execute,scope=__doc__,hardware_index=hwref,candidate_bda_sha256=meta['bda_sha256'],tree_cut_passed=cut_pass,tree_report=tree_ref,files=rows,historical_offline_manifest_and_INDEX_modified=False,production_modified=False,USB_opened=False,release_or_github_updated=False)
 (output/'update-plan.json').write_bytes(json_bytes(report));print(json.dumps(dict(executed=a.execute,files=len(rows),tree_cut_passed=cut_pass,hardware_index_sha256=hwref['sha256'],output=rel(output))))
if __name__=='__main__':main()
