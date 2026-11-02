"""Update round2 documentation after root confirms physical C6 acceptance."""
from pathlib import Path
import argparse,hashlib,json,struct
ROOT=Path.cwd();OUT=ROOT/'provenance/verification/v0.30/performance/round2';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--profile',type=Path,required=True);p.add_argument('--cut',type=Path,required=True);p.add_argument('--confirmed',action='store_true',required=True);a=p.parse_args()
profile=a.profile.resolve();cut=a.cut.resolve();final=json.loads(profile.read_bytes());cuts=json.loads(cut.read_bytes());base=json.loads((ROOT/'build/v030/perf-union-aligned/verification.json').read_bytes());meta=json.loads((ROOT/'build/v030/perf-candidate6/DOTA2.build.json').read_bytes())
assert final['passed']and cuts['passed']and final['bda_sha256']==cuts['bda_sha256']==meta['bda_sha256']=='b79b98ce7d6389b060cc5b586d8631178bc28bea5e886bf74e0e86762d0bd81f'
assert final['driver_sha256']==base['driver_sha256']and final['lineup_hero_ids']==base['lineup_hero_ids']==[0,3,6,8,4,1,2,7,5,9]
assert [x['name']for x in final['profiles']]==[x['name']for x in base['profiles']]and len(final['profiles'])==11
for run in [base,final]:
 assert run['desktop_after']['menu_idle']==1 and run['desktop_after']['return_code']==0
 assert run['after_exit']['resources']['heap_bytes']==run['after_exit']['resources']['open_files']==run['after_exit']['cache'][9]==0
 assert run['after_exit']['audio'][16]==run['after_exit']['audio'][17]
 for window in run['profiles']:
  assert window['after']['resources']['errors']==0 and all(window['after']['audio'][i]==0 for i in [2,6,23,24,25])
ip=OUT/'INDEX.json';index=json.loads(ip.read_bytes());files={f['path']:f for f in index['files']}
def copy(src,dst=None,category='final-c6-physical'):
 src=Path(src);src=src if src.is_absolute()else ROOT/src;target=OUT/(dst or src.relative_to(ROOT));target.parent.mkdir(parents=True,exist_ok=True);data=src.read_bytes()
 if target.exists():assert target.read_bytes()==data,target
 else:target.write_bytes(data)
 row=dict(path=target.relative_to(ROOT).as_posix(),source=src.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),byte_identical_at_capture=True,category=category);files[row['path']]=row;return dict(path=row['path'],sha256=row['sha256'])
for folder in [profile.parent,cut.parent]:
 for f in sorted(folder.rglob('*')):
  if f.is_file()and f.suffix.lower()in{'.json','.py','.png','.bin'}:copy(f)
deployment=[]
for f in sorted((ROOT/'build/v030').glob('*deploy*.json')):
 try:r=json.loads(f.read_bytes())
 except(ValueError,OSError):continue
 upload=r.get('upload',r)
 if upload.get('sha256')==meta['bda_sha256']and (upload.get('completed')or r.get('completed')):
  deployment.append(copy(f,category='final-c6-deployment'))
assert deployment,'missing final CRC-verified C6 deployment receipt'
for f in sorted((ROOT/'build/v030').glob('*/verification.json')):
 try:r=json.loads(f.read_bytes())
 except(ValueError,OSError):continue
 if r.get('bda_sha256')==meta['bda_sha256']and r.get('passed')and r.get('actual_mips32'):
  for src in sorted(f.parent.rglob('*')):
   if src.is_file()and src.suffix.lower()in{'.json','.txt','.c','.ld'}:copy(src,category='final-c6-mips-correctness')
