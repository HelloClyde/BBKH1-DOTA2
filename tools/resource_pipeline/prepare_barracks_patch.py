"""Render small same-source barracks removal regions, never the whole panorama.

All four living/dead states of each lane pair use the original scene, material
sampler, order and 2x raster. The installed HBK12 panorama remains an input.
"""
from pathlib import Path
import collections,hashlib,json,re,sys
from gltf_assets import ROOT,Gltf,np
from PIL import Image
from bake_map import MapBake,BASIS,SCALE,ORIGIN,tree_sources
from bake_world_instances import read_world,read_scene_tints,tree_entity_tints
from bake_sprite_raster import Raster
from bake_surface import SurfaceBake
from terrain_bake import linear
from source2_materials import OriginalMaterials

OUT=ROOT/'build/v0251/barracks-visual'
EXPORTS=ROOT/'private/source2/exports'
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def log(**x):print(json.dumps(x),flush=True)
def project(xyz):
    # Quantize in the original global pixel frame before translating a crop.
    return np.column_stack((xyz[:,0]*180/256-ORIGIN[0],(xyz[:,1]-xyz[:,2])*180*181/65536-ORIGIN[1],32768-xyz[:,1]-xyz[:,2])).astype(np.float32)
def overlaps(a,b):return a[0]<b[2] and b[0]<a[2] and a[1]<b[3] and b[1]<a[3]
def bbox(projected):return [float(projected[:,0].min()),float(projected[:,1].min()),float(projected[:,0].max()),float(projected[:,1].max())]
def bit_for_entity(e):
    name=e.get('npc_unit',e.get('MapUnitName',e.get('targetname','')))
    team=1 if 'badguys' in name or 'bad_rax' in name else 0
    lane=0 if name.endswith('_top') else 2 if name.endswith('_bot') else 1
    kind=1 if 'melee' in name else 2
    return (team*3+lane)*2+kind-1
def visible(p):return not any(k in p['name'].lower() for k in ('stump','inspector','physics','collision','cavern'))

class Crop:
    def __init__(self,pair,rect,state=3):
        self.pair=pair;self.rect=list(map(int,rect));self.state=state
        self.raster=Raster(rect[2]-rect[0],rect[3]-rect[1],ss=2)
    def clone(self,state):
        out=Crop(self.pair,self.rect,state)
        out.raster.colour[:]=self.raster.colour;out.raster.depth[:]=self.raster.depth
        return out
    def draw(self,proj,faces,uv,normals,texture,**kw):
        if not len(faces) or not overlaps(bbox(proj),self.rect):return
        p=proj[faces,:2]
        lo=p.min(1);hi=p.max(1);r=self.rect
        hit=(hi[:,0]>=r[0])&(lo[:,0]<=r[2])&(hi[:,1]>=r[1])&(lo[:,1]<=r[3])
        if not hit.any():return
        q=proj.copy();q[:,0]-=r[0];q[:,1]-=r[1]
        self.raster.draw(q,faces[hit],uv,normals,texture,**kw)
    def finish(self,contact=False):
        rgba,depth=self.raster.finish();valid=rgba[:,:,3]>0
        rgb=rgba[:,:,:3].copy();rgb[~valid]=[25,40,36]
        depth=np.where(valid,np.clip(np.rint(depth),0,65534),65535).astype('<u2')
        if contact:
            occ=np.zeros(depth.shape,np.float32)
            for dx,dy in ((-2,0),(2,0),(0,-2),(0,2),(-5,0),(5,0),(0,-5),(0,5)):
                other=np.roll(depth,(dy,dx),(0,1)).astype(np.int32);delta=depth.astype(np.int32)-other
                mask=(delta>5)&(delta<120)&valid&(other<65535)
                if dy>0:mask[:dy]=False
                if dy<0:mask[dy:]=False
                if dx>0:mask[:,:dx]=False
                if dx<0:mask[:,dx:]=False
                occ+=mask*.025
            rgb=np.rint(rgb*(1-occ[:,:,None])).astype(np.uint8)
        self.rgb=rgb;self.depth=depth
        return rgb,depth
    def reseed(self):
        ss=self.raster.ss
        for c in range(3):self.raster.colour[:,:,c]=np.repeat(np.repeat(linear(self.rgb[:,:,c]/255.),ss,0),ss,1)
        self.raster.colour[:,:,3]=1;self.raster.depth[:]=np.repeat(np.repeat(self.depth,ss,0),ss,1)

