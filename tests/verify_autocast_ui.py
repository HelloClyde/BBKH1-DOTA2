"""Actual native autocast key/touch routing and HUD rendering on host services."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/'tests/autocast_ui.c',*[ROOT/'platform'/n for n in ['render.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c','tree_visual.c','resource_pack.c','match_storage.c']],*sorted((ROOT/'game').glob('*.c'))]
 inputs=[*sources,Path(__file__),ROOT/'tests/vision_ui.c',ROOT/'platform/native.c',*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h')),*sorted((ROOT/'third_party/miniz').glob('*.c')),*sorted((ROOT/'third_party/miniz').glob('*.h'))];identity=lambda:{q.relative_to(ROOT).as_posix():sha(q)for q in inputs};before=identity();env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'autocast-ui.exe'
 flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-DDOTA_RESOURCE_PACK','-Igame','-Iplatform','-Isdk/sdk/include']
 c=subprocess.run(list(map(str,['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe])),cwd=ROOT,env=env,capture_output=True,text=True);(out/'compile.txt').write_text(c.stdout+c.stderr);assert not c.returncode,c.stdout+c.stderr
 c=subprocess.run([str(exe),str(a.bundle.resolve()),str(out)],cwd=ROOT,env=env,capture_output=True,text=True);(out/'output.txt').write_text(c.stdout+c.stderr);assert not c.returncode,c.stdout+c.stderr
 report=json.loads(re.search(r'RESULT (.+)',c.stdout)[1]);report.update(scope=__doc__,source_sha256=before,source_current=before==identity(),binary_sha256=sha(exe),bundle_sha256=sha(a.bundle),USB_opened=False);assert report['source_current']
 for name in ['autocast-on','autocast-off']:Image.open(out/(name+'.ppm')).save(out/(name+'.png'))
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='source_sha256'}))
if __name__=='__main__':main()
