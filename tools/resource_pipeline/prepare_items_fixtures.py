"""Actual full-core initial fixtures for native UI/equipment tests."""
from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 out=ROOT/'private/items/fixtures';out.mkdir(parents=True,exist_ok=True)
 sources=[ROOT/'tests/items_visual_fixtures.c',*sorted((ROOT/'game').glob('*.c'))]
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'items_visual_fixtures.exe'
 subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror',*[str(p) for p in sources],'-o',str(exe)],check=True,env=env)
 text=subprocess.check_output([str(exe),str(ROOT/'private/items/current-source/catalog.hit'),str(out)],text=True,env=env)
 layout=json.loads(text);layout['fixtures']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.bin')}
 layout['input_sha256']={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in [*sources,*sorted((ROOT/'game').glob('*.h'))]}
 (out/'layout.json').write_text(json.dumps(layout,indent=2)+'\n',encoding='utf8');print(json.dumps(layout))
if __name__=='__main__':main()
