"""Diagnostic immutable obstruction-plane variants built on frozen C3 objects."""
from pathlib import Path
root=Path.cwd();builder=root/'build/v030/game-cost-study/build-height-variants.py'
injection=r'''
baseplane=source.replace('int x,y,cx,cy,radius,cell_radius,height,team,region,bonus,heading_x,heading_y,flying;','int x,y,cx,cy,radius,cell_radius,height,team,region,bonus,heading_x,heading_y,flying;\n    const unsigned char *obstacle_plane;'.replace('\\n','\n'))
baseplane=baseplane.replace('typedef struct {\n    const DotaGame *owner;',('typedef struct { int height,region,flying;unsigned valid,age;unsigned char cells[GRID*GRID]; } SightObstaclePlane;\ntypedef struct {\n    const DotaGame *owner;').replace('\\n','\n'))
baseplane=baseplane.replace('unsigned static_tree_stamp,static_valid,static_changed;','unsigned static_tree_stamp,static_valid,static_changed;\n    SightObstaclePlane obstacle_planes[BASE_COUNT];unsigned obstacle_age;'.replace('\\n','\n'))
baseplane=baseplane.replace('if(work){int i;work->owner=0;','if(work){int i;for(i=0;i<BASE_COUNT;++i)work->obstacle_planes[i].valid=0;work->owner=0;')
before='/* 0 clear, 1 tree (its cell may be seen), 2 higher ground/unknown/pit edge. */'
helper='''+"'''"+r'''
static const unsigned char *obstacle_plane_for(const SightSource *s){
    SightObstaclePlane *p=0;int i,c;
    for(i=0;i<BASE_COUNT;++i){SightObstaclePlane *a=&work->obstacle_planes[i];
        if(a->valid && a->height==s->height && a->region==s->region && a->flying==s->flying){a->age=++work->obstacle_age;return a->cells;}
        if(!p || !a->valid || (p->valid && a->age<p->age))p=a;
    }
    for(c=0;c<GRID*GRID;++c)p->cells[c]=(unsigned char)(work->regions[c]!=s->region || (!s->flying && (s->height==32767 || height_level(c)>s->height))?2:0);
    p->height=s->height;p->region=s->region;p->flying=s->flying;p->age=++work->obstacle_age;p->valid=1;return p->cells;
}
'''+"'''"+r'''
assert before in baseplane;baseplane=baseplane.replace(before,helper+'\n'+before)
before='''+"'''"+r'''    if(work->regions[c]!=s->region)return 2;
    if(s->flying)return 0;
    if(s->height==32767)return 2;
    if(height_level(c)>s->height)return 2;'''+"'''"+r'''
assert before in baseplane;baseplane=baseplane.replace(before,'    if(s->obstacle_plane[c])return 2;\n    if(s->flying)return 0;'.replace('\\n','\n'))
before='zero(s->mask,sizeof(s->mask));set(s->mask,MAX_RADIUS*LOCAL_SIDE+MAX_RADIUS);'
assert before in baseplane;baseplane=baseplane.replace(before,'s->obstacle_plane=obstacle_plane_for(s);'+before)
'''
text=builder.read_text();needle="for label,text in [('base',source),('heightplane',heightplane),('rawlimit',raw)]:"
assert needle in text
text=text.replace(needle,injection+"\nfor label,text in [('baseplane4',baseplane.replace('BASE_COUNT','4')),('baseplane8',baseplane.replace('BASE_COUNT','8'))]:")
exec(compile(text,str(builder),'exec'))
