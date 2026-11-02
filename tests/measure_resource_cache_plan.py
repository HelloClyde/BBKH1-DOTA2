"""Replay exact MIPS logical IO against the actual C reader and canonical bytes.

This measures cache counters only. It never opens USB or reports physical FPS.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 source=ROOT/'platform/resource_pack.c';original=source.read_text();source_hash=sha(source);pack=ROOT/'assets/runtime/DOTA2.H1R';refs=[ROOT/'build/v030/legacy'/n for n in ['SCENE.H1P','SOUND.H1A','VISION.H1V']]+[ROOT/'build/v030/tree-sparse/TREE.H1T'];results={};env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 for index in [2,4,8]:
  for chunks in [2,4,8]:
   name=f'i{index}-c{chunks}';text=original.replace('#include "../third_party/miniz/miniz_tinfl.c"','#include "'+(ROOT/'third_party/miniz/miniz_tinfl.c').as_posix()+'"');assert text.count('#define INDEX_CACHE_SLOTS 2u')==1 and text.count('#define CHUNK_CACHE_SLOTS 2u')==1;text=text.replace('#define INDEX_CACHE_SLOTS 2u',f'#define INDEX_CACHE_SLOTS {index}u').replace('#define CHUNK_CACHE_SLOTS 2u',f'#define CHUNK_CACHE_SLOTS {chunks}u');path=out/(name+'.c');path.write_text(text);exe=out/(name+'.exe')
   cmd=['C:/msys64/ucrt64/bin/gcc.exe','-O2','-std=c99','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-Iplatform',ROOT/'tests/replay_resource_cache_plan.c',path,ROOT/'platform/map_format.c','-o',exe]
   r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True);assert not r.returncode,r.stderr
   r=subprocess.run(list(map(str,[exe,pack,a.plan.resolve(),*refs])),cwd=ROOT,env=env,capture_output=True,text=True);assert not r.returncode,r.stdout+r.stderr;results[name]=[json.loads(line)for line in r.stdout.splitlines()];print(name,json.dumps(results[name]),flush=True)
 report=dict(passed=True,source_current=sha(source)==source_hash,production_source_modified=False,USB_opened=False,physical_h1_tested=False,scope=__doc__,source_sha256=source_hash,driver_sha256=sha(Path(__file__)),harness_sha256=sha(ROOT/'tests/replay_resource_cache_plan.c'),pack_sha256=sha(pack),canonical_sha256={str(p.relative_to(ROOT)):sha(p)for p in refs},plan_sha256=sha(a.plan.resolve()),results=results,limitations='Reader replay preserves logical order and caches across phases. The modeled trace already includes upper renderer cache hits/misses. Audio streaming calls and physical H1 scheduler/latency are absent. Every returned byte is compared with canonical streams; no asset bytes change.')
 (out/'measurement.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
