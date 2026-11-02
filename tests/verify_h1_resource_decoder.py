"""Run prepared decoder modules as actual MIPS locally; fake ticks are not timing.

Checks vector CRC, opcode behavior, cooperative cancellation, stack guards and
static GCC frame sizes. Does not open USB or claim physical throughput/FPS.
"""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN,UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_PC,UC_MIPS_REG_V0,UC_MIPS_REG_CP0_STATUS
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--preparation',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();prep=json.loads(a.preparation.read_text());out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);report=dict(passed=False,physical_h1_tested=False,USB_opened=False,scope=__doc__,preparation_sha256=sha(a.preparation),tests=[],stack_usage=[])
 for v in prep['vectors']:
  m=v['module'];source=Path(v['source']);assert sha(source)==v['source_sha256'];binary=Path(m['binary']);assert sha(binary)==m['sha256'];base=m['base'];raw=binary.read_bytes()
  for opcode,cancel_at in [(0,0),(1,0),(0,3)]:
   u=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);u.mem_map(0,64*1024*1024);u.reg_write(UC_MIPS_REG_CP0_STATUS,0)
   def write(at,data):u.mem_write(at&0x1fffffff,data)
   def read(at,n):return u.mem_read(at&0x1fffffff,n)
   write(base,raw);stack=0x81010000;floor=stack-32768;write(floor,bytes([0xa5])*32768);api=0x81200000;call=api+128;output=api+256;args=api+512;write(args,bytes([12]));write(api,struct.pack('<7I',1,28,0x80001000,0x80001100,0,0,0));write(call,struct.pack('<10I',1,40,opcode,1,args,output,4096,0,api+1024,16384));state=dict(ticks=0,polls=0)
   def hook(uc,address,size,user):
    if address==0x80001000:state['ticks']+=1;uc.reg_write(UC_MIPS_REG_V0,state['ticks']);uc.reg_write(UC_MIPS_REG_PC,uc.reg_read(UC_MIPS_REG_RA))
    elif address==0x80001100:state['polls']+=1;uc.reg_write(UC_MIPS_REG_V0,int(bool(cancel_at and state['polls']>=cancel_at)));uc.reg_write(UC_MIPS_REG_PC,uc.reg_read(UC_MIPS_REG_RA))
   u.hook_add(UC_HOOK_CODE,hook);u.reg_write(UC_MIPS_REG_A0,api);u.reg_write(UC_MIPS_REG_A1,call);u.reg_write(UC_MIPS_REG_SP,stack);u.reg_write(UC_MIPS_REG_RA,0x80001f00);u.emu_start(base+m['entry_offset'],0x80001f00,timeout=15_000_000,count=100_000_000);assert u.reg_read(UC_MIPS_REG_PC)==0x80001f00
   values=struct.unpack('<20I',read(output,80));assert values[0]==1 and values[1]==v['codec'] and values[5]==v['canonical_crc32'];assert values[6]==v['canonical_crc32'];assert values[14]==int(bool(cancel_at));assert values[7]>0 and state['polls']>=values[7]*2;assert struct.unpack('<I',read(call+28,4))[0]==80
   touched=read(floor,32768);first=next((i for i,x in enumerate(touched) if x!=0xa5),32768);assert first>16384
   report['tests'].append(dict(codec=v['codec'],opcode=opcode,cancel_at_poll=cancel_at,polls=state['polls'],iterations=values[7],canonical_crc32=values[6],flags=values[14],return_value=u.reg_read(UC_MIPS_REG_V0),observed_stack_highwater_bytes=32768-first,stack_guard_16KiB_intact=True,module_sha256=m['sha256'],fake_ticks_not_performance=True))
  tc=ROOT/'.tools/toolchain/bin';obj=out/f'codec{v["codec"]}.o';cmd=[tc/'mipsel-none-elf-gcc.exe','-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-O2','-ffreestanding','-fno-builtin','-fno-common','-fno-stack-protector','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-fstack-usage','-I',ROOT.parent/'experiments/h1-usb-runner','-I',ROOT.parent/'vendor/h1-sdk/sdk/include','-c',source,'-o',obj];r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True);assert not r.returncode,r.stdout+r.stderr;report['stack_usage'].append(dict(codec=v['codec'],compiler_stack_usage=obj.with_suffix('.su').read_text()))
 report['passed']=True;(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,actual_mips_tests=len(report['tests']),observed_max_stack_bytes=max(t['observed_stack_highwater_bytes'] for t in report['tests']),physical_h1_tested=False,report=str(out/'verification.json'))))
if __name__=='__main__':main()
