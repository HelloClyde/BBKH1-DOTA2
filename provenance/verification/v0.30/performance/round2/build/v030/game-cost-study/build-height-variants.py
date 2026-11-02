"""Diagnostic relinks: exact C3 objects except isolated sight TU variants."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path.cwd();BASE=ROOT/'build/v030/perf-candidate3';OUT=ROOT/'build/v030/game-cost-study'
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'sdk')]
from common import BUILD_DATE,binary_check
from h1_bda.header import HeaderFields,encode_header
from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
meta=json.loads((BASE/'DOTA2.build.json').read_bytes());commands=json.loads((BASE/'commands.json').read_bytes())
source=(ROOT/'game/sight.c').read_text();(OUT/'sight-c3-before-height.c').write_text(source,newline='\n')
heightplane=source.replace('unsigned static_tree_stamp,static_valid,static_changed;','unsigned static_tree_stamp,static_valid,static_changed;\n    int16_t leveled_height[GRID*GRID];')
before='''static int height_level(int cell){
    int h;if(!terrain_height)return 0;h=terrain_height[cell];
    if(h==DOTA_SIGHT_HEIGHT_UNKNOWN)return 32767;
    return h>=0?(h+16)/32:-((-h+16)/32);
}'''
assert before in heightplane
heightplane=heightplane.replace(before,'static int height_level(int cell){return work->leveled_height[cell];}')
before='terrain_height=height;terrain_trees=trees;work=memory;zero(work,sizeof(*work));'
assert before in heightplane
heightplane=heightplane.replace(before,before+'\n    for(y=0;y<GRID*GRID;++y){int h=height?height[y]:0;work->leveled_height[y]=(int16_t)(h==DOTA_SIGHT_HEIGHT_UNKNOWN?32767:h>=0?(h+16)/32:-((-h+16)/32));}')
raw=source.replace('int x,y,cx,cy,radius,cell_radius,height,team,region,bonus,heading_x,heading_y,flying;','int x,y,cx,cy,radius,cell_radius,height,team,region,bonus,heading_x,heading_y,flying;\n    int height_limit;')
assert raw!=source and 's->height=height;s->region=region;'in raw
raw=raw.replace('s->height=height;s->region=region;','s->height=height;s->height_limit=height*32+(height<0?16:15);s->region=region;')
assert 'if(height_level(c)>s->height)return 2;'in raw
raw=raw.replace('if(height_level(c)>s->height)return 2;','if(terrain_height){int h=terrain_height[c];if(h==DOTA_SIGHT_HEIGHT_UNKNOWN || h>s->height_limit)return 2;}')
def resolve(x):return x.replace('<REPO>',str(ROOT)).replace('<TOOLCHAIN>',str(ROOT/'.tools/toolchain/bin'))
for label,text in [('base',source),('heightplane',heightplane),('rawlimit',raw)]:
 out=OUT/label;assert not out.exists();out.mkdir();src=out/'sight-variant.c';src.write_text(text,newline='\n')
 cmd=next([resolve(x)for x in c]for c in commands['compile']if c[c.index('-c')+1].endswith('game\\sight.c'))
 cmd[cmd.index('-c')+1]=str(src);cmd[cmd.index('-o')+1]=str(out/'sight.o');subprocess.run(cmd,check=True,capture_output=True)
 link=[resolve(x)for x in commands['link']]
 link=[x.replace(str(BASE/'DOTA2.elf'),str(out/'DOTA2.elf')).replace(str(BASE/'DOTA2.map'),str(out/'DOTA2.map'))if x!=str(BASE/'sight.o')else str(out/'sight.o')for x in link]
 link.insert(-1,'-Wl,--undefined=dota_sight_current_bits');subprocess.run(link,check=True,capture_output=True)
 subprocess.run([str(Path(cmd[0]).with_name('mipsel-none-elf-objcopy.exe')),'-O','binary',str(out/'DOTA2.elf'),str(out/'DOTA2.bin')],check=True,capture_output=True)
 payload=(out/'DOTA2.bin').read_bytes();pad=(-(PAYLOAD_OFFSET+len(payload)))&3
 fields=HeaderFields(category=0x47,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+pad-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
 (out/'DOTA2.bda').write_bytes(encode_header(fields,title='DOTA2',build_time=BUILD_DATE)+(BASE/'DOTA2.bda').read_bytes()[136:PAYLOAD_OFFSET]+payload+bytes(pad))
 binary=binary_check(out/'DOTA2.bda',out/'DOTA2.elf',len(payload))
 result=dict(diagnostic_only=True,physical_h1_tested=False,USB_opened=False,label=label,bda_sha256=sha(out/'DOTA2.bda'),elf_sha256=sha(out/'DOTA2.elf'),bda_bytes=(out/'DOTA2.bda').stat().st_size,binary=binary,reused_objects_from_exact_build='build/v030/perf-candidate3',reused_build_metadata_sha256=sha(BASE/'DOTA2.build.json'),only_recompiled_translation_unit='game/sight.c',variant_path=str(src.relative_to(ROOT)),variant_sha256=sha(src),sight_header_sha256=sha(ROOT/'game/sight.h'),prototype_linker_retains_getter_for_testing=True)
 (out/'DOTA2.build.json').write_text(json.dumps(result,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(dict(compile=cmd,link=link),indent=2)+'\n');print(label,result['bda_sha256'],binary['ram_margin_bytes'],flush=True)
