"""Link a renderer-only diagnostic against the exact C7 objects; no device."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();base=ROOT/'build/v030/perf-candidate7';out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 meta=json.loads((base/'DOTA2.build.json').read_text());commands=json.loads((base/'commands.json').read_text());resolve=lambda x:x.replace('<REPO>',str(ROOT)).replace('<TOOLCHAIN>',str(ROOT/'.tools/toolchain/bin'))
 source=out/'baked_scene.c';source.write_bytes((ROOT/'platform/baked_scene.c').read_bytes())
 for n in ['compressed_cache.h','sprite_rows.h']:
  if(ROOT/'platform'/n).exists():(out/n).write_bytes((ROOT/'platform'/n).read_bytes())
 command=next([resolve(x)for x in c]for c in commands['compile']if Path(c[c.index('-c')+1]).name=='baked_scene.c');command[command.index('-c')+1]=str(source);command[command.index('-o')+1]=str(out/'baked_scene.o')
 run=subprocess.run(command,capture_output=True,text=True);(out/'compile.txt').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
 link=[resolve(x)for x in commands['link']];link=[x.replace(str(base/'DOTA2.elf'),str(out/'DOTA2.elf')).replace(str(base/'DOTA2.map'),str(out/'DOTA2.map'))if x!=str(base/'baked_scene.o')else str(out/'baked_scene.o')for x in link]
 for n in ['bs_reserve_sprite_hot_cache','bs_reserve_sprite_row_cache','bs_prewarm_game_start']:
  if n+'('in source.read_text() or (n=='bs_reserve_sprite_row_cache' and (out/'sprite_rows.h').exists()):link.append('-Wl,-u,'+n)
 run=subprocess.run(link,capture_output=True,text=True);(out/'link.txt').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
 subprocess.run([str(Path(command[0]).with_name('mipsel-none-elf-objcopy.exe')),'-O','binary',str(out/'DOTA2.elf'),str(out/'DOTA2.bin')],check=True,capture_output=True)
 sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'sdk')];from common import BUILD_DATE,binary_check
 from h1_bda.header import HeaderFields,encode_header
 from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES
 payload=(out/'DOTA2.bin').read_bytes();padding=(-(PAYLOAD_OFFSET+len(payload)))&3;fields=HeaderFields(category=0x47,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+padding-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
 (out/'DOTA2.bda').write_bytes(encode_header(fields,title='DOTA2',build_time=BUILD_DATE)+(base/'DOTA2.bda').read_bytes()[136:PAYLOAD_OFFSET]+payload+bytes(padding));binary=binary_check(out/'DOTA2.bda',out/'DOTA2.elf',len(payload))
 meta.update(diagnostic_only=True,physical_h1_tested=False,actual_mips32=False,bda_sha256=sha(out/'DOTA2.bda'),elf_sha256=sha(out/'DOTA2.elf'),bda_bytes=(out/'DOTA2.bda').stat().st_size,binary=binary,reused_objects_from='build/v030/perf-candidate7',only_recompiled_translation_unit='platform/baked_scene.c',reused_base_metadata_sha256=sha(base/'DOTA2.build.json'))
 meta['source_sha256']['platform/baked_scene.c']=sha(source)
 for n in ['compressed_cache.h','sprite_rows.h']:
  if(out/n).exists():meta['source_sha256']['platform/'+n]=sha(out/n)
 (out/'DOTA2.build.json').write_text(json.dumps(meta,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(dict(compile=command,link=link),indent=2)+'\n');print(json.dumps(dict(bda_sha256=meta['bda_sha256'],ram_margin_bytes=binary['ram_margin_bytes'])))
if __name__=='__main__':main()
