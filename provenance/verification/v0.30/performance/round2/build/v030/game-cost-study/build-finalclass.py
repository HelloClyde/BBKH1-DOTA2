"""Diagnostic C6 plus cached final obstruction classes, no production edits."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path.cwd();BASE=ROOT/'build/v030/perf-candidate6';OUT=ROOT/'build/v030/game-cost-study/finalclass';OUT.mkdir()
source=(ROOT/'game/sight.c').read_text();(OUT.parent/'sight-c6-before-finalclass.c').write_text(source,newline='\n')
source=source.replace('unsigned char dynamic_tree_cells[GRID*GRID];','unsigned char tree_cells[2][GRID*GRID];unsigned active_tree_plane;')
start=source.index('static void dynamic_trees(');end=source.index('static const unsigned char *obstacle_plane_for',start);block=source[start:end]
block=block.replace('unsigned hash=2166136261u;int i;','unsigned hash=2166136261u;int i;unsigned char *next;')
block=block.replace('work->trees_valid=1;', 'next=work->tree_cells[work->active_tree_plane^1u];work->trees_valid=1;')
block=block.replace('zero(work->dynamic_tree_cells,sizeof(work->dynamic_tree_cells))','zero(next,GRID*GRID)').replace('work->dynamic_tree_cells[','next[')
at=block.rfind('\n}');assert at>=0
patch='''
    for(i=0;i<GRID*GRID;++i)if(next[i]!=work->tree_cells[work->active_tree_plane][i]){int p;
        for(p=0;p<OBSTACLE_PLANES;++p){SightObstaclePlane*a=&work->obstacle_planes[p];
            if(a->valid && !a->flying && a->cells[i]!=2)a->cells[i]=next[i];
        }
    }
    work->active_tree_plane^=1u;
'''
block=block[:at]+patch+block[at:];source=source[:start]+block+source[end:]
old='p->cells[c]=(unsigned char)(work->regions[c]!=s->region || (!s->flying && (s->height==32767 || height_level(c)>s->height))?2:0);'
assert old in source;source=source.replace(old,'p->cells[c]=(unsigned char)(work->regions[c]!=s->region || (!s->flying && (s->height==32767 || height_level(c)>s->height))?2:s->flying?0:work->tree_cells[work->active_tree_plane][c]);')
source=source.replace('if(s->obstacle_plane){if(s->obstacle_plane[c])return 2;}','if(s->obstacle_plane)return s->obstacle_plane[c];')
source=source.replace('return work->dynamic_tree_cells[c];','return work->tree_cells[work->active_tree_plane][c];');assert'dynamic_tree_cells'not in source
src=OUT/'sight-variant.c';src.write_text(source,newline='\n')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();meta=json.loads((BASE/'DOTA2.build.json').read_bytes());commands=json.loads((BASE/'commands.json').read_bytes());resolve=lambda x:x.replace('<REPO>',str(ROOT)).replace('<TOOLCHAIN>',str(ROOT/'.tools/toolchain/bin'))
cmd=next([resolve(x)for x in c]for c in commands['compile']if c[c.index('-c')+1].endswith('game\\sight.c'));cmd[cmd.index('-c')+1]=str(src);cmd[cmd.index('-o')+1]=str(OUT/'sight.o');subprocess.run(cmd,check=True,capture_output=True)
link=[resolve(x)for x in commands['link']];link=[x.replace(str(BASE/'DOTA2.elf'),str(OUT/'DOTA2.elf')).replace(str(BASE/'DOTA2.map'),str(OUT/'DOTA2.map'))if x!=str(BASE/'sight.o')else str(OUT/'sight.o')for x in link];subprocess.run(link,check=True,capture_output=True)
subprocess.run([str(Path(cmd[0]).with_name('mipsel-none-elf-objcopy.exe')),'-O','binary',str(OUT/'DOTA2.elf'),str(OUT/'DOTA2.bin')],check=True,capture_output=True)
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'sdk')];from common import BUILD_DATE,binary_check
from h1_bda.header import HeaderFields,encode_header
from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES
payload=(OUT/'DOTA2.bin').read_bytes();pad=(-(PAYLOAD_OFFSET+len(payload)))&3;fields=HeaderFields(category=0x47,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+pad-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
(OUT/'DOTA2.bda').write_bytes(encode_header(fields,title='DOTA2',build_time=BUILD_DATE)+(BASE/'DOTA2.bda').read_bytes()[136:PAYLOAD_OFFSET]+payload+bytes(pad));binary=binary_check(OUT/'DOTA2.bda',OUT/'DOTA2.elf',len(payload))
result=dict(diagnostic_only=True,physical_h1_tested=False,label='finalclass',bda_sha256=sha(OUT/'DOTA2.bda'),elf_sha256=sha(OUT/'DOTA2.elf'),binary=binary,reused_objects_from_exact_build='build/v030/perf-candidate6',reused_build_metadata_sha256=sha(BASE/'DOTA2.build.json'),only_recompiled_translation_unit='game/sight.c',variant_sha256=sha(src))
(OUT/'DOTA2.build.json').write_text(json.dumps(result,indent=2)+'\n');(OUT/'commands.json').write_text(json.dumps(dict(compile=cmd,link=link),indent=2)+'\n');print(result['bda_sha256'],binary['ram_margin_bytes'])
