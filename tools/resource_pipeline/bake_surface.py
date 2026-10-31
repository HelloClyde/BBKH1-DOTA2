"""Bake original multiblend surfaces into per-face albedo atlases on the PC.

SurfaceBake.bake_parts(g, primitive, worldXYZ, faces, worldNormals) yields
(XYZ, triangles, atlasUV, normals, RGBA). Source geometry and face order remain
unchanged; only vertices are expanded so independent UV islands cannot bleed.
"""
from pathlib import Path
import collections
import hashlib
from gltf_assets import np,ROOT
from terrain_bake import TerrainBake,linear


class SurfaceBake:
    def __init__(self,max_batch=128):
        if not 1<=int(max_batch)<=128:raise ValueError('Surface batch must contain1..128 triangles')
        self.max_batch=int(max_batch)
        # Reuse the source shader sampler without creating a top-down terrain
        # raster/temporary file; this path evaluates barycentrics in each face.
        self.sampler=object.__new__(TerrainBake)
        self.sampler.images={};self.sampler.linear_images={}
        self.triangles=0;self.atlases=0;self.sampled_texels=0;self.maximum_atlas=[0,0]
        self.resolutions=collections.Counter();self.materials=collections.Counter()

    @staticmethod
    def resolutions_for(points,faces):
        projected=np.column_stack((points[:,0]*180/256,(points[:,1]-points[:,2])*180*181/65536))
        corners=projected[faces]
        lengths=np.max(np.linalg.norm(corners-np.roll(corners,1,axis=1),axis=2),axis=1)
        # Include both endpoint texels. One atlas texel is approximately one
        # native screen pixel; supersampling still resolves mesh edges at2x.
        desired=np.ceil(lengths).astype(int)+1
        return np.select([desired<=8,desired<=16,desired<=32],[8,16,32],default=64).astype(int)

    def bake_parts(self,g,p,points,faces,normals):
        points=np.asarray(points,dtype=float);faces=np.asarray(faces,dtype=np.int32);normals=np.asarray(normals,dtype=float)
        if not len(faces):return
        if points.shape!=normals.shape or points.ndim!=2 or points.shape[1]!=3:raise ValueError('Source XYZ/normal dimensions differ')
        material=g.material(p['material']);vmat=material.get('source',{}).get('extras',{}).get('vmat',{})
        if vmat.get('ShaderName')!='multiblend.vfx':raise ValueError('Surface atlas requires an original multiblend material')
        node=g.doc['nodes'][p['node_id']]
        attributes=g.doc['meshes'][node['mesh']]['primitives'][p['primitive_id']]['attributes']
        paint=g.accessor(attributes['_TEXCOORD_1'])[:,:3]
        softness=g.accessor(attributes['_TEXCOORD_2'])[:,:3]
        tint=linear(g.accessor(attributes['_TEXCOORD_3'])[:,:3]) if '_TEXCOORD_3' in attributes else np.ones_like(points)
        if len(paint)!=len(points) or len(softness)!=len(points) or len(p['uv'])!=len(points):raise ValueError('Missing original per-vertex surface data')
        # TerrainBake.colours records: material,z,uv2,paint3,softness3,tint3.
        source=np.column_stack((points[:,2],p['uv'],paint,softness,tint))
        sizes=self.resolutions_for(points,faces)
        for start in range(0,len(faces),self.max_batch):
            group=faces[start:start+self.max_batch];group_sizes=sizes[start:start+self.max_batch]
            placement=[];x=y=row_height=0;width=0
            limit=min(1024,int(np.sum(group_sizes+2)))
            for size in group_sizes:
                pitch=int(size)+2
                if x+pitch>limit:x=0;y+=row_height;row_height=0
                placement.append((x,y,int(size)));x+=pitch;row_height=max(row_height,pitch);width=max(width,x)
            height=y+row_height
            if max(width,height)>1024:raise ValueError('Surface atlas exceeds1024 pixels')
            rgba=np.zeros((height,width,4),dtype=np.uint8);rgba[:,:,3]=255
            samples=[];uvs=[];counts=[]
            for face,(left,top,size) in zip(group,placement):
                # Centre the three atlas endpoints on authored texels. The
                # one-pixel exterior and unused half-tile clamp to the triangle
                # boundary, so bilinear taps never reach another material.
                xx,yy=np.meshgrid(np.arange(size+2),np.arange(size+2))
                b=(xx.ravel()-1)/(size-1);c=(yy.ravel()-1)/(size-1)
                bary=np.column_stack((1-b-c,b,c));bary=np.maximum(bary,0)
                bary/=bary.sum(axis=1,keepdims=True)
                values=np.empty((len(bary),13),dtype=float);values[:,0]=p['material'];values[:,1:]=bary@source[face]
                samples.append(values);counts.append(len(values))
                uvs.append(np.array([[left+1.5,top+1.5],[left+size+.5,top+1.5],[left+1.5,top+size+.5]])/[width,height])
            encoded=np.rint(np.clip(self.sampler.colours(g,material,vmat,np.concatenate(samples)),0,1)*255).astype(np.uint8)
            offset=0
            for count,(left,top,size) in zip(counts,placement):
                rgba[top:top+size+2,left:left+size+2,:3]=encoded[offset:offset+count].reshape(size+2,size+2,3);offset+=count
            self.triangles+=len(group);self.atlases+=1;self.sampled_texels+=sum(counts)
            self.maximum_atlas=[max(self.maximum_atlas[0],width),max(self.maximum_atlas[1],height)]
            self.resolutions.update(map(int,group_sizes));self.materials[vmat.get('Name',material['name'])]+=len(group)
            # Keep the original primitive/face order. It also preserves the
            # previous raster's nearest-depth tie behaviour at touching faces.
            yield (points[group].reshape(-1,3),np.arange(len(group)*3,dtype=np.int32).reshape(-1,3),
                   np.concatenate(uvs),normals[group].reshape(-1,3),rgba)

    def parts(self,g,p,points,faces,normals):
        return self.bake_parts(g,p,points,faces,normals)

    def receipt(self):
        return {'kind':'multiblend_surface_atlases','source_triangles':self.triangles,'atlases':self.atlases,
                'sampled_texels':self.sampled_texels,'resolution_histogram':dict(sorted(self.resolutions.items())),
                'maximum_atlas_size':self.maximum_atlas,'materials':dict(self.materials),
                'method':'unchanged source triangles; original UV/weights/softness/tintmask/linear vertex colour evaluated in per-face barycentric atlases;1pixel clamped pad',
                'geometry':'source XYZ and face order preserved exactly; no simplification or culling',
                'helper_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'shader_sampler_sha256':hashlib.sha256((ROOT/'tools/terrain_bake.py').read_bytes()).hexdigest(),
                'source_textures':[{'path':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
                                   for _,path in self.sampler.images.values()]}
