"""Original-map no-ward geometry and atomic public-item acceptance; host only."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);(out/'objects').mkdir();env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');cc='C:/msys64/ucrt64/bin/gcc.exe';sources=[ROOT/'tests/ward_placement_test.c',*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/baked_io.c',ROOT/'platform/item_assets.c'];inputs=[*sources,*sorted((ROOT/'game').glob('*.h')),Path(__file__).resolve(),ROOT/'provenance/ward-placement/source-data/source-audit.json'];before={p.relative_to(ROOT).as_posix():sha(p) for p in inputs};logs=[]
 def run(cmd):
  r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True,encoding='utf-8',errors='replace');logs.append(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
 def compile_one(s):
  o=out/'objects'/(s.stem+'.o');run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-I',ROOT/'game','-I',ROOT/'platform','-c',s,'-o',o]);return o
 with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,sources))
 exe=out/'ward-placement.exe';run([cc,*objects,'-o',exe]);(out/'compile.txt').write_text(''.join(logs),encoding='utf-8');text=run([exe,ROOT/'assets/runtime/SCENE.H1P',ROOT/'assets/runtime/VISION.H1V']);(out/'output.txt').write_text(text,encoding='utf-8');print(text,end='');report=json.loads(re.search(r'^RESULT (.*)$',text,re.M)[1]);report.update(actual_core=True,source_current=before=={p.relative_to(ROOT).as_posix():sha(p) for p in inputs},source_sha256=before,binary_sha256=sha(exe),actual_mips32=False,USB_opened=False,scene_sha256=sha(ROOT/'assets/runtime/SCENE.H1P'),vision_sha256=sha(ROOT/'assets/runtime/VISION.H1V'));(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert report['passed'] and report['source_current']
if __name__=='__main__':main()
