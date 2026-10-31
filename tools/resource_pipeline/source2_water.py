"""Export original default-world water surfaces and authored material data."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

from gltf_assets import Gltf,np
from source2_prepare import ROOT
from source2_vpk import VpkArchive


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota'))
    parser.add_argument('--tool',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--reuse',action='store_true');args=parser.parse_args()
    inventory=json.loads((ROOT/'build/tests/map-source-audit/missing-ground-resources.json').read_text())
    entries=[e for e in inventory['water_entries_not_in_134_ground_list'] if re.search(r'n0_lr0_',e)]
    if len(entries)!=194:raise ValueError('Expected the audited 194 default-world water entries')
    out=ROOT/'private/source2/exports/map-water';out.mkdir(parents=True,exist_ok=True)
    reports=ROOT/'private/source2/reports';log=reports/'water-export.log'
    command=[str(args.tool),'-d','-i',str(args.game/'maps/dota.vpk'),'-f',','.join(entries),'-o',str(out),
             '--game',str(args.game/'gameinfo.gi'),'--gltf_export_format','gltf','--gltf_export_materials',
             '--gltf_textures_adapt','--gltf_export_extras','--threads','2']
    if not args.reuse:
        with log.open('w',encoding='utf8') as stream:process=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,check=False)
        if process.returncode:raise RuntimeError('VRF water export failed; see '+str(log))
    scene_path=ROOT/'private/source2/exports/map-geometry/dota.gltf';scene=Gltf(scene_path);matrices=scene.globals()
    by_stem={Path(e).name[:-7]:e for e in entries};scene_nodes={e:[] for e in entries}
    for i,node in enumerate(scene.doc['nodes']):
        if 'mesh' not in node:continue
        name=scene.doc['meshes'][node['mesh']].get('name','').split('.')[0]
        if name in by_stem:scene_nodes[by_stem[name]].append(i)
    vpk=VpkArchive(args.game/'maps/dota.vpk');assets=[];materials={}
    for entry in entries:
        path=out/entry.removesuffix('.vmdl_c');path=path.with_suffix('.gltf')
        if not path.exists():raise FileNotFoundError(path)
        g=Gltf(path);parts=list(g.primitives());points=np.concatenate([p['points'] for p in parts]);source_nodes=scene_nodes[entry]
        if not source_nodes:raise ValueError('No original full-map node for '+entry)
        error=max(float(np.abs(matrices[n]-np.eye(4)).max()) for n in source_nodes)
        checks=[]
        for n in source_nodes[:2]:
            node=scene.doc['nodes'][n]
            for primitive in scene.doc['meshes'][node['mesh']]['primitives']:
                p=scene.accessor(primitive['attributes']['POSITION']);same=[a['points'] for a in parts if len(a['points'])==len(p)]
                if same:checks.append(min(float(np.abs(a-p).max()) for a in same))
        for m in g.doc.get('materials',[]):
            vmat=m.get('extras',{}).get('vmat',{});name=vmat.get('Name',m.get('name',''))
            if vmat:materials[name]=vmat
        assets.append({'entry':entry,'source':vpk.identity(entry),'gltf':str(path),'gltf_sha256':sha(path),
            'triangles':sum(len(p['faces']) for p in parts),'layer_index':0,'world_baked':error<1e-5,
            'source_scene_nodes':source_nodes,'max_global_matrix_error_vs_identity':error,
            'global_matrices_column_major':[matrices[n].T.reshape(-1).tolist() for n in source_nodes],
            'bounds_min_metres':points.min(0).tolist(),'bounds_max_metres':points.max(0).tolist(),
            'prototype_raw_position_difference_metres':max(checks) if checks else None})
    images=[]
    for path in out.rglob('*.png'):images.append({'png':str(path),'sha256':sha(path),'bytes':path.stat().st_size})
    result={'complete':True,'scope':'Original default world layer (lr0) water geometry and vmat parameters. No destruction layers and no synthetic river plane.',
            'command':command,'tool_sha256':sha(args.tool),'log':str(log),'source_scene_sha256':sha(scene_path),
            'assets':assets,'paths':[a['gltf'] for a in assets],'materials':materials,'images':images,
            'triangle_count':sum(a['triangles'] for a in assets),'coordinate_basis':'glTF metres/Y-up, canonical game x from glTF Z and game y from negative glTF X.'}
    dest=reports/'water-manifest.json';temporary=dest.with_suffix('.tmp');temporary.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');temporary.replace(dest)
    print(json.dumps({'manifest':str(dest),'assets':len(assets),'triangles':result['triangle_count'],'materials':len(materials),'images':len(images),'complete':True}))


if __name__=='__main__':main()
