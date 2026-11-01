"""Real-asset cut/regrow depth, immediate fog-cache invalidation and camera stability."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,subprocess
import numpy as np
from PIL import Image,ImageChops,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--tree-pack',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/'tests/tree_fog_ui.c',*[ROOT/'platform'/n for n in ['render.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c','tree_visual.c']],*sorted((ROOT/'game').glob('*.c'))]
 inputs=[*sources,Path(__file__),ROOT/'tests/vision_ui.c',ROOT/'platform/native.c',*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h'))];identity=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in inputs};before=identity();assets={n:sha(p) for n,p in [('SCENE.H1P',ROOT/'assets/runtime/SCENE.H1P'),('MAP12.H1C',ROOT/'assets/runtime/MAP12.H1C'),('TREE.H1T',a.tree_pack)]};report=dict(passed=False,scope=__doc__,physical_h1_tested=False,source_sha256=before,asset_sha256=assets)
 flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-Igame','-Iplatform','-Isdk/sdk/include'];env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'tree-fog.exe'
 try:
  for name,cmd in [('compile',['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe]),('run',[exe,ROOT/'assets/runtime/SCENE.H1P',a.tree_pack.resolve(),out])]:
   r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=600);(out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stdout+r.stderr
   if name=='run':report.update(json.loads(re.search(r'RESULT (.+)',r.stdout)[1]))
  frames=np.fromfile(out/'frames.bin',dtype='<u2').reshape(65,192,480);rows=list(csv.DictReader((out/'frames.csv').open()));compared=mismatched=0;gif=[]
  for i,f in enumerate(frames):
   if i:
    dx=int(rows[i]['origin_x'])-int(rows[i-1]['origin_x']);dy=int(rows[i]['origin_y'])-int(rows[i-1]['origin_y']);x0,x1=max(0,-dx),min(480,480-dx);y0,y1=max(0,-dy),min(192,192-dy);now=f[y0:y1,x0:x1];old=frames[i-1,y0+dy:y1+dy,x0+dx:x1+dx];compared+=now.size;mismatched+=int(np.count_nonzero(now!=old))
   p=f.astype(np.uint32);rgb=np.stack(((p>>11)*255//31,((p>>5)&63)*255//63,(p&31)*255//31),axis=-1).astype('uint8');gif.append(Image.fromarray(rgb).resize((960,384),Image.Resampling.NEAREST))
  gif[0].save(out/'cut-camera.gif',save_all=True,append_images=gif[1:],duration=70,loop=0)
  for name in ['standing','cut','two-cut','regrown']:Image.open(out/(name+'.ppm')).save(out/(name+'.png'))
  standing=Image.open(out/'standing.png').convert('RGB');cut=Image.open(out/'cut.png').convert('RGB');box=ImageChops.difference(standing,cut).getbbox();assert box
  x0,y0,x1,y1=box;box=max(0,x0-14),max(24,y0-14),min(480,x1+14),min(216,y1+14);w,h=box[2]-box[0],box[3]-box[1];sheet=Image.new('RGB',(w*9,h*3+25))
  for i,name in enumerate(['standing','cut','regrown']):
   sheet.paste(Image.open(out/(name+'.png')).crop(box).resize((w*3,h*3),Image.Resampling.NEAREST),(i*w*3,25));ImageDraw.Draw(sheet).text((i*w*3+5,6),name,fill='white')
  sheet.save(out/'cut-regrow-closeup.png')
  report.update(compared_pixels=compared,mismatched_pixels=mismatched,source_current=before==identity());report['passed']=not mismatched and report['source_current'];assert report['passed'],dict(mismatched=mismatched,source_current=report['source_current'])
 except Exception as e:report.update(passed=False,error=str(e));raise
 finally:(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 print(json.dumps({k:v for k,v in report.items() if k not in ['source_sha256','asset_sha256']}))
if __name__=='__main__':main()