class Renderer(MapBake):
    def __init__(self,crops):
        self.crops=crops;self.images={};self.sources={};self.triangles=0;self.instances=0;self.aggregate_audit=[]
        self.surface_bake=SurfaceBake();self.replacements={};self.skin_images={}
        self.original_materials=OriginalMaterials(ROOT/'private/source2-v022/tree-materials')
        self.part_cache={};self.bound_cache={}
    def draw(self,xyz,faces,uv,normals,texture,collect_height=False,**kw):
        projected=project(xyz)
        for c in self.crops:c.draw(projected,faces,uv,normals,texture,**kw)
        self.triangles+=len(faces)
    def material(self,g,p):
        material=g.material(p['material']);source=material.get('source',{}).get('extras',{}).get('vmat',{}).get('Name')
        replacement=self.replacements.get(source)
        if replacement and replacement!=source:
            if replacement not in self.skin_images:self.skin_images[replacement]=self.original_materials.material(replacement)
            rgba,cutoff,factor=self.skin_images[replacement];return rgba,cutoff,factor.copy()
        return super().material(g,p)
    def parts(self,g):
        key=str(g.path)
        if key not in self.part_cache:self.part_cache[key]=list(g.primitives())
        return self.part_cache[key]
    def model(self,g,matrix=None,visible=None,parts=None,**kw):
        matrix=np.eye(4) if matrix is None else matrix
        selected=self.parts(g) if parts is None else parts
        key=str(g.path)
        if key not in self.bound_cache:
            points=np.concatenate([p['points'][np.unique(p['faces'])] for p in self.parts(g)])
            lo=points.min(0);hi=points.max(0)
            self.bound_cache[key]=np.array([[x,y,z] for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])])
        xyz=(self.bound_cache[key]@matrix[:3,:3].T+matrix[:3,3])@BASIS.T*SCALE+[2048,2048,0]
        if not any(overlaps(bbox(project(xyz)),c.rect) for c in self.crops):return
        return super().model(g,matrix,visible=visible,parts=selected,**kw)

def setup():
    source=read(ROOT/'private/source2/reports/map-landmarks-manifest.json');records=[]
    for asset in source['assets']:
        if 'rax' not in asset['name']:continue
        g=Gltf(Path(asset['gltf']));points=np.concatenate([p['points'][np.unique(p['faces'])] for p in g.primitives()])
        for placement in asset['placements']:
            entity=placement['source_entity'];bit=bit_for_entity(entity)
            matrix=np.asarray(placement['matrix_column_major']).reshape(4,4).T
            xyz=(points@matrix[:3,:3].T+matrix[:3,3])@BASIS.T*SCALE+[2048,2048,0]
            q=project(xyz);bounds=[*np.floor(q[:,:2].min(0)).astype(int).tolist(),*np.ceil(q[:,:2].max(0)).astype(int).tolist()]
            records.append(dict(bit=bit,entity=entity,gltf=str(g.path),matrix=placement['matrix_column_major'],bounds=bounds,min_depth=int(np.floor(q[:,2].min()))))
    records.sort(key=lambda x:x['bit']);assert [x['bit'] for x in records]==list(range(12))
    crops=[]
    for pair in range(6):
        a,b=records[pair*2:pair*2+2];rect=[min(a['bounds'][0],b['bounds'][0])-12,min(a['bounds'][1],b['bounds'][1])-12,max(a['bounds'][2],b['bounds'][2])+13,max(a['bounds'][3],b['bounds'][3])+13]
        for other in records:
            if other['bit']//2!=pair:assert not overlaps(rect,other['bounds']),('Cross-pair overlap',pair,other['bit'])
        crops.append(Crop(pair,rect))
    return records,crops

