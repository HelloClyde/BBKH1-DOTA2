"""Read exported glTF geometry and bake its actual skeleton animation offline.

This runs on the PC only. It follows glTF column-major matrices and linear blend
skinning; no glTF parser, bone hierarchy or floating point runs on the H1.
"""
from pathlib import Path
import base64
import bisect
import hashlib
import json
import math
import struct
import sys
from urllib.parse import unquote

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'.tools/dota3d-deps'))
import numpy as np

DTYPES={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}
COMPONENTS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT2':4,'MAT3':9,'MAT4':16}

def quaternion_matrix(q):
    x,y,z,w=q;n=x*x+y*y+z*z+w*w
    if n<1e-20:return np.eye(4)
    s=2/n
    return np.array([[1-s*(y*y+z*z),s*(x*y-z*w),s*(x*z+y*w),0],
                     [s*(x*y+z*w),1-s*(x*x+z*z),s*(y*z-x*w),0],
                     [s*(x*z-y*w),s*(y*z+x*w),1-s*(x*x+y*y),0],[0,0,0,1]],dtype=np.float64)

def slerp(a,b,t):
    a=np.asarray(a,dtype=np.float64);b=np.asarray(b,dtype=np.float64)
    dot=float(a@b)
    if dot<0:b=-b;dot=-dot
    if dot>.9995:
        q=a*(1-t)+b*t;return q/np.linalg.norm(q)
    theta=math.acos(min(1,max(-1,dot)));den=math.sin(theta)
    return (a*math.sin((1-t)*theta)+b*math.sin(t*theta))/den

