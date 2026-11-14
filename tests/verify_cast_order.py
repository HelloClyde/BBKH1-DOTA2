"""Actual C spell/item orders on compiled Source navigation and bounded real LOS fixtures. Host only."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scene',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);(out/'objects').mkdir();env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cc='C:/msys64/ucrt64/bin/gcc.exe';sources=[ROOT/'tests/cast_order_test.c',*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/baked_io.c',ROOT/'platform/item_assets.c'];inputs=[*sources,*sorted((ROOT/'game').glob('*.h')),Path(__file__).resolve()];identity=lambda:{p.relative_to(ROOT).as_posix():sha(p)for p in inputs};before=identity();logs=[]
 def run(cmd):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True);logs.append(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr;return r.stdout
 def compile_one(s):
  o=out/'objects'/(s.stem+'.o');run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-Igame','-Iplatform','-c',s,'-o',o]);return o
 inputs.extend(ROOT/'platform'/name for name in ['target_input.h','extra_ability.h','ability_icon_state.h']);before=identity()
 with ThreadPoolExecutor(max_workers=4)as pool:objects=list(pool.map(compile_one,sources))
 exe=out/'cast-order.exe';run([cc,*objects,'-o',exe]);(out/'compile.txt').write_text(''.join(logs));text=run([exe,a.scene.resolve(),out/'trace.csv']);(out/'output.txt').write_text(text);report=json.loads(re.search(r'^RESULT (.*)$',text,re.M)[1]);report.update(source_current=before==identity(),source_sha256=before,binary_sha256=sha(exe),scene_sha256=sha(a.scene),USB_opened=False,scope=__doc__);(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'];print(text);print(out/'verification.json')
if __name__=='__main__':main()
