"""Focused host test of the actual core and real external item provider."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--pack',type=Path,default=ROOT/'assets/runtime/SCENE.H1P');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);(out/'objects').mkdir()
    sources=[*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/item_assets.c',ROOT/'platform/baked_io.c',ROOT/'tests/match_time_test.c']
    inputs=[*sources,*sorted((ROOT/'game').glob('*.h')),Path(__file__).resolve(),ROOT/'provenance/match-time/source.json']
    identities=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in inputs};before=identities();cc=Path('C:/msys64/ucrt64/bin/gcc.exe');env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    logs=[];report=dict(passed=False,actual_core=True,actual_mips32=False,physical_h1_tested=False,USB_opened=False,source_sha256=before,pack_sha256=sha(args.pack))
    def run(command):
        result=subprocess.run(list(map(str,command)),env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=150);logs.append(result.stdout+result.stderr);assert result.returncode==0,result.stdout+result.stderr;return result.stdout
    def compile_one(source):
        obj=out/'objects'/(source.stem+'.o');run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-I',ROOT/'game','-I',ROOT/'platform','-c',source,'-o',obj]);return obj
    try:
        with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,sources))
        exe=out/'match-time.exe';run([cc,*objects,'-o',exe]);(out/'compile.txt').write_text(''.join(logs),encoding='utf8');result=run([exe,args.pack.resolve()]);(out/'model.txt').write_text(result,encoding='utf8');print(result,end='')
        report.update(json.loads(re.search(r'^RESULT (.+)$',result,re.M)[1]));report.update(source_current=before==identities(),source_after_sha256=identities(),binary_sha256=sha(exe),scenarios=[json.loads(s[5:]) for s in result.splitlines() if s.startswith('CASE ')]);assert report['source_current']
    except Exception as error:
        report.update(passed=False,error=str(error));(out/'compile.txt').write_text(''.join(logs),encoding='utf8');(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');raise
    (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps({k:report[k] for k in ('passed','cases','checks','source_current','game_bytes','unit_bytes')}))
if __name__=='__main__':main()