comparison=dict(passed=True,physical_h1_tested=True,game_state_ram_writes=0,baseline_bda_sha256=base['bda_sha256'],final_bda_sha256=final['bda_sha256'],driver_sha256=final['driver_sha256'],lineup_hero_ids=final['lineup_hero_ids'],observed_menu_frames=dict(baseline=base['menu_frame_before_enter'],final=final['menu_frame_before_enter']),same_observed_menu_frame=base['menu_frame_before_enter']==final['menu_frame_before_enter'],baseline_report=dict(path=(OUT/'baseline-c3/verification.json').relative_to(ROOT).as_posix(),sha256=sha(OUT/'baseline-c3/verification.json')),final_report=dict(path=(OUT/profile.relative_to(ROOT)).relative_to(ROOT).as_posix(),sha256=sha(profile)),windows=[],scope='One matched-lineup normal-input run per binary, same driver/window sequence, about8s per window. Observed menu frame may differ due asynchronous key dispatch. No forced identical AI/tick replay, later team fight or per-second minimum claim.')
for x,y in zip(base['profiles'],final['profiles']):comparison['windows'].append(dict(name=x['name'],baseline={k:x[k]for k in ['fps','frames','seconds','game_ticks','resource_delta']},final={k:y[k]for k in ['fps','frames','seconds','game_ticks','resource_delta']},observed_fps_change_percent=100*(y['fps']/x['fps']-1)))
cp=OUT/'ALIGNED-COMPARISON.json';cp.write_text(json.dumps(comparison,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
snapshots=[]
for label,folder in [('C3',ROOT/'build/v030/perf-union-aligned'),('C6',profile.parent)]:
 for f in sorted(folder.glob('*-game.bin')):
  raw=f.read_bytes();snapshots.append(dict(binary=label,name=f.name,sha256=sha(f),unit0=dict(zip(['x','y','kills','deaths'],[struct.unpack_from('<i',raw,o)[0]for o in [24,28,208,212]]))))
sp=OUT/'SNAPSHOT-FIELDS.json';sp.write_text(json.dumps(dict(unit_bytes=240,offsets=dict(x=24,y=28,kills=208,deaths=212),snapshots=snapshots,limitation='These paused endpoint snapshots do not establish an uninterrupted player trajectory, no movement, or a walking benchmark.'),ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
copy(Path(__file__).resolve(),'finalize-docs-script.py','archive-script')
index.update(candidate_bda_sha256=meta['bda_sha256'],candidate_elf_sha256=meta['elf_sha256'],candidate_role='final-c6-bounded-cache',pending_final_candidate=None,installation_verified=True,performance_acceptance_passed=True,physical_cut_only_acceptance_passed=True,full_feature_acceptance_complete=False,release_pin_created=False,github_updated=False,physical_fps_samples=comparison['windows'],comparison=dict(path=cp.relative_to(ROOT).as_posix(),sha256=sha(cp)),observed_menu_frames=comparison['observed_menu_frames'],physical_profile=comparison['final_report'],physical_cut_only=dict(path=(OUT/cut.relative_to(ROOT)).relative_to(ROOT).as_posix(),sha256=sha(cut)),scope='Final C6 physical short-window FPS and cut-only acceptance passed. C3 full180s regrow is historical; C6 natural regrow logic is host/MIPS checked. No full feature/teamfight/per-second minimum/full97-frame C6 renderer rerun claimed.')
index['verification_inheritance']['physical_tree_natural_regrow']='C3 physical full180s regrow is historical; current C6 passed normal-input two-tree cut-only smoke and cleanup. No fresh C6 physical180s regrow wait claimed.'
index['files']=sorted(files.values(),key=lambda f:f['path']);index['archived_files']=len(files);index['archived_raw_bytes']=sum(f['bytes']for f in files.values());index['derived_files']=[dict(path=x.relative_to(ROOT).as_posix(),sha256=sha(x))for x in [cp,sp]]
ip.write_text(json.dumps(index,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n');assert all(sha(ROOT/f['path'])==f['sha256']for f in index['files'])
index_ref=dict(path=ip.relative_to(ROOT).as_posix(),sha256=sha(ip));comparison_ref=dict(path=cp.relative_to(ROOT).as_posix(),sha256=sha(cp))
global_path=ROOT/'provenance/verification/v0.30/performance/INDEX.json';global_index=json.loads((OUT/'history/round1-INDEX-before.json').read_bytes());global_index.update(round=1,scope='Round1 D569/C3 fields and raw receipts retained below. Latest round2 C3/C6 acceptance is linked separately.',latest_round=2,latest_candidate_bda_sha256=meta['bda_sha256'],latest_evidence_index=index_ref);global_path.write_text(json.dumps(global_index,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
h1_path=ROOT/'docs/verification/v0.30-h1.json';h1=json.loads((OUT/'history/round1-h1-before.json').read_bytes());h1.update(bda_sha256=meta['bda_sha256'],bda_bytes=meta['bda_bytes'],elf_sha256=meta['elf_sha256'],binary=meta['binary'],physical_h1_game_tested=True,functional_h1_passed=None,performance_acceptance_passed=True,full_feature_acceptance_complete=False,hardware_deployment_verified=True,current_performance_changes_offline_checks_passed=True,all_current_offline_checks_passed=None,runtime_acceptance='Final C6 passed normal-input loading,11 about8s matched-lineup FPS windows,pause/resume,camera,audio,normal exit/cleanup and two normal tree cuts. C3 physical180s regrow is historical. Full new-feature coverage is not claimed.',installation_report=deployment[0],performance_evidence_index=index_ref,physical_cut_only_acceptance=dict(passed=True,evidence=index['physical_cut_only'],natural180s_regrow_on_current_bda_tested=False),offline_verification_scope='Fresh C6 exact-BDA vision54,trees2161,getter196608 checks; bounded-cache host98 cases. Resource100 checks inherited from unchanged C5 reader source/interface. C5 shader97-frame proof plus unchanged shader source and C6 guard mask parity are composed evidence, not a fresh C6 complete97-frame run.')
active=[w['final']['fps']for w in comparison['windows']if'paused'not in w['name']and w['name']!='menu'];post=[w['final']['fps']for w in comparison['windows']if w['name']in('post-horn-mid-play','post-horn-hero-play')]
h1['physical_fps']=dict(scope=comparison['scope'],comparison=comparison_ref,observed_menu_frames=comparison['observed_menu_frames'],final_active_window_fps_range=[min(active),max(active)],final_post_horn_active_fps=post);h1_path.write_text(json.dumps(h1,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
labels=['主菜单','新开局泉水','同泉水暂停','泉水恢复','中路镜头首次窗口','中路镜头后续窗口','同中路镜头暂停','固定坐标点击后（driver名mid-approach）','自然首波后中路镜头','自然首波后英雄镜头','自然首波后英雄镜头暂停'];table='\n'.join(f"| {n} | {w['baseline']['fps']:.2f} | {w['final']['fps']:.2f} |"for n,w in zip(labels,comparison['windows']))
old_doc=(OUT/'history/round1-performance-before.md').read_text().split('\n',1)[1].lstrip();fps={w['name']:w['final']['fps']for w in comparison['windows']}
new_doc=f'''# v0.30 性能调查与限定验收

当前安装为第二轮C6，BDA `{meta['bda_sha256']}`，{meta['bda_bytes']:,}字节；ELF `{meta['elf_sha256']}`。完成正常输入加载、11个约8秒FPS窗口、暂停恢复、镜头、音频、两次吃树/砍树及正常退出/资源释放；完整新功能覆盖、版本封存、GitHub更新仍未完成。

## 第二轮：C3 → C6

同一驱动 `{final['driver_sha256']}`，同阵容 `[0,3,6,8,4,1,2,7,5,9]`，同窗口顺序。观测菜单帧C3为{base['menu_frame_before_enter']}、C6为{final['menu_frame_before_enter']}；USB输入异步调度下以期望阵容检查拒绝错配，未强称观测帧相同。每版本一次约8秒短窗，使用PC读完成帧计数的中点计时；AI和逻辑tick未强制逐字节锁定，不是多轮统计、每秒最低值或后期团战。

| 窗口 | C3基线FPS | C6最终FPS |
| --- | ---: | ---: |
{table}

`mid-approach`只有固定坐标点击，未连续采样英雄位置，不能独立称为走路基准。暂停端点坐标/KD见[快照字段](../provenance/verification/v0.30/performance/round2/SNAPSHOT-FIELDS.json)，端点相同不排除中途移动、死亡和复活。所有FPS窗口资源错误、音频错误/欠载/重启计数为0；退出后资源句柄、应用cache总heap和音频heap为0，音频初始化/销毁计数相等，桌面idle/返回码0。音频结论来自设备计数。

## 本轮开销与修改

未砍树时，重生计时函数在C3两个快照中每tick仅8个MIPS指令槽，不能解释持续掉帧。原VISION树木阻挡格与2113棵完整原树重建的8374个阻挡格逐位相同，未砍树的视线几何未因可交互树改变。主要已定位开销是动态视野阴影计算、每帧小地图查询和渲染。

1. 视野缓存8类只含不可变地形的障碍平面，动态树木另用字节平面；减少每个阴影格反复计算高度舍入、区域及树位提取。缓存填满后，其他类别走原始region/height/flying判定，不反复重建整张平面。MIPS workspace为1,662,184字节，加139,264字节VISION负载共1,801,448字节。98场景/740,553项host检查、3,211,264字节迷雾记录与原实现相同，包含超过8类、负高度、未知高度、飞行、肉山区域和树木生命周期。
2. 小地图每帧读取一次只读current bitplane，替代5184次单点查询。576场景/75,202,560像素word对照相同；最终C6完整BDA的196,608格MIPS getter对照通过。
3. 树木贴图patch按相关瓦片的实际砍树状态失效；远处树变化不再让无关瓦片重复解码，同瓦片选树复用解码。颜色、深度和树冠选取输出的host对照通过。树变化仍会使视野tree_stamp整体更新，可能触发全局阴影重算，这部分尖峰尚保留。
4. 迷雾双像素RGB565算术保留C3原mix，3,145,728项算术和C5的97帧颜色/深度对照通过。C6 shader源码相同，guard由独立98场景迷雾等价验证；这是分层继承，未宣称C6重跑整套97帧。

曾测试的additive查表核在H1上更慢：相同1,048,576次叠加，原核12 raw ticks、查表16 raw ticks，两种先后顺序结果一致，已回退。微核时间不换算整机FPS。C5中间候选的FPS和首个阵容错配拒绝报告也完整保留，不冒充C6最终验收。

最终C6新跑视野54、树木2161项MIPS断言；资源读取100项MIPS检查来自C5，reader源码和接口未变，明确继承。C6真机完成两次正常树操作和清理；自然180秒完整长回的物理证据来自C3，C6长回规则由host/MIPS验证，未再等待当前BDA自然180秒。

本轮[原始证据索引](../provenance/verification/v0.30/performance/round2/INDEX.json)、[配对比较](../provenance/verification/v0.30/performance/round2/ALIGNED-COMPARISON.json)与[当前设备摘要](verification/v0.30-h1.json)。下面第一轮表格和故障历史保留原范围。

## 第一轮：D569 → C3（历史记录）

{old_doc}'''
(ROOT/'docs/PERFORMANCE-v0.30.md').write_text(new_doc,encoding='utf8',newline='\n')
readme=ROOT/'README.md';s=readme.read_text();old=next(x for x in s.splitlines()if x.startswith('**v0.30已完成限定范围的H1性能验收'))
new=f"**v0.30第二轮C6已完成限定H1性能验收，完整新增功能覆盖仍待完成。** 同阵容短窗平均：新开局泉水{fps['fresh-fountain-play']:.2f} FPS、恢复后{fps['fountain-resumed']:.2f} FPS，自然首波后中路/英雄镜头{fps['post-horn-mid-play']:.2f} / {fps['post-horn-hero-play']:.2f} FPS；音频无欠载，正常吃树/砍树及退出资源释放通过。窗口不代表全程15 FPS、后期团战或独立走路基准，详见[性能记录](docs/PERFORMANCE-v0.30.md)和[设备摘要](docs/verification/v0.30-h1.json)。下方v0.29及更早记录仅作为历史结果。"
readme.write_text(s.replace(old,new),encoding='utf8',newline='\n')
notes=ROOT/'UPDATE-NOTES.txt';s=notes.read_text();old=next(x for x in s.splitlines()if x.startswith('v0.30最终候选已完成限定H1验收'))
new=f"v0.30第二轮C6已完成限定H1验收：加载、11个约8秒同阵容帧率窗口、暂停恢复、镜头、音频、两次正常树操作及退出/资源释放通过；完整新增功能覆盖仍未完成。新开局泉水{fps['fresh-fountain-play']:.2f} FPS、恢复后{fps['fountain-resumed']:.2f} FPS，自然首波后中路/英雄镜头{fps['post-horn-mid-play']:.2f} / {fps['post-horn-hero-play']:.2f} FPS，不代表全程15 FPS、走路或后期团战基准。自然180秒物理长回证据为C3历史，C6长回逻辑由host/MIPS验证。对齐比较与原始证据见docs/PERFORMANCE-v0.30.md、docs/verification/v0.30-h1.json；旧版验收不冒充本版结果。"
notes.write_text(s.replace(old,new),encoding='utf8',newline='\n')
building=ROOT/'docs/BUILDING.md';s=building.read_text().replace('v0.30最终候选增加静态视野union复用','v0.30第一轮C3候选增加静态视野union复用')
needle='另有184,320字节的可选雾着色结果缓存'
addition='第二轮C6复用8类不可变地形障碍平面及版本化树木字节平面，缓存满后走原高度/区域判定以限制填充成本；MIPS工作区1,662,184字节，加139,264字节VISION负载共1,801,448字节。小地图每帧只借用一次current bitplane，树贴图按局部状态失效，迷雾使用双像素精确RGB565算术。当前C6的host/MIPS与短窗H1范围见[性能记录](PERFORMANCE-v0.30.md)；C5的97帧shader证据和资源reader检查为明确分层继承，不伪称当前完整重跑。\n\n'
assert needle in s;building.write_text(s.replace(needle,addition+needle),encoding='utf8',newline='\n')
assert all(sha(ROOT/f['path'])==f['sha256']for f in index['files'])
print(json.dumps(dict(passed=True,archived_files=len(files),archived_bytes=index['archived_raw_bytes'],round2_index_sha256=sha(ip),h1_sha256=sha(h1_path),performance_sha256=sha(ROOT/'docs/PERFORMANCE-v0.30.md'),comparison_sha256=sha(cp)),ensure_ascii=False))
