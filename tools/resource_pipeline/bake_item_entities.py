"""Original Steam ward skins, real Book of the Dead units and planted tree."""
from pathlib import Path
import concurrent.futures,hashlib,json,math,subprocess,sys
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from bake_sprites import Model,source_normals
from bake_hero_shadows import project_shadow,compact_shadow,projection_metadata
from bake_roshan_ancients import unit_block,preview_frame
from source2_vpk import VpkArchive
SOURCE=ROOT/'private/source2/exports/item-entities';OUT=ROOT/'private/baked/item-entities';SCALE=100/2.54/4
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota');TOOL=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
SPECS=[dict(id=88,name='observer',asset='default_ward',unit='npc_dota_observer_wards',scale=1,skin=0,clips={0:'default_ward_idle',2:'default_ward_idle',4:'default_ward_die'}),
 dict(id=89,name='sentry',asset='default_ward',unit='npc_dota_sentry_wards',scale=1,skin=1,clips={0:'default_ward_idle',2:'default_ward_idle',4:'default_ward_die'}),
 dict(id=90,name='demonicon_melee',asset='necro_warrior',unit='npc_dota_necronomicon_warrior_3',scale=.6,skin=0,clips={0:'idle',1:'run',2:'attack',4:'death'}),
 dict(id=91,name='demonicon_ranged',asset='necro_archer',unit='npc_dota_necronomicon_archer_3',scale=.6,skin=0,clips={0:'archer_idle',1:'archer_run',2:'archer_attack',4:'archer_death'}),
 dict(id=92,name='ironbranch_tree',asset='ggbranch',unit=None,scale=1,skin=0,clips={0:None})]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def rel(p):return Path(p).resolve().relative_to(ROOT).as_posix()
class Entity(Model):
 def __init__(self,spec):
  self.spec=spec;self.slug=spec['name'];self.model_id=spec['id'];self.paths=[SOURCE/spec['asset']/(spec['asset']+'.gltf')];self.docs=[Gltf(self.paths[0])];self.body=self.docs[0];self.names={a['name']:i for i,a in enumerate(self.body.doc.get('animations',[]))};self.textures={};self.scale=SCALE*spec['scale'];self.height=1
  self.root=self.body.doc['skins'][0]['joints'][0] if self.body.doc.get('skins') else None;self.root0=self.body.globals(self.names.get(spec['clips'][0]),0)[self.root][:3,3] if self.root is not None else np.zeros(3)
  if spec['skin']==1:self.textures[0,0]=(np.asarray(Image.open(SOURCE/'default_ward/sentry_color.png').convert('RGBA')).copy(),0,np.ones(4))
  pose=self.pose(spec['clips'][0],0);points=np.concatenate([p['points'] for p in pose]);self.height=int(math.ceil(points[:,2].max()));self.triangles=sum(len(p['faces']) for p in pose)
 def pose(self,clip,seconds):
  ai=self.names.get(clip);matrices=self.body.globals(ai,seconds);motion=matrices[self.root][:3,3]-self.root0 if self.root is not None else np.zeros(3);out=[]
  for original in self.body.primitives(ai,seconds):
   if any(t in original['name'].lower() for t in ('physics','collision','inspector')):continue
   p=dict(original,doc=0);q=p['points'];n=source_normals(self.body,p,matrices);p['points']=np.column_stack((-q[:,0]+motion[0],q[:,2]-motion[2],q[:,1]))*self.scale;p['normals']=np.column_stack((-n[:,0],n[:,2],n[:,1]));out.append(p)
  return out
