"""Exact RGB565 pair arithmetic and before/after real 97-frame renderer parity.

Host C execution only. No physical H1 or FPS claim; no resource pixels,
visibility masks, depth values or Game outcomes are substituted.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def function(text,name):
 match=re.search(r'^static[^;\n]*\b'+name+r'\([^;]*?\)\s*\{',text,re.M);assert match
 end=match.end();depth=1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[match.start():end]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True);p.add_argument('--legacy',type=Path,default=ROOT/'build/v030/legacy');p.add_argument('--bundle',type=Path,default=ROOT/'assets/runtime/DOTA2.H1R');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);frozen=a.before.resolve();current=ROOT/'platform/baked_scene.c'
 pair=function(current.read_text(), 'fog_hidden_pair')
 proof=out/'pair-arithmetic.c';proof.write_text('''#include <stdint.h>
#include <stdio.h>
#include "vision_assets.h"
typedef uint32_t FogCopyWord;
'''+pair+'''
static unsigned checks;
static int check(uint32_t value){uint32_t expected=(uint32_t)vision_assets_fog_color((uint16_t)value,3)|((uint32_t)vision_assets_fog_color((uint16_t)(value>>16),3)<<16);++checks;if(fog_hidden_pair(value)!=expected){printf("FAIL %08x\\n",value);return 0;}return 1;}
int main(void){static const uint16_t edges[]={0,65535,31,2016,63488,0x39e7,0x18e3,0x7bef};uint32_t color,i,rng=0x927a3f19;
for(color=0;color<65536u;++color)for(i=0;i<sizeof(edges)/sizeof(edges[0]);++i){if(!check(color|((uint32_t)edges[i]<<16))||!check(((uint32_t)color<<16)|edges[i]))return 1;}
for(i=0;i<2097152u;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;if(!check(rng))return 1;}
printf("{\\"passed\\":true,\\"checks\\":%u,\\"all_65536_colors_in_both_lanes\\":true,\\"edge_neighbors\\":8,\\"random_pairs\\":2097152}\\n",checks);return 0;}
''',encoding='utf8')
 platform=['render.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c','tree_visual.c','resource_pack.c','match_storage.c']
 common=[ROOT/'tests/resource_renderer_ui.c',*[ROOT/'platform'/n for n in platform],*sorted((ROOT/'game').glob('*.c'))]
 inputs=[*common,current,ROOT/'tests/vision_ui.c',ROOT/'platform/native.c',*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h')),Path(__file__)]
 identity=lambda:{str(path.relative_to(ROOT)):sha(path)for path in inputs}
 before=identity();report=dict(passed=False,scope=__doc__,physical_h1_tested=False,actual_mips32=False,before_renderer_sha256=sha(frozen),after_renderer_sha256=sha(current),source_sha256=before,bundle_sha256=sha(a.bundle))
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');gcc='C:/msys64/ucrt64/bin/gcc.exe'
 def run(command,log):
  result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=600);log.write_text(result.stdout+result.stderr,encoding='utf8');assert result.returncode==0,result.stdout+result.stderr;return result.stdout
 try:
  exe=out/'pair-arithmetic.exe';run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-Iplatform','-Igame',proof,'-o',exe],out/'proof-compile.txt');result=run([exe],out/'proof-run.txt');report['arithmetic']=json.loads(result)
  flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-DDOTA_RESOURCE_PACK','-Igame','-Iplatform','-Isdk/sdk/include']
  runs={}
  for label,renderer in [('before',frozen),('after',current)]:
   dest=out/label;dest.mkdir();exe=dest/'renderer.exe';run([gcc,*flags,*common,renderer,'-o',exe],dest/'compile.txt')
   stdout=run([exe,'unified',a.bundle.resolve(),(a.legacy/'TREE.H1T').resolve(),(a.legacy/'VISION.H1V').resolve(),dest],dest/'run.txt');runs[label]=json.loads(re.search(r'RESULT (.+)',stdout)[1])
  comparisons={}
  for name in ['frames.bin','depths.bin','frames.csv','full-ui.ppm','standing.ppm','cut.ppm','two-cut.ppm','regrown.ppm']:
   left=(out/'before'/name).read_bytes();right=(out/'after'/name).read_bytes();assert left==right,name;comparisons[name]=dict(bytes=len(right),sha256=hashlib.sha256(right).hexdigest(),exact=True)
  report.update(runs=runs,comparisons=comparisons,source_current=before==identity());assert report['source_current'],'Other renderer inputs changed during compile/run; retain this report and rerun after freeze'
  report['passed']=True
 except Exception as error:report['error']=str(error);raise
 finally:(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(passed=True,arithmetic=report['arithmetic'],frame_count=97,source_current=report['source_current'],before_renderer_sha256=report['before_renderer_sha256'],after_renderer_sha256=report['after_renderer_sha256'])))
if __name__=='__main__':main()
