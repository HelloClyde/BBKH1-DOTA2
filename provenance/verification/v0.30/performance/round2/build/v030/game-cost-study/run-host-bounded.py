"""Bounded obstruction cache: >8 source classes preserve exact old masks."""
from pathlib import Path
root=Path.cwd();p=root/'build/v030/game-cost-study/run-host-enhanced.py';text=p.read_text().replace('host-reference','host-bounded2')
bounded=r'''
unsigned new_obstacle_count(void),new_obstacle_hash(void);
int new_source_is_cached(void),new_source_has_cached_class(void);
void old_mode_source(const DotaGame*,int,int,int,int),new_mode_source(const DotaGame*,int,int,int,int);
static void bounded_classes(void){
 static const short height[]={-64,-32,0,32,64,96,128,160,192,224,256,288,320,32767};
 unsigned i,frozen=0;int x,y=2008;
 dota_trees_detach();memset(terrain,0,sizeof(terrain));memset(tree_bits,0,sizeof(tree_bits));
 for(i=0;i<14u;++i){x=264+(int)i*160;terrain[(y/16)*256+x/16]=height[i];}
 attach_both(terrain,tree_bits);
 for(i=0;i<24u;++i){unsigned c=i<14u?i:(i-14u)%8u;int flying=i>=22u;x=264+(int)c*160;
  if(i==23u){x=dota_roshan_pit_positions[0][0];y=dota_roshan_pit_positions[0][1];}
  old_mode_source(&game,x,y,400,flying);new_mode_source(&game,x,y,400,flying);old_copy_masks(reference_masks);new_copy_masks(candidate_masks);
  CHECK(!memcmp(reference_masks,candidate_masks,sizeof(reference_masks)));compared_bytes+=sizeof(reference_masks);
  CHECK(new_obstacle_count()==(i<8u?i+1u:8u));
  if(i<8u)CHECK(new_source_is_cached());else if(i<14u||i>=22u)CHECK(!new_source_is_cached());else CHECK(new_source_has_cached_class());
  if(i==7u)frozen=new_obstacle_hash();else if(i>7u)CHECK(new_obstacle_hash()==frozen);
  ++cases;printf("CASE %u bounded-cache-class-%u cached%d\n",cases,i,new_source_is_cached());
 }
}
'''
needle="fixture=fixture.replace('int main(int argc,char**argv){',extra+"
assert needle in text;text=text.replace(needle,'bounded='+repr(bounded)+'\n'+needle.replace('extra+','extra+bounded+'))
text=text.replace('mutations();fine_radius();height_edges();slark_and_failure','mutations();fine_radius();height_edges();bounded_classes();slark_and_failure')
oldhelper='void old_mode_source(const DotaGame*g,int x,int y,int r,int f){work->active_count=0;dynamic_trees(g);zero(work->current,sizeof(work->current));source_add(10,0,x,y,r,0,0,0,f);}\n'
newhelper='''void new_mode_source(const DotaGame*g,int x,int y,int r,int f){work->active_count=0;dynamic_trees(g);work->static_changed=!work->static_valid || work->static_tree_stamp!=work->tree_stamp;zero(work->next_static_members,sizeof(work->next_static_members));source_add(10,0,x,y,r,0,0,0,f,1);merge_sources();}
unsigned new_obstacle_count(void){unsigned n=0;int i;for(i=0;i<OBSTACLE_PLANES;++i)n+=work->obstacle_planes[i].valid!=0;return n;}
int new_source_is_cached(void){return work->source[10].obstacle_plane!=0;}
int new_source_has_cached_class(void){SightSource*s=&work->source[10];int i;for(i=0;i<OBSTACLE_PLANES;++i){SightObstaclePlane*p=&work->obstacle_planes[i];if(p->valid&&p->height==s->height&&p->region==s->region&&p->flying==s->flying)return 1;}return 0;}
unsigned new_obstacle_hash(void){unsigned h=2166136261u;int i,c;for(i=0;i<OBSTACLE_PLANES;++i){SightObstaclePlane*p=&work->obstacle_planes[i];h=mix(h,(unsigned)p->height);h=mix(h,(unsigned)p->region);h=mix(h,(unsigned)p->flying);for(c=0;c<GRID*GRID;++c)h=mix(h,p->cells[c]);}return h;}
'''
wrapper_code="with wrapper.open('a')as f:f.write("+repr(oldhelper)+")\nwith newwrapper.open('a')as f:f.write("+repr(newhelper)+")\nrename=["
injection="text=text.replace('rename=[',"+repr(wrapper_code)+")\n"
text=text.replace("exec(compile(text,str(study/'run-host.py'),'exec'))",injection+"exec(compile(text,str(study/'run-host.py'),'exec'))")
exec(compile(text,str(p),'exec'))