def bake(spec):
 m=Entity(spec);folder=OUT/spec['name'];folder.mkdir(parents=True,exist_ok=True);frames=[];motions=[];mins=[];maxs=[]
 for clip,source in spec['clips'].items():
  count=8 if source else 1;duration=m.body.duration(m.names[source]) if source else .05;ticks=max(count,round(duration*20));loop=clip in (0,1);previews=[]
  motions.append(dict(model=m.model_id,clip=clip,source_clip=source,frames=count,directions=16,duration_ticks=ticks,loop=loop))
  for number in range(count):
   seconds=duration*number/(count if loop else max(1,count-1));pose=m.pose(source,seconds);points=np.concatenate([p['points'] for p in pose]);mins.append(points.min(0));maxs.append(points.max(0))
   for direction in range(16):
    rgba,depth,ax,ay=m.frame(pose,direction,2);shadow,sax,say=compact_shadow(*project_shadow(m,pose,direction,2));path=folder/f'{clip}-{direction:02d}-{number:02d}.npz';np.savez_compressed(path,rgba=rgba,depth=depth,shadow=shadow)
    frames.append(dict(model=m.model_id,clip=clip,direction=direction,frame=number,frames=count,duration_ticks=ticks,source_clip=source,source_seconds=seconds,loop=loop,anchor_x=int(ax),anchor_y=int(ay),width=rgba.shape[1],height=rgba.shape[0],height_world=m.height,npz=rel(path),sha256=sha(path),flags=4,shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],shadow_anchor_x=int(sax),shadow_anchor_y=int(say),shadow_pixels=int(np.count_nonzero(shadow)),shadow_alpha_max=int(shadow.max())))
    if direction==4:previews.append(preview_frame(rgba,ax,ay,shadow,sax,say,scale=3))
  previews[0].save(folder/f'preview-{clip}.png')
  if len(previews)>1:previews[0].save(folder/f'preview-{clip}.gif',save_all=True,append_images=previews[1:],duration=100,loop=0)
  print(json.dumps(dict(model=m.model_id,name=m.slug,clip=clip,frames=count*16)),flush=True)
 hi=np.max(maxs,0);lo=np.min(mins,0);height=int(math.ceil(hi[2]))
 for f in frames:f['height_world']=height
 receipt=dict(id=m.model_id,name=m.slug,source_unit=spec['unit'],source_model_scale=spec['scale'],source_skin=spec['skin'],gltf_metres_to_world=SCALE,source_origin_preserved=True,root_horizontal_motion_removed=True,vertical_bone_motion_preserved=True,source_triangles=m.triangles,source=[dict(gltf=rel(m.paths[0]),sha256=sha(m.paths[0]),buffers=m.body.buffer_sha256)],height_world=height,bounds_world=[lo.tolist(),hi.tolist()],clips=motions)
 if spec['id']==92:receipt['adaptation']='Original Valve ggbranch planted-tree mesh. Source temporary-tree runtime model selection is not recovered; H1 explicitly selects this existing Steam tree at native scale1. No normal NPC substitutes the tree.'
 if spec['id'] in (88,89):receipt['adaptation']='Original default_ward shared rig, original observer/default or sentry/materialGroup1 texture. No attack exists for stationary nonattacking wards: clip2 reuses original idle as a harmless fallback.'
 return receipt,frames,motions
def main():
 OUT.mkdir(parents=True,exist_ok=True);SOURCE.mkdir(parents=True,exist_ok=True);v=VpkArchive(STEAM/'pak01_dir.vpk');units=v.read('scripts/npc/npc_units.txt');(SOURCE/'npc_units.txt').write_bytes(units);defs={}
 for s in SPECS:
  if s['unit']:
   unit=unit_block(units.decode('utf-8-sig'),s['unit']);assert float(unit['ModelScale'])==s['scale'];defs[s['unit']]=unit
 # Skin1 is the actual source material group, not a recolored observer image.
 entry='materials/models/props_gameplay/sentry_ward_color_psd_e5e79182.vtex_c';folder=SOURCE/'default_ward';folder.mkdir(parents=True,exist_ok=True);raw=folder/'sentry_color.vtex_c';raw.write_bytes(v.read(entry));png=folder/'sentry_color.png'
 if not png.exists():
  p=subprocess.run([str(TOOL),'-d','-i',str(raw),'-o',str(png)],capture_output=True,text=True,encoding='utf8');(folder/'sentry-export.log').write_text(p.stdout+p.stderr,encoding='utf8');assert p.returncode==0
 with concurrent.futures.ProcessPoolExecutor(max_workers=3) as pool:results=list(pool.map(bake,SPECS))
 report=dict(complete=True,models=[r[0] for r in results],frames=[f for _,rows,_ in results for f in rows],motions=[m for _,_,ms in results for m in ms],source_units=v.identity('scripts/npc/npc_units.txt'),unit_definitions=defs,sentry_material=v.identity('materials/models/props_gameplay/sentry_ward.vmat_c'),sentry_skin_texture=dict(source=v.identity(entry),png=rel(png),png_sha256=sha(png)),shadow=projection_metadata(2),projection=dict(zoom_q8=180,pitch_q8=181,directions=16),source_sha256={'tools/'+n:sha(ROOT/'tools'/n) for n in ['bake_item_entities.py','bake_sprites.py','bake_hero_shadows.py','gltf_assets.py','bake_sprite_raster.py','bake_sprite_raster.c']},scope='Original ward skins and complete real item_creeps/necro warrior/archer rigs, source ModelScale and Source/4; original planted tree mesh, each pose with a separate compact soft geometry shadow.')
 (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,frames=len(report['frames']),models=len(report['models']))))
if __name__=='__main__':main()