def base_geometry(renderer):
    paths=sorted((EXPORTS/'map-ground/batch').rglob('*.gltf'));base=read(ROOT/'private/source2/reports/base-layers-manifest.json')
    paths.extend(Path(e['gltf']) for e in base['assets'] if e['layer_index'] in (2,4) and e['world_baked']);assert len(paths)==138
    albedo=ROOT/'build/conversion/baked-map/albedo';info=read(albedo/'manifest.json');texture=np.asarray(Image.open(albedo/'source-albedo.png').convert('RGBA'))
    for pi,path in enumerate(paths):
        g=Gltf(path)
        for p in g.primitives():
            xyz=p['points']@BASIS.T*SCALE+[2048,2048,0]
            if not any(overlaps(bbox(project(xyz)),c.rect) for c in renderer.crops):continue
            renderer.record(g);faces=p['faces'];n=np.cross(xyz[faces[:,1]]-xyz[faces[:,0]],xyz[faces[:,2]]-xyz[faces[:,0]])
            floor=np.abs(n[:,2])>=np.linalg.norm(n,axis=1)*.5
            uv=(xyz[:,:2]-info['origin'])/(np.array(info['size'])*info['step_world']);normals=renderer.normals(g,p,np.eye(4))
            if floor.any():renderer.draw(xyz,faces[floor],uv,normals,texture,repeat=False)
            if (~floor).any():
                material=g.material(p['material']);vmat=material.get('source',{}).get('extras',{}).get('vmat',{})
                if vmat.get('ShaderName')=='multiblend.vfx':
                    # Preserve each original 128-face atlas batch, while
                    # skipping entire batches which cannot touch a crop.
                    side=faces[~floor];q=project(xyz)[side,:2];lo=q.min(1);hi=q.max(1)
                    hit=np.zeros(len(side),bool)
                    for c in renderer.crops:
                        r=c.rect;hit|=(hi[:,0]>=r[0])&(lo[:,0]<=r[2])&(hi[:,1]>=r[1])&(lo[:,1]<=r[3])
                    for start in range(0,len(side),128):
                        if not hit[start:start+128].any():continue
                        for points,triangles,texcoords,normal,tex in renderer.surface_bake.bake_parts(g,p,xyz,side[start:start+128],normals):renderer.draw(points,triangles,texcoords,normal,tex,repeat=False)
                else:
                    tex,cutoff,factor=renderer.material(g,p);renderer.draw(xyz,faces[~floor],p['uv'],normals,tex,repeat=True,factor=factor,cutoff=cutoff)
        if pi%20==0:log(phase='ground',resources=pi+1)

def original_static(renderer,scene):
    world=read_world();tints=read_scene_tints();matrices=scene.globals()
    for manifest in ('map-static-manifest.json','map-landmarks-manifest.json'):
        for e in read(ROOT/'private/source2/reports'/manifest)['assets']:
            if 'rax' in e.get('name',''):continue
            g=Gltf(Path(e.get('gltf',e.get('output'))));parts=renderer.parts(g)
            if e.get('stem') in world:
                for placement in e['placements']:
                    for node_id in placement['source_nodes']:
                        mesh=scene.doc['meshes'][scene.doc['nodes'][node_id]['mesh']];fragment=int(re.search(r'_fragment(\d+)$',mesh['name']).group(1))-1
                        instance=world[e['stem']][fragment];matrix=matrices[node_id]
                        assert np.abs(instance['matrix']-matrix).max()<.001
                        part=parts[instance['draw']];faces=scene.accessor(mesh['primitives'][0]['indices']).reshape(-1,3).astype(np.int32)
                        renderer.model(g,matrix,parts=[dict(part,faces=faces)],tint=instance['tint'])
            else:
                for placement in e['placements']:
                    matrix=np.asarray(placement['matrix_column_major']).reshape(4,4).T;tint=(1,1,1)
                    if e.get('stem') in tints:
                        entries=tints[e['stem']];errors=[float(np.abs(c['matrix']-matrix).max()) for c in entries];nearest=int(np.argmin(errors));assert errors[nearest]<.001;tint=entries[nearest]['tint']
                    renderer.model(g,matrix,tint=tint)
        log(phase='static',manifest=manifest)

