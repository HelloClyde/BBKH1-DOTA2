"""Actual source-extracted fog shader/cache MIPS workload; no frame-rate claim."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN,UC_HOOK_BLOCK
from unicorn.mips_const import UC_MIPS_REG_PC,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_A0,UC_MIPS_REG_V0,UC_MIPS_REG_CP0_STATUS
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def function(s,n):
 m=re.search(r'^static[^;\n]*\b'+re.escape(n)+r'\([^;]*?\)\s*\{',s,re.M);assert m,n
 i=m.end();d=1
 while d:
  d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
PREFIX=r'''
#include "baked_scene.h"
#include "vision_assets.h"
#define FOG_STEP 8
#define FOG_COLUMNS (BS_WIDTH/FOG_STEP)
#define FOG_ROWS (BS_HEIGHT/FOG_STEP)
#define FOG_BACKGROUND_KEYS (8+2+1)
static unsigned char fog_visible[FOG_COLUMNS*FOG_ROWS],fog_saved_visible[FOG_COLUMNS*FOG_ROWS];
static uint16_t depths[BS_WIDTH*BS_HEIGHT],pixels[BS_WIDTH*272],storage[BS_WIDTH*BS_HEIGHT],*fog_background;
static int fog_background_key[FOG_BACKGROUND_KEYS],fog_saved_key[FOG_BACKGROUND_KEYS];
static BSFogCacheStats fog_cache_stats;
static BSFogStats fog_stats;
static BSWaterStats water_stats;
static BSStats stats;
static unsigned fog_cached_dimmed,fog_cached_soft,variant,query_count;
static unsigned long long query_digest;
static DotaGame game;
static BSCamera camera={2048,2048,0};
typedef uint32_t FogCopyWord __attribute__((__may_alias__));
static int dota_sight_fog_state(const DotaGame *g,int team,int x,int y){
 (void)g;(void)team;++query_count;query_digest=(query_digest^(unsigned)x)*1099511628211ull;query_digest=(query_digest^(unsigned)y)*1099511628211ull;
 return variant==2?2:variant==1?((((unsigned)x>>5)^((unsigned)y>>5))&1u?2:0):0;
}
'''
SUFFIX=r'''
int probe_prepare(int kind){
 unsigned i;variant=(unsigned)kind;query_count=0;query_digest=1469598103934665603ull;
 for(i=0;i<BS_WIDTH*272u;++i)pixels[i]=(uint16_t)(i*329u+97u);
 for(i=0;i<BS_WIDTH*BS_HEIGHT;++i)depths[i]=32768-2048;
 fog_stats.cells=fog_stats.queries=fog_stats.dimmed_pixels=fog_stats.soft_cells=fog_stats.sprites_rejected=0;
 return 1;
}
int probe_reset(int ignored){unsigned i;(void)ignored;for(i=0;i<sizeof(fog_cache_stats);++i)((unsigned char *)&fog_cache_stats)[i]=0;return 1;}
int probe_draw(int cache){fog_background=cache?storage:0;draw_fog(pixels,&game,&camera);return (int)query_count;}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 inputs=[ROOT/'platform/baked_scene.c',ROOT/'platform/baked_scene.h',ROOT/'platform/vision_assets.h',Path(__file__)];before={p.relative_to(ROOT).as_posix():sha(p) for p in inputs};s=inputs[0].read_text();names=['floor_div','global_x','global_y','fog_world_point','fog_light_row','fog_background_copy','fog_background_hit','fog_background_capture','draw_fog'];source=out/'fog-probe.c';source.write_text(PREFIX+'\n'+'\n'.join(function(s,n) for n in names)+SUFFIX,encoding='utf8');elf=out/'fog-probe.elf'
 cmd=[str(ROOT/'.tools/toolchain/bin/mipsel-none-elf-gcc.exe'),'-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-O2','-std=c99','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-nostdlib','-Wl,-Ttext,0x83c00020','-Wl,-e,probe_draw','-Wl,--gc-sections','-Wl,-u,probe_prepare','-Wl,-u,probe_reset','-Igame','-Iplatform',str(source),'-lgcc','-o',str(elf)]
 r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True);(out/'compile.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);uc.mem_map(0,0x4000000);uc.reg_write(UC_MIPS_REG_CP0_STATUS,0)
 with elf.open('rb') as f:
  e=ELFFile(f);syms={s.name:int(s['st_value']) for s in e.get_section_by_name('.symtab').iter_symbols()};sizes={s.name:int(s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()}
  for seg in e.iter_segments():
   if seg['p_type']=='PT_LOAD' and seg['p_filesz']:uc.mem_write(int(seg['p_vaddr'])&0x1fffffff,seg.data())
 slots=0;record=False;stop=0x80001000
 def block(uc,address,size,data):
  nonlocal slots
  if record:slots+=size//4
 uc.hook_add(UC_HOOK_BLOCK,block)
 def call(n,arg,measure=False):
  nonlocal slots,record
  slots=0;record=measure;uc.reg_write(UC_MIPS_REG_A0,arg);uc.reg_write(UC_MIPS_REG_SP,0x83fdfff0);uc.reg_write(UC_MIPS_REG_RA,stop);uc.emu_start(syms[n],stop,count=200_000_000);record=False;assert uc.reg_read(UC_MIPS_REG_PC)==stop
  return slots,uc.reg_read(UC_MIPS_REG_V0)
 def read(n):return bytes(uc.mem_read(syms[n]&0x1fffffff,sizes[n]))
 results=[]
 for variant in range(3):
  call('probe_reset',0);call('probe_prepare',variant);base,q=call('probe_draw',0,True);reference=read('pixels');depth=read('depths');queries=read('query_digest');fog=read('fog_stats');assert base>100000 and q==1440
  measured=[]
  for i in range(6):
   call('probe_prepare',variant);count,q=call('probe_draw',1,True);assert read('pixels')==reference and read('depths')==depth and read('query_digest')==queries and read('fog_stats')==fog and q==1440;measured.append(count)
  cache=list(struct.unpack('<6I',read('fog_cache_stats')));assert cache[1]==2 and cache[3]==1 and cache[5]==1
  results.append(dict(mask=variant,baseline_slots=base,warm_cache_slots=measured,cache_stats=cache,framebuffer_sha256=hashlib.sha256(reference).hexdigest(),depth_sha256=hashlib.sha256(depth).hexdigest(),all_framebuffers_depth_queries_and_stats_equal=True))
 report=dict(passed=True,actual_mips32=True,physical_h1_tested=False,actual_source_functions=names,source_sha256=before,source_current=before=={p.relative_to(ROOT).as_posix():sha(p) for p in inputs},probe_source_sha256=sha(source),elf_sha256=sha(elf),results=results,scope=__doc__,limits='Isolated actual shader/cache functions, deterministic cached-FOW fixture and RGB565/depth source. Instruction slots are not physical cycles, bus/cache timing or game FPS. Full gameplay renderer equivalence is checked separately.')
 assert report['source_current'];(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results))
if __name__=='__main__':main()
