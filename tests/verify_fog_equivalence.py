"""Compare actual old/new C fog functions on identical framebuffer fixtures.

Pass the preserved pre-optimization baked_scene.c; its identity is checked
against the physical-candidate source. No display/gameplay implementation is
recreated in Python, and elapsed host time is not reported as H1 performance.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
REFERENCE_SHA='1aa2340907578c4a50ee6d3afaeebfd513cee13e68e7c0cbdf5bc545e5d68ebd'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def function(text,name):
 m=re.search(r'^static\s+(?:inline\s+)?(?:int|void)\s+'+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M);assert m,name
 i=m.end();depth=1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[m.start():i]
HARNESS=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "baked_scene.h"
#include "vision_assets.h"
#define FOG_STEP 8
#define FOG_COLUMNS (BS_WIDTH/FOG_STEP)
#define FOG_ROWS (BS_HEIGHT/FOG_STEP)
static unsigned char fog_visible[FOG_COLUMNS*FOG_ROWS],mask[FOG_COLUMNS*FOG_ROWS];
static uint16_t depths[BS_WIDTH*BS_HEIGHT],source[480*272],reference[480*272],candidate[480*272];
static BSFogStats fog_stats;
static unsigned queries;
static uint64_t query_hash;
static int dota_sight_fog_state(const DotaGame *g,int team,int x,int y){unsigned at=queries++;(void)g;(void)team;query_hash=(query_hash^(uint32_t)x)*1099511628211ull;query_hash=(query_hash^(uint32_t)y)*1099511628211ull;if(at>=1440)abort();return mask[at];}
'''
MAIN=r'''
int main(void){
    static DotaGame game;BSCamera camera={2048,2048,0};unsigned test,i,q1,q2;uint32_t random=0x1842c319u;BSFogStats old_stats;uint64_t qh1,qh2,digest=1469598103934665603ull;
    for(test=0;test<96;++test){
        camera.x=test>=80?32:2048;camera.y=test>=88?4060:2048;
        for(i=0;i<480u*272u;++i){random=random*1664525u+1013904223u;source[i]=test<3?(uint16_t)i:(uint16_t)(random>>8);}
        for(i=0;i<BS_WIDTH*BS_HEIGHT;++i)depths[i]=(uint16_t)(test>=64&&i%23==0?65535:32768-camera.y);
        for(i=0;i<1440;++i){unsigned x=i%60,y=i/60;random=random*1664525u+1013904223u;mask[i]=(unsigned char)(test<3?test:test%8==3?(x<30?2:0):test%8==4?(y<12?2:1):test%8==5?(x<y*2?2:0):test%8==6?((x+y)&1)*2:test%8==7?((x%7==0&&y%5==0)?2:0):(random>>30)%3);}
        memcpy(reference,source,sizeof(source));memset(&fog_stats,0,sizeof(fog_stats));queries=0;query_hash=0;reference_draw_fog(reference,&game,&camera);old_stats=fog_stats;q1=queries;qh1=query_hash;
        memcpy(candidate,source,sizeof(source));memset(&fog_stats,0,sizeof(fog_stats));queries=0;query_hash=0;draw_fog(candidate,&game,&camera);q2=queries;qh2=query_hash;
        if(memcmp(reference,candidate,sizeof(reference))||memcmp(&old_stats,&fog_stats,sizeof(fog_stats))||q1!=q2||qh1!=qh2){fprintf(stderr,"Mismatch fixture %u queries %u/%u\n",test,q1,q2);return 1;}
        for(i=0;i<480u*272u;++i)digest=(digest^candidate[i])*1099511628211ull;
    }
    printf("RESULT {\"passed\":true,\"cases\":96,\"framebuffer_pixels_compared\":12533760,\"full_framebuffers_equal\":true,\"fog_stats_equal\":true,\"query_count_and_world_positions_equal\":true,\"digest\":\"%016llx\",\"physical_h1_tested\":false,\"actual_mips32\":false}\n",(unsigned long long)digest);return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();assert sha(a.reference)==REFERENCE_SHA
 current=ROOT/'platform/baked_scene.c';old=a.reference.read_text(encoding='utf8');new=current.read_text(encoding='utf8');helpers=['floor_div','global_x','global_y','fog_world_point','fog_light_row'];shared=[]
 for name in helpers:
  before=function(old,name);assert before==function(new,name),name;shared.append(before)
 reference=function(old,'draw_fog').replace('static void draw_fog(','static void reference_draw_fog(',1);candidate=function(new,'draw_fog');out.mkdir(parents=True)
 source=out/'equivalence.c';source.write_text(HARNESS+'\n'+'\n'.join(shared)+'\n'+reference+'\n'+candidate+'\n'+MAIN,encoding='utf8');exe=out/'equivalence.exe'
 inputs={p.relative_to(ROOT).as_posix():sha(p) for p in [current,ROOT/'platform/vision_assets.h',ROOT/'platform/baked_scene.h',Path(__file__)]};report=dict(passed=False,reference_source_sha256=REFERENCE_SHA,source_sha256=inputs,actual_source_functions=True,physical_h1_tested=False,actual_mips32=False,scope='Actual pre-optimization draw_fog and current draw_fog extracted from their source, identical helper functions, RGB565 framebuffer/depth fixtures and deterministic cached-FOW answers. Tests pixel/stat/query equivalence; no H1 timing claim.')
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','')
 try:
  for name,command in [('compile',['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror','-Igame','-Iplatform',source,'-o',exe]),('fixture',[exe])]:
   r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120);(out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,r.stdout+r.stderr
  report.update(json.loads(re.search(r'RESULT (.+)',r.stdout)[1]));report['source_current']=all(sha(ROOT/n)==v for n,v in inputs.items());assert report['source_current'];report['executable_sha256']=sha(exe);report['generated_source_sha256']=sha(source)
 except Exception as e:report.update(passed=False,error=str(e));(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');raise
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps({k:report[k] for k in ['passed','cases','framebuffer_pixels_compared','source_current']}))
if __name__=='__main__':main()
