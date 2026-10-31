"""Independent original-source audit of added hero and item-entity graphics."""
from pathlib import Path
import hashlib,json
from urllib.parse import unquote
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from source2_vpk import VpkArchive
from source2_bind_wearables import source_skeleton

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def check(x,message):
 if not x:raise AssertionError(message)
def main():
 v=VpkArchive(Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk'));tool=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe';reports=ROOT/'private/source2/reports';results=[]
 for slug,wanted in [('extra-heroes',3240),('item-entities',1808)]:
  folder=ROOT/'private/baked'/slug;manifest=read(folder/'manifest.json');check(manifest['complete'],'Incomplete asset manifest');keys=set();hashes={};stats={};rigs=[];identities=[]
  for f in manifest['frames']:
   key=tuple(f[k] for k in ('model','clip','direction','frame'));check(key not in keys,'Duplicate frame');keys.add(key);path=ROOT/f['npz'];check(sha(path)==f['sha256'],'NPZ SHA changed')
   with np.load(path) as data:
    im=data['rgba'];z=data['depth'];h,w=im.shape[:2];check(im.dtype==np.uint8 and im.shape==(f['height'],f['width'],4) and max(h,w)<=256,'RGBA bounds/type');check(z.shape==(h,w) and z.dtype==np.dtype('<i2') and np.any(z[im[:,:,3]>0]),'Missing relative depth');check(np.any(im[:,:,3]),'Empty sprite')
    if f['flags']&4:
     s=data['shadow'];check(s.dtype==np.uint8 and s.shape==(f['shadow_height'],f['shadow_width']),'Missing original compact shadow');check(int(np.count_nonzero(s))==f['shadow_pixels']>0,'Empty shadow');check(int(s.max())==f['shadow_alpha_max']<=128,'Shadow opacity differs');check(np.any(s[0]) and np.any(s[-1]) and np.any(s[:,0]) and np.any(s[:,-1]),'Shadow not compact')
    hashes[key]=hashlib.sha256(im.tobytes()).hexdigest();stat=stats.setdefault(f['model'],dict(frames=0,max_width=0,max_height=0));stat['frames']+=1;stat['max_width']=max(w,stat['max_width']);stat['max_height']=max(h,stat['max_height'])
  check(len(keys)==wanted,'Missing expected frame count')
  for motion in manifest['motions']:
   mid,clip=motion['model'],motion['clip'];check({k for k in keys if k[:2]==(mid,clip)}=={(mid,clip,d,n) for d in range(motion['directions']) for n in range(motion['frames'])},'Missing clip/direction/frame')
  for model in manifest['models']:
   mid=model['id']
   if 'source' not in model:continue
   check(abs(model['gltf_metres_to_world']-100/2.54/4)<1e-10 and model['source_origin_preserved'],'Arbitrary native model scaling/origin')
   triangles=0;pieces=[];body_names=None
   for index,src in enumerate(model['source']):
    path=ROOT/src['gltf'];check(sha(path)==src['sha256'],'Original glTF changed');g=Gltf(path);check(g.buffer_sha256==src['buffers'],'Original geometry/animation buffer changed');triangles+=sum(len(p['faces']) for p in g.primitives())
    proofname=('extra_'+model['name']+'_'+('body' if index==0 else path.stem)) if slug=='extra-heroes' else 'equipment_'+path.stem
    proof=read(reports/(proofname+'-provenance.json'));check(sha(tool)==proof['tool_sha256'],'Original exporter changed');identities.append(proof['source'])
    for output in proof['output_files']:check(sha(path.parent/unquote(output['uri']))==output['sha256'],'Decoded original model image/buffer changed')
    for dep in proof.get('dependencies',[]):
     if dep.get('status')!='not_found_in_base_pak01':identities.append(dep)
    weighted=set();inverse_count=0;error=0
    if g.doc.get('skins'):
     original=source_skeleton(proofname,tool,reports);original_names={n.casefold() for n in original['bone_names']}
     if body_names is None:body_names=original_names
     for node in g.doc['nodes']:
      if 'skin' not in node or 'mesh' not in node:continue
      skin=g.doc['skins'][node['skin']];binds=g.accessor(skin['inverseBindMatrices']);check(len(binds)==len(skin['joints']),'Part inverse binds missing');inverse_count+=len(binds)
      for primitive in g.doc['meshes'][node['mesh']]['primitives']:
       at=primitive['attributes']
       if 'JOINTS_0' not in at:continue
       joints=g.accessor(at['JOINTS_0']);weights=g.accessor(at['WEIGHTS_0']);error=max(error,float(np.max(np.abs(weights.sum(1)-1))))
       for joint in np.unique(joints[weights>1e-6]):
        check(int(joint)<len(skin['joints']),'Invalid original JOINTS index');bone=g.doc['nodes'][skin['joints'][int(joint)]]['name'];check(bone.casefold() in original_names and bone.casefold() in body_names,'Bone absent from original DATA/body rig');weighted.add(bone)
     check(error<1e-4,'Malformed original weights')
    pieces.append(dict(part=path.stem,original_triangles=sum(len(p['faces']) for p in g.primitives()),weighted_bones=len(weighted),part_inverse_binds=inverse_count,max_weight_sum_error=error))
   check(triangles==model['source_triangles'],'Original triangles omitted');rigs.append(dict(model=mid,name=model['name'],source_scale=model['source_model_scale'],source_triangles=triangles,pieces=pieces))
   if mid in (77,78):check(len(model['source'])==6 and len(model['default_wearables'])==5 and stats[mid]['frames']==1152,'Default full rig/wearables missing')
   check(hashes[mid,0,0,0]!=hashes[mid,0,4,0],'Repeated portrait instead of directional mesh')
   for motion in model['clips']:
    clip=motion['clip']
    if motion['frames']>1 and clip!=0:check(len({hashes[mid,clip,4,n] for n in range(motion['frames'])})>=4,'Static skeletal animation')
  if slug=='extra-heroes':
   check({m['id'] for m in manifest['models']}==set(range(77,88)),'Added hero/FX model missing');check({m['name'] for m in manifest['original_fx_meshes']}=={'necrolyte_skull','necrolyte_reapers_scythe'},'Original skull/scythe missing')
   for fx in manifest['original_fx_meshes']:
    g=Gltf(ROOT/fx['gltf']);check(sha(ROOT/fx['gltf'])==fx['sha256'] and g.buffer_sha256==fx['buffers'],'Original FX mesh changed')
   for icon in manifest['icons']:
    check(sha(ROOT/icon['path'])==icon['sha256'] and Image.open(ROOT/icon['path']).size==(24,24),'UI changed');identities.append(icon['source']['source'])
   for src in manifest['particle_sources']:
    identities.append(src['source'])
    for image in src['decoded']:check(sha(ROOT/image['path'])==image['sha256'],'Original particle PNG changed')
   for graph in manifest['particle_graphs']:identities.extend(graph['compiled_graphs'])
   for mid,clip in ((80,0),(81,0),(83,1),(86,0),(87,0),(87,1)):check(hashes[mid,clip,0,3]!=hashes[mid,clip,4,3],'Directional FX missing')
   for mid in range(79,88):
    for clip in range(3):check(len({hashes[mid,clip,0,n] for n in range(8)})>=4,'Static skill FX')
   config=manifest['scythe_particle_config'];check(config['source_constant_radius']==config['applied_particle_model_scale']==3 and sha(ROOT/config['data_dump'])==config['data_dump_sha256'],'Original giant scythe particle scale missing')
  else:
   check({m['id'] for m in manifest['models']}==set(range(88,93)),'Original equipment entity missing');identities.extend([manifest['source_units'],manifest['sentry_material'],manifest['sentry_skin_texture']['source']]);check(sha(ROOT/manifest['sentry_skin_texture']['png'])==manifest['sentry_skin_texture']['png_sha256'],'Original blue Sentry skin missing')
  for src in identities:check(v.identity(src['path'])['sha256']==src['sha256'],'Installed original source differs')
  report=dict(complete=True,asset_complete=True,frames=len(keys),models=stats,original_rig_audit=rigs,source_identities_checked=len(identities),checks=['Every NPZ SHA/RGBA/depth/compact geometry shadow','Every original skeletal clip and16-direction Cartesian product','Source ModelScale/origin, exactly one Source/4 conversion','Original compiled DATA weighted-bone names and part-specific inverse binds','Complete default wearable geometry and original material/color PNGs','Installed VPK CRC/SHA source identities','Original giant scythe particle scale, skull and normal attack separation'],manifest_sha256=sha(folder/'manifest.json'),verification_tool_sha256=sha(__file__))
  (folder/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');results.append(dict(asset=slug,frames=len(keys),complete=True))
 print(json.dumps(results))
if __name__=='__main__':main()
