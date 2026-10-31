"""Offline full-mesh map photograph plus per-pixel depth for H1 tile streaming.

Keeps the original default ground layers, source tree species, courtyard meshes
and fountain/barracks instances. Live towers/Ancients are separate sprites.
No reduced runtime geometry is used to render this panorama.
"""
from pathlib import Path
import argparse,collections,concurrent.futures,hashlib,json,os,re,subprocess,sys
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from terrain_bake import TerrainBake,linear
from bake_sprite_raster import Raster
from bake_world_instances import read_world,read_scene_tints,tree_entity_tints
from bake_surface import SurfaceBake

EXPORTS=ROOT/'private/source2/exports'
OUT=ROOT/'build/conversion/baked-map'
BASIS=np.array([[0,0,1],[-1,0,0],[0,1,0]],float)
SCALE=4096/(16384*.0254)
ORIGIN=(-512,-512);WIDTH=3840;HEIGHT=2816

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(Path(p).read_text(encoding='utf8'))

class Albedo(TerrainBake):
    def __init__(self):
        self.folder=OUT/'albedo';self.folder.mkdir(parents=True,exist_ok=True)
        self.input=self.folder/'triangles.f32';self.stream=self.input.open('wb')
        self.materials=[];self.material_keys={};self.images={};self.linear_images={}
        self.lo=np.full(2,np.inf);self.hi=-self.lo;self.triangles=0

    def finish(self,step=1):
        self.stream.close();self.step=step
        self.origin=np.floor(self.lo/256).astype(int)*256
        edge=np.ceil(self.hi/256).astype(int)*256
        width,height=((edge-self.origin)//step).tolist()
        cc=Path('C:/msys64/ucrt64/bin/gcc.exe');exe=self.folder/'terrain_samples.exe'
        env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        subprocess.run([str(cc),'-std=c99','-O2','-ffp-contract=off',str(ROOT/'tools/terrain_samples.c'),'-lm','-o',str(exe)],check=True,env=env)
        path=self.folder/'samples.f32'
        rgb=np.zeros((height,width,3),dtype=np.uint8);known=np.zeros((height,width),bool)
        # Stripe the expensive four-layer linear-light sampling, keeping peak
        # memory bounded even when the albedo is sampled at one world unit.
        for start in range(0,height,128):
            rows=min(128,height-start)
            result=subprocess.run([str(exe),str(self.input),str(path),str(self.origin[0]),str(self.origin[1]+start*step),str(step),str(width),str(rows)],capture_output=True,text=True,env=env)
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            part=np.fromfile(path,dtype='<f4').reshape(rows,width,13);ids=np.rint(part[:,:,0]).astype(int)
            target=rgb[start:start+128];known[start:start+128]=ids>=0
            for index,(g,material,vmat) in enumerate(self.materials):
                selected=ids==index
                if selected.any():target[selected]=np.rint(np.clip(self.colours(g,material,vmat,part[selected]),0,1)*255).astype(np.uint8)
            if start%1024==0:print(json.dumps({'albedo_rows':start,'total':height}),flush=True)
        self.source_coverage=known.copy()
        for _ in range(8):
            for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
                valid=np.roll(known,(dy,dx),(0,1))&~known
                if dy>0:valid[:dy]=False
                if dy<0:valid[dy:]=False
                if dx>0:valid[:,:dx]=False
                if dx<0:valid[:,dx:]=False
                rgb[valid]=np.roll(rgb,(dy,dx),(0,1))[valid];known[valid]=True
        self.rgb=rgb;Image.fromarray(rgb).save(self.folder/'source-albedo.png')
        self.receipt={'source_triangles':self.triangles,'origin':self.origin.tolist(),'step_world':step,'size':[width,height],
                      'source_textures':[{'path':str(p.relative_to(ROOT)),'sha256':sha(p)} for _,p in self.images.values()]}
        (self.folder/'manifest.json').write_text(json.dumps(self.receipt,indent=2)+'\n')
        return self

def tree_sources(scene):
    matrices=scene.globals();placements=collections.defaultdict(list)
    for i,n in enumerate(scene.doc['nodes']):
        name=n.get('name','')
        if 'mesh' not in n or not name.startswith(('tree_','dire_tree')) or not re.search(r'_model(?:\.\d+)?$',name) or 'stump' in name:continue
        pos=BASIS@matrices[i][:3,3]*SCALE+[2048,2048,0]
        if not(-512<pos[0]<4608 and -512<pos[1]<4608):continue
        placements[name.split('.')[0]].append((matrices[i],i))
    def export(name):
        path=EXPORTS/'bake-trees'/name/(name+'.gltf')
        if not path.exists():
            command=[sys.executable,str(ROOT/'tools/source2_export.py'),'--name','bake_'+name,'--entry','models/props_tree/'+name+'.vmdl_c','--output',str(path)]
            result=subprocess.run(command,capture_output=True,text=True,encoding='utf8',errors='replace')
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return name,path
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:files=dict(pool.map(export,placements))
    return files,placements

class MapBake:
    def __init__(self,ss=2):
        self.raster=Raster(WIDTH,HEIGHT,ss=ss);self.images={};self.sources={};self.triangles=0;self.instances=0
        self.aggregate_audit=[]
        self.surface_bake=SurfaceBake()
        self.height_stream=(OUT/'height-triangles.f32').open('wb');self.height_count=0

    def record(self,g):
        self.sources[str(g.path.relative_to(ROOT))]={'sha256':sha(g.path),'buffers':g.buffer_sha256}

    def draw(self,xyz,faces,uv,normals,texture,cutoff=.5,repeat=False,unlit=False,factor=(1,1,1,1),collect_height=False):
        projected=np.column_stack((xyz[:,0]*180/256-ORIGIN[0],(xyz[:,1]-xyz[:,2])*180*181/65536-ORIGIN[1],32768-xyz[:,1]-xyz[:,2]))
        self.raster.draw(projected,faces,uv,normals,texture,cutoff=cutoff,repeat=repeat,unlit=unlit,factor=factor)
        self.triangles+=len(faces)
        if collect_height:
            np.asarray(xyz[faces],dtype='<f4').tofile(self.height_stream);self.height_count+=len(faces)

    def normals(self,g,p,matrix):
        n=g.doc['nodes'][p['node_id']];at=g.doc['meshes'][n['mesh']]['primitives'][p['primitive_id']]['attributes']
        if 'NORMAL' in at:
            normals=g.accessor(at['NORMAL']).astype(float)
            combined=matrix[:3,:3]@g.globals()[p['node_id']][:3,:3]
            normals=normals@np.linalg.inv(combined)
            normals=normals@BASIS.T
        else:
            normals=np.zeros_like(p['points']);faces=p['faces'];xyz=p['points']
            fn=np.cross(xyz[faces[:,1]]-xyz[faces[:,0]],xyz[faces[:,2]]-xyz[faces[:,0]])
            for k in range(3):np.add.at(normals,faces[:,k],fn)
            normals=normals@np.linalg.inv(matrix[:3,:3])@BASIS.T
        normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-12)
        return normals

    def material(self,g,p):
        material=g.material(p['material']);source=material.get('source',{});vmat=source.get('extras',{}).get('vmat',{})
        key=(str(g.path),p['material'])
        if key not in self.images:
            if material.get('image'):
                rgba=np.array(Image.open(material['image']).convert('RGBA'))
            else:rgba=np.array([[[255,255,255,255]]],dtype=np.uint8)
            if source.get('alphaMode','OPAQUE')=='OPAQUE':rgba[:,:,3]=255
            self.images[key]=rgba
        factor=np.asarray(material.get('factor',[1,1,1,1]),float)
        tint=vmat.get('VectorParams',{}).get('g_vColorTint',[1,1,1])
        factor[:3]*=np.asarray(tint[:3])
        cutoff=source.get('alphaCutoff',.5) if source.get('alphaMode')=='MASK' else .01
        return self.images[key],cutoff,factor

    def model(self,g,matrix=None,visible=None,parts=None,tint=(1,1,1),collect_height=False):
        self.record(g);matrix=np.eye(4) if matrix is None else matrix
        for p in parts if parts is not None else g.primitives():
            if visible and not visible(p):continue
            xyz=(p['points']@matrix[:3,:3].T+matrix[:3,3])@BASIS.T*SCALE+[2048,2048,0]
            texture,cutoff,factor=self.material(g,p)
            factor[:3]*=linear(np.asarray(tint))
            self.draw(xyz,p['faces'],p['uv'],self.normals(g,p,matrix),texture,cutoff=cutoff,repeat=True,factor=factor,collect_height=collect_height)
        self.instances+=1

    def finish(self):
        self.height_stream.close();cc=Path('C:/msys64/ucrt64/bin/gcc.exe');exe=OUT/'height_grid.exe'
        env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        subprocess.run([str(cc),'-std=c99','-O2','-ffp-contract=off',str(ROOT/'tools/height_grid.c'),'-lm','-o',str(exe)],check=True,env=env)
        result=subprocess.run([str(exe),str(OUT/'height-triangles.f32'),str(OUT/'ground-grid.f32'),'129'],capture_output=True,text=True,env=env)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        ground=np.fromfile(OUT/'ground-grid.f32','<f4').reshape(129,129);known=ground> -1e20;known_count=int(known.sum())
        for _ in range(258):
            if known.all():break
            for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
                valid=np.roll(known,(dy,dx),(0,1))&~known
                if dy>0:valid[:dy]=False
                if dy<0:valid[dy:]=False
                if dx>0:valid[:,:dx]=False
                if dx<0:valid[:,dx:]=False
                ground[valid]=np.roll(ground,(dy,dx),(0,1))[valid];known[valid]=True
        if not known.all():raise ValueError('Height grid could not be filled from source surfaces')
        np.save(OUT/'ground-grid.npy',np.rint(ground).clip(-32768,32767).astype('<i2'))
        rgba,depth=self.raster.finish();valid=rgba[:,:,3]>0
        rgb=rgba[:,:,:3].copy();rgb[~valid]=[25,40,36]
        depth=np.where(valid,np.clip(np.rint(depth),0,65534),65535).astype('<u2')
        # Offline contact shading from nearby visible surfaces. The panorama's
        # source depth remains unchanged; no shadow calculation runs on H1.
        occlusion=np.zeros((HEIGHT,WIDTH),np.float32)
        for dx,dy in ((-2,0),(2,0),(0,-2),(0,2),(-5,0),(5,0),(0,-5),(0,5)):
            other=np.roll(depth,(dy,dx),(0,1)).astype(np.int32);delta=depth.astype(np.int32)-other
            mask=(delta>5)&(delta<120)&valid&(other<65535)
            if dy>0:mask[:dy]=False
            if dy<0:mask[dy:]=False
            if dx>0:mask[:,:dx]=False
            if dx<0:mask[:,dx:]=False
            occlusion+=mask*.025
        rgb=np.rint(rgb*(1-occlusion[:,:,None])).astype(np.uint8)
        Image.fromarray(rgb).save(OUT/'map.png');np.save(OUT/'map-depth.npy',depth)
        report={'origin_pixels':list(ORIGIN),'width':WIDTH,'height':HEIGHT,'zoom_q8':180,'pitch_q8':181,
                'supersamples':self.raster.ss,'source_triangles_drawn':self.triangles,'source_instances':self.instances,
                'sources':self.sources,'map_sha256':sha(OUT/'map.png'),'depth_sha256':sha(OUT/'map-depth.npy'),
                'aggregate_instance_audit':self.aggregate_audit,'worldnode_data_sha256':sha(ROOT/'private/source2/reports/worldnode-DATA.txt'),
                'sidewall_multiblend_bake':self.surface_bake.receipt(),
                'ground_grid_sha256':sha(OUT/'ground-grid.npy'),'ground_grid_source_triangles':self.height_count,'ground_grid_direct_samples':known_count,
                'source_geometry':'full original meshes; no QEM','lighting':'offline smooth source normals and fixed daylight; local depth contact shading',
                'scope':'Fixed camera panorama, default source terrain/static structures/trees. Dynamic towers and Ancients excluded.'}
        (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:report[k] for k in ('width','height','source_triangles_drawn','source_instances')}),flush=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--reuse-albedo',action='store_true');parser.add_argument('--ss',type=int,default=2);args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    paths=sorted((EXPORTS/'map-ground/batch').rglob('*.gltf'))
    base=load(ROOT/'private/source2/reports/base-layers-manifest.json')
    paths.extend(Path(e['gltf']) for e in base['assets'] if e['layer_index'] in (2,4) and e['world_baked'])
    if len(paths)!=138:raise ValueError(f'Expected 138 default ground resources; found {len(paths)}')
    if not args.reuse_albedo:
        albedo=Albedo()
        for path in paths:
            g=Gltf(path)
            for p in g.primitives():
                xyz=p['points']@BASIS.T*SCALE+[2048,2048,0];faces=p['faces'];n=np.cross(xyz[faces[:,1]]-xyz[faces[:,0]],xyz[faces[:,2]]-xyz[faces[:,0]])
                floor=np.abs(n[:,2])>=np.linalg.norm(n,axis=1)*.5
                if floor.any():albedo.add(g,dict(p,faces=faces[floor]),xyz)
        albedo.finish();del albedo
    albedo_info=load(OUT/'albedo/manifest.json');albedo_rgb=np.asarray(Image.open(OUT/'albedo/source-albedo.png').convert('RGBA'))
    bake=MapBake(args.ss)
    for path in paths:
        g=Gltf(path);bake.record(g)
        for p in g.primitives():
            xyz=p['points']@BASIS.T*SCALE+[2048,2048,0];faces=p['faces'];n=np.cross(xyz[faces[:,1]]-xyz[faces[:,0]],xyz[faces[:,2]]-xyz[faces[:,0]])
            floor=np.abs(n[:,2])>=np.linalg.norm(n,axis=1)*.5
            uv=(xyz[:,:2]-albedo_info['origin'])/(np.array(albedo_info['size'])*albedo_info['step_world'])
            normals=bake.normals(g,p,np.eye(4))
            if floor.any():bake.draw(xyz,faces[floor],uv,normals,albedo_rgb,repeat=False,collect_height=True)
            if (~floor).any():
                material=g.material(p['material']);vmat=material.get('source',{}).get('extras',{}).get('vmat',{})
                if vmat.get('ShaderName')=='multiblend.vfx':
                    for points,triangles,texcoords,normal,texture in bake.surface_bake.bake_parts(g,p,xyz,faces[~floor],normals):
                        bake.draw(points,triangles,texcoords,normal,texture,repeat=False)
                else:
                    texture,cutoff,factor=bake.material(g,p)
                    bake.draw(xyz,faces[~floor],p['uv'],normals,texture,repeat=True,factor=factor)
    print(json.dumps({'ground_done':bake.triangles}),flush=True)
    # Original water geometry/material low-end surface colour, fully baked.
    water=load(ROOT/'private/source2/reports/water-manifest.json')
    for entry in water['assets']:
        path=Path(entry.get('gltf',entry.get('output','')))
        if not path.is_file():continue
        g=Gltf(path)
        for p in g.primitives():
            m=g.material(p['material']);v=m.get('source',{}).get('extras',{}).get('vmat',{})
            if m.get('name') not in ('water_generic_000','water_generic_000_slope'):continue
            colour=v.get('VectorParams',{}).get('g_vLowEndSurfaceColor')
            if not colour:continue
            tex=np.array([[list(np.rint(np.asarray(colour[:3])*255).astype(np.uint8))+[255]]],np.uint8)
            xyz=p['points']@BASIS.T*SCALE+[2048,2048,0]
            bake.draw(xyz,p['faces'],p['uv'],bake.normals(g,p,np.eye(4)),tex,unlit=True);bake.record(g)
    scene=Gltf(EXPORTS/'map-geometry/dota.gltf');matrices=scene.globals();world=read_world();scene_tints=read_scene_tints()
    for manifest in ('map-static-manifest.json','map-landmarks-manifest.json'):
        for e in load(ROOT/'private/source2/reports'/manifest)['assets']:
            g=Gltf(Path(e.get('gltf',e.get('output'))))
            if e.get('stem') in world:
                # The world exporter names aggregate fragments starting at 1.
                # A placement references one draw call, not the entire model.
                # Use its exact full-map face indices, and verify source XYZ
                # plus the independently decoded Source instance matrix.
                parts=list(g.primitives());checked=0;largest=0;count=0;tints=set()
                for placement in e['placements']:
                    for node_id in placement['source_nodes']:
                        node=scene.doc['nodes'][node_id];mesh=scene.doc['meshes'][node['mesh']]
                        fragment=int(re.search(r'_fragment(\d+)$',mesh['name']).group(1))-1
                        instance=world[e['stem']][fragment];matrix=matrices[node_id]
                        error=float(np.abs(instance['matrix']-matrix).max());largest=max(largest,error)
                        if error>.001:raise ValueError(f'Aggregate transform mismatch {e["stem"]} fragment {fragment}: {error}')
                        part=parts[instance['draw']];original=mesh['primitives']
                        if len(original)!=1:raise ValueError('Expected one fragment draw primitive')
                        xyz=scene.accessor(original[0]['attributes']['POSITION'])
                        if xyz.shape!=part['points'].shape or np.max(np.abs(xyz-part['points']))>1e-4:raise ValueError('Fragment vertex space mismatch')
                        faces=scene.accessor(original[0]['indices']).reshape(-1,3).astype(np.int32)
                        known=set(map(tuple,part['faces'].tolist()))
                        if any(tuple(face) not in known for face in faces.tolist()):raise ValueError('Fragment faces not in selected source draw call')
                        bake.model(g,matrix,parts=[dict(part,faces=faces)],tint=instance['tint'],collect_height=True)
                        checked+=1;count+=len(faces);tints.add(tuple(instance['tint']))
                bake.aggregate_audit.append({'stem':e['stem'],'fragments':checked,'triangles':count,'distinct_tints':len(tints),'max_matrix_error_metres':largest,'source_faces_exact':True})
                continue
            for placement in e['placements']:
                matrix=np.array(placement['matrix_column_major']).reshape(4,4).T
                tint=(1,1,1)
                if e.get('stem') in scene_tints:
                    candidates=scene_tints[e['stem']];errors=[float(np.abs(c['matrix']-matrix).max()) for c in candidates]
                    nearest=int(np.argmin(errors))
                    if errors[nearest]>.001:raise ValueError('Scene instance tint transform mismatch')
                    tint=candidates[nearest]['tint']
                bake.model(g,matrix,tint=tint,collect_height=manifest=='map-static-manifest.json')
        print(json.dumps({'static_done':manifest,'triangles':bake.triangles}),flush=True)
    files,placements=tree_sources(scene);tree_tints=tree_entity_tints(scene)
    for name,path in files.items():
        g=Gltf(path)
        for matrix,node_id in placements[name]:
            bake.model(g,matrix,lambda p: not any(s in p['name'].lower() for s in ('stump','inspector','physics','collision','cavern')),tint=tree_tints[node_id])
        print(json.dumps({'tree':name,'instances':len(placements[name])}),flush=True)
    bake.finish()

if __name__=='__main__':main()
