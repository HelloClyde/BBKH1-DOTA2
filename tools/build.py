"""Build the standalone H1 v0.27 target with its original options and source order."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,os,sys
from common import *

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--toolchain',type=Path,default=Path(os.environ.get('H1_GNU_BIN',ROOT/'.tools/toolchain/bin')));p.add_argument('--output',type=Path,default=ROOT/'build/native');p.add_argument('--jobs',type=int,default=4);a=p.parse_args()
 sdk_check();assets=assets_check();origin=curation_check();sources=source_files();before=input_hashes();out=local(a.output)
 require(not out.exists(),'Choose a new output directory; existing build is preserved')
 cc=find_tool(a.toolchain,'gcc');objcopy=find_tool(a.toolchain,'objcopy');compiler=run([cc,'--version']).splitlines()[0]
 require('15.2.0' in compiler,'Exact-reference builds require GNU MIPS GCC15.2.0')
 flags=['-DDOTA_BAKED','-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-O2','-g','-std=c99','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections','-Wall','-Wextra',f'-ffile-prefix-map={ROOT}=bbk-h1/h1-dota2',f'-fdebug-prefix-map={ROOT}=bbk-h1/h1-dota2']
 for folder in (ROOT/'game',ROOT/'platform',ROOT/'private/generated',SDK/'sdk/include'):flags+=['-I',str(folder)]
 out.mkdir(parents=True);commands=[]
 def compile_one(source):
  obj=out/(source.stem+'.o');command=[cc,*flags,'-c',source,'-o',obj];run(command);return obj,command
 with ThreadPoolExecutor(max_workers=max(1,a.jobs)) as pool:compiled=list(pool.map(compile_one,sources))
 objects=[item[0] for item in compiled];commands=[item[1] for item in compiled]
 elf=out/'DOTA2.elf';raw=out/'DOTA2.bin';bda=out/'DOTA2.bda'
 link=[cc,*flags,'-nostdlib','-Wl,--gc-sections','-Wl,--build-id=none',f'-Wl,-T,{ROOT/"platform/h1.ld"}',f'-Wl,-Map,{out/"DOTA2.map"}','-o',elf,*objects,'-lgcc'];run(link)
 run([objcopy,'-O','binary',elf,raw])
 from h1_bda.header import HeaderFields,encode_header
 from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES,build_icon_resources
 payload=raw.read_bytes();padding=(-(PAYLOAD_OFFSET+len(payload)))&3
 header=HeaderFields(category=0x47,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+padding-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
 bda.write_bytes(encode_header(header,title='DOTA2',build_time=BUILD_DATE)+build_icon_resources(ROOT/'private/generated/icon.png')+payload+bytes(padding))
 binary=binary_check(bda,elf,len(payload));require(before==input_hashes(),'Build inputs changed during compilation')
 require(sha(bda)==REFERENCE_BDA,'Rebuilt BDA differs from the validated v0.27 reference; preserve this build for diagnosis')
 metadata=dict(title='DOTA2',version=VERSION,build_date=BUILD_DATE,bda_sha256=sha(bda),bda_bytes=bda.stat().st_size,elf_sha256=sha(elf),payload_bytes=len(payload),sdk_commit=SDK_COMMIT,compiler=compiler,compiler_optimization=dict(default='O2',size_optimized_modules=[]),translation_units=[relative(x) for x in sources],source_sha256=before,source_origin=origin,runtime_assets=assets,binary=binary,diagnostic_only=False,physical_h1_tested=False,actual_mips32=False,reference_v027_bda_matches=True,verification_scope='Recompiled standalone sources, BDA envelope, actual ELF payload/ABI/RAM and exact reference SHA. No emulator, firmware or device was executed by this tool.')
 write(out/'DOTA2.build.json',metadata);(out/'DOTA2.bda.sha256').write_text(sha(bda)+'  DOTA2.bda\n',encoding='ascii')
 # The command transcript uses repository-relative path placeholders for privacy.
 def portable(value):return str(value).replace(str(ROOT),'<REPO>').replace(str(Path(a.toolchain).resolve()),'<TOOLCHAIN>')
 write(out/'commands.json',dict(compile=[[portable(v) for v in row] for row in commands],link=[portable(v) for v in link]))
 print(json.dumps(dict(passed=True,bda_sha256=sha(bda),bda_bytes=bda.stat().st_size,elf_sha256=sha(elf),output=relative(out),device_accessed=False)))

if __name__=='__main__':main()
