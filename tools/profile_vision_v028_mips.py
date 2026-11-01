"""Count MIPS instruction slots of production sight with synthetic observers.

No game entry, firmware, audio, rendering, MXU, USB or frame-rate measurement.
Item/roster bonus queries are explicit neutral stubs, not gameplay assertions.
"""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN,UC_HOOK_BLOCK
from unicorn.mips_const import UC_MIPS_REG_PC,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_V0,UC_MIPS_REG_CP0_STATUS
ROOT=Path(__file__).resolve().parents[1]
WRAPPER=r'''
#include "sight.c"
const DotaPoint dota_fountains[2]={{256,3840},{3840,256}};
const DotaPoint dota_objective_positions[DOTA_OBJECTIVES]={{0,0}};
int dota_barracks_kind(const DotaUnit *u){(void)u;return 0;}
int dota_items_special_night_vision_bonus(const DotaGame *g,int i){(void)g;(void)i;return 0;}
int dota_items_special_vision_percent(const DotaGame *g,int i){(void)g;(void)i;return 100;}
int dota_items_equipped_item(const DotaGame *g,int i,int s){(void)g;(void)i;(void)s;return -1;}
int dota_items_parameter(int i,int key,int rank,int fallback){(void)i;(void)key;(void)rank;return fallback;}
int dota_item_world_owner(const DotaGame *g,int i){(void)g;(void)i;return -1;}
int dota_roster_vision_bonus(const DotaGame *g,int i){(void)g;(void)i;return 0;}
DotaGame probe_game;
int16_t probe_height[256*256];
uint8_t probe_trees[256*256/8];
static SightWork probe_work;
int probe_entry(int operation,int count,int movement){
 int i;
 if(!operation){zero(&probe_game,sizeof(probe_game));probe_game.tick=2000;
  for(i=0;i<count;++i){DotaUnit *u=&probe_game.units[i];u->active=1;u->hp=u->max_hp=100;u->type=i<10?DOTA_HERO:DOTA_CREEP;u->hero_id=0;u->team=i&1;u->x=600+(i*197)%2800;u->y=600+(i*351)%2800;}
  return dota_sight_attach(probe_height,probe_trees,&probe_work,sizeof(probe_work));
 }
 ++probe_game.tick;
 if(movement)for(i=0;i<count;++i)probe_game.units[i].x=600+(probe_game.units[i].x-600+movement)%2800;
 return dota_sight_fog_state(&probe_game,0,2008,2008);
}
'''
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 inputs=[ROOT/'game/sight.c',ROOT/'game/sight.h',ROOT/'game/match_time.c',ROOT/'game/match_time.h',ROOT/'game/vision.c',ROOT/'private/generated/dota_roshan_vision.h',ROOT/'assets/runtime/VISION.H1V',Path(__file__).resolve()];before={str(x.relative_to(ROOT)):sha(x) for x in inputs};source=out/'probe.c';source.write_text(WRAPPER,encoding='utf-8',newline='\n');elf=out/'probe.elf'
 cmd=[str(a.compiler.resolve()),'-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-O2','-std=c99','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-nostdlib','-Wl,-Ttext,0x83c00020','-Wl,-e,probe_entry','-Wl,--gc-sections','-I',str(ROOT/'game'),str(source),str(ROOT/'game/match_time.c'),str(ROOT/'game/vision.c'),'-lgcc','-o',str(elf)]
 c=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.txt').write_text(c.stdout+c.stderr,encoding='utf-8');assert c.returncode==0,c.stdout+c.stderr
 uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);uc.mem_map(0,0x4000000);uc.reg_write(UC_MIPS_REG_CP0_STATUS,0)
 with elf.open('rb') as f:
  e=ELFFile(f);entry=int(e['e_entry']);symbols={s.name:int(s['st_value']) for s in e.get_section_by_name('.symtab').iter_symbols()};sizes={s.name:int(s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()}
  for seg in e.iter_segments():
   if seg['p_type']=='PT_LOAD' and seg['p_filesz']:uc.mem_write(int(seg['p_vaddr'])&0x1fffffff,seg.data())
 sidecar=(ROOT/'assets/runtime/VISION.H1V').read_bytes();header=struct.unpack('<16I',sidecar[:64]);assert header[4]==256 and header[7]==131072 and header[9]==8192
 uc.mem_write(symbols['probe_height']&0x1fffffff,sidecar[header[6]:header[6]+header[7]]);uc.mem_write(symbols['probe_trees']&0x1fffffff,sidecar[header[8]:header[8]+header[9]])
 slots=0;stop=0x80001000
 def block(uc,address,size,data):
  nonlocal slots
  slots+=size//4
 uc.hook_add(UC_HOOK_BLOCK,block)
 def call(op,count,movement):
  nonlocal slots
  slots=0;uc.reg_write(UC_MIPS_REG_A0,op);uc.reg_write(UC_MIPS_REG_A1,count);uc.reg_write(UC_MIPS_REG_A2,movement);uc.reg_write(UC_MIPS_REG_SP,0x83fdfff0);uc.reg_write(UC_MIPS_REG_RA,stop);uc.emu_start(entry,stop,count=200_000_000);assert uc.reg_read(UC_MIPS_REG_PC)==stop
  return slots,uc.reg_read(UC_MIPS_REG_V0)
 groups=[]
 for count in (2,30,100):
  setup,result=call(0,count,0);assert result==1;cold,_=call(1,count,0);stationary=[call(1,count,0)[0] for _ in range(3)];moving=[call(1,count,4)[0] for _ in range(4)]
  groups.append(dict(synthetic_team_observers=count,attach_slots=setup,cold_refresh_slots=cold,stationary_refresh_slots=stationary,moving_refresh_slots=moving));print(json.dumps(groups[-1]),flush=True)
 report=dict(passed=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,synthetic_fixture=True,neutral_item_and_roster_stubs=True,workspace_bytes_mips32=sizes['probe_work'],source_current=before=={str(x.relative_to(ROOT)):sha(x) for x in inputs},source_sha256=before,probe_source_sha256=sha(source),mips_elf_sha256=sha(elf),groups=groups,scope='Basic-block MIPS instruction slots are workload only, not cycles or FPS. Production sight.c, match_time.c and vision.c execute unchanged against original height/tree sidecar. Synthetic observers and no-item/no-roster bonuses are benchmark setup. No game-result validation claim.')
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert report['source_current']
if __name__=='__main__':main()
