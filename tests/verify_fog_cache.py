"""Actual full renderer cache on/off/heap-failure equivalence, no H1 timing."""
from pathlib import Path
import hashlib,json,os,re,subprocess,argparse
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/'tests/fog_cache_ui.c',*[ROOT/'platform'/n for n in ['render.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c']],*sorted((ROOT/'game').glob('*.c'))]
 if (ROOT/'platform/tree_visual.c').exists():sources.append(ROOT/'platform/tree_visual.c')
 inputs=[*sources,ROOT/'tests/vision_ui.c',ROOT/'platform/native.c',Path(__file__),*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h'))]
 identities=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in sorted(set(inputs))};before=identities();report=dict(passed=False,source_sha256=before,physical_h1_tested=False,actual_mips32=False,scope=__doc__)
 exe=out/'fog-cache.exe';env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','')
 flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,--wrap=dota_sight_fog_state','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-Igame','-Iplatform','-Isdk/sdk/include']
 try:
  commands=[('compile',['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe])]+[(name,[exe,ROOT/'assets/runtime/SCENE.H1P',str(mode),out/(name+'.bin')]) for mode,name in [(0,'reference'),(1,'cache'),(2,'heap-failure')]]
  runs={}
  for name,cmd in commands:
   r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=240);(out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,r.stdout+r.stderr
   if name!='compile':runs[name]=json.loads(re.search(r'RESULT (.+)',r.stdout)[1])
  raw=(out/'reference.bin').read_bytes();assert raw==(out/'cache.bin').read_bytes();assert raw==(out/'heap-failure.bin').read_bytes();assert len(raw)==196*(480*272*2+480*192*2+20+4+8)
  report.update(passed=True,frames=196,full_framebuffer_pixels_compared=196*480*272*2,depth_pixels_compared=196*480*192*2,full_framebuffers_equal=True,depth_equal=True,world_query_count_and_order_and_coordinates_equal=True,fog_stats_equal=True,heap_failure_equal=True,runs=runs,digest=hashlib.sha256(raw).hexdigest(),source_current=before==identities());assert report['source_current']
 except Exception as e:report.update(passed=False,error=str(e));raise
 finally:(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps({k:report[k] for k in ['passed','frames','source_current','runs']}))
if __name__=='__main__':main()
