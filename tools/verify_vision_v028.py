"""Host-only focused tests of the real sight, ward, and Slark gameplay core."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sources=[ROOT/'tests/vision_v028_test.c',*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/item_assets.c',ROOT/'platform/baked_io.c'];inputs=[*sources,*sorted((ROOT/'game').glob('*.h'))];before={str(x.relative_to(ROOT)):sha(x) for x in inputs};env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');exe=out/'vision.exe'
 c=subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST',*[str(x) for x in sources],'-o',str(exe)],env=env,capture_output=True,text=True);(out/'compile.txt').write_text(c.stdout+c.stderr,encoding='utf-8');assert c.returncode==0,c.stdout+c.stderr
 r=subprocess.run([str(exe),str(ROOT/'assets/runtime/SCENE.H1P')],env=env,capture_output=True,text=True);(out/'output.txt').write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,end='');report=json.loads(r.stdout);report.update(source_current=before=={str(x.relative_to(ROOT)):sha(x) for x in inputs},source_sha256=before,binary_sha256=sha(exe),actual_mips32=False,USB_opened=False);(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert r.returncode==0 and report['passed'] and report['source_current']
if __name__=='__main__':main()
