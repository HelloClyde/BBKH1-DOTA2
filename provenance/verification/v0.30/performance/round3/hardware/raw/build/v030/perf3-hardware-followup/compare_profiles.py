"""Read-only comparison of two completed physical_resource_profile runs.

Reports raw window measurements and health checks. A sequential pair cannot
attribute a change to one optimization or establish repeat-run variance.
"""
from pathlib import Path
import argparse,hashlib,json,math,statistics,sys
ROOT=Path(__file__).resolve().parents[3]
RESOURCE='mounted version file_bytes open_files heap_bytes last_error mount_attempts open_attempts read_requests bytes_returned physical_reads physical_bytes seeks index_hits index_misses chunk_hits chunk_misses decoded_chunks decoded_bytes service_calls errors'.split()
RESOURCE_COUNTERS=RESOURCE[6:]
CACHE='magic version rgb_bytes sprite_bytes resident_frames scene_heap_bytes io_bytes io_heap_bytes rules_bytes total_heap_bytes fallback_flags released icon_slots icon_heap_bytes audio_heap_bytes vision_heap_bytes'.split()
AUDIO='magic ready error music_asset queued played_frames underruns read_calls read_bytes music_blocks fx_loads fx_hits fx_dropped events stale_events loops initialized destroyed heap_bytes fx_cache_bytes active_effects closes encoded_cache_bytes music_restarts voice_restarts device_restarts retry_attempts last_runtime_error music_changes silent_groups_skipped reserved0 reserved1'.split()
AUDIO_COUNTERS='played_frames underruns read_calls read_bytes music_blocks fx_loads fx_hits fx_dropped events stale_events loops initialized destroyed closes music_restarts voice_restarts device_restarts retry_attempts music_changes silent_groups_skipped'.split()
WINDOWS=['menu','fresh-fountain-play','same-fountain-paused','fountain-resumed','mid-camera-first','mid-camera-warm','same-mid-paused','mid-approach-play','post-horn-mid-play','post-horn-hero-play','post-horn-hero-paused']
PAUSED={'menu','same-fountain-paused','same-mid-paused','post-horn-hero-paused'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validhash(v):return isinstance(v,str)and len(v)==64 and all(c in '0123456789abcdef'for c in v)
def decode(values,names):return dict(zip(names,values))if isinstance(values,list)and len(values)==len(names)else None
def allknown(values):return False if False in values else None if None in values else True
def finite(v):return isinstance(v,(int,float))and not isinstance(v,bool)and math.isfinite(v)
def compare(baseline,candidate,baseline_build,candidate_build,window_seconds=None,duration_tolerance=.25,min_fps=None):
 checks=[];reports={};identities={};health={};raw={};unknown=[]
 def check(name,value,group='comparison',detail=None):checks.append(dict(name=name,passed=value if isinstance(value,bool)else None,group=group,detail=detail))
 for label,directory,build in [('baseline',baseline,baseline_build),('candidate',candidate,candidate_build)]:
  report_path=directory/'verification.json';driver_path=directory/'driver.py';report=json.loads(report_path.read_text(encoding='utf8'));reports[label]=report
  meta=json.loads((build/'DOTA2.build.json').read_text(encoding='utf8'));identities[label]=dict(report_path=str(report_path),report_sha256=sha(report_path),driver_path=str(driver_path),driver_sha256=sha(driver_path)if driver_path.is_file()else None,bda_sha256=report.get('bda_sha256'),elf_sha256=report.get('elf_sha256'),build_path=str(build),metadata_sha256=sha(build/'DOTA2.build.json'),menu_frame_before_enter=report.get('menu_frame_before_enter'),lineup_hero_ids=report.get('lineup_hero_ids'))
  check(label+' completed normal-input physical run',report.get('passed')is True and report.get('physical_h1_tested')is True and report.get('normal_inputs_only')is True and report.get('game_state_ram_writes')==0)
  check(label+' driver bytes match receipt',driver_path.is_file()and sha(driver_path)==report.get('driver_sha256'))
  check(label+' exact local BDA/ELF match receipt',validhash(report.get('bda_sha256'))and validhash(report.get('elf_sha256'))and report.get('bda_sha256')==meta.get('bda_sha256')==sha(build/'DOTA2.bda')and report.get('elf_sha256')==meta.get('elf_sha256')==sha(build/'DOTA2.elf'))
  lineup=report.get('lineup_hero_ids');check(label+' unique full ten-hero roster',isinstance(lineup,list)and len(lineup)==10 and set(lineup)==set(range(10)))
  expected=WINDOWS if report.get('post_horn_requested')is True else WINDOWS[:7]
  profiles=report.get('profiles',[]);check(label+' exact declared window order',[x.get('name')for x in profiles]==expected)
  raw[label]={}
  for profile in profiles:
   name=profile.get('name');group=label+'_health';start=len(checks);seconds=profile.get('seconds');fps=profile.get('fps');frames=profile.get('frames');ticks=profile.get('game_ticks')
   check(label+' '+str(name)+' FPS arithmetic',finite(seconds)and seconds>0 and finite(fps)and finite(frames)and frames>0 and abs(fps-frames/seconds)<1e-8)
   check(label+' '+str(name)+' pause/tick state',isinstance(ticks,int)and(ticks==0 if name in PAUSED else ticks>0),group)
   expected_screen=0 if name=='menu'else 2 if name in PAUSED else 1
   before=profile.get('before',{});after=profile.get('after',{});resource_delta=None;audio_delta=None;decoded={}
   for edge,sample in [('before',before),('after',after)]:
    debug=sample.get('debug');resources=sample.get('resources');audio=decode(sample.get('audio'),AUDIO);cache=decode(sample.get('cache'),CACHE);decoded[edge]=dict(audio=audio,cache=cache,resources=resources,scene=sample.get('scene'),debug=debug)
    check(label+' '+str(name)+' '+edge+' app/debug healthy',isinstance(debug,list)and len(debug)==8 and debug[0]==0x31415444 and debug[2]==expected_screen and debug[7]==0,group)
    schema=isinstance(resources,dict)and all(k in resources and isinstance(resources[k],int)for k in RESOURCE)
    check(label+' '+str(name)+' '+edge+' resource schema',schema)
    check(label+' '+str(name)+' '+edge+' resources healthy',None if not schema else resources['mounted']==resources['version']==1 and resources['file_bytes']==112861448 and resources['open_files']>0 and resources['heap_bytes']>0 and resources['last_error']==resources['errors']==0,group)
    check(label+' '+str(name)+' '+edge+' audio ABI',audio is not None)
    check(label+' '+str(name)+' '+edge+' cache ABI',cache is not None)
    check(label+' '+str(name)+' '+edge+' audio healthy',None if audio is None else audio['magic']==0x31415544 and audio['ready']==1 and audio['error']==audio['underruns']==audio['last_runtime_error']==0 and audio['heap_bytes']>0,group)
    check(label+' '+str(name)+' '+edge+' required caches healthy',None if cache is None else cache['magic']==0x31435048 and cache['version']==3 and cache['sprite_bytes']>=12*1024*1024 and cache['total_heap_bytes']>0 and cache['released']==0 and cache['fallback_flags']==0,group)
    for kind,value in [('audio',audio),('cache',cache),('resources',resources if schema else None)]:
     if value is None:unknown.append(label+' '+str(name)+' '+edge+' '+kind+' unavailable')
    scene=sample.get('scene');check(label+' '+str(name)+' '+edge+' renderer healthy',isinstance(scene,dict)and scene.get('read_failures')==scene.get('missing_frames')==scene.get('rendered_triangles')==0,group)
   if isinstance(before.get('resources'),dict)and isinstance(after.get('resources'),dict)and all(k in before['resources']and k in after['resources']for k in RESOURCE_COUNTERS):
    resource_delta={k:after['resources'][k]-before['resources'][k]for k in RESOURCE_COUNTERS};check(label+' '+str(name)+' recorded resource deltas exact',profile.get('resource_delta')==resource_delta);check(label+' '+str(name)+' resource counters monotonic',all(v>=0 for v in resource_delta.values()),group)
   if decoded['before']['audio']and decoded['after']['audio']:
    audio_delta={k:decoded['after']['audio'][k]-decoded['before']['audio'][k]for k in AUDIO_COUNTERS};check(label+' '+str(name)+' audio progresses',audio_delta['played_frames']>0,group);check(label+' '+str(name)+' no audio fault recovery',all(audio_delta[k]==0 for k in ['underruns','music_restarts','voice_restarts','device_restarts','retry_attempts']),group)
   raw[label][name]=dict(fps=fps,frames=frames,seconds=seconds,game_ticks=ticks,resource_delta=resource_delta,audio_delta=audio_delta,before=decoded['before'],after=decoded['after'])
  end=report.get('after_exit');group=label+'_health'
  if not isinstance(end,dict):check(label+' normal-exit sample available',None,group);unknown.append(label+' after_exit missing')
  else:
   resources=end.get('resources',{});cache=decode(end.get('cache'),CACHE);audio=decode(end.get('audio'),AUDIO);tree=end.get('tree_stats')
   check(label+' resources released on normal exit',all(resources.get(k)==0 for k in ['mounted','open_files','heap_bytes','last_error','errors']),group)
   check(label+' application cache released on normal exit',None if cache is None else cache['released']==1 and cache['total_heap_bytes']==0,group)
   check(label+' audio released on normal exit',None if audio is None else audio['ready']==audio['error']==audio['heap_bytes']==audio['active_effects']==0 and audio['initialized']==audio['destroyed']and audio['closes']>0,group)
   check(label+' tree released on normal exit',None if not isinstance(tree,list)or len(tree)<16 else tree[1:4]==[0,0,0]and tree[14]>0 and tree[15]==1,group)
   identities[label]['cleanup']=dict(resources=resources,cache=cache,audio=audio,tree=tree,status=report.get('status_after'),desktop=report.get('desktop_after'))
  status=report.get('status_after',{});desktop=report.get('desktop_after',{});check(label+' normal exit returned healthy desktop',status.get('state')==1 and status.get('last_error')==status.get('usb_fault')==0 and desktop.get('menu_idle')==1 and desktop.get('error')==desktop.get('return_code')==0,group)
  health[label]=allknown([x['passed']for x in checks if x['group']==group])
 check('distinct raw run receipts',baseline.resolve()!=candidate.resolve()and identities['baseline']['report_sha256']!=identities['candidate']['report_sha256'])
 check('identical executed profile driver',reports['baseline'].get('driver_sha256')==reports['candidate'].get('driver_sha256'))
 check('identical initialized ten-hero roster',reports['baseline'].get('lineup_hero_ids')==reports['candidate'].get('lineup_hero_ids'))
 check('identical post-horn option',reports['baseline'].get('post_horn_requested')==reports['candidate'].get('post_horn_requested'))
 baseline_names=list(raw['baseline']);candidate_names=list(raw['candidate']);check('identical ordered windows',baseline_names==candidate_names)
 durations=[x['seconds']for x in raw['baseline'].values()if finite(x['seconds'])]
 target=window_seconds if window_seconds is not None else statistics.median(durations)if durations else None
 comparisons=[]
 for name in baseline_names:
  if name not in raw['candidate']:continue
  b=raw['baseline'][name];c=raw['candidate'][name]
  duration_ok=target is not None and finite(b['seconds'])and finite(c['seconds'])and abs(b['seconds']-target)<=duration_tolerance and abs(c['seconds']-target)<=duration_tolerance
  check(name+' matched measurement duration',duration_ok,detail=dict(baseline_seconds=b['seconds'],candidate_seconds=c['seconds'],target_seconds=target,tolerance_seconds=duration_tolerance,target_basis='explicit argument'if window_seconds is not None else'baseline measured median; original driver did not record requested --seconds'))
  delta=None if not finite(b['fps'])or not finite(c['fps'])else c['fps']-b['fps'];percent=None if delta is None or b['fps']==0 else 100*delta/b['fps']
  comparisons.append(dict(name=name,baseline=dict(fps=b['fps'],frames=b['frames'],seconds=b['seconds'],game_ticks=b['game_ticks'],resource_delta=b['resource_delta'],audio_delta=b['audio_delta'],cache=b['after']['cache']),candidate=dict(fps=c['fps'],frames=c['frames'],seconds=c['seconds'],game_ticks=c['game_ticks'],resource_delta=c['resource_delta'],audio_delta=c['audio_delta'],cache=c['after']['cache']),fps_delta=delta,fps_delta_percent=percent))
 comparison_valid=allknown([x['passed']for x in checks if x['group']=='comparison']);threshold=None if min_fps is None else all(finite(x['candidate']['fps'])and x['candidate']['fps']>=min_fps for x in comparisons if x['name']not in PAUSED)
 fixture=any(x.get('fixture_only')is True for x in reports.values())
 cold_io={label:dict(first_menu_sample_resources=raw[label].get('menu',{}).get('before',{}).get('resources'),fresh_fountain_window_resources=raw[label].get('fresh-fountain-play',{}).get('resource_delta'),fresh_fountain_window_audio=raw[label].get('fresh-fountain-play',{}).get('audio_delta'),all_run_resource_counters=reports[label].get('after_exit',{}).get('resources'))for label in ['baseline','candidate']}
 return dict(passed=allknown([comparison_valid,health['baseline'],health['candidate']])is True and not fixture and (min_fps is None or threshold is True),scope=__doc__,read_only=True,USB_opened=False,physical_h1_tested=not fixture and all(x.get('physical_h1_tested')is True for x in reports.values()),synthetic_fixture_inputs=fixture,cold_io=cold_io,source_reports=identities,baseline_bda_sha256=identities['baseline']['bda_sha256'],baseline_elf_sha256=identities['baseline']['elf_sha256'],candidate_bda_sha256=identities['candidate']['bda_sha256'],candidate_elf_sha256=identities['candidate']['elf_sha256'],acceptance=dict(comparison_valid=comparison_valid,baseline_health=health['baseline'],candidate_health=health['candidate'],all_live_windows_at_requested_min_fps=threshold,requested_min_fps=min_fps,performance_gain_required=None),checks=checks,unknowns=unknown,windows=comparisons,observed_menu_frame_counters=dict(baseline=reports['baseline'].get('menu_frame_before_enter'),candidate=reports['candidate'].get('menu_frame_before_enter'),equal=reports['baseline'].get('menu_frame_before_enter')==reports['candidate'].get('menu_frame_before_enter'),acceptance_basis='Actual initialized lineup must match; the observed pre-ENTER counter can differ because input is applied by a later frame.'),duration_target_seconds=target,raw_samples=raw,limitations=['Sequential physical runs with matching driver, roster and window order; no per-feature causal attribution.','The actual Game continues to advance between windows; corresponding live snapshots need not be byte-identical.','Resource/audio/cache sample RPCs are outside the FPS counter interval; their counter deltas cover slightly wider time.','Physical resource counters include independent RAW audio reads. They are not exclusively visual IO.','No repeated-run confidence interval or guaranteed FPS bound is inferred.','Missing required observations remain null or fail their schema check; they are never treated as zero.'])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--baseline-build',type=Path,default=ROOT/'build/v030/perf-candidate7');p.add_argument('--candidate-build',type=Path,default=ROOT/'build/v030/perf-candidate8');p.add_argument('--window-seconds',type=float);p.add_argument('--duration-tolerance',type=float,default=.25);p.add_argument('--min-fps',type=float);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.parent.mkdir(parents=True,exist_ok=True)
 try:r=compare(a.baseline.resolve(),a.candidate.resolve(),a.baseline_build.resolve(),a.candidate_build.resolve(),a.window_seconds,a.duration_tolerance,a.min_fps)
 except Exception as error:r=dict(passed=False,read_only=True,USB_opened=False,error=type(error).__name__+': '+str(error),acceptance=dict(comparison_valid=False,baseline_health=None,candidate_health=None,all_live_windows_at_requested_min_fps=None))
 r['comparison_script_sha256']=sha(Path(__file__));out.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({k:r.get(k)for k in ['passed','acceptance','error']},ensure_ascii=False));return 0 if r['passed']else 1
if __name__=='__main__':sys.exit(main())
