"""Exact original/final/optional-failure sprite renderer fixture comparison.

Host execution of the actual renderer and unified assets, never H1 timing.
The added fixture places all ten original hero models at terrain-grid, map
edge and deterministic random coordinates. It compares every RGB565/depth
pixel, rendered pixel counters and selection bounds. No resource pixels,
depth planes or renderer results are patched.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
STRESS=r'''
 /* Explicit test placement only; rendering remains read-only Game work. */
 {static const int edge[]={0,1,15,16,17,31,32,63,64,127,128,255,256,511,512,1023,1024,2047,2048,3071,3072,3583,3584,3967,3968,4031,4032,4063,4064,4079,4094,4095};
 static const int dx[]={0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237,256,237,181,98};
 static const int dy[]={256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
 unsigned rng=0x6b59271du;int fixture,j;
 for(fixture=0;fixture<256;++fixture){DotaUnit *u=&dota_game.units[0];BSCamera sample;const BSStats *rendered;
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  u->x=fixture<64?edge[fixture&31]:(int)(rng&4095u);
  u->y=fixture<32?edge[(fixture*7)&31]:fixture<64?edge[(fixture*13)&31]:(int)((rng>>12)&4095u);
  u->hero_id=fixture%10;u->generation+=1;u->move_order=1;
  u->move_x=u->x+dx[fixture&15];u->move_y=u->y+dy[fixture&15];
  dota_game.tick+=3;dota_sight_invalidate();sample.x=u->x;sample.y=u->y;sample.z=bs_ground_height(u->x,u->y);
  render_resource(&sample);rendered=bs_stats();
  CHECK(fwrite(dota_pixels+480*24,480*192*2,1,frames)==1);
  CHECK(fwrite(bs_depth_buffer(),480*192*2,1,depths)==1);
  fprintf(meta,"fixture,%d,%d,%d,%d,%u,%u,%u,%u\n",fixture,u->hero_id,u->x,u->y,rendered->sprite_pixels,rendered->blended_pixels,rendered->depth_rejects,rendered->units_drawn);
  for(j=0;j<DOTA_MAX_UNITS;++j){int b[4]={0,0,0,0};uint16_t z=0;int valid=bs_unit_screen_bounds(j,b,&z);fprintf(meta,"bounds,%d,%d,%d,%d,%d,%d,%d,%u\n",fixture,j,valid,b[0],b[1],b[2],b[3],z);}
 }
 }
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 platform=['render.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c','tree_visual.c','resource_pack.c','match_storage.c']
 common=[*[ROOT/'platform'/n for n in platform],*sorted((ROOT/'game').glob('*.c'))]
 current=ROOT/'platform/baked_scene.c';headers=[*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'game').glob('*.h'))]
 inputs=[*common,*headers,current,ROOT/'tests/vision_ui.c',ROOT/'tests/resource_renderer_ui.c',ROOT/'platform/native.c',Path(__file__)]
 identities=lambda:{str(p.relative_to(ROOT)):sha(p)for p in inputs};identity=identities()
 report=dict(passed=False,physical_h1_tested=False,actual_mips32=False,scope=__doc__,source_sha256=identity,before_renderer_sha256=sha(a.before),fixture_frames=353,selection_bounds_compared=True,original_vs_final=True,optional_row_alloc_failure=True,optional_shadow_curve_fallback=True)
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-DDOTA_RESOURCE_PACK','-Itests','-Igame','-Iplatform','-Isdk/sdk/include']
 def run(cmd,log):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=600);log.write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
 try:
  for label in ['original','final','fallback']:
   dest=out/label;dest.mkdir();harness=dest/'harness.c';content=(ROOT/'tests/resource_renderer_ui.c').read_text()
   # Record all selectable units during the original moving-camera fixtures.
   content=content.replace('fprintf(meta,"%d,%d,%d,%d\\n",i,240-sx,120-sy,i>=65);','fprintf(meta,"%d,%d,%d,%d\\n",i,240-sx,120-sy,i>=65);{int j;for(j=0;j<DOTA_MAX_UNITS;++j){int b[4]={0,0,0,0};uint16_t z=0;int valid=bs_unit_screen_bounds(j,b,&z);fprintf(meta,"base-bounds,%d,%d,%d,%d,%d,%d,%d,%u\\n",i,j,valid,b[0],b[1],b[2],b[3],z);}}')
   needle=' CHECK(!fclose(frames)&&!fclose(depths)&&!fclose(meta));';assert needle in content;content=content.replace(needle,STRESS+needle)
   renderer=a.before.resolve() if label=='original' else current
   if label=='final':content=content.replace('CHECK(!bs_reserve_fog_cache(0));','CHECK(!bs_reserve_fog_cache(0));CHECK(bs_reserve_sprite_row_cache(128u)==128u);',1)
   elif label=='fallback':
    # Expose only a test ownership operation; all scalar rendering is normal.
    renderer=dest/'baked_scene.c';renderer.write_text(current.read_text()+'\nvoid bs_test_drop_shadow_curve(void){release_shadow_curve();}\n')
    content='void bs_test_drop_shadow_curve(void);\n'+content
    content=content.replace('CHECK(!bs_reserve_fog_cache(0));','CHECK(!bs_reserve_fog_cache(0));bs_test_drop_shadow_curve();fail_alloc=1;CHECK(!bs_reserve_sprite_row_cache(128u));fail_alloc=0;',1)
   harness.write_text(content);exe=dest/'renderer.exe';run(['C:/msys64/ucrt64/bin/gcc.exe',*flags,harness,*common,renderer,'-o',exe],dest/'compile.txt')
   stdout=run([exe,'unified',ROOT/'assets/runtime/DOTA2.H1R',ROOT/'build/v030/legacy/TREE.H1T',ROOT/'build/v030/legacy/VISION.H1V',dest],dest/'run.txt');report[label]=json.loads(re.search(r'RESULT (.+)',stdout)[1])
  comparisons={}
  for name in ['frames.bin','depths.bin','frames.csv']:
   old=(out/'original'/name).read_bytes();comparisons[name]=dict(bytes=len(old),sha256=hashlib.sha256(old).hexdigest(),final_exact=old==(out/'final'/name).read_bytes(),fallback_exact=old==(out/'fallback'/name).read_bytes());assert comparisons[name]['final_exact'] and comparisons[name]['fallback_exact'],name
  report.update(passed=True,comparisons=comparisons,source_current=identity==identities());assert report['source_current'],'Sources changed; repeat after freeze'
 finally:(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(dict(passed=report['passed'],frames=353,selection_bounds_exact=True,optional_failure_exact=True)))
if __name__=='__main__':main()
