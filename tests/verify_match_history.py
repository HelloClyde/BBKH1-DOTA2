from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sources=[ROOT/'tests/match_history_test.c',ROOT/'game/match_history.c',ROOT/'game/match_time.c',ROOT/'platform/match_storage.c'];inputs=[*sources,*sorted((ROOT/'game').glob('*.h')),ROOT/'platform/match_storage.h',Path(__file__).resolve()];before={x.relative_to(ROOT).as_posix():sha(x) for x in inputs}
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');exe=out/'match-history.exe'
 command=['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-DMATCH_STORAGE_HOST',*[str(x) for x in sources],'-o',str(exe)]
 r=subprocess.run(command,env=env,capture_output=True,text=True);(out/'compile.txt').write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,r.stdout+r.stderr
 r=subprocess.run([str(exe)],env=env,capture_output=True,text=True);(out/'output.txt').write_text(r.stdout+r.stderr,encoding='utf8');print(r.stdout+r.stderr,end='');assert r.returncode==0
 result=json.loads(r.stdout);result.update(source_current=before=={x.relative_to(ROOT).as_posix():sha(x) for x in inputs},source_sha256=before,binary_sha256=sha(exe),scope='Actual match_history+match_storage C with explicit item/owner capture fixtures and injectable host file services; not the complete gameplay, renderer, firmware or device.')
 (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');assert result['passed'] and result['source_current']
if __name__=='__main__':main()
