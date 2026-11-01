"""Focused actual-C host tests; no device, audio playback or FPS claims."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'build/tests/vision-ui-run-1');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists()
 sources=[ROOT/'tests/vision_ui.c',*[ROOT/'platform'/n for n in ['render.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c']],*sorted((ROOT/'game').glob('*.c'))]
 inputs=[*sources,ROOT/'platform/native.c',Path(__file__),*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h')),*sorted((ROOT/'sdk/sdk/include').glob('*.h')),ROOT/'platform/dota_audio.c']
 identities=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in sorted(set(inputs))};before=identities();out.mkdir(parents=True)
 sidecar=ROOT/'assets/runtime/VISION.H1V';data=bytearray(sidecar.read_bytes());bad_header=bytearray(data);bad_header[16]^=1;(out/'bad-header.H1V').write_bytes(bad_header);bad_crc=bytearray(data);bad_crc[500]^=1;(out/'bad-crc.H1V').write_bytes(bad_crc);(out/'truncated.H1V').write_bytes(data[:-1])
 exe=out/'vision-ui.exe';env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','')
 flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-Igame','-Iplatform','-Isdk/sdk/include']
 report=dict(passed=False,source_sha256=before,physical_h1_tested=False,actual_mips32=False,scope='Actual native input/render/provider/core host execution, modeled SDK keyboard/timer and audio UI. Synthetic flat-ground gameplay fixture; exact source-derived sidecar loader/validation/lifetime independently checked. No hardware or frame-rate claim.')
 try:
  for step,cmd in [('compile',['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe]),('fixture',[exe,ROOT/'assets/runtime/SCENE.H1P',sidecar,out/'bad-header.H1V',out/'bad-crc.H1V',out/'truncated.H1V',out])]:
   r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=180);(out/(step+'.txt')).write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,r.stdout+r.stderr
  report.update(json.loads(re.search(r'RESULT (.+)',r.stdout)[1]));report['source_current']=before==identities();assert report['source_current'];report['executable_sha256']=sha(exe);report['sidecar_sha256']=sha(sidecar)
  for f in out.glob('*.ppm'):
   im=Image.open(f);assert im.size==(480,272);im.save(f.with_suffix('.png'))
  report['screenshots']={f.name:sha(f) for f in sorted(out.glob('*.png'))}
 except Exception as e:report.update(passed=False,error=str(e));(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');raise
 (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({k:report[k] for k in ['passed','checks','cases','workspace_bytes','source_current']}))
if __name__=='__main__':main()
