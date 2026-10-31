"""Derive a compact gameplay water mask from the original water and walk surfaces.

Uses the same Source transforms and exact height triangles as the panorama.
Bridges/river banks higher than the water plane are excluded. Runtime precision
is four H1 world units (16 Source units), with no hand-drawn river boundaries.
"""
from pathlib import Path
import hashlib,json,os,subprocess
from gltf_assets import Gltf,np,ROOT
from bake_water import BASIS,SCALE,WATER_NAMES

def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()

def main():
 out=ROOT/'private/items/special-source/water';out.mkdir(parents=True,exist_ok=True)
 source=ROOT/'private/source2/reports/water-manifest.json'
 triangles=[];proof=[]
 for entry in json.loads(source.read_text(encoding='utf8'))['assets']:
  path=Path(entry['gltf']);g=Gltf(path);included=False
  for p in g.primitives():
   if g.material(p['material']).get('name') not in WATER_NAMES:continue
   xyz=p['points']@BASIS.T*SCALE+[2048,2048,0]
   triangles.append(np.asarray(xyz[p['faces']],dtype='<f4'));included=True
  if included:proof.append({'path':str(path.relative_to(ROOT)),'sha256':sha(path),'buffers':g.buffer_sha256})
 tri=np.concatenate(triangles);assert tri.shape==(3905,3,3)
 watertri=out/'water-triangles.f32';tri.tofile(watertri)
 compiler=Path('C:/msys64/ucrt64/bin/gcc.exe');exe=out/'height_grid.exe';env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
 subprocess.run([str(compiler),'-std=c99','-O2','-ffp-contract=off',str(ROOT/'tools/height_grid.c'),'-lm','-o',str(exe)],check=True,env=env)
 n=1025;grids=[];groundtri=ROOT/'build/conversion/baked-map/height-triangles.f32'
 for name,path in [('water',watertri),('ground',groundtri)]:
  dest=out/(name+'.f32');subprocess.run([str(exe),str(path),str(dest),str(n)],check=True,env=env)
  grids.append(np.fromfile(dest,dtype='<f4').reshape(n,n));dest.unlink()
 water,ground=grids;wet=water>-1e20;known=ground>-1e20
 mask=wet&known&(ground<=water+3)
 rows=[]
 for row in mask:
  transitions=np.diff(np.r_[False,row,False].astype(np.int8));starts=np.flatnonzero(transitions==1);ends=np.flatnonzero(transitions==-1)
  edges=[]
  for a,b in zip(starts,ends):edges.extend((int(a),int(b)))
  rows.append(edges)
 # Eight-row blocks bound query work and need no resident decompression cache.
 # A repeated row is one zero byte. Other equal-topology rows encode signed
 # boundary differences; checkpoints/topology changes encode absolute gaps.
 def varint(value):
  encoded=[]
  while value>127:encoded.append((value&127)|128);value>>=7
  return encoded+[value]
 block_rows=8;offsets=[];code=[];previous=[]
 for y,edges in enumerate(rows):
  absolute=y%block_rows==0 or len(edges)!=len(previous)
  if y%block_rows==0:offsets.append(len(code))
  if not absolute and edges==previous:code.append(0)
  else:
   code.append(len(edges)|(128 if absolute else 0))
   if absolute:
    prior=0
    for edge in edges:code.extend(varint(edge-prior));prior=edge
   else:
    for edge,prior in zip(edges,previous):
     delta=edge-prior;code.extend(varint(2*delta if delta>=0 else -2*delta-1))
  previous=edges
 offsets.append(len(code));assert len(code)<65536 and max(map(len,rows))<=32
 reference=np.packbits(mask.reshape(-1),bitorder='little').tobytes();reference_path=out/'reference-1025.bits'
 if reference_path.exists():assert reference_path.read_bytes()==reference,'Original water membership changed during lossless compression'
 else:reference_path.write_bytes(reference)
 header=ROOT/'game/water_world_data.h'
 def array(name,values,ctype):return 'static const unsigned '+ctype+' '+name+'['+str(len(values))+']={\n'+''.join(' '+','.join(map(str,values[i:i+20]))+',\n' for i in range(0,len(values),20))+'};\n'
 header.write_text('/* Generated from original Steam water/ground triangles. See extract_water_world.py. */\n#ifndef H1_WATER_WORLD_DATA_H\n#define H1_WATER_WORLD_DATA_H\n#define DOTA_WATER_GRID_SIZE '+str(n)+'\n#define DOTA_WATER_BLOCK_ROWS '+str(block_rows)+'\n'+array('water_block',offsets,'short')+array('water_code',code,'char')+'#endif\n',encoding='utf8')
 def point(selected):
  yy,xx=np.nonzero(selected);i=len(xx)//2;return [round(int(xx[i])*4095/(n-1)),round(int(yy[i])*4095/(n-1))]
 # Samples are selected from the original geometry, never invented coordinates.
 report={'complete':True,'grid_size':n,'maximum_quantization_world':2,'ground_tolerance_world':3,'runtime_bytes':2*len(offsets)+len(code),'original_rle_runtime_bytes':12708,'lossless_encoding':'8-row absolute checkpoints, repeat rows, zigzag unsigned7-bit varint boundary deltas','block_rows':block_rows,'maximum_row_boundaries':max(map(len,rows)),'reference_bits_sha256':hashlib.sha256(reference).hexdigest(),'water_samples':int(mask.sum()),'raised_surface_samples_excluded':int((wet&known&~mask).sum()),'wet_example':point(mask),'raised_surface_example':point(wet&known&(ground>water+10)),'dry_example':point(~wet&known),'source_water_triangles':len(tri),'source_water_manifest_sha256':sha(source),'ground_triangles_sha256':sha(groundtri),'source_resources':proof,'header_sha256':sha(header),'generator_sha256':sha(__file__),'scope':'Original geometric water plane projected top-down. Exact panorama walk-surface triangles reject bridges and raised river banks; quantized four world units. No tree/high-ground line-of-sight simulation.'}
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 for p in [watertri,exe]:p.unlink()
 print(json.dumps({k:report[k] for k in ('runtime_bytes','water_samples','raised_surface_samples_excluded','wet_example','raised_surface_example','dry_example')}))
if __name__=='__main__':main()
