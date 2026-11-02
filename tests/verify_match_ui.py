"""Actual C result/history renderer and native keyboard/touch; local fixtures only."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,default=ROOT/'assets/runtime/DOTA2.H1R');p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/'tests/match_ui_preview.c',*[ROOT/'platform'/n for n in ['render.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c','tree_visual.c','resource_pack.c','match_storage.c']],*sorted((ROOT/'game').glob('*.c'))]
 inputs=set(sources)|{Path(__file__),ROOT/'tests/vision_ui.c',ROOT/'platform/native.c'}
 for folder in ('game','platform','private/generated','third_party/miniz'):inputs.update((ROOT/folder).glob('*.h'))
 identity=lambda:{p.relative_to(ROOT).as_posix():sha(p)for p in sorted(inputs)};before=identity();report=dict(passed=False,scope=__doc__,physical_h1_tested=False,fixture_only=True,source_sha256=before,bundle_sha256=sha(a.bundle));env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'match-ui.exe'
 try:
  flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-DDOTA_RESOURCE_PACK','-DMATCH_STORAGE_HOST','-Igame','-Iplatform','-Isdk/sdk/include']
  r=subprocess.run(list(map(str,['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe])),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=180);(out/'compile.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
  r=subprocess.run([str(exe),str(a.bundle.resolve()),str(out)],cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=180);(out/'run.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
  report['run']=json.loads(re.search(r'RESULT (.+)',r.stdout)[1]);report['screenshots']=[]
  for ppm in sorted(out.glob('*.ppm')):
   image=Image.open(ppm);assert image.size==(480,272);png=ppm.with_suffix('.png');image.save(png);report['screenshots'].append(dict(path=png.relative_to(ROOT).as_posix(),sha256=sha(png),width=480,height=272,fixture_only=True))
  report['source_current']=identity()==before;assert report['source_current'];report['passed']=True
 except Exception as exc:report['error']=str(exc);raise
 finally:(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(passed=True,run=report['run'],screenshots=len(report['screenshots']),source_current=True,physical_h1_tested=False)))
if __name__=='__main__':main()
