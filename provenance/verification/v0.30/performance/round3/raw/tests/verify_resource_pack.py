"""Actual C H1R decoder: full stream equality, nested RAW audio and corrupt containers.

Host correctness/ownership tests only. No device or FPS measurement is performed.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,zlib
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def put(d,o,n):struct.pack_into('<I',d,o,n)
def get(d,o):return struct.unpack_from('<I',d,o)[0]
def crc(d):return zlib.crc32(d)&0xffffffff
def deflate(d):
 c=zlib.compressobj(9,zlib.DEFLATED,-15);return c.compress(d)+c.flush()
def fixture():
 raw0=bytes(range(64));raw1=bytes(range(256))*32;raw2=struct.pack('<256H',*(1000+i*3 for i in range(256)));raw3=b'vision'*40;raw4=b'tree0123456789ab'
 prev=0;zs=[]
 for x, in struct.iter_unpack('<H',raw2):
  d=(x-prev)&65535;prev=x;zs.append(((d<<1)^(-(d>>15)))&65535)
 transformed=bytes(x&255 for x in zs)+bytes(x>>8 for x in zs)
 raws=[raw0,raw1,raw2,raw3,raw4];bodies=[raw0,deflate(raw1),deflate(transformed),deflate(raw3),raw4];codecs=[0,1,2,1,0]
 streams=[raw0+raw1+raw2,bytes(range(128)),raw3,raw1+raw4];spans=[[(0,64,0),(64,8192,1),(8256,512,2)],[],[(0,len(raw3),3)],[(0,8192,1),(8192,16,4)]]
 d=bytearray(4224);d[:8]=b'DOTA2H1R';guide_offsets={0:536,2:560,3:584};pages={0:708,2:744,3:756};locations=[]
 for raw,body,codec in zip(raws,bodies,codecs):
  d.extend(bytes((-len(d))%4));at=len(d);locations.append(at);d.extend(body)
  rid=len(locations)-1;struct.pack_into('<5I',d,608+rid*20,at,len(body),len(raw),crc(raw),codec)
 d[4096:4224]=streams[1]
 for sid,stream in enumerate(streams):
  at=128+sid*96;struct.pack_into('<4I',d,at,sid,int(sid==1),len(stream),crc(stream));d[at+16:at+48]=hashlib.sha256(stream).digest()
  if sid==1:struct.pack_into('<2I',d,at+64,4096,len(stream));continue
  records=b''.join(struct.pack('<3I',*r) for r in spans[sid]);page=pages[sid];d[page:page+len(records)]=records
  struct.pack_into('<6I',d,guide_offsets[sid],0,len(stream),page,len(records),len(spans[sid]),crc(records));struct.pack_into('<4I',d,at+48,len(spans[sid]),1,guide_offsets[sid],crc(d[guide_offsets[sid]:guide_offsets[sid]+24]))
 struct.pack_into('<6I',d,512,0,5,608,100,crc(d[608:708]),0)
 h=[0]*32;h[2:19]=[1,128,len(d),4,128,96,5,1,512,24,20,512,16384,0,4096,crc(d[128:512]),crc(d[512:536])]
 for i in range(2,32):put(d,i*4,h[i])
 put(d,124,crc(d[:128]));return d,streams,locations
def resign(d,chunk=False,span=None):
 if chunk:put(d,528,crc(d[608:708]))
 if span is not None:
  g={0:536,2:560,3:584}[span];page=get(d,g+8);size=get(d,g+12);put(d,g+20,crc(d[page:page+size]));put(d,128+span*96+60,crc(d[g:g+24]))
 put(d,68,crc(d[128:512]));put(d,72,crc(d[512:536]));put(d,124,0);put(d,124,crc(d[:128]))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pack',type=Path);p.add_argument('--sources',type=Path,nargs=4,metavar=('SCENE','SOUND','VISION','TREE'));p.add_argument('--output',type=Path,required=True);p.add_argument('--gcc',default='C:/msys64/ucrt64/bin/gcc.exe');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();assert bool(a.pack)==bool(a.sources);out.mkdir(parents=True)
 inputs=[ROOT/n for n in ['tests/resource_pack_test.c','tests/verify_resource_pack.py','platform/resource_pack.c','platform/resource_pack.h','platform/map_format.c','platform/map_sidecar_format.h','platform/map_crc_table.h','platform/cache_memory.h','third_party/miniz/miniz_tinfl.c','third_party/miniz/miniz_tinfl.h','third_party/miniz/miniz_freestanding.h']];identity=lambda:{x.relative_to(ROOT).as_posix():sha(x) for x in inputs};before=identity()
 env=dict(os.environ);env['PATH']=str(Path(a.gcc).parent)+';'+env.get('PATH','');exe=out/'resource-test.exe';cmd=[a.gcc,'-O2','-std=c99','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-Iplatform','-Igame','tests/resource_pack_test.c','platform/resource_pack.c','platform/map_format.c','-o',str(exe)]
 r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True);(out/'compile.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 logs=[]
 def run(args):
  r=subprocess.run([str(exe),*map(str,args)],cwd=ROOT,env=env,capture_output=True,text=True);logs.append(dict(args=list(map(str,args)),returncode=r.returncode,stdout=r.stdout,stderr=r.stderr));assert r.returncode==0,r.stdout+r.stderr
 original,streams,loc=fixture();good=out/'valid.H1R';good.write_bytes(original);source_paths=[]
 for i,stream in enumerate(streams):q=out/f'section{i}.bin';q.write_bytes(stream);source_paths.append(q)
 run([good,*source_paths]);run(['optional-cache-failure',good,source_paths[0]])
 run(['optional-read-cache-failure',good,source_paths[0]])
 # A v1 compressed chunk can end exactly at EOF without the optional pad.
 # Move VISION's compressed chunk there; all logical streams stay identical.
 eof=bytearray(original);old,stored=struct.unpack_from('<2I',eof,668);tail=bytes(eof[old:old+stored]);start=len(eof);eof+=tail;assert len(eof)%4;put(eof,668,start);put(eof,16,len(eof));resign(eof,chunk=True);eof_path=out/'valid-final-compressed-no-padding.H1R';eof_path.write_bytes(eof);run([eof_path,*source_paths]);run(['optional-cache-failure',eof_path,source_paths[0]]);cases=[]
 def case(name,change,error=4,mount=False,sid=0,offset=0,length=1,resign_chunk=False,resign_span=None,header_only=False):
  d=bytearray(original);change(d)
  if resign_chunk or resign_span is not None:resign(d,chunk=resign_chunk,span=resign_span)
  elif header_only:put(d,124,0);put(d,124,crc(d[:128]))
  path=out/(name+'.H1R');path.write_bytes(d);run(['mount-failure' if mount else 'read-failure',path,error,sid,offset,length]);cases.append(name)
 case('header-crc',lambda d:d.__setitem__(80,1),mount=True)
 case('section-crc',lambda d:d.__setitem__(144,d[144]^1),mount=True)
 case('chunk-guide-crc',lambda d:d.__setitem__(528,d[528]^1),mount=True)
 case('span-guide-crc',lambda d:d.__setitem__(556,d[556]^1),mount=True)
 case('span-page-crc',lambda d:d.__setitem__(708,d[708]^1))
 case('chunk-page-crc',lambda d:d.__setitem__(608,d[608]^1))
 case('raw-chunk-crc',lambda d:d.__setitem__(loc[0],d[loc[0]]^1))
 case('canonical-deflate-crc',lambda d:put(d,640,get(d,640)^1),offset=64,resign_chunk=True)
 case('deflate-trailing',lambda d:put(d,632,get(d,632)+1),error=9,offset=64,resign_chunk=True)
 case('deflate-truncated',lambda d:put(d,632,get(d,632)-1),error=9,offset=64,resign_chunk=True)
 case('deflate-output-overrun',lambda d:put(d,636,8190),error=9,offset=64,resign_chunk=True)
 case('unknown-codec',lambda d:put(d,644,3),error=3,resign_chunk=True)
 case('delta-odd-length',lambda d:put(d,656,511),error=3,resign_chunk=True)
 case('chunk-physical-range',lambda d:put(d,608,0x7ffffffc),error=3,resign_chunk=True)
 case('span-id-range',lambda d:put(d,716,5),error=3,resign_span=0)
 case('span-noncontiguous',lambda d:put(d,720,65),error=3,resign_span=0)
 if a.pack:run([a.pack.resolve(),*(p.resolve() for p in a.sources)])
 report=dict(passed=True,source_current=before==identity(),source_sha256=before,exe_sha256=sha(exe),malformed_cases=cases,optional_cache_allocation_failure_fallback=True,optional_cache_reenabled_after_remount=True,no_padding_eof=dict(sha256=sha(eof_path),physical_tail_bytes=stored,rounded_bytes=(stored+3)&~3,file_bytes=len(eof)),full_pack_sha256=sha(a.pack) if a.pack else None,full_source_sha256=[sha(p) for p in a.sources] if a.sources else [],tests=logs,physical_h1_tested=False,scope=__doc__);assert report['source_current'];(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,source_current=True,malformed_cases=len(cases),full_stream_equality=bool(a.pack),report=str(out/'verification.json'))))
if __name__=='__main__':main()