def default_trees(renderer,scene):
    files,placements=tree_sources(scene);tints=tree_entity_tints(scene)
    for name,path in files.items():
        g=Gltf(path)
        for matrix,node_id in placements[name]:renderer.model(g,matrix,visible=visible,tint=tints[node_id])
    log(phase='default_trees',models=len(files))

def completion(renderer,scene):
    for asset in read(ROOT/'private/source2/reports/map-completion-manifest.json')['assets']:
        g=Gltf(ROOT/asset['gltf']);parts=renderer.parts(g)
        for p in asset['placements']:
            if 'filler_index' in p:continue
            entity=p.get('entity',{});selected=None
            if asset['kind']=='aggregate':
                ni=p['node'];primitive=scene.doc['meshes'][scene.doc['nodes'][ni]['mesh']]['primitives'][0]
                faces=scene.accessor(primitive['indices']).reshape(-1,3).astype(np.int32);selected=[dict(parts[p['draw']],faces=faces)]
            if entity.get('classname')=='npc_dota_lotus_pool':selected=[q for q in parts if not q['name'].rsplit('.',1)[-1].startswith('mango')]
            renderer.model(g,np.asarray(p['matrix_column_major']).reshape(4,4).T,visible=visible,parts=selected,tint=p['tint'])
    log(phase='completion')

def active_trees(renderer):
    added=[e for e in read(ROOT/'private/source2-v022/active-entities/trees.json') if e['_source_layer']!='default'];assert len(added)==169
    gltfs={};groups={}
    for e in added:
        entry=re.search(r'"([^"]+)"',e['model'])[1];slug=Path(entry).stem
        if slug not in gltfs:
            gltfs[slug]=Gltf(EXPORTS/f'bake-trees/{slug}/{slug}.gltf');groups[slug]=renderer.original_materials.groups(entry)
        renderer.replacements=groups[slug][str(e['skin'])]
        renderer.model(gltfs[slug],np.asarray(e['matrix_column_major']).reshape(4,4).T,visible=visible,tint=np.asarray(e['rendercolor'])/255.)
    renderer.replacements={};log(phase='active_trees',instances=len(added))

def main():
    OUT.mkdir(parents=True,exist_ok=True);records,crops=setup();renderer=Renderer(crops)
    (OUT/'layout.json').write_text(json.dumps(dict(records=records,pairs=[c.rect for c in crops]),indent=2)+'\n',encoding='utf8')
    log(phase='start',crop_pixels=sum((c.rect[2]-c.rect[0])*(c.rect[3]-c.rect[1]) for c in crops))
    base_geometry(renderer);scene=Gltf(EXPORTS/'map-geometry/dota.gltf');original_static(renderer,scene)
    states=[c.clone(state) for c in crops for state in range(4)];renderer.crops=states
    # Clone the common background before adding either member of a lane pair.
    for rec in records:
        g=Gltf(Path(rec['gltf']));saved=renderer.crops;renderer.crops=[c for c in states if c.pair==rec['bit']//2 and c.state&(1<<(rec['bit']%2))]
        renderer.model(g,np.asarray(rec['matrix']).reshape(4,4).T);renderer.crops=saved
    default_trees(renderer,scene)
    for c in states:c.finish(contact=True);c.reseed()
    completion(renderer,scene)
    for c in states:c.finish();c.reseed()
    active_trees(renderer)
    for c in states:
        c.finish();np.savez_compressed(OUT/f'pair-{c.pair}-state-{c.state}.npz',rgb=c.rgb,depth=c.depth)
        Image.fromarray(c.rgb).save(OUT/f'pair-{c.pair}-state-{c.state}.png')
    (OUT/'render-inputs.json').write_text(json.dumps(dict(sources=renderer.sources,tools={p.name:sha(p) for p in (Path(__file__),ROOT/'tools/bake_map.py',ROOT/'tools/bake_sprite_raster.py',ROOT/'tools/bake_sprite_raster.c',ROOT/'tools/bake_surface.py')}),indent=2)+'\n',encoding='utf8')
    log(phase='render_complete',regions=len(crops),states=len(states),source_files=len(renderer.sources))
if __name__=='__main__':main()
