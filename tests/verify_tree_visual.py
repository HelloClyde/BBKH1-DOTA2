"""Actual TREE.H1T C reader: lossless codecs, overlap, state invalidation and failure cleanup."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,zlib
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
P=16384
def rle(a):
 out=bytearray();at=0
 while at<len(a):
  run=0
  while at+run<len(a) and not a[at+run] and run<128:run+=1
  if run>=2:out.append(128+run-1);at+=run;continue
  start=at;at+=1
  while at<len(a) and at-start<128:
   if a[at]==0 and at+1<len(a) and a[at+1]==0:break
   at+=1
  out.append(at-start-1);out.extend(a[start:at])
 return out
def transformed(raw,layers):
 out=bytearray(raw)
 for start in [16,16+P*2,*[16+P*4+k*P*7+j*P*2 for k in range(layers) for j in range(3)]]:
  a=np.frombuffer(raw,dtype='<u2',count=P,offset=start).astype('int32');d=(a-np.r_[0,a[:-1]])&65535;d=np.where(d>32767,d-65536,d);zz=((d<<1)^(d>>31)).astype('uint16')
  out[start:start+P*2]=(zz&255).astype('uint8').tobytes()+(zz>>8).astype('uint8').tobytes()
 return rle(out)
def pack(codec):
 rgb=np.full(P,0x1234,dtype='<u2');dep=np.full(P,500,dtype='<u2');dep[1]=80;rgb[1]=0xffff;dep[2]=50;rgb[2]=0x001f
 raw=bytearray(struct.pack('<4I',0x31545048,4,P,0)+rgb.tobytes()+dep.tobytes())
 for k in range(4):
  owner=np.full(P,65535,dtype='<u2');color=np.zeros(P,dtype='<u2');depth=np.full(P,65535,dtype='<u2');alpha=np.zeros(P,dtype='uint8')
  if k==0:
   for at,token,col,z in [(0,0,0x07e0,100),(1,32767,0xffff,80),(2,0,0x07e0,100)]:owner[at]=token;color[at]=col;depth[at]=z;alpha[at]=255
  if k==1:owner[4]=32768;color[4]=0x8410;depth[4]=150;alpha[4]=255
  if k==2:owner[0]=1;color[0]=0xf800;depth[0]=200;alpha[0]=255
  raw.extend(owner.tobytes()+color.tobytes()+depth.tobytes()+alpha.tobytes())
 data=raw if not codec else transformed(raw,4)
 xy=bytearray(8452);bbox=bytearray(16904);struct.pack_into('<8h',bbox,0,0,0,128,128,0,0,128,128)
 dirs=bytearray(660*24);struct.pack_into('<6I',dirs,0,41324,len(data),len(raw),zlib.crc32(raw),codec,5)
 tables=xy+bbox+dirs;h=[0x31525448,1,128,41324+len(data),3840,2816,128,30,22,(-512)&0xffffffff,(-512)&0xffffffff,2113,128,8580,25484,41324,24,len(raw),len(data),zlib.crc32(tables),1,4,*struct.unpack('<8I',bytes.fromhex('c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd')),0,0]
 header=bytearray(struct.pack('<32I',*h));struct.pack_into('<I',header,124,zlib.crc32(header));return header+tables+data
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/p for p in ['tests/tree_visual_test.c','platform/tree_visual.c','platform/map_codec.c','platform/map_format.c','game/trees.c']]
 inputs=[*sources,Path(__file__).resolve(),*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h'))];identity=lambda:{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs};before=identity();results=[]
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'tree-visual.exe'
 cmd=['C:/msys64/ucrt64/bin/gcc.exe','-O2','-std=c99','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-Igame','-Iplatform',*map(str,sources),'-o',str(exe)]
 r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True);(out/'compile.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 for codec in (0,1):
  path=out/f'codec{codec}.H1T';path.write_bytes(pack(codec))
  for mode in (0,2):
   r=subprocess.run([str(exe),str(path),str(mode)],cwd=ROOT,capture_output=True,text=True);results.append(dict(codec=codec,mode=mode,passed=not r.returncode,output=r.stdout+r.stderr));assert not r.returncode,results[-1]
  corrupt=bytearray(path.read_bytes());corrupt[128]^=1;bad=out/f'bad{codec}.H1T';bad.write_bytes(corrupt)
  r=subprocess.run([str(exe),str(bad),'1'],capture_output=True,text=True);results.append(dict(codec=codec,mode='tables-crc',passed=not r.returncode,output=r.stdout+r.stderr));assert not r.returncode,results[-1]
 report=dict(passed=True,runs=results,source_sha256=before,source_current=before==identity(),physical_h1_tested=False);assert report['source_current'];(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results))
if __name__=='__main__':main()
