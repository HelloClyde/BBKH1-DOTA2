"""Shared PC-only source-mesh rasterizer; no triangle code runs on the H1.

Raster(width,height,ss=2).draw(projected_xyz,faces,uv,normals,rgba_texture)
uses native-pixel x/y and arbitrary ascending depth. finish() returns RGBA8
and float32 depth, preserving the nearest non-transparent supersample depth.
Colours resolve in premultiplied linear RGB before converting to straight sRGB.
"""
from pathlib import Path
import ctypes
import hashlib
import os
import subprocess
from gltf_assets import np,ROOT


def library():
    source=Path(__file__).with_suffix('.c');folder=ROOT/'build/conversion/baked';folder.mkdir(parents=True,exist_ok=True)
    output=folder/('raster-'+hashlib.sha256(source.read_bytes()).hexdigest()[:12]+'.dll')
    if not output.exists():
        cc=Path('C:/msys64/ucrt64/bin/gcc.exe');env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        subprocess.run([str(cc),'-std=c99','-O3','-shared','-Wall','-Wextra',str(source),'-o',str(output),'-lm'],check=True,env=env,capture_output=True)
    dll=ctypes.CDLL(str(output));ptr=ctypes.c_void_p
    dll.bake_clear.argtypes=[ctypes.c_int,ctypes.c_int,ptr,ptr]
    dll.bake_draw.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_int,ptr,ptr,ptr,ptr,ctypes.c_int,ptr,ctypes.c_int,ctypes.c_int,ctypes.c_float,ptr,ctypes.c_int,ctypes.c_int]
    return dll


class Raster:
    def __init__(self,width,height,ss=2):
        self.width=int(width);self.height=int(height);self.ss=int(ss)
        self.colour=np.empty((self.height*self.ss,self.width*self.ss,4),dtype=np.float32)
        self.depth=np.empty(self.colour.shape[:2],dtype=np.float32);self.dll=library()
        self.dll.bake_clear(self.width*self.ss,self.height*self.ss,self.colour.ctypes.data,self.depth.ctypes.data)

    def draw(self,projected,faces,uv,normals,texture,cutoff=0.5,factor=(1,1,1,1),repeat=False,unlit=False):
        vertices=np.ascontiguousarray(np.column_stack((projected,uv,normals)),dtype=np.float32)
        faces=np.ascontiguousarray(faces,dtype=np.uint32);texture=np.ascontiguousarray(texture,dtype=np.uint8)
        factor=np.ascontiguousarray(factor,dtype=np.float32)
        self.dll.bake_draw(self.width*self.ss,self.height*self.ss,self.ss,self.colour.ctypes.data,self.depth.ctypes.data,
                           vertices.ctypes.data,faces.ctypes.data,len(faces),texture.ctypes.data,texture.shape[1],texture.shape[0],
                           cutoff,factor.ctypes.data,int(repeat),int(unlit))

    def finish(self):
        rgba=self.colour.reshape(self.height,self.ss,self.width,self.ss,4).mean(axis=(1,3))
        alpha=rgba[:,:,3:4];linear=np.divide(rgba[:,:,:3],alpha,out=np.zeros_like(rgba[:,:,:3]),where=alpha>1e-8)
        rgb=np.where(linear<=.0031308,linear*12.92,1.055*np.maximum(linear,0)**(1/2.4)-.055)
        output=np.rint(np.clip(np.concatenate((rgb,alpha),axis=2),0,1)*255).astype(np.uint8)
        depth=self.depth.reshape(self.height,self.ss,self.width,self.ss).min(axis=(1,3))
        depth[output[:,:,3]==0]=np.inf
        return output,depth
