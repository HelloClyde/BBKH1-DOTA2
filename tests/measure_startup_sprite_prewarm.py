"""Isolated existing-cache prewarm trial using exact C7 get_sprite and real IDs.

This intentionally optimistic trial knows the initial visible clip/directions
from the normal C7 trace. It measures transferred/added loading work, not FPS.
No application entry, simulation mutation, sound playback or USB is executed.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--trace',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 scene=ROOT/'build/v030/legacy/SCENE.H1P';pack=ROOT/'assets/runtime/DOTA2.H1R';sprites=json.loads(a.trace.read_text())
 with scene.open('rb')as f:h=struct.unpack('<16I',f.read(64));f.seek(h[9]);frames=list(struct.iter_unpack('<IIHHhh8H',f.read(h[10]*32)))
 groups={(0,0,0),(3,1,8),(93,1,9),(95,1,12),(77,1,12),(75,0,4),(62,0,0)};ids={i for i,row in enumerate(frames)if row[6:9]in groups};ids|={i for i,row in enumerate(frames)if row[6]in[3,93,95,77]and row[7:10]==(0,0,0)}
 for name,values in [('menu',[x[2]for x in sprites if x[0]=='menu']),('play',[x[2]for x in sprites if x[0]=='play']),('prewarm',sorted(ids))]:(out/(name+'.bin')).write_bytes(b''.join(struct.pack('<I',x)for x in values))
 harness='''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource_pack.h"
#include "SOURCE"
static void fail(int n){fprintf(stderr,"failed line%d resource%u scene%d\\n",n,resource_stats()->last_error,bs_last_error());exit(1);}
#define C(x) do{if(!(x))fail(__LINE__);}while(0)
static void phase(unsigned n,const ResourceStats*b){const ResourceStats*a=resource_stats();printf("{\\"phase\\":%u,\\"requests\\":%u,\\"reads\\":%u,\\"physical_bytes\\":%u,\\"seeks\\":%u,\\"decoded\\":%u,\\"decoded_bytes\\":%u}\\n",n,a->read_requests-b->read_requests,a->physical_reads-b->physical_reads,a->physical_bytes-b->physical_bytes,a->seeks-b->seeks,a->decoded_chunks-b->decoded_chunks,a->decoded_bytes-b->decoded_bytes);}
static void replay(const char*path,FILE*reference,unsigned char*scratch){FILE*f=fopen(path,"rb");unsigned id;C(f);while(fread(&id,4,1,f)==1){const unsigned char*p=get_sprite(id);const RuntimeFrame*r=frame_info(id);unsigned bytes=rf_size(r);C(p&&r&&bytes<=524288);C(!fseek(reference,(long)r->offset,SEEK_SET));C(fread(scratch,1,bytes,reference)==bytes);C(!memcmp(p,scratch,bytes));}C(feof(f));fclose(f);}
int main(int argc,char**argv){ResourceStats b;FILE*ref;unsigned char*scratch=malloc(524288);C(argc==7&&scratch);ref=fopen(argv[2],"rb");C(ref);C(resource_mount(argv[1]));C(resource_reserve_read_cache()>0);C(baked_io_open_resource(0)&&bs_initialize());C(baked_io_reserve_cache(262144)==262144);C(bs_reserve_cache(14u*1024u*1024u)==14u*1024u*1024u);C(bs_prewarm_fountain(0,0)==624);b=*resource_stats();replay(argv[3],ref,scratch);phase(0,&b);b=*resource_stats();if(atoi(argv[6]))replay(argv[5],ref,scratch);phase(1,&b);b=*resource_stats();replay(argv[4],ref,scratch);phase(2,&b);C(!bs_last_error()&&!resource_stats()->last_error);bs_close();C(resource_unmount()&&!resource_heap_bytes());fclose(ref);free(scratch);return 0;}
'''.replace('SOURCE',a.source.resolve().as_posix());(out/'trial.c').write_text(harness)
 meta=json.loads((ROOT/'build/v030/perf-candidate7/commands.json').read_text());all_sources=[Path(c[c.index('-c')+1].replace('<REPO>',str(ROOT)))for c in meta['compile']];all_sources=[s for s in all_sources if s.suffix=='.c'and s.name not in ['native.c','baked_scene.c','render.c','dota_audio.c']];env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');exe=out/'trial.exe';cmd=['C:/msys64/ucrt64/bin/gcc.exe','-O2','-std=c99','-DBAKED_IO_HOST','-DDOTA_BAKED','-DDOTA_RESOURCE_PACK','-ffunction-sections','-fdata-sections','-Iplatform','-Igame','-Iprivate/generated','-Isdk/sdk/include',out/'trial.c',*all_sources,'-Wl,--gc-sections','-o',exe];r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True);(out/'compile.txt').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 results={}
 for warm in [0,1]:
  r=subprocess.run(list(map(str,[exe,pack,scene,out/'menu.bin',out/'play.bin',out/'prewarm.bin',warm])),cwd=ROOT,env=env,capture_output=True,text=True);assert not r.returncode,r.stdout+r.stderr;results[str(warm)]=[json.loads(x)for x in r.stdout.splitlines()]
 report=dict(passed=True,scope=__doc__,actual_frozen_C7_sprite_source_sha256=sha(a.source),trace_sha256=sha(a.trace),pack_sha256=sha(pack),reader_sha256=sha(ROOT/'platform/resource_pack.c'),candidate_frames=len(ids),candidate_raw_bytes=sum(frames[i][1]for i in ids),candidate_ids=sorted(ids),physical_h1_tested=False,USB_opened=False,full_returned_frame_bytes_equal_canonical=True,results=results,limitations='Cache-only replay executes real C7 get_sprite and warmup. It omits unrelated map/item/audio reads and sim/renderer scheduling, so this is an optimistic lower bound on contention, not native startup latency. No Game/tick/history exists in the trial. Prewarm has oracle knowledge of the captured roster/directions and is not a production feature.');(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
