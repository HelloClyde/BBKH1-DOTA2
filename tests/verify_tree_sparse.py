"""Actual C TREE v1/v2 canonical equality, bounded scratch and malformed-frame rejection."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,struct,subprocess,zlib,statistics,time
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--legacy',type=Path,required=True);p.add_argument('--sparse',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 inputs=[ROOT/n for n in ['tests/tree_sparse_test.c','platform/tree_visual.c','platform/tree_visual.h','platform/map_codec.c','platform/map_format.c','game/trees.c','tools/pack_tree_sparse.py']];inputs+=[Path(__file__).resolve()];identity=lambda:{x.relative_to(ROOT).as_posix():sha(x) for x in inputs};before=identity()
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');lib=out/'decoder.dll';cmd=['C:/msys64/ucrt64/bin/gcc.exe','-O2','-std=c99','-Wall','-Wextra','-Werror','-shared','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-Igame','-Iplatform','tests/tree_sparse_test.c','platform/map_codec.c','platform/map_format.c','game/trees.c','-o',str(lib)];r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True);(out/'compile.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 dll=ctypes.CDLL(str(lib));dll.tree_test_open.argtypes=[ctypes.c_char_p];dll.tree_test_snapshot.argtypes=[ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint];dll.tree_test_repeat.argtypes=[ctypes.c_uint,ctypes.c_uint];buffer=ctypes.create_string_buffer(16*1024*1024);expected={};total=0;timings={}
 def benchmark(label):
  timings[label]=[]
  for tile in [319,346,347,376,377,566]:
   values=[]
   for _ in range(3):
    start=time.perf_counter();assert dll.tree_test_repeat(tile,30);values.append((time.perf_counter()-start)/30*1000)
   timings[label].append(dict(tile=tile,median_ms=statistics.median(values)))
 assert dll.tree_test_open(os.fsencode(a.legacy.resolve()))
 for tile in range(660):
  n=dll.tree_test_snapshot(tile,buffer,len(buffer));assert n>=0
  if n:expected[tile]=buffer.raw[:n]
 old=dict(scratch_bytes=dll.tree_test_scratch(),extra_bytes=dll.tree_test_extra(),total_heap=dll.tree_test_live());benchmark('legacy');dll.tree_test_close();assert not dll.tree_test_live()
 assert dll.tree_test_open(os.fsencode(a.sparse.resolve()))
 for tile in range(660):
  n=dll.tree_test_snapshot(tile,buffer,len(buffer));assert n>=0
  if tile in expected:assert n==len(expected[tile]) and buffer.raw[:n]==expected[tile],tile;total+=n
  else:assert n==0,tile
 new=dict(scratch_bytes=dll.tree_test_scratch(),extra_bytes=dll.tree_test_extra(),total_heap=dll.tree_test_live());assert new['extra_bytes']==81920;benchmark('sparse');dll.tree_test_close();assert not dll.tree_test_live()
 original=bytearray(a.sparse.read_bytes());tile=next(iter(expected));do=25484+tile*24;start,stored,raw,crc,codec,cov=struct.unpack_from('<6I',original,do);head=start+8+65536;w,h=struct.unpack_from('<2H',original,head+6);second=head+16+w*h*5
 def fix(d):
  struct.pack_into('<I',d,76,zlib.crc32(d[128:41324])&0xffffffff);struct.pack_into('<I',d,124,0);struct.pack_into('<I',d,124,zlib.crc32(d[:128])&0xffffffff)
 cases=[]
 def case(name,change):
  d=bytearray(original);change(d);fix(d);path=out/(name+'.H1T');path.write_bytes(d);assert dll.tree_test_open(os.fsencode(path));assert dll.tree_test_snapshot(tile,buffer,len(buffer))==-1,name;assert dll.tree_visual_error()!=0;dll.tree_test_close();assert not dll.tree_test_live();cases.append(name);path.unlink()
 case('reserved-frame',lambda d:struct.pack_into('<H',d,head+10,1))
 case('width-out-of-tile',lambda d:struct.pack_into('<H',d,head+6,129))
 case('duplicate-owner',lambda d:struct.pack_into('<H',d,second,struct.unpack_from('<H',d,head)[0]))
 case('zero-layer-count',lambda d:struct.pack_into('<I',d,start+4,0))
 case('too-many-layers',lambda d:struct.pack_into('<I',d,start+4,129))
 case('truncated-last-frame',lambda d:struct.pack_into('<I',d,do+4,stored-1))
 case('base-crc',lambda d:d.__setitem__(start+8,d[start+8]^1))
 alpha=start+8+65536+16+w*h*4;valid=next(i for i in range(w*h) if original[alpha+i]);depth=head+16+w*h*2+valid*2
 case('invalid-active-depth',lambda d:struct.pack_into('<H',d,depth,65535))
 report=dict(passed=True,source_current=before==identity(),source_sha256=before,legacy_sha256=sha(a.legacy),sparse_sha256=sha(a.sparse),all_directory_entries=660,nonempty_tiles=len(expected),byte_exact_canonical_comparison_bytes=total,legacy_memory=old,sparse_memory=new,malformed_tiles_rejected=cases,host_actual_decoder_timings=timings,timing_scope='Actual C load_tile, warm host standalone file IO, reconstruction/inverse transform and canonical CRC; median 3x30. Excludes outer H1R decoding and is not H1 FPS.',physical_h1_tested=False)
 assert report['source_current'];(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))
if __name__=='__main__':main()
