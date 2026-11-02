"""Diagnostic base8 plus exact versioned tree-cell bytes."""
from pathlib import Path
root=Path.cwd();out=root/'build/v030/game-cost-study';s=(out/'baseplane8/sight-variant.c').read_text()
old='uint32_t current[2][GRID_WORDS],explored[2][GRID_WORDS],dynamic_trees[GRID_WORDS];'
assert old in s;s=s.replace(old,'uint32_t current[2][GRID_WORDS],explored[2][GRID_WORDS];\n    unsigned char dynamic_tree_cells[GRID*GRID];')
s=s.replace('zero(work->dynamic_trees,sizeof(work->dynamic_trees))','zero(work->dynamic_tree_cells,sizeof(work->dynamic_tree_cells))')
old='work->static_trees_ready=(unsigned)dota_trees_ready();'
assert old in s;s=s.replace(old,old+'\n    if(!work->static_trees_ready && terrain_trees)for(i=0;i<GRID*GRID/8;++i){unsigned mask=terrain_trees[i],b;for(b=0;mask;++b,mask>>=1)if(mask&1u)work->dynamic_tree_cells[i*8+b]=1;}')
s=s.replace('set(work->dynamic_trees,yy*GRID+xx)','work->dynamic_tree_cells[yy*GRID+xx]=1')
s=s.replace('set(work->dynamic_trees,(t->y/CELL)*GRID+t->x/CELL)','work->dynamic_tree_cells[(t->y/CELL)*GRID+t->x/CELL]=1')
old='if((!work->static_trees_ready && terrain_trees && ((terrain_trees[(unsigned)c>>3]>>(c&7))&1u)) || bit(work->dynamic_trees,c))return 1;\n    return 0;'
assert old in s;s=s.replace(old,'return work->dynamic_tree_cells[c];');assert 'work->dynamic_trees'not in s
(out/'sight-base8-treebyte.c').write_text(s,newline='\n')
builder=out/'build-height-variants.py';text=builder.read_text();needle="for label,text in [('base',source),('heightplane',heightplane),('rawlimit',raw)]:";assert needle in text
text=text.replace(needle,"for label,text in [('base8treebyte',(OUT/'sight-base8-treebyte.c').read_text())]:")
exec(compile(text,str(builder),'exec'))
