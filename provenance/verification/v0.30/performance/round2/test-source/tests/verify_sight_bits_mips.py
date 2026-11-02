"""Exact complete-BDA bitplane API checked by a scratch MIPS caller loop.

The loop calls the candidate's getter and fog_state for every grid cell in
both teams. It does not execute application entry or physical H1 hardware.
"""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from mips_core_model import CoreMachine
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--snapshot',type=Path,default=ROOT/'build/v030/perf-baseline-aligned/post-horn-paused-game.bin');p.add_argument('--legacy',type=Path,default=ROOT/'build/v030/legacy');a=p.parse_args()
 build=a.build.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 meta=json.loads((build/'DOTA2.build.json').read_bytes());assert meta['bda_sha256']==sha(build/'DOTA2.bda')and meta['elf_sha256']==sha(build/'DOTA2.elf')
 assert meta['binary']['abi']['DotaGame']==179008 and len(a.snapshot.read_bytes())==179008
 m=CoreMachine(build/'DOTA2.bda',build/'DOTA2.elf',{'DOTA2.H1R':ROOT/'assets/runtime/DOTA2.H1R'})
 helper=out/'caller.c';helper.write_text('''#include "sight.h"
unsigned test_all_current_bits(const DotaGame*g,int team){
 const uint32_t*p=dota_sight_current_bits(g,team);unsigned c;
 if(!p)return 0;
 for(c=0;c<65536u;++c){unsigned present=(p[c>>5]>>(c&31u))&1u;
  if(present!=(unsigned)(dota_sight_fog_state(g,team,(int)(c%256u)*16+8,(int)(c/256u)*16+8)==2))return 0x80000000u|c;
 }
 return 65536u;
}
''',newline='\n')
 linker=out/'caller.ld';linker.write_text('OUTPUT_ARCH(mips)\nENTRY(test_all_current_bits)\nSECTIONS { . = 0x81510000; .text : { *(.text*) *(.rodata*) } /DISCARD/ : { *(.comment*) *(.note*) *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.debug*) } }\n',newline='\n')
 cc=ROOT/'.tools/toolchain/bin/mipsel-none-elf-gcc.exe';objcopy=cc.with_name('mipsel-none-elf-objcopy.exe');obj=out/'caller.o';elf=out/'caller.elf';raw=out/'caller.bin'
 flags=['-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-O2','-std=c99','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-I',str(ROOT/'game')]
 commands=[[str(cc),*flags,'-c',str(helper),'-o',str(obj)],[str(cc),*flags,'-nostdlib',f'-Wl,-T,{linker}','-Wl,--build-id=none',*[f'-Wl,--defsym,{n}=0x{m.symbols[n]:08x}'for n in ('dota_sight_current_bits','dota_sight_fog_state')],str(obj),'-o',str(elf)],[str(objcopy),'-O','binary',str(elf),str(raw)]]
 for command in commands:subprocess.run(command,check=True,capture_output=True)
 (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 code=raw.read_bytes();assert len(code)<4096;m.write(0x81510000,code);m.symbols['test_all_current_bits']=0x81510000
 report=dict(passed=False,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,entry_executed=False,bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],build_metadata_sha256=sha(build/'DOTA2.build.json'),snapshot_sha256=sha(a.snapshot),test_source_sha256=sha(__file__),scratch_caller=dict(address='0x81510000',bytes=len(code),source_sha256=sha(helper),code_sha256=sha(raw),scope='Only ABI caller loop in separate modeled scratch RAM; production BDA code and gameplay outcomes are never patched.'),checks=[])
 checks=report['checks'];start=time.monotonic()
 try:
  gp=m.symbols['dota_game'];assert m.call('dota_sight_current_bits',gp,0)==0;checks.append('missing-workspace-null')
  assert m.call('baked_io_open_default')==1 and m.call('item_assets_open')==1;m.call('item_assets_reserve_rules_cache',65536)
  vision=(a.legacy/'VISION.H1V').read_bytes();xy=(a.legacy/'TREE.H1T').read_bytes()[128:128+2113*4]
  terrain=m.alloc(len(vision)-64);m.write(terrain,vision[64:]);wb=m.call('dota_sight_workspace_bytes');ws=m.alloc(wb);assert m.call('dota_sight_attach',terrain,terrain+131072,ws,wb)==1
  tp=m.alloc(len(xy));m.write(tp,xy);assert m.call('dota_trees_attach',tp,2113)==1
  m.write(gp,a.snapshot.read_bytes());m.call('dota_sight_reset');m.call('dota_set_paused',gp,0)
  assert m.call('dota_sight_current_bits',0,0)==0 and m.call('dota_sight_current_bits',gp,2)==0 and m.call('dota_sight_current_bits',gp,-1)==0;checks.append('null-and-invalid-team-null')
  before=m.read(gp,179008);results=[]
  for team in range(2):
   count=m.call('test_all_current_bits',gp,team);assert count==65536,(team,count);results.append(count)
   pointer=m.call('dota_sight_current_bits',gp,team)&0xffffffff;assert pointer==ws+4+team*8192
  assert m.read(gp,179008)==before;checks.append('both-teams-all-65536-cells-current-bit-equals-fog-state2');checks.append('borrowed-plane-address-and-game-unchanged')
  m.call('dota_sight_reset');assert m.call('test_all_current_bits',gp,0)==65536;assert m.read(gp,179008)==before;checks.append('reset-and-refresh-query-equivalence')
  m.call('dota_trees_detach');m.call('dota_sight_detach');assert m.call('dota_sight_current_bits',gp,0)==0;checks.append('detach-null')
  m.free(tp);m.free(ws);m.free(terrain);m.call('item_assets_close');m.call('baked_io_close');assert m.call('resource_unmount')==1 and not m.allocations and not m.handles;checks.append('cleanup-zero-allocations-handles')
  report.update(passed=True,workspace_bytes=wb,all_cell_checks=sum(results)+65536,cleanup=dict(allocations=0,handles=0),outcome_game_ram_writes_after_setup=0,modeled_api_calls=m.calls,elapsed_seconds=time.monotonic()-start)
  (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,all_cell_checks=report['all_cell_checks'],bda_sha256=meta['bda_sha256'],workspace_bytes=wb)))
 finally:m.dispose()
if __name__=='__main__':main()
