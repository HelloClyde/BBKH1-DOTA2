"""Actual game C tree/item/collision/LOS outcomes, host only; no USB."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,subprocess,sys,struct
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'.tools/dota3d-deps'))
import numpy as np
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);(out/'objects').mkdir()
 src=ROOT/'provenance/tree-navigation';trees=json.loads((ROOT/'provenance/vision/trees.json').read_bytes())['trees'];(out/'xy.bin').write_bytes(b''.join(struct.pack('<HH',t['x_q8']//256,t['y_q8']//256) for t in trees))
 def plane(n):return np.unpackbits(np.frombuffer((src/n).read_bytes(),np.uint8),bitorder='little').reshape(256,256).astype(bool)
 base=plane('gnv-walk.bin');old=plane('legacy-standing-walk.bin');info=json.loads((src/'source-buildings.json').read_bytes());yy,xx=np.mgrid[:256,:256];wx=xx*16+8;wy=yy*16+8;static=base.copy()
 def hull(r):
  x,y=r['world_xy'];return np.maximum(abs(wx-x)-8,0)**2+np.maximum(abs(wy-y)-8,0)**2<=r['collision_radius_world']**2
 for r in info['static_barracks']:static&=~hull(r)
 for cell in info['source_passages']['cells']:static.flat[cell]=True
 for r in info['dynamic_towers']+info['dynamic_fillers']:old&=~hull(r);static&=~hull(r)
 for n,plane_ in [('before.u8',old),('allclear.u8',base),('living.u8',static)]: (out/n).write_bytes(plane_.astype(np.uint8).tobytes())
 masks={}
 for radius in [16,22]:
  full=np.zeros_like(base);local=np.zeros_like(base)
  for t in trees:
   x,y=t['x_q8']//256,t['y_q8']//256;full|=(wx-x)**2+(wy-y)**2<=radius*radius
   for cy in range(y//16-2,y//16+2):
    for cx in range(x//16-2,x//16+2):
     if 0<=cx<256 and 0<=cy<256 and (cx*16+8-x)**2+(cy*16+8-y)**2<=radius*radius:local[cy,cx]=True
  assert np.array_equal(full,local);masks[radius]=np.packbits(local.ravel(),bitorder='little').tobytes()
 vision=(ROOT/'assets/runtime/VISION.H1V').read_bytes();assert masks[16]==vision[64+131072:]
 sources=[ROOT/'tests/trees_test.c',*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/item_assets.c',ROOT/'platform/baked_io.c'];inputs=[*sources,*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'private/generated').glob('*nav.h')),Path(__file__).resolve(),*sorted(src.glob('*.bin')),src/'source-buildings.json',ROOT/'provenance/vision/trees.json'];before={x.relative_to(ROOT).as_posix():sha(x) for x in inputs};env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');logs=[]
 def run(cmd):
  r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True);logs.append(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
 def compile_(s):
  o=out/'objects'/(s.stem+'.o');run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-c',s,'-o',o]);return o
 with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_,sources))
 exe=out/'trees.exe';run(['C:/msys64/ucrt64/bin/gcc.exe',*objects,'-o',exe]);(out/'compile.txt').write_text(''.join(logs));r=subprocess.run([str(exe),str(ROOT/'assets/runtime/SCENE.H1P'),str(out/'xy.bin'),str(out/'before.u8'),str(out/'allclear.u8'),str(out/'living.u8')],env=env,capture_output=True,text=True);(out/'output.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);report=json.loads(r.stdout);report.update(source_current=before=={x.relative_to(ROOT).as_posix():sha(x) for x in inputs},source_sha256=before,binary_sha256=sha(exe),actual_mips32=False,USB_opened=False,standing_vision_mask_byte_equal_VISION=True,standing_local_nav_raster_byte_equal_original=True);(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');assert r.returncode==0 and report['passed'] and report['source_current']
if __name__=='__main__':main()