class Gltf:
    def __init__(self,path):
        self.path=Path(path);raw=self.path.read_bytes();embedded=[]
        if raw[:4]==b'glTF':
            magic,version,size=struct.unpack_from('<III',raw)
            if version!=2 or size!=len(raw):raise ValueError('Invalid GLB header')
            offset=12;self.doc=None
            while offset<len(raw):
                count,kind=struct.unpack_from('<II',raw,offset);offset+=8
                block=raw[offset:offset+count];offset+=count
                if kind==0x4e4f534a:self.doc=json.loads(block)
                elif kind==0x004e4942:embedded.append(block)
            if self.doc is None:raise ValueError('GLB has no JSON')
        else:self.doc=json.loads(raw)
        self.buffers=[]
        self.buffer_sha256={}
        for buffer in self.doc.get('buffers',[]):
            uri=buffer.get('uri')
            if uri and uri.startswith('data:'):data=base64.b64decode(uri.split(',',1)[1])
            elif uri:data=(self.path.parent/unquote(uri)).read_bytes()
            else:data=embedded.pop(0)
            if len(data)<buffer['byteLength']:raise ValueError('Truncated glTF buffer')
            self.buffers.append(data)
            self.buffer_sha256[uri or '__GLB_BIN__']=hashlib.sha256(data).hexdigest()
        self.parents=[-1]*len(self.doc.get('nodes',[]));self._accessors={}
        for i,node in enumerate(self.doc.get('nodes',[])):
            for child in node.get('children',[]):
                if self.parents[child]!=-1:raise ValueError('glTF node has multiple parents')
                self.parents[child]=i

    def accessor(self,index):
        if index in self._accessors:return self._accessors[index]
        a=self.doc['accessors'][index];dt=np.dtype(DTYPES[a['componentType']]);n=COMPONENTS[a['type']]
        if 'bufferView' in a:
            view=self.doc['bufferViews'][a['bufferView']]
            stride=view.get('byteStride',n*dt.itemsize)
            out=np.ndarray((a['count'],n),dtype=dt,buffer=self.buffers[view['buffer']],
                offset=view.get('byteOffset',0)+a.get('byteOffset',0),strides=(stride,dt.itemsize)).copy()
        else:out=np.zeros((a['count'],n),dtype=dt)
        if 'sparse' in a:
            sparse=a['sparse'];ids=sparse['indices'];values=sparse['values']
            bv=self.doc['bufferViews'][ids['bufferView']];idt=np.dtype(DTYPES[ids['componentType']])
            indices=np.frombuffer(self.buffers[bv['buffer']],dtype=idt,count=sparse['count'],offset=bv.get('byteOffset',0)+ids.get('byteOffset',0))
            bv=self.doc['bufferViews'][values['bufferView']]
            vals=np.frombuffer(self.buffers[bv['buffer']],dtype=dt,count=sparse['count']*n,offset=bv.get('byteOffset',0)+values.get('byteOffset',0)).reshape(-1,n)
            out[indices]=vals
        if a.get('normalized') and dt.kind in 'iu':
            out=out.astype(np.float64)/np.iinfo(dt).max
            if dt.kind=='i':out=np.maximum(out,-1)
        self._accessors[index]=out
        return out

    def duration(self,animation):
        a=self.doc['animations'][animation]
        return max((float(self.accessor(s['input'])[-1,0]) for s in a['samplers']),default=0)

    def globals(self,animation=None,seconds=0,external_bones=None):
        nodes=self.doc['nodes'];overrides={}
        # Source 2 bone StringTokens use ASCII-insensitive name matching.
        external_bones={name.casefold():matrix for name,matrix in external_bones.items()} if external_bones else None
        if animation is not None:
            a=self.doc['animations'][animation]
            for channel in a['channels']:
                target=channel['target'];path=target.get('path')
                if path=='weights':continue
                sampler=a['samplers'][channel['sampler']];times=self.accessor(sampler['input'])[:,0]
                values=self.accessor(sampler['output']);kind=sampler.get('interpolation','LINEAR')
                j=max(0,min(len(times)-1,bisect.bisect_right(times,seconds)-1));k=min(j+1,len(times)-1)
                t=0 if j==k else max(0,min(1,(seconds-float(times[j]))/float(times[k]-times[j])))
                if kind=='CUBICSPLINE':
                    # glTF tangents are derivatives in seconds; quaternion result normalizes.
                    p=values[j*3+1];q=values[k*3+1];span=float(times[k]-times[j])
                    v=(2*t**3-3*t*t+1)*p+(t**3-2*t*t+t)*span*values[j*3+2]+(-2*t**3+3*t*t)*q+(t**3-t*t)*span*values[k*3]
                    if path=='rotation':v=v/np.linalg.norm(v)
                elif kind=='STEP':v=values[j]
                elif path=='rotation':v=slerp(values[j],values[k],t)
                else:v=values[j]*(1-t)+values[k]*t
                overrides[target['node'],path]=v
        matrices=[None]*len(nodes);visiting=set()
        def world(i):
            if matrices[i] is not None:return matrices[i]
            if i in visiting:raise ValueError('Cyclic glTF nodes')
            visiting.add(i);node=nodes[i]
            if external_bones and node.get('name','').casefold() in external_bones:
                matrices[i]=external_bones[node['name'].casefold()];visiting.remove(i);return matrices[i]
            if 'matrix' in node and not any((i,k) in overrides for k in ('translation','rotation','scale')):
                local=np.array(node['matrix'],dtype=np.float64).reshape(4,4).T
            else:
                local=quaternion_matrix(overrides.get((i,'rotation'),node.get('rotation',[0,0,0,1])))
                local[:3,:3]*=np.asarray(overrides.get((i,'scale'),node.get('scale',[1,1,1])))[None,:]
                local[:3,3]=overrides.get((i,'translation'),node.get('translation',[0,0,0]))
            matrices[i]=world(self.parents[i])@local if self.parents[i]>=0 else local
            visiting.remove(i);return matrices[i]
        for i in range(len(nodes)):world(i)
        return matrices

    def primitives(self,animation=None,seconds=0,external_bones=None):
        matrices=self.globals(animation,seconds,external_bones)
        for node_id,node in enumerate(self.doc['nodes']):
            if 'mesh' not in node:continue
            mesh=self.doc['meshes'][node['mesh']]
            for primitive_id,p in enumerate(mesh['primitives']):
                if p.get('mode',4)!=4:continue
                at=p['attributes'];points=self.accessor(at['POSITION']).astype(np.float64)
                homogeneous=np.column_stack((points,np.ones(len(points))))
                if 'skin' in node and 'JOINTS_0' in at and 'WEIGHTS_0' in at:
                    skin=self.doc['skins'][node['skin']];joints=self.accessor(at['JOINTS_0']).astype(np.int32)
                    weights=self.accessor(at['WEIGHTS_0']).astype(np.float64)
                    weights/=np.maximum(weights.sum(axis=1,keepdims=True),1e-12)
                    binds=self.accessor(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1) if 'inverseBindMatrices' in skin else np.repeat(np.eye(4)[None],len(skin['joints']),axis=0)
                    transforms=np.array([matrices[j]@b for j,b in zip(skin['joints'],binds)])
                    output=np.zeros((len(points),3),dtype=np.float64)
                    for slot in range(joints.shape[1]):
                        transformed=np.einsum('nij,nj->ni',transforms[joints[:,slot]],homogeneous)[:,:3]
                        output+=transformed*weights[:,slot,None]
                else:output=(homogeneous@matrices[node_id].T)[:,:3]
                faces=self.accessor(p['indices']).reshape(-1,3).astype(np.int32) if 'indices' in p else np.arange(len(points),dtype=np.int32).reshape(-1,3)
                uv=self.accessor(at['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in at else np.zeros((len(points),2))
                yield {'node_id':node_id,'primitive_id':primitive_id,'name':node.get('name',mesh.get('name','')),
                       'mesh_name':mesh.get('name',''),'points':output,'faces':faces,'uv':uv,'material':p.get('material',-1)}

    def image_path(self,index):
        image=self.doc['images'][index]
        if 'uri' not in image:raise ValueError('Embedded glTF image should be exported as PNG first')
        return self.path.parent/unquote(image['uri'])

    def image_bytes(self,index):
        image=self.doc['images'][index]
        if 'uri' in image:
            uri=image['uri']
            return base64.b64decode(uri.split(',',1)[1]) if uri.startswith('data:') else self.image_path(index).read_bytes()
        view=self.doc['bufferViews'][image['bufferView']];offset=view.get('byteOffset',0)
        return self.buffers[view['buffer']][offset:offset+view['byteLength']]

    def material(self,index):
        if index<0:return {'name':'missing_material','image':None,'factor':[.45,.5,.4,1]}
        m=self.doc['materials'][index];pbr=m.get('pbrMetallicRoughness',{})
        image=None;image_data=None;texture=pbr.get('baseColorTexture')
        if texture:
            t=self.doc['textures'][texture['index']]
            if 'uri' in self.doc['images'][t['source']] and not self.doc['images'][t['source']]['uri'].startswith('data:'):
                image=self.image_path(t['source'])
            image_data=self.image_bytes(t['source'])
        return {'name':m.get('name',str(index)),'image':image,'image_data':image_data,
                'factor':pbr.get('baseColorFactor',[1,1,1,1]),'source':m}
