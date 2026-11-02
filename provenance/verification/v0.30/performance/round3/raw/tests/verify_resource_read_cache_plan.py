"""Verify the selected optional reader cache against the measured frozen matrix."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plan',type=Path,required=True);p.add_argument('--matrix',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 sources=[ROOT/n for n in ['platform/resource_pack.c','platform/resource_pack.h','tests/replay_resource_cache_plan.c','tests/verify_resource_read_cache_plan.py']];identity=lambda:{str(p.relative_to(ROOT)):sha(p)for p in sources};before=identity();env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'reader.exe'
 cmd=['C:/msys64/ucrt64/bin/gcc.exe','-O2','-std=c99','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-DUSE_RESERVED_READ_CACHE','-Iplatform',ROOT/'tests/replay_resource_cache_plan.c',ROOT/'platform/resource_pack.c',ROOT/'platform/map_format.c','-o',exe]
 r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True);assert not r.returncode,r.stderr
 pack=ROOT/'assets/runtime/DOTA2.H1R';refs=[ROOT/'build/v030/legacy'/n for n in ['SCENE.H1P','SOUND.H1A','VISION.H1V']]+[ROOT/'build/v030/tree-sparse/TREE.H1T'];r=subprocess.run(list(map(str,[exe,pack,a.plan.resolve(),*refs])),cwd=ROOT,env=env,capture_output=True,text=True);assert not r.returncode,r.stdout+r.stderr;results=[json.loads(x)for x in r.stdout.splitlines()];matrix=json.loads(a.matrix.read_text());assert matrix['plan_sha256']==sha(a.plan.resolve()) and matrix['pack_sha256']==sha(pack)
 for got,want in zip(results,matrix['results']['i8-c4']):assert {k:v for k,v in got.items()if k!='heap_bytes'}=={k:v for k,v in want.items()if k!='heap_bytes'}
 report=dict(passed=True,source_current=before==identity(),source_sha256=before,physical_h1_tested=False,USB_opened=False,full_returned_byte_equivalence=True,stats_equal_fixed_i8_c4=True,plan_sha256=sha(a.plan.resolve()),matrix_sha256=sha(a.matrix),pack_sha256=sha(pack),results=results,limitations='No physical H1 timing or streaming audio is simulated. Canonical bytes and reader counters only; optional heap descriptors add a small amount beyond fixed-slot matrix.');assert report['source_current'];(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
