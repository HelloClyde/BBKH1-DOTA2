"""Export original static cliff/base/stair meshes that cover the Radiant ROI.

Selection was checked against actual triangle-used bounds in the full installed
map export, not just model names. World placements come from that same scene.
No new floor geometry is generated.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from source2_prepare import ROOT
from gltf_assets import Gltf,np

MAP_ASSETS=['cliff_wall001','cliff_wall002','cliff_wall003','good_base_wall003','good_base_wall001',
            'stone_wall001','stone_wall003','temple_stairs001']
PROPS=['ramp_ext_lft_001a','ramp_ext_rt_001a']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota'))
    parser.add_argument('--scene',type=Path,default=ROOT/'private/source2/exports/map-geometry/dota.gltf')
    parser.add_argument('--workers',type=int,default=3)
    args=parser.parse_args();jobs=[]
    for short in MAP_ASSETS:
        stem='n0_lr0_agg_prop_'+short+'_0'
        # agg_prop is still an instanced local prototype. Its scene matrices
        # contain nontrivial rotation/scale/translation, unlike merge ground.
        jobs.append({'name':'static_'+short,'stem':stem,'entry':'maps/dota/worldnodes/'+stem+'.vmdl_c','archive':args.game/'maps/dota.vpk','world_baked':False})
    for short in PROPS:
        jobs.append({'name':'static_'+short,'stem':short,'entry':'models/props_structures/'+short+'.vmdl_c','archive':args.game/'pak01_dir.vpk','world_baked':False})
    def export(job):
        output=ROOT/'private/source2/exports/map-static'/job['name']/(job['name']+'.gltf')
        command=[sys.executable,str(ROOT/'tools/source2_export.py'),'--name',job['name'],'--entry',job['entry'],
                 '--archive',str(job['archive']),'--gameinfo',str(args.game/'gameinfo.gi'),'--output',str(output)]
        if output.is_file():command+=['--inspect-existing']
        process=subprocess.run(command,capture_output=True,text=True,encoding='utf8',errors='replace',check=False)
        result={**job,'archive':str(job['archive']),'output':str(output),'exit_code':process.returncode,
                'result':process.stdout.strip(),'error':process.stderr[-3000:] if process.returncode else None}
        print(json.dumps({'name':job['name'],'exit_code':process.returncode}),flush=True);return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.workers,4))) as pool:
        results=list(pool.map(export,jobs))
    scene=Gltf(args.scene);matrices=scene.globals()
    by_stem={r['stem']:r for r in results};placements={stem:{} for stem in by_stem}
    for node_index,node in enumerate(scene.doc['nodes']):
        if 'mesh' not in node:continue
        mesh=scene.doc['meshes'][node['mesh']];name=mesh.get('name','')
        for stem in by_stem:
            if not (name==stem or name.startswith(stem+'.') or name.startswith(stem+'_fragment')):continue
            matrix=matrices[node_index]
            key=tuple(np.rint(matrix.reshape(-1)*1e6).astype(int))
            if key not in placements[stem]:
                placements[stem][key]={'matrix_column_major':matrix.T.reshape(-1).tolist(),'source_nodes':[],'source_meshes':[]}
            record=placements[stem][key];record['source_nodes'].append(node_index)
            if name not in record['source_meshes']:record['source_meshes'].append(name)
            break
    for r in results:
        r['placements']=list(placements[r['stem']].values())
        r['gltf']=r['output']
        r['global_matrices_column_major']=[p['matrix_column_major'] for p in r['placements']]
        if r['exit_code']==0:
            prototype=Gltf(r['output']);parts=list(prototype.primitives())
            points=np.concatenate([p['points'] for p in parts]);lo=points.min(0);hi=points.max(0)
            r['prototype_bounds_min_metres']=lo.tolist();r['prototype_bounds_max_metres']=hi.tolist()
            corners=np.array([[x,y,z,1] for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])])
            for placement in r['placements']:
                matrix=np.array(placement['matrix_column_major']).reshape(4,4).T
                placed=corners@matrix.T
                placement['bounds_min_metres']=placed[:,:3].min(0).tolist()
                placement['bounds_max_metres']=placed[:,:3].max(0).tolist()
            # Cross-check prototype coordinates against the full map's raw
            # pre-instance vertices. Names alone do not prove geometry space.
            errors=[]
            for node_index in [n for p in r['placements'] for n in p['source_nodes']][:3]:
                node=scene.doc['nodes'][node_index]
                for primitive in scene.doc['meshes'][node['mesh']]['primitives']:
                    original=scene.accessor(primitive['attributes']['POSITION'])
                    candidates=[p['points'] for p in parts if len(p['points'])==len(original)]
                    if candidates:errors.append(min(float(np.abs(p-original).max()) for p in candidates))
            r['prototype_crosscheck']={'source':'full map pre-instance POSITION accessors','compared_primitive_samples':len(errors),
                                        'max_position_difference_metres':max(errors) if errors else None,
                                        'passed':bool(errors) and max(errors)<1e-4}
        r['placement_note']='Apply original scene matrix to model-local prototype geometry, including agg_prop resources. Never infer world-baked status from an aggregate filename.'
    manifest={'complete':all(r['exit_code']==0 and r['placements'] for r in results),'scene':str(args.scene),
              'scene_sha256':hashlib.sha256(args.scene.read_bytes()).hexdigest(),
              'selection_roi_metres_xz':[[-245,-225],[-85,-95]],
              'coordinate_basis':'glTF metres, Y-up; same as the 134 original ground chunks.',
              'assets':results,'scope':'Original Steam static meshes and original world placements. No synthetic ground patches.'}
    dest=ROOT/'private/source2/reports/map-static-manifest.json';temporary=dest.with_suffix('.tmp')
    temporary.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8');temporary.replace(dest)
    print(json.dumps({'manifest':str(dest),'complete':manifest['complete'],'assets':len(results)}))
    if not manifest['complete']:raise SystemExit(1)


if __name__=='__main__':main()
