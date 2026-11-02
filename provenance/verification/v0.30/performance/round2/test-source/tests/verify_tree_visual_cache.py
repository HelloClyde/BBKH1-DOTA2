"""Old/new actual tree compositor parity and local cache invalidation. No H1/FPS claim."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,struct,subprocess,zlib
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
P=128*128
FIELDS='open_attempts ready error heap_bytes metadata_bytes cache_bytes scratch_bytes read_requests read_bytes tile_hits tile_misses decoded_tiles patched_pixels depth_queries closes released'.split()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def mix(fg,bg,a):
 return (((((fg>>11)*a+(bg>>11)*(255-a)+127)//255)&31)<<11)|((((((fg>>5)&63)*a+((bg>>5)&63)*(255-a)+127)//255)&63)<<5)|((((fg&31)*a+(bg&31)*(255-a)+127)//255)&31)
def fixture(tint=0):
 xy=bytearray(8452);bounds=bytearray(16904);directory=bytearray(660*24)
 for i in range(6):struct.pack_into('<2H',xy,i*4,100+i*20,100)
 for i,b in enumerate([(0,0,128,128),(0,0,128,128),(128,0,256,128),(100,0,160,128),(512,0,640,128),(768,0,896,128)]):struct.pack_into('<4H',bounds,i*8,*b)
 bodies=[];original=[];at=41324
 for tile in range(2):
  pixel=np.arange(P,dtype=np.uint32);x=pixel%128
  rgb=((pixel*73+tile*233+tint)&65535).astype('<u2');depth=np.full(P,1000,dtype='<u2')
  owner=[];colors=[];depths=[];alphas=[]
  ids=[32767,0,32768,1,3,32769]if tile==0 else[32767,2,32770,3,32767,65535]
  zs=[80,100,150,200,250,300]if tile==0 else[90,120,180,250,350,65535]
  aa=[np.where(pixel%23==0,255,0),np.where(pixel%5,173,255),np.where(pixel%17<5,255,0),np.where(pixel%7,255,0),np.where((x>=100 if tile==0 else x<32)&(pixel%9!=0),127,0),np.where(pixel%11<3,255,0)]
  if tile:aa[4]=np.where(pixel%19==0,31,0);aa[5]=np.zeros(P,dtype=np.uint32)
  for k,(token,z)in enumerate(zip(ids,zs)):
   owner.append(np.full(P,token,dtype='<u2'));colors.append(((pixel*(k+3)*17+tile*811+k*131+tint)&65535).astype('<u2'));depths.append(np.full(P,z,dtype='<u2'));alphas.append(aa[k].astype('uint8'))
  raw=bytearray(struct.pack('<4I',0x31545048,6,P,0)+rgb.tobytes()+depth.tobytes())
  for k in range(6):raw.extend(owner[k].tobytes()+colors[k].tobytes()+depths[k].tobytes()+alphas[k].tobytes())
  # Original uncut map contains constant props and live crowns, no stumps.
  out=rgb.astype(np.uint32);d=depth.astype(np.uint32)
  for k in range(5,-1,-1):
   token=ids[k]
   if token==65535 or token&32768:continue
   alpha=alphas[k].astype(np.uint32);z=depths[k].astype(np.uint32);valid=(alpha>0)&(z<=d)
   color=colors[k].astype(np.uint32);out=np.where(valid,np.where(alpha==255,color,mix(color,out,alpha)),out);d=np.where(valid,np.minimum(z,d),d)
  original.append((out.astype('<u2').reshape(128,128),d.astype('<u2').reshape(128,128)))
  struct.pack_into('<6I',directory,tile*24,at,len(raw),len(raw),zlib.crc32(raw),0,6);at+=len(raw);bodies.append(raw)
 tables=xy+bounds+directory;h=[0x31525448,1,128,at,3840,2816,128,30,22,(-512)&0xffffffff,(-512)&0xffffffff,2113,128,8580,25484,41324,24,max(map(len,bodies)),max(map(len,bodies)),zlib.crc32(tables),1,6,*struct.unpack('<8I',bytes.fromhex('c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd')),0,0]
 head=bytearray(struct.pack('<32I',*h));struct.pack_into('<I',head,124,zlib.crc32(head))
 return head+tables+b''.join(bodies),np.hstack([q[0]for q in original]).copy(),np.hstack([q[1]for q in original]).copy()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 before=a.before.resolve();current=ROOT/'platform/tree_visual.c';inputs=[before,current,Path(__file__).resolve(),ROOT/'tests/tree_visual_cache_test.c',ROOT/'game/trees.c',ROOT/'platform/map_codec.c',ROOT/'platform/map_format.c'];identity={str(q):sha(q)for q in inputs};env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 data,rgb,depth=fixture();path=out/'fixture.H1T';path.write_bytes(data);data2,rgb2,depth2=fixture(12345);path2=out/'fixture2.H1T';path2.write_bytes(data2)
 results={};dumps={};checks=0
 for label,source in [('before',before),('after',current)]:
  wrapper=out/(label+'-wrapper.c');wrapper.write_text('#define TREE_VISUAL_IMPLEMENTATION '+json.dumps(source.as_posix())+'\n#include '+json.dumps((ROOT/'tests/tree_visual_cache_test.c').as_posix())+'\n',encoding='utf8')
  dll=out/(label+'.dll');cmd=['C:/msys64/ucrt64/bin/gcc.exe','-shared','-O2','-std=c99','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-Igame','-Iplatform',str(wrapper),'game/trees.c','platform/map_codec.c','platform/map_format.c','-o',str(dll)]
  c=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True);(out/(label+'-compile.txt')).write_text(c.stdout+c.stderr);assert c.returncode==0,c.stdout+c.stderr
  d=ctypes.CDLL(str(dll));d.cache_test_open.argtypes=[ctypes.c_char_p];d.cache_test_apply.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int];d.cache_test_stats.argtypes=[ctypes.c_void_p]
  def stats():
   v=(ctypes.c_uint32*16)();d.cache_test_stats(v);return dict(zip(FIELDS,v))
  trace=[];dump=bytearray()
  def apply(name,base_rgb=rgb,base_depth=depth):
   nonlocal checks
   rr=base_rgb.copy();zz=base_depth.copy();old=stats();d.cache_test_apply(rr.ctypes.data,zz.ctypes.data,-512,-512,256,128);new=stats();assert not new['error'];checks+=1;dump.extend(rr.tobytes()+zz.tobytes());trace.append(dict(name=name,delta={k:new[k]-old[k]for k in ['read_requests','read_bytes','tile_hits','tile_misses','decoded_tiles']},stats=new));return rr,zz
  assert d.cache_test_open(str(path).encode());apply('zero-cut-fast-path');assert stats()['cache_bytes']==0
  old=stats()
  for j in range(16):dump.extend(struct.pack('<i',d.cache_test_pick(-512+j,-512+j)))
  trace.append(dict(name='same-tile-picks',delta={k:stats()[k]-old[k]for k in ['read_requests','read_bytes']}))
  assert d.cache_test_cut(4,0);apply('remote-first-cut')
  assert d.cache_test_cut(0,10);apply('local-tile0-cut');apply('same-state-warm')
  assert d.cache_test_cut(2,20);apply('local-tile1-cut')
  assert d.cache_test_cut(5,25);apply('remote-cut-both-visible-tiles-warm')
  assert d.cache_test_cut(3,30);apply('cross-tile-cut')
  for tick,name in [(3600,'remote-regrow'),(3610,'tile0-regrow'),(3620,'tile1-regrow'),(3625,'remote-second-regrow'),(3630,'all-uncut-regrow')]:d.cache_test_tick(tick);apply(name)
  for j in range(32):dump.extend(struct.pack('<II',d.cache_test_depth(-512+j*7,-512+j*3,1000),d.cache_test_pick(-512+j*7,-512+j*3)&0xffffffff))
  assert d.cache_test_cut(0,4000);apply('cut-before-owner-switch');d.cache_test_switch_owner();apply('new-owner-same-cut-bits');d.cache_test_reset_same();apply('same-address-new-game')
  d.cache_test_close();assert d.cache_test_live()==0
  assert d.cache_test_open(str(path2).encode());assert d.cache_test_cut(0,10);apply('resource-reopen-different-pixels',rgb2,depth2);d.cache_test_close();assert not d.cache_test_live()
  # Preserve the pre-existing mandatory-allocation failure behavior/cleanup.
  for failure in (1,2):
   assert d.cache_test_open(str(path).encode());assert d.cache_test_cut(0,10);d.cache_test_fail_next(failure);rr=rgb.copy();zz=depth.copy();d.cache_test_apply(rr.ctypes.data,zz.ctypes.data,-512,-512,256,128);assert stats()['error'];d.cache_test_close();assert not d.cache_test_live();d.cache_test_fail_next(0);checks+=1
  (out/(label+'.bin')).write_bytes(dump);dumps[label]=dump;results[label]=dict(trace=trace,dll_sha256=sha(dll),output_sha256=sha(out/(label+'.bin')),peak_heap=d.cache_test_peak(),cleanup_heap=d.cache_test_live())
 assert dumps['before']==dumps['after'];assert all(sha(q)==v for q,v in zip(inputs,identity.values()))
 after={q['name']:q for q in results['after']['trace']};old={q['name']:q for q in results['before']['trace']}
 for name in ['same-state-warm','remote-cut-both-visible-tiles-warm','remote-regrow','remote-second-regrow']:assert after[name]['delta']['read_requests']==0
 assert after['same-tile-picks']['delta']['read_requests']<old['same-tile-picks']['delta']['read_requests']
 assert after['local-tile1-cut']['delta']['read_requests']<old['local-tile1-cut']['delta']['read_requests']
 report=dict(passed=True,scope=__doc__,source_current=True,source_sha256=identity,checks=checks,byte_equal_color_depth_pick_outputs=len(dumps['before']),runs=results,physical_h1_tested=False,actual_mips32=False)
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(passed=True,checks=checks,bytes_compared=len(dumps['before']),report=str(out/'verification.json'))))
if __name__=='__main__':main()
