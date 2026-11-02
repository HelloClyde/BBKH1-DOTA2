"""Exact old/new RGB565 alpha mix: channel exhaustion and standalone MIPS calls.

Instruction counts measure only the compiled function, not a full BDA frame,
hardware timing, visual quality change or FPS. Original arithmetic is the oracle.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,struct,subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN,UC_HOOK_BLOCK
from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_PC,UC_MIPS_REG_V0,UC_MIPS_REG_CP0_STATUS
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def function(text,name):
 m=re.search(r'^static[^;\n]*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M);assert m
 end=m.end();depth=1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[m.start():end]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 current=ROOT/'platform/baked_scene.c';old=a.before.read_text();new=current.read_text();oldfn=function(old,'mix');newfn=function(new,'mix');addfn=function(new,'add_full_rgb565');table='static unsigned char shadow_curve[256][96];\nstatic int shadow_curve_ready;\n'+function(new,'initialize_shadow_curve')+'\n'
 report=dict(passed=False,scope=__doc__,physical_h1_tested=False,full_BDA_executed=False,old_renderer_sha256=sha(a.before),new_renderer_sha256=sha(current),test_sha256=sha(__file__))
 env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 def run(command,log):
  r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120);log.write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr;return r.stdout
 try:
  source=out/'mix-proof.c';source.write_text('#include <stdint.h>\n#include <stdio.h>\n'+table+addfn+'\n'+oldfn.replace(' mix(',' reference_mix(',1)+'\n'+newfn+'''
static unsigned checks;
static int check(unsigned fg,unsigned bg,unsigned a){unsigned kind;for(kind=0;kind<2;++kind){++checks;if(mix((uint16_t)fg,(uint16_t)bg,a,(int)kind)!=reference_mix((uint16_t)fg,(uint16_t)bg,a,(int)kind)){printf("FAIL %04x %04x %u %u\\n",fg,bg,a,kind);return 0;}}return 1;}
int main(void){unsigned n,c,a,f,b,shift,max,rng=0x91d48a7b;initialize_shadow_curve();
for(a=0;a<256;++a)for(c=0;c<96;++c){unsigned value=c<32?c:c-32;++checks;if(shadow_curve[a][c]!=(value*(255u-a)+127u)/255u)return 2;}
for(c=0;c<3;++c){shift=c==0?0:c==1?5:11;max=c==1?63:31;for(a=0;a<256;++a)for(f=0;f<=max;++f)for(b=0;b<=max;++b)if(!check(f<<shift,b<<shift,a))return 1;}
for(n=0;n<2097152u;++n){unsigned fg,bg;rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;fg=rng&65535;rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;bg=rng&65535;a=rng>>24;if(!check(fg,bg,a))return 1;}
printf("{\\"passed\\":true,\\"checks\\":%u,\\"all_channels_alpha_fg_bg_exhaustive\\":true,\\"all_24576_existing_shadow_entries\\":true,\\"random_RGB565_pairs\\":2097152,\\"both_additive_modes\\":true}\\n",checks);return 0;}
''',encoding='utf8')
  exe=out/'mix-proof.exe';run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-Wall','-Wextra','-Werror',source,'-o',exe],out/'host-compile.txt');report['host']=json.loads(run([exe],out/'host-run.txt'))
  # Compile the exact old/new source functions under the production O32/O2
  # flags, with only a public function-call boundary added for this diagnostic.
  cc=ROOT/'.tools/toolchain/bin/mipsel-none-elf-gcc.exe';profiles={};results={}
  for label,body in [('before',oldfn),('after',newfn)]:
   c=out/(label+'.c');c.write_text('#include <stdint.h>\n'+table+addfn+'\n'+body+'\nvoid mix_init_probe(void){initialize_shadow_curve();}\nunsigned mix_probe(unsigned fg,unsigned bg,unsigned a,int additive){return mix((uint16_t)fg,(uint16_t)bg,a,additive);}\n');elf=out/(label+'.elf')
   flags=['-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-O2','-std=c99','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-nostdlib','-Wl,--gc-sections','-Wl,--build-id=none','-Wl,-Ttext=0x83c00020','-Wl,-e,mix_probe','-Wl,-u,mix_init_probe']
   run([cc,*flags,c,'-o',elf],out/(label+'-mips-compile.txt'))
   uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);uc.mem_map(0,64*1024*1024);uc.reg_write(UC_MIPS_REG_CP0_STATUS,0)
   with elf.open('rb')as f:
    e=ELFFile(f);symbols={s.name:int(s['st_value'])for s in e.get_section_by_name('.symtab').iter_symbols()}
    for seg in e.iter_segments():
     if seg['p_type']=='PT_LOAD' and seg['p_filesz']:uc.mem_write(int(seg['p_vaddr'])&0x1fffffff,seg.data())
   uc.reg_write(UC_MIPS_REG_SP,0x818ff000);uc.reg_write(UC_MIPS_REG_RA,0x80001f00);uc.emu_start(symbols['mix_init_probe'],0x80001f00,count=2000000);assert uc.reg_read(UC_MIPS_REG_PC)==0x80001f00
   slots=0;multiplies=0;multiplies_high=0
   def block(_uc,at,size,_user):
    nonlocal slots,multiplies,multiplies_high
    slots+=size//4
    for word in struct.unpack('<'+'I'*(size//4),bytes(_uc.mem_read(at&0x1fffffff,size))):
     if (word>>26)==28 and (word&63)==2:multiplies+=1
     if (word>>26)==0 and (word&63) in [24,25]:multiplies_high+=1
   hook=uc.hook_add(UC_HOOK_BLOCK,block);counts={};outputs=[]
   for mode in [0,1]:
    start=slots;start_mul=multiplies;start_mulhi=multiplies_high;count=0
    for alpha in range(256):
     for fg,bg in [(0,65535),(65535,65535),(0xf800,0x1f),((alpha*977+3329)&65535,(alpha*3571+8849)&65535)]:
      for reg,value in zip((UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3),(fg,bg,alpha,mode)):uc.reg_write(reg,value)
      uc.reg_write(UC_MIPS_REG_SP,0x818ff000);uc.reg_write(UC_MIPS_REG_RA,0x80001f00);uc.emu_start(symbols['mix_probe'],0x80001f00,count=10000);assert uc.reg_read(UC_MIPS_REG_PC)==0x80001f00;outputs.append(uc.reg_read(UC_MIPS_REG_V0));count+=1
    counts[str(mode)]=dict(calls=count,slots=slots-start,mean_slots=(slots-start)/count,mul_per_call=(multiplies-start_mul)/count,mult_or_multu_per_call=(multiplies_high-start_mulhi)/count)
   uc.hook_del(hook);results[label]=outputs;profiles[label]=dict(elf_sha256=sha(elf),source_sha256=sha(c),modes=counts)
  assert results['before']==results['after'];report.update(passed=True,standalone_actual_MIPS=True,mips_profiles=profiles,mips_calls_all_equal=True,source_current=sha(current)==report['new_renderer_sha256']);assert report['source_current']
 except Exception as error:report['error']=str(error);raise
 finally:(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 print(json.dumps(report))
if __name__=='__main__':main()
