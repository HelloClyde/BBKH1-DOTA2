"""One actual renderer binary: old files versus unified H1R, original vision and tree state."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,subprocess
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--legacy',type=Path,required=True);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/'tests/resource_renderer_ui.c',*[ROOT/'platform'/n for n in ['render.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c','tree_visual.c','resource_pack.c','match_storage.c']],*sorted((ROOT/'game').glob('*.c'))]
 inputs=[*sources,Path(__file__),ROOT/'tests/vision_ui.c',ROOT/'platform/native.c',*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h')),*sorted((ROOT/'third_party/miniz').glob('*.c')),*sorted((ROOT/'third_party/miniz').glob('*.h'))];identity=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in inputs};before=identity();assets={n:sha(a.legacy/n) for n in ['SCENE.H1P','TREE.H1T','VISION.H1V']};assets['DOTA2.H1R']=sha(a.bundle);report=dict(passed=False,scope=__doc__,physical_h1_tested=False,source_sha256=before,asset_sha256=assets)
 flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-DDOTA_RESOURCE_PACK','-Igame','-Iplatform','-Isdk/sdk/include'];env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'renderer.exe'
 try:
  r=subprocess.run(list(map(str,['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe])),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=600);(out/'compile.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
  runs={}
  for mode in ['legacy','unified']:
   dest=out/mode;dest.mkdir();cmd=[exe,mode,a.bundle.resolve() if mode=='unified' else (a.legacy/'SCENE.H1P').resolve(),(a.legacy/'TREE.H1T').resolve(),(a.legacy/'VISION.H1V').resolve(),dest]
   r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=600);(dest/'run.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr;runs[mode]=json.loads(re.search(r'RESULT (.+)',r.stdout)[1])
   for name in ['full-ui','standing','cut','two-cut','regrown']:Image.open(dest/(name+'.ppm')).save(dest/(name+'.png'))
  comparisons={}
  for name in ['frames.bin','depths.bin','frames.csv','full-ui.ppm','standing.ppm','cut.ppm','two-cut.ppm','regrown.ppm']:
   old=(out/'legacy'/name).read_bytes();new=(out/'unified'/name).read_bytes();assert old==new,name;comparisons[name]=dict(bytes=len(new),sha256=hashlib.sha256(new).hexdigest(),exact=True)
  frames=np.fromfile(out/'unified/frames.bin',dtype='<u2').reshape(97,192,480);depths=np.fromfile(out/'unified/depths.bin',dtype='<u2').reshape(97,192,480);rows=list(csv.DictReader((out/'unified/frames.csv').open()));compared=mismatch=0;depth_compared=depth_mismatch=0
  for i in range(1,65):
   dx=int(rows[i]['origin_x'])-int(rows[i-1]['origin_x']);dy=int(rows[i]['origin_y'])-int(rows[i-1]['origin_y']);x0,x1=max(0,-dx),min(480,480-dx);y0,y1=max(0,-dy),min(192,192-dy);now=frames[i,y0:y1,x0:x1];old=frames[i-1,y0+dy:y1+dy,x0+dx:x1+dx];compared+=now.size;mismatch+=int(np.count_nonzero(now!=old));nd=depths[i,y0:y1,x0:x1];od=depths[i-1,y0+dy:y1+dy,x0+dx:x1+dx];depth_compared+=nd.size;depth_mismatch+=int(np.count_nonzero(nd!=od))
  assert not mismatch and not depth_mismatch,(mismatch,depth_mismatch)
  report.update(runs=runs,comparisons=comparisons,camera_world_color_pixels=compared,camera_world_color_mismatches=mismatch,camera_world_depth_pixels=depth_compared,camera_world_depth_mismatches=depth_mismatch,source_current=before==identity());report['passed']=report['source_current'];assert report['passed'],'Compilation inputs changed during run; retain this report and rerun after freeze.'
 except Exception as e:report.update(error=str(e));raise
 finally:(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:v for k,v in report.items() if k not in ['source_sha256','asset_sha256','comparisons']}))
if __name__=='__main__':main()
