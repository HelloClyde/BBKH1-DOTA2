"""Compare the exact old/new minimap background loops; host pixel proof, no FPS."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT = Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def background(p, name):
    text = Path(p).read_text(encoding='utf8')
    start = text.index('static void minimap(')
    end = text.index('    for(i=0;i<DOTA_MAX_UNITS;', start)
    return text[start:end].replace('static void minimap(', 'static void '+name+'(', 1) + '}\n'

PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "render.h"
#include "sight.h"
#include "hud_layout.h"
#include "vision_assets.h"
#define RGB(r,g,b) ((uint16_t)(((r)>>3)<<11|((g)>>2)<<5|((b)>>3)))
uint16_t dota_pixels[480*272];
static uint16_t reference[480*272];
static uint16_t dota_overview[DOTA_HUD_MAP_W*DOTA_HUD_MAP_W];
static uint32_t current[2][2048],explored[2][2048];
static DotaGame game;
static unsigned rng=0x98765432u,queries,getters,attached=1,checks;
static unsigned next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int dota_roshan_region_at(int x,int y){return (x/512+y/512)%3;}
int dota_sight_fog_state(const DotaGame *g,int team,int x,int y){
 unsigned c;++queries;if(!g||!attached||(unsigned)team>=2||(unsigned)x>=4096||(unsigned)y>=4096)return 0;
 c=(unsigned)(y/16)*256u+(unsigned)(x/16);
 return ((current[team][c>>5]>>(c&31))&1u)?2:((explored[team][c>>5]>>(c&31))&1u)?1:0;
}
const uint32_t *dota_sight_current_bits(const DotaGame *g,int team){
 ++getters;return g&&attached&&(unsigned)team<2?current[team]:0;
}
'''
SUFFIX = r'''
int main(void){
 unsigned n,i,team,region;DotaView view;memset(&view,0,sizeof(view));
 for(i=0;i<72;++i)if(((i*4096/72)/16)!=(i*256/72))return 2;
 for(n=0;n<64;++n){
  for(i=0;i<72*72;++i)dota_overview[i]=(uint16_t)next();
  for(i=0;i<2048;++i){current[0][i]=n==0?0:n==1?~0u:n==2?0xaaaaaaaau:next();current[1][i]=next();explored[0][i]=next();explored[1][i]=next();}
  attached=n!=3;
  for(team=0;team<3;++team)for(region=0;region<3;++region){
   game.player=0;game.units[0].team=(int)team;game.units[0].x=(int)(region*512);game.units[0].y=0;
   memset(dota_pixels,0xa5,sizeof(dota_pixels));queries=getters=0;
   reference_background(&game,&view);if(queries!=72*72||getters)return 3;
   memcpy(reference,dota_pixels,sizeof(reference));
   memset(dota_pixels,0xa5,sizeof(dota_pixels));queries=getters=0;
   current_background(&game,&view);if(queries||getters!=1)return 4;
   if(memcmp(reference,dota_pixels,sizeof(reference)))return 5;
   ++checks;
  }
 }
 printf("{\"passed\":true,\"cases\":%u,\"pixel_words_compared\":%u,\"old_queries_per_frame\":5184,\"new_getters_per_frame\":1}\n",checks,checks*480u*272u);
 return 0;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a=p.parse_args(); out=a.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    out.mkdir(parents=True)
    inputs=[a.baseline.resolve(),ROOT/'platform/render.c',ROOT/'game/sight.h',ROOT/'platform/hud_layout.h',ROOT/'platform/vision_assets.h',Path(__file__).resolve()]
    before={str(x.relative_to(ROOT)):sha(x) for x in inputs}
    c=out/'minimap-background.c'
    c.write_text(PREFIX+background(a.baseline,'reference_background')+background(ROOT/'platform/render.c','current_background')+SUFFIX,encoding='utf8')
    exe=out/'minimap-background.exe'
    cmd=['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Werror','-Igame','-Iplatform',str(c),'-o',str(exe)]
    env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
    r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True)
    (out/'compile.txt').write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,r.stderr
    r=subprocess.run([str(exe)],cwd=ROOT,env=env,capture_output=True,text=True)
    (out/'output.txt').write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
    result=json.loads(r.stdout);assert result['passed']
    report=dict(passed=True,physical_h1_tested=False,actual_source_extracted=True,provider_is_deterministic_fixture=True,scope=__doc__,result=result,source_sha256=before,source_current=before=={str(x.relative_to(ROOT)):sha(x) for x in inputs},probe_sha256=sha(c),exe_sha256=sha(exe),limitations='Tests only the minimap background loops. Original core visibility and getter outcomes require the complete-BDA vision tests; no real H1 or frame-rate claim.')
    assert report['source_current'];(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(result))
if __name__=='__main__': main()
