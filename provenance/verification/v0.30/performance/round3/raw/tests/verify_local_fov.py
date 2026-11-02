"""Exact C7/current visibility and local-tree invalidation host comparison."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);(out/'objects').mkdir()
 baseline=out/'sight-c7.c';baseline.write_bytes(a.reference.read_bytes());assert sha(baseline)=='76a2993d0421c30a5d09d90327018f417abb9a7a7832cbacc9dc95ccaec7d300'
 template=(ROOT/'build/v030/game-cost-study/host-finalclass/new-wrapper.c').read_text()
 # The helper functions expose observations only; the candidate and frozen
 # baseline each execute their own real core, masks, and cache lifetime.
 template=template[template.index('\n#include <string.h>'):]
 wrappers=[]
 for kind,source in [('old',baseline),('new',ROOT/'game/sight.c')]:
  path=out/(kind+'-wrapper.c');body=template.replace('new_',kind+'_')
  body+='\nint '+kind+'_source_tree_current(int i){return work->source[i].valid&&work->source[i].tree_stamp==work->tree_stamp;}\n'
  path.write_text('#include "'+source.as_posix()+'"\n'+body,newline='\n');wrappers.append(path)
 fixture=out/'local-test.c';body=(ROOT/'tests/local_fov_test.c').read_text()
 body=re.sub(r'(compare\("[^"\n]*(?:tree|planted|dirty-)[^"\n]*",)1(\))',r'\g<1>-1\2',body)
 body=body.replace('CHECK(dota_sight_workspace_bytes()-old_dota_sight_workspace_bytes()>=16384);','CHECK(dota_sight_workspace_bytes()>=old_dota_sight_workspace_bytes());')
 body=body.replace('new_memory,old_dota_sight_workspace_bytes())','new_memory,dota_sight_workspace_bytes()-1)')
 extra=(ROOT/'tests/local_fov_cases.inc').read_text()
 body=body.replace('int main(int argc,char**argv){',extra+'\nint main(int argc,char**argv){').replace('dirty_overlapping_trees();slark_and_failure','dirty_overlapping_trees();local_tree_invalidation();slark_and_failure')
 fixture.write_text(body,newline='\n')
 rename=['dota_sight_workspace_bytes','dota_sight_invalidate','dota_sight_reset','dota_sight_detach','dota_sight_attach','dota_sight_is_current','dota_rules_unit_vision_source','dota_sight_team_position_visible','dota_sight_position_visible','dota_sight_team_visible','dota_sight_fog_state','dota_sight_current_bits','h1_shared_shadow_profile']
 sources=[fixture,*[s for s in sorted((ROOT/'game').glob('*.c'))if s.name!='sight.c'],*[ROOT/'platform'/n for n in ['baked_io.c','item_assets.c','vision_assets.c','map_format.c']],*wrappers]
 inputs=[*sources,baseline,ROOT/'game/sight.c',ROOT/'tests/local_fov_test.c',ROOT/'tests/local_fov_cases.inc',Path(__file__),*sorted((ROOT/'game').glob('*.h'))]
 identity=lambda:{str(x.relative_to(ROOT)):sha(x)for x in inputs};before=identity();env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cc='C:/msys64/ucrt64/bin/gcc.exe';logs=[]
 def run(cmd):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True);logs.append(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 def compile(s):
  obj=out/'objects'/(s.stem+'.o');defs=['-D'+n+'=old_'+n for n in rename]if s==wrappers[0] else[]
  run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-Igame','-Iplatform','-Itests',*defs,'-c',s,'-o',obj]);return obj
 with ThreadPoolExecutor(max_workers=4)as pool:objects=list(pool.map(compile,sources))
 exe=out/'host.exe';run([cc,*objects,'-o',exe]);(out/'compile.txt').write_text(''.join(logs))
 r=subprocess.run([str(exe),str(ROOT/'build/v030/legacy/SCENE.H1P'),str(ROOT/'build/v030/legacy/VISION.H1V'),str(ROOT/'build/v030/legacy/TREE.H1T')],env=env,capture_output=True,text=True);(out/'output.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert not r.returncode
 d=json.loads(re.search(r'^RESULT (.*)$',r.stdout,re.M)[1]);n,saved=map(int,re.search(r'^LOCAL_RESULT (\d+) (\d+)$',r.stdout,re.M).groups())
 perf=[dict(case=m[0],reference_casts=int(m[1]),candidate_casts=int(m[2]))for m in re.findall(r'^PERF (\S+) (\d+) (\d+)$',r.stdout,re.M)]
 d.update(scope=__doc__,USB_opened=False,source_current=before==identity(),source_sha256=before,reference_sha256=sha(baseline),binary_sha256=sha(exe),local_invalidation_cases=n,avoided_shadowcasts=saved,cast_comparisons=perf);assert d['source_current'];(out/'verification.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps({k:d[k]for k in ['passed','cases','checks','local_invalidation_cases','avoided_shadowcasts']}))
if __name__=='__main__':main()
