"""Depth-seed original active base-layer trees into a new versioned panorama."""
from pathlib import Path
import argparse,collections,hashlib,json,re,shutil
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from bake_map import MapBake,WIDTH,HEIGHT,ORIGIN
from bake_sprite_raster import Raster
from terrain_bake import linear
from source2_materials import OriginalMaterials
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
class TreeBake(MapBake):
    def __init__(self,baseline,ss,materials):
        self.raster=Raster(WIDTH,HEIGHT,ss=ss);self.images={};self.sources={};self.triangles=0;self.instances=0;self.aggregate_audit=[]
        self.original_rgb=np.asarray(Image.open(baseline/'map.png').convert('RGB'));self.original_depth=np.load(baseline/'map-depth.npy')
        for c in range(3):self.raster.colour[:,:,c]=np.repeat(np.repeat(linear(self.original_rgb[:,:,c]/255.),ss,0),ss,1)
        self.raster.colour[:,:,3]=1;self.raster.depth[:]=np.repeat(np.repeat(self.original_depth,ss,0),ss,1)
        self.materials=materials;self.replacements={};self.skin_images={}
    def material(self,g,p):
        material=g.material(p['material']);source=material.get('source',{}).get('extras',{}).get('vmat',{}).get('Name')
        replacement=self.replacements.get(source)
        if replacement and replacement!=source:
            if replacement not in self.skin_images:self.skin_images[replacement]=self.materials.material(replacement)
            rgba,cutoff,factor=self.skin_images[replacement];return rgba,cutoff,factor.copy()
        return super().material(g,p)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--entities',type=Path,default=ROOT/'private/source2-v022/active-entities');ap.add_argument('--baseline',type=Path,default=ROOT/'build/conversion/baked-map');ap.add_argument('--output',type=Path,default=ROOT/'private/baked/v022/map');ap.add_argument('--ss',type=int,default=2);a=ap.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);trees=read(a.entities/'trees.json');added=[e for e in trees if e['_source_layer']!='default'];assert len(added)==169
    before={p:sha(a.baseline/p) for p in ('map.png','map-depth.npy','ground-grid.npy','manifest.json')}
    materials=OriginalMaterials(ROOT/'private/source2-v022/tree-materials');renderer=TreeBake(a.baseline,a.ss,materials);gltfs={};groups={};records=[]
    for i,e in enumerate(added):
        entry=re.search(r'"([^"]+)"',e['model'])[1];slug=Path(entry).stem
        if slug not in gltfs:
            path=ROOT/f'private/source2/exports/bake-trees/{slug}/{slug}.gltf';assert path.is_file(),path
            gltfs[slug]=Gltf(path);groups[slug]=materials.groups(entry)
        g=gltfs[slug];skin=str(e['skin']);assert skin in groups[slug],(slug,skin)
        renderer.replacements=groups[slug][skin];matrix=np.asarray(e['matrix_column_major']).reshape(4,4).T
        renderer.model(g,matrix,visible=lambda p:not any(s in p['name'].lower() for s in ('stump','inspector','physics','collision','cavern')),tint=np.asarray(e['rendercolor'])/255.)
        records.append(dict(source_layer=e['_source_layer'],hammer_id=e['hammerUniqueId'],model=entry,skin=skin,body=e.get('body'),matrix_column_major=e['matrix_column_major'],world_xyz=e['world_xyz'],material_mapping=renderer.replacements,tint=e['rendercolor']))
        if i%20==0:print(json.dumps(dict(trees=i+1,source_triangles=renderer.triangles)),flush=True)
    rgba,depth=renderer.raster.finish();rgb=rgba[:,:,:3];depth=np.rint(depth).clip(0,65535).astype('<u2');changed=np.any(rgb!=renderer.original_rgb,axis=2)|(depth!=renderer.original_depth)
    Image.fromarray(rgb).save(out/'map.png');np.save(out/'map-depth.npy',depth);shutil.copy2(a.baseline/'ground-grid.npy',out/'ground-grid.npy')
    material_manifest=materials.save();manifest=read(a.baseline/'manifest.json')
    manifest.update(map_sha256=sha(out/'map.png'),depth_sha256=sha(out/'map-depth.npy'),ground_grid_sha256=sha(out/'ground-grid.npy'),source_instances=manifest['source_instances']+renderer.instances,source_triangles_drawn=manifest['source_triangles_drawn']+renderer.triangles)
    manifest['sources'].update(renderer.sources)
    manifest['initial_active_layer_trees']=dict(baseline=str(a.baseline.relative_to(ROOT)),baseline_sha256=before,entities_manifest=str((a.entities/'manifest.json').relative_to(ROOT)),entities_manifest_sha256=sha(a.entities/'manifest.json'),total_source_trees=2475,added_trees=len(added),trees=records,changed_pixels=int(changed.sum()),unchanged_pixels=int((~changed).sum()),ground_grid_unchanged=before['ground-grid.npy']==sha(out/'ground-grid.npy'),material_manifest=str(material_manifest.relative_to(ROOT)),material_manifest_sha256=sha(material_manifest),source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),ROOT/'tools/source2_materials.py']})
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    assert before=={p:sha(a.baseline/p) for p in before},'The versioned baseline must remain untouched'
    Image.fromarray(changed.astype(np.uint8)*255).save(out/'new-tree-pixels.png')
    for name,world in [('radiant-highground',(650,3370,80)),('dire-highground',(3370,780,80))]:
        x,y,z=world;cx=round(x*180/256-ORIGIN[0]);cy=round((y-z)*180*181/65536-ORIGIN[1]);Image.fromarray(rgb).crop((cx-240,cy-136,cx+240,cy+136)).save(out/(name+'.png'))
    print(json.dumps(dict(complete=True,added_trees=len(added),triangles=renderer.triangles,changed_pixels=int(changed.sum()),output=str(out))),flush=True)
if __name__=='__main__':main()
