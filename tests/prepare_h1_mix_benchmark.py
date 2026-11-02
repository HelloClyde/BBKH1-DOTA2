"""Prepare bounded old/new additive-mix H1 EXEC source, without opening USB.

Source functions are extracted exactly from the frozen old and current renderer.
The compile base is offline sizing only; h1_debug exec run must rebuild using
the fresh resident allocation. No complete BDA, file, display or audio is run.
"""
from pathlib import Path
import argparse,hashlib,json,re,sys
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
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--base',type=lambda s:int(s,0),default=0x81700000);p.add_argument('--batch-iterations',type=int,choices=[16384,262144],default=262144,help='H1 poll_cancel yields about one raw tick; use larger bounded batches for timing');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 before=a.before.resolve();after=ROOT/'platform/baked_scene.c';old=before.read_text();new=after.read_text();source=out/'module.c'
 declarations='#include "module_abi.h"\nstatic unsigned char shadow_curve[256][96];\nstatic int shadow_curve_ready;\n'
 declarations+=function(new,'initialize_shadow_curve')+'\n'+function(new,'add_full_rgb565')+'\n'
 declarations+=function(old,'mix').replace(' mix(',' mix_original(',1)+'\n'+function(new,'mix').replace(' mix(',' mix_optimized(',1)+'\n'
 source.write_text('/* Original renderer SHA256 '+sha(before)+'; optimized '+sha(after)+'. */\n'+declarations+'''
typedef uint16_t (*MixFn)(uint16_t,uint16_t,unsigned,int);
typedef struct {uint32_t count,begin,end,ticks,crc,flags;} BenchCase;
static uint32_t crc_word(uint32_t crc,uint32_t value){unsigned b,k;for(b=0;b<4;++b){crc^=(value>>(b*8))&255u;for(k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&-(int)(crc&1u));}return crc;}
/* One batch remains bounded and both algorithms see the same deterministic
 * RGB/alpha sequence. Polling and the per-batch CRC cost are included. */
static __attribute__((noinline,noclone)) BenchCase measure(const h1_exec_api *api,unsigned variant,unsigned requested){
 BenchCase r={0,0,0,0,0,0};MixFn fn=variant?mix_optimized:mix_original;uint32_t rng=0x91d48a7bu,crc=0xffffffffu;
 r.begin=api->tick_80hz();
 while(r.count<requested){uint32_t n=requested-r.count,i,sum=0;if(n>16384u)n=16384u;
  for(i=0;i<n;++i){uint16_t fg,bg;unsigned a;rng=rng*1664525u+1013904223u;fg=(uint16_t)(rng>>16);bg=(uint16_t)(rng^(rng>>11));a=(rng>>8)&255u;sum+=(uint32_t)fn(fg,bg,a,1)+(r.count+i);}
  r.count+=n;crc=crc_word(crc,sum);
  if(api->poll_cancel()){r.flags|=1u;break;}
  if(api->tick_80hz()-r.begin>=24u && r.count<requested){r.flags|=2u;break;}
 }
 r.end=api->tick_80hz();r.ticks=r.end-r.begin;r.crc=crc^0xffffffffu;return r;
}
int h1_module_entry(const h1_exec_api *api,h1_exec_call *call){
 uint32_t requested=1048576u,*out;BenchCase old,new;unsigned i;
 if(!api || api->version!=H1_EXEC_ABI || !call || call->version!=H1_EXEC_ABI || call->opcode>1u || call->output_capacity<80u)return -1;
 if(call->arg_size){if(call->arg_size!=4u)return -2;requested=call->args[0]|((uint32_t)call->args[1]<<8)|((uint32_t)call->args[2]<<16)|((uint32_t)call->args[3]<<24);}
 if(!requested || requested>2097152u)return -3;
 initialize_shadow_curve();if(api->poll_cancel())return -4;
 if(call->opcode){new=measure(api,1,requested);old=measure(api,0,new.count);}else{old=measure(api,0,requested);new=measure(api,1,old.count);}
 out=(uint32_t *)(void *)call->output;for(i=0;i<20u;++i)out[i]=0;
 out[0]=1;out[1]=call->opcode;out[2]=requested;out[3]=old.count;out[4]=old.begin;out[5]=old.end;out[6]=old.ticks;out[7]=old.crc;out[8]=old.flags;
 out[9]=new.count;out[10]=new.begin;out[11]=new.end;out[12]=new.ticks;out[13]=new.crc;out[14]=new.flags;
 out[15]=old.count==new.count && old.crc==new.crc;out[16]=24576;out[17]=16384;out[18]=24;out[19]=(uint32_t)shadow_curve_ready;call->output_size=80;
 return out[15] && !(old.flags|new.flags)?0:-5;
}
'''.replace('16384u',str(a.batch_iterations)+'u'),encoding='utf8')
 sys.path.insert(0,str(ROOT.parent/'tools'));from h1_debug import build_module
 metadata,binary=build_module(source,a.base,out/'offline-module');assert metadata['memory_size']<65536
 receipt=dict(passed=True,scope=__doc__,USB_opened=False,physical_h1_tested=False,before_renderer_sha256=sha(before),after_renderer_sha256=sha(after),prepare_sha256=sha(__file__),module_source_sha256=sha(source),builder_sha256=sha(ROOT.parent/'tools/h1_debug.py'),offline_base=a.base,batch_iterations=a.batch_iterations,module=metadata,output_u32_fields=['version','order_opcode','requested_count','old_count','old_begin_tick','old_end_tick','old_elapsed_ticks','old_crc32_of_batch_sums','old_flags','new_count','new_begin_tick','new_end_tick','new_elapsed_ticks','new_crc32_of_batch_sums','new_flags','same_count_and_crc','existing_table_bytes','batch_iterations','case_soft_limit_raw_ticks','shadow_table_ready'],execution='Use fresh h1_debug exec run module.c --directory <new-dir> --opcode 0 or1 --budget-ticks 160. Optional args little-endian iteration count1..2097152, default1048576. Opcode0 old then new;1 reverses order. API raw tick is not asserted80Hz. Each bounded batch polls; poll time is included, and historical16384 batches hid compute differences behind one-tick yields. New default262144 keeps4polls per1M case. Each case stops after24raw ticks if count unfinished. flags1 canceled/flags2 duration cap. Only matched counts/CRC and zero flags support same-work timing comparison. Microbenchmark excludes complete game/render/display/audio and is not FPS. No dynamic allocations or persistent device resources. Module source and table are replaced only by root after checking the current EXEC state.')
 (out/'preparation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(passed=True,module_source=str(source),image_bytes=metadata['image_size'],memory_bytes=metadata['memory_size'],USB_opened=False)))
if __name__=='__main__':main()
