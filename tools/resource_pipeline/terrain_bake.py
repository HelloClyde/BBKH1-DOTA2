"""Bake the installed terrain's painted material layers on the PC, before QEM.

The H1 receives small albedo tiles on real XYZ terrain. The original UVs,
painted weights, blend softness, vertex tint and material tint masks are sampled
at ground texels; Source 2 PBR/normal/parallax and water effects are omitted.
Blend equations follow ValveResourceFormat Renderer/Shaders/multiblend.*.slang.
"""
from pathlib import Path
import hashlib
import json
import math
import os
import subprocess
from PIL import Image
from gltf_assets import np, ROOT

EXPORTS=ROOT/'private/source2/exports'


def linear(x):return np.where(x<=.04045,x/12.92,((x+.055)/1.055)**2.4)
def gamma(x):return np.where(x<=.0031308,x*12.92,1.055*np.maximum(x,0)**(1/2.4)-.055)


class TerrainBake:
    def __init__(self):
        self.folder=ROOT/'build/conversion/terrain';self.folder.mkdir(parents=True,exist_ok=True)
        self.input=self.folder/'triangles.f32';self.stream=self.input.open('wb')
        self.materials=[];self.material_keys={};self.images={};self.linear_images={};self.lo=np.full(2,np.inf);self.hi=-self.lo
        self.triangles=0

    def add(self,g,p,points):
        material=g.material(p['material']);vmat=material.get('source',{}).get('extras',{}).get('vmat',{})
        key=vmat.get('Name',str(g.path)+':'+str(p['material']))
        if key not in self.material_keys:
            self.material_keys[key]=len(self.materials);self.materials.append((g,material,vmat))
        matid=self.material_keys[key];faces=p['faces'];count=len(points)
        node=g.doc['nodes'][p['node_id']]
        attributes=g.doc['meshes'][node['mesh']]['primitives'][p['primitive_id']]['attributes']
        weights=np.zeros((count,3));softness=np.ones((count,3));tint=np.ones((count,3))
        if vmat.get('ShaderName')=='multiblend.vfx':
            weights=g.accessor(attributes['_TEXCOORD_1'])[:,:3]
            softness=g.accessor(attributes['_TEXCOORD_2'])[:,:3]
            if '_TEXCOORD_3' in attributes:tint=linear(g.accessor(attributes['_TEXCOORD_3'])[:,:3])
        vertices=np.column_stack((points,p['uv'],weights,softness,tint))
        records=np.empty((len(faces),43),dtype='<f4');records[:,0]=matid
        records[:,1:]=vertices[faces].reshape(-1,42);records.tofile(self.stream)
        self.lo=np.minimum(self.lo,points[:,:2].min(0));self.hi=np.maximum(self.hi,points[:,:2].max(0))
        self.triangles+=len(faces)

    def image(self,g,vmat,key):
        resource=vmat['TextureParams'][key];stem=Path(resource).stem
        if stem not in self.images:
            path=g.path.parent/(stem+'.png')
            if not path.exists():path=EXPORTS/'map-materials'/(stem+'.png')
            if not path.exists():raise ValueError(f'Missing source layer texture {resource}')
            self.images[stem]=(np.asarray(Image.open(path).convert('RGBA'),dtype=float)/255.,path)
        return self.images[stem][0]

    @staticmethod
    def sample(image,uv):
        h,w=image.shape[:2];xy=(uv%1)*[w,h]-.5;lo=np.floor(xy).astype(int);f=xy-lo
        x=lo[:,0]%w;y=lo[:,1]%h;xx=(x+1)%w;yy=(y+1)%h
        return ((image[y,x]*(1-f[:,0,None])+image[y,xx]*f[:,0,None])*(1-f[:,1,None])+
                (image[yy,x]*(1-f[:,0,None])+image[yy,xx]*f[:,0,None])*f[:,1,None])

    def colours(self,g,material,vmat,samples):
        uv=samples[:,2:4];tint=samples[:,10:13];n=len(samples)
        shader=vmat.get('ShaderName','')
        if shader.startswith('water'):
            vectors=vmat.get('VectorParams',{})
            colour=vectors.get('g_vLowEndSurfaceColor') or vectors.get('g_vRefractionTint')
            if not colour:raise ValueError('Original water material has no surface colour parameter')
            return np.broadcast_to(colour[:3],(n,3))
        if shader!='multiblend.vfx':
            if material.get('image'):
                image=np.asarray(Image.open(material['image']).convert('RGBA'),dtype=float)/255.
                return self.sample(image,uv)[:,:3]*np.array(material['factor'][:3])*tint
            return np.broadcast_to(material.get('factor',[1,1,1])[:3],(n,3))*tint
        floats=vmat.get('FloatParams',{});vectors=vmat.get('VectorParams',{});ints=vmat.get('IntParams',{})
        count=2 if ints.get('F_TWO_LAYER_BLEND') else 4
        result=np.zeros((n,3));remaining=np.ones(n);layers=[];weights=[None]*count
        mask_image=self.image(g,vmat,'g_tTintMasks') if ints.get('F_TINT_MASK') else None
        for layer in range(count):
            suffix=str(layer);angle=math.radians(floats.get('g_flTexCoordRotate'+suffix,0))
            rotation=np.array([[math.cos(angle),math.sin(angle)],[-math.sin(angle),math.cos(angle)]])
            coords=uv@rotation/max(floats.get('g_flTexCoordScale'+suffix,1),1e-6)
            coords+=.5+np.array(vectors.get('g_vTexCoordOffset'+suffix,[0,0])[:2])
            resource=vmat['TextureParams']['g_tColor'+suffix]
            if resource not in self.linear_images:
                encoded=self.image(g,vmat,'g_tColor'+suffix)
                self.linear_images[resource]=np.concatenate((linear(encoded[:,:,:3]),encoded[:,:,3:4]),axis=2)
            sampled=self.sample(self.linear_images[resource],coords)
            colour_tint=linear(np.array(vectors.get('g_vColorTint'+suffix,[1,1,1])[:3]))
            if mask_image is not None:
                mask=self.sample(mask_image,coords)[:,layer,None]
                other=linear(np.array(vectors.get('g_vColorTintB'+suffix,[1,1,1])[:3]))
                colour_tint=other*(1-mask)+colour_tint*mask
            layers.append(sampled[:,:3]*colour_tint)
            if layer:
                soft=samples[:,6+layer];paint=samples[:,3+layer];alpha=sampled[:,3]
                lo=np.maximum(0,alpha-soft);hi=np.minimum(1,alpha+soft)
                t=np.clip((paint-lo)/np.maximum(hi-lo,1e-8),0,1)
                weights[layer]=np.minimum(t*t*(3-2*t),remaining);remaining-=weights[layer]
        weights[0]=remaining
        for layer in range(count):result+=layers[layer]*weights[layer][:,None]
        result*=np.clip(tint,0,1)*linear(np.array(vectors.get('g_vGlobalTint',[1,1,1])[:3]))
        return gamma(result)

    def finish(self):
        self.stream.close();step=4;self.step=step
        self.origin=np.floor(self.lo/256).astype(int)*256
        edge=np.ceil(self.hi/256).astype(int)*256
        width,height=((edge-self.origin)//step).tolist()
        compiler=Path('C:/msys64/ucrt64/bin/gcc.exe');source=ROOT/'tools/terrain_samples.c'
        exe=self.folder/'terrain_samples.exe';output=self.folder/'samples.f32'
        environment=dict(os.environ);environment['PATH']=str(compiler.parent)+os.pathsep+environment.get('PATH','')
        subprocess.run([str(compiler),'-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',str(source),'-lm','-o',str(exe)],check=True,env=environment)
        subprocess.run([str(exe),str(self.input),str(output),str(self.origin[0]),str(self.origin[1]),str(step),str(width),str(height)],check=True,env=environment)
        samples=np.fromfile(output,dtype='<f4').reshape(height,width,13)
        ids=np.rint(samples[:,:,0]).astype(int);rgb=np.zeros((height,width,3),dtype=float)
        for index,(g,material,vmat) in enumerate(self.materials):
            selected=ids==index
            if selected.any():rgb[selected]=self.colours(g,material,vmat,samples[selected])
        # Extend edge texels only, to prevent dark filtering seams. This does
        # not add geometry or make holes in the source ground walkable.
        known=ids>=0;self.source_coverage=known.copy()
        for _ in range(16):
            if known.all():break
            for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
                valid=np.roll(known,(dy,dx),(0,1))&~known
                if dy>0:valid[:dy]=False
                if dy<0:valid[dy:]=False
                if dx>0:valid[:,:dx]=False
                if dx<0:valid[:,dx:]=False
                rgb[valid]=np.roll(rgb,(dy,dx),(0,1))[valid];known[valid]=True
        self.rgb=np.rint(np.clip(rgb,0,1)*255).astype(np.uint8)
        Image.fromarray(self.rgb).save(self.folder/'source-albedo.png')
        self.receipt={'kind':'terrain_albedo_bake','source_triangles':self.triangles,
                      'origin':self.origin.tolist(),'grid_step_world':step,'grid_size':[width,height],
                      'method':'source triangle barycentric UV, original four-layer weights/softness, RGBA masks, tint masks and vertex colour in linear RGB',
                      'helper_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                      'bake_sha256':hashlib.sha256(self.rgb.tobytes()).hexdigest(),
                      'shader_reference':'https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/Renderer/Shaders/multiblend.frag.slang',
                      'source_textures':[{'path':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
                                         for _,path in self.images.values()],
                      'tiles':[],
                      'limitations':'albedo only; 64px cells near bases, 32px elsewhere in arena/16px outside, no normal, parallax, specular or water animation'}
        return self

    def tile(self,cx,cy):
        samples=256//self.step
        x,y=((np.array([cx,cy])*256-self.origin)//self.step).tolist()
        rgb=self.rgb[y:y+samples,x:x+samples]
        if rgb.shape[:2]!=(samples,samples):raise ValueError('Terrain cell leaves bake grid')
        base=(0<=cx<=2 and 12<=cy<=15) or (13<=cx<=15 and 0<=cy<=3)
        size=64 if base else 32 if 0<=cx<16 and 0<=cy<16 else 16
        self.receipt['tiles'].append({'cell':[cx,cy],'size':size,
                                     'original_covered_texels':int(self.source_coverage[y:y+samples,x:x+samples].sum()),
                                     'sampled_texels':samples*samples})
        if size!=samples:rgb=np.asarray(Image.fromarray(rgb).resize((size,size),Image.Resampling.BOX))
        rgb=rgb.astype(np.uint16);pixels=((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)
        pixels[pixels==0xf81f]=0xf81e
        return pixels
