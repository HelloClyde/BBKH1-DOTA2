"""Add source map props to the frozen panorama and bake dynamic filler sprites.

The prior panorama is a versioned input. Its RGB and nearest source depth seed
the supersampled raster; only newly visible original prop samples replace it.
This avoids changing old tree/material lighting and keeps all unrelated pixels
byte-identical. Original terrain heights remain unchanged: new trees, walls and
objectives must never be mistaken for walkable ground or a hero's floor.
"""
from pathlib import Path
import math
import argparse, hashlib, json, shutil
from PIL import Image, ImageDraw
from gltf_assets import Gltf, np, ROOT
from bake_map import MapBake, BASIS, SCALE, ORIGIN, WIDTH, HEIGHT
from bake_sprite_raster import Raster
from bake_sprites import Model
from bake_projectiles import write_frame
from terrain_bake import linear

CATALOG=ROOT/'private/source2/reports/map-completion-manifest.json'
MAP=ROOT/'build/conversion/baked-map'
BEFORE=ROOT/'build/map-completion-before'
OUT=ROOT/'private/baked/map-completion'

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p): return json.loads(Path(p).read_text(encoding='utf8'))
def rel(p): return Path(p).relative_to(ROOT).as_posix()
def visible(p): return not any(k in p['name'].lower() for k in ('inspector','physics','collision','stump','cavern'))

def fixture_header(objectives,models):
    """Bounds use the same quantized entity anchor as production bs_project."""
    classes=('npc_dota_xp_fountain','npc_dota_lotus_pool','npc_dota_lantern')
    original=read(ROOT/'private/source2/reports/entity-default-parsed.json')
    ordered=[]
    for c in classes:
        for e in original:
            if e.get('classname')==c:ordered.append(next(p for p in objectives if p['entity']['hammerUniqueId']==e['hammerUniqueId']))
    text=['#ifndef H1_DOTA_MAP_FIXTURES_H','#define H1_DOTA_MAP_FIXTURES_H','',
          '/* Generated from original triangle-used map model vertices; objective order XP2,lotus2,watchers10.',
          ' * Bounds are relative to bs_project at the rounded original entity origin, before foreground clipping. */',
          'static const short dota_fixture_pixel_bounds[14][4]={']
    depths=[]
    for p in ordered:
        x,y,z=[math.floor(v+.5) for v in p['world_xyz']]
        px=x*180//256+512;py=(y-z)*180*181//65536+512
        lo_x,lo_y,w,h=p['projected_global_bounds']
        text.append('    {%d,%d,%d,%d},'%(lo_x-px,lo_y-py,w,h))
        depths.append(p['minimum_source_depth'])
    text+=['};','static const unsigned short dota_fixture_min_depth[14]={'+','.join(map(str,depths))+'};',
           'static const short dota_filler_source_height[14]={'+','.join(str(math.floor(m['world_xyz'][2]+.5)) for m in sorted(models,key=lambda m:m['id']) if m['id']<73)+'};',
           '','#endif','']
    path=ROOT/'private/generated/dota_map_fixtures.h';path.write_text('\n'.join(text),encoding='utf8');return path

class CompletionMap(MapBake):
    def __init__(self, ss):
        self.raster=Raster(WIDTH,HEIGHT,ss=ss);self.images={};self.sources={};self.triangles=0;self.instances=0
        self.aggregate_audit=[]
        rgb=np.asarray(Image.open(BEFORE/'map.png').convert('RGB'))
        depth=np.load(BEFORE/'map-depth.npy')
        for c in range(3): self.raster.colour[:,:,c]=np.repeat(np.repeat(linear(rgb[:,:,c]/255.),ss,0),ss,1)
        self.raster.colour[:,:,3]=1
        self.raster.depth[:]=np.repeat(np.repeat(depth,ss,0),ss,1)
        self.original_rgb=rgb;self.original_depth=depth
        self.lotus_dire=False

    def material(self,g,p):
        tex,cutoff,factor=super().material(g,p)
        if self.lotus_dire and g.doc['materials'][p['material']]['name']=='fountain_of_life_update':
            tex=self.dire_body
        return tex,cutoff,factor

class FillerModel(Model):
    def __init__(self,g,placement,renderer,part_name=None,model_id=None):
        self.slug='filler_'+str(placement['filler_index']) if part_name is None else 'lotus_'+str(model_id)+'_'+part_name
        self.model_id=placement['model_id'] if model_id is None else model_id
        self.docs=[g];self.paths=[g.path];self.body=g;self.root_name=None;self.textures={};self.scale=SCALE
        self.renderer=renderer;self.tint=np.asarray(placement['tint']);self.placement=placement
        matrix=np.asarray(placement['matrix_column_major']).reshape(4,4).T
        world=np.asarray(placement['world_xyz']);self.parts=[]
        for original in g.primitives():
            if not visible(original):continue
            if part_name is not None and original['name'].rsplit('.',1)[-1]!=part_name:continue
            p=dict(original,doc=0)
            xyz=(p['points']@matrix[:3,:3].T+matrix[:3,3])@BASIS.T*SCALE+[2048,2048,0]-world
            p['points']=xyz;p['normals']=renderer.normals(g,original,matrix);self.parts.append(p)
        points=np.concatenate([p['points'] for p in self.parts])
        self.height=int(np.ceil(np.ptp(points[:,2])))
    def texture(self,di,mi):
        p=next(p for p in self.parts if p['material']==mi)
        tex,cutoff,factor=self.renderer.material(self.body,p)
        factor[:3]*=linear(self.tint)
        return tex,cutoff,factor

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ss',type=int,default=2)
    ap.add_argument('--fillers-only',action='store_true')
    args=ap.parse_args();catalog=read(CATALOG);assert catalog['complete']
    BEFORE.mkdir(exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
    for name in ('map.png','map-depth.npy','ground-grid.npy','manifest.json'):
        if not (BEFORE/name).exists():shutil.copy2(MAP/name,BEFORE/name)
    old=read(BEFORE/'manifest.json')
    assert sha(BEFORE/'map.png')==old['map_sha256'] and sha(BEFORE/'map-depth.npy')==old['depth_sha256']
    # Small filler-only jobs avoid allocating the panorama raster at all.
    renderer=CompletionMap(args.ss) if not args.fillers_only else object.__new__(CompletionMap)
    if args.fillers_only:renderer.images={};renderer.lotus_dire=False
    skin=read(ROOT/read(CATALOG)['lotus_dire_skin']['path'])
    renderer.dire_body=np.asarray(Image.open(ROOT/skin['png']).convert('RGBA')).copy();renderer.dire_body[:,:,3]=255
    scene=Gltf(ROOT/'private/source2/exports/map-geometry/dota.gltf')
    source_records=[];frames=[];models=[];static_instances=[];previews=[];objectives=[]
    for asset in catalog['assets']:
        g=Gltf(ROOT/asset['gltf']);assert sha(g.path)==asset['sha256']
        parts=list(g.primitives())
        for placement in asset['placements']:
            if 'filler_index' in placement:
                model=FillerModel(g,placement,renderer)
                rgba,depth,ax,ay,shadow=model.frame(model.parts,0,args.ss,include_shadow=True)
                folder=OUT/model.slug;folder.mkdir(exist_ok=True)
                rec=write_frame(folder,model.model_id,0,0,0,1,1,True,rgba,depth,ax,ay,
                    height_world=model.height,flags=4,source_seconds=0,source_clip='static-source-map-instance',
                    shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],
                    shadow_anchor_x=model.last_shadow_anchor[0],shadow_anchor_y=model.last_shadow_anchor[1])
                np.savez_compressed(ROOT/rec['npz'],rgba=rgba,depth=depth,shadow=shadow)
                rec['sha256']=sha(ROOT/rec['npz'])
                frames.append(rec)
                models.append(dict(id=model.model_id,name=model.slug,height_world=model.height,
                    filler_index=placement['filler_index'],world_xyz=placement['world_xyz'],
                    entity=placement['entity'],original_map_matrix=placement['matrix_column_major'],
                    clips=[dict(id=0,frames=1,directions=1,duration_ticks=1,loop=True)]))
                image=Image.new('RGB',(128,112),(34,47,42));src=Image.fromarray(rgba)
                image.paste(src,(64-ax,92-ay),src);ImageDraw.Draw(image).text((3,3),model.slug,fill='white')
                image.save(folder/'preview.png');previews.append(image)
                continue
            entity=placement.get('entity',{})
            if entity.get('classname')=='npc_dota_lotus_pool':
                pools=[e for e in read(ROOT/'private/source2/reports/entity-default-parsed.json') if e.get('classname')=='npc_dota_lotus_pool']
                pool_index=next(i for i,e in enumerate(pools) if e['hammerUniqueId']==entity['hammerUniqueId'])
                model_id=73+pool_index;fruit_frames=[]
                for i in range(6):
                    part='mango'+str(i+1);model=FillerModel(g,placement,renderer,part_name=part,model_id=model_id)
                    rgba,depth,ax,ay=model.frame(model.parts,0,args.ss)
                    folder=OUT/('lotus_'+str(pool_index));folder.mkdir(exist_ok=True)
                    frames.append(write_frame(folder,model_id,i,0,0,1,1,True,rgba,depth,ax,ay,
                        height_world=model.height,flags=0,source_seconds=0,source_clip=part))
                    fruit_frames.append(dict(id=i,source_part=part,frames=1,directions=1,duration_ticks=1,loop=True))
                models.append(dict(id=model_id,name='lotus_'+str(pool_index),pool_index=pool_index,
                    world_xyz=placement['world_xyz'],entity=entity,original_map_matrix=placement['matrix_column_major'],clips=fruit_frames))
            if entity:
                matrix=np.asarray(placement['matrix_column_major']).reshape(4,4).T
                points=[];minimum_depth=65534
                for p in parts:
                    if not visible(p) or p['name'].rsplit('.',1)[-1].startswith('mango'):continue
                    q=(p['points'][np.unique(p['faces'])]@matrix[:3,:3].T+matrix[:3,3])@BASIS.T*SCALE+[2048,2048,0]
                    minimum_depth=min(minimum_depth,int(np.floor((32768-q[:,1]-q[:,2]).min())))
                    points.append(np.column_stack((q[:,0]*180/256-ORIGIN[0],(q[:,1]-q[:,2])*180*181/65536-ORIGIN[1])))
                points=np.concatenate(points);lo=np.floor(points.min(0)).astype(int);hi=np.ceil(points.max(0)).astype(int)+1
                objectives.append(dict(entity=entity,world_xyz=placement['world_xyz'],
                    minimum_source_depth=minimum_depth,
                    projected_global_bounds=[int(lo[0]),int(lo[1]),int(hi[0]-lo[0]),int(hi[1]-lo[1])],
                    bounds_method='Original triangle-used vertices through exact source scene matrix; native map pixels, origin[-512,-512], before foreground depth clipping.'))
            if args.fillers_only:continue
            matrix=np.asarray(placement['matrix_column_major']).reshape(4,4).T
            selected=None
            if asset['kind']=='aggregate':
                ni=placement['node'];primitive=scene.doc['meshes'][scene.doc['nodes'][ni]['mesh']]['primitives'][0]
                faces=scene.accessor(primitive['indices']).reshape(-1,3).astype(np.int32)
                selected=[dict(parts[placement['draw']],faces=faces)]
            if entity.get('classname')=='npc_dota_lotus_pool':
                selected=[p for p in parts if not p['name'].rsplit('.',1)[-1].startswith('mango')]
                renderer.lotus_dire=str(entity['skin'])=='1'
            renderer.model(g,matrix,visible=visible,parts=selected,tint=placement['tint'])
            renderer.lotus_dire=False
            static_instances.append(dict(asset=asset['name'],entity=placement.get('entity'),
               node=placement.get('node'),nodes=placement.get('nodes'),
               matrix_column_major=placement['matrix_column_major'],triangles=sum(len(p['faces']) for p in selected or parts if visible(p))))
        source_records.append(dict(gltf=asset['gltf'],sha256=sha(g.path),buffers=g.buffer_sha256,
                                  provenance=asset['provenance'],provenance_sha256=sha(ROOT/asset['provenance'])))
        print(json.dumps(dict(asset=asset['name'],static_instances=len(static_instances),filler_frames=len(frames))),flush=True)
    assert len(frames)==26 and {r['model'] for r in frames}==set(range(59,75))
    contact=Image.new('RGB',(128*7,112*2))
    for i,p in enumerate(previews):contact.paste(p,((i%7)*128,(i//7)*112))
    contact.save(OUT/'fillers-contact.png')
    (OUT/'objective-layout.json').write_text(json.dumps(dict(objectives=objectives,projection=dict(origin_pixels=list(ORIGIN),zoom_q8=180,pitch_q8=181)),indent=2)+'\n',encoding='utf8')
    header=fixture_header(objectives,models)
    output=dict(complete=True,models=sorted(models,key=lambda m:m['id']),frames=sorted(frames,key=lambda f:f['model']),
       sources=source_records,source_manifest=rel(CATALOG),source_manifest_sha256=sha(CATALOG),
       objective_layout=rel(OUT/'objective-layout.json'),objective_layout_sha256=sha(OUT/'objective-layout.json'),
       fixture_header=rel(header),fixture_header_sha256=sha(header),
       source_sha256={rel(Path(__file__)):sha(__file__)},
       scope='14 destructible filler buildings and12 individually harvestable original lotus meshes; original source pose, per-entity orientation, material tint and original scale. One fixed camera sprite per original instance, no static-map copy.')
    (OUT/'manifest.json').write_text(json.dumps(output,indent=2)+'\n',encoding='utf8')
    if args.fillers_only:return
    rgba,depth=renderer.raster.finish()
    rgb=rgba[:,:,:3];depth=np.rint(depth).clip(0,65535).astype('<u2')
    changed=np.any(rgb!=renderer.original_rgb,axis=2)|(depth!=renderer.original_depth)
    Image.fromarray(rgb).save(MAP/'map.png');np.save(MAP/'map-depth.npy',depth)
    new=dict(old,map_sha256=sha(MAP/'map.png'),depth_sha256=sha(MAP/'map-depth.npy'))
    new['source_instances']+=renderer.instances;new['source_triangles_drawn']+=renderer.triangles
    new['sources'].update(renderer.sources)
    new['map_completion']=dict(source_manifest=rel(CATALOG),source_manifest_sha256=sha(CATALOG),
        source_sha256={rel(Path(__file__)):sha(__file__)},base_manifest=rel(BEFORE/'manifest.json'),
        base_manifest_sha256=sha(BEFORE/'manifest.json'),base_rgb_sha256=sha(BEFORE/'map.png'),
        base_depth_sha256=sha(BEFORE/'map-depth.npy'),static_instances=static_instances,
        added_instances=renderer.instances,added_triangles=renderer.triangles,
        changed_pixels=int(changed.sum()),unchanged_pixels=int((~changed).sum()),
        ground_grid_unchanged=True,method='Original source matrices and source material/tints in depth-seeded2x raster; old panorama and ground heights retained outside new visible prop samples.')
    (MAP/'manifest.json').write_text(json.dumps(new,indent=2)+'\n',encoding='utf8')
    for name,world in [('radiant-highground',(650,3370,80)),('dire-highground',(3370,780,80)),
        ('west-lotus',(160,1000,30)),('east-lotus',(3920,3150,30)),('bridge',(2000,2000,30))]:
        x,y,z=world;cx=round(x*180/256-ORIGIN[0]);cy=round((y-z)*180*181/65536-ORIGIN[1])
        Image.fromarray(rgb).crop((cx-240,cy-140,cx+240,cy+140)).save(OUT/(name+'.png'))
    print(json.dumps(dict(complete=True,static_instances=renderer.instances,added_triangles=renderer.triangles,
                         changed_pixels=int(changed.sum()),filler_frames=14,lotus_frames=12)),flush=True)

if __name__=='__main__':main()
