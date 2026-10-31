"""Export omitted map props from installed Source2 resources and exact map instances.

The original preview exporter selected only a Radiant courtyard ROI. This
catalogue adds the missing high-ground trees, structural props and map
objectives, preserving scene matrices, entity skins and source tints. Filler
buildings remain dynamic sprites and are never painted into the map.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, re, subprocess, sys
from gltf_assets import Gltf, np, ROOT
from source2_export import STEAM
from source2_vpk import VpkArchive
from bake_world_instances import read_world, read_scene_tints, tree_entity_tints

REPORT=ROOT/'private/source2/reports/map-completion-manifest.json'
EXPORT=ROOT/'private/source2/exports/map-completion'
AGGREGATES=(
 'tree_gnarly001','tree_oak_leaves_05','tree_oak_leaves_08','tree_oak_leaves_blank',
 'bridge_mines001','waterfall_bridge_statue002_tintable','good_base_wall004',
 'radiant_wall001','stone_wall002','stone_wall004','temple_stairs_blocks002',
 'bad_wallstatue002a','dark_statue','dire_river_statue','radiant_statue002',
 'radiant_tower_base002','statue001','wood_fence001_tintable','wood_walls001',
)
ENTITY_CLASSES=('npc_dota_xp_fountain','npc_dota_lotus_pool','npc_dota_lantern',
                'npc_dota_watch_tower','npc_dota_unit_twin_gate','npc_dota_building')

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p): return json.loads(Path(p).read_text(encoding='utf8'))
def model_path(e): return re.search(r'"([^"]+)"', e['model'])[1]

def lotus_skin():
    """Source material group1 replaces only the opaque pool body's colour map."""
    out=EXPORT/'lotus-dire';out.mkdir(exist_ok=True)
    vpk=VpkArchive(STEAM/'pak01_dir.vpk');tool=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
    entry='materials/models/props_gameplay/fountain_of_life/fountain_of_life_update_dire.vmat_c'
    raw=out/'lotus-dire.vmat_c';raw.write_bytes(vpk.read(entry))
    proc=subprocess.run([str(tool),'-i',str(raw),'-b','DATA'],capture_output=True,text=True,check=True)
    data=out/'material-DATA.txt';data.write_text(proc.stdout,encoding='utf8')
    texture=re.search(r'm_name = "g_tColor"\s+m_pValue = resource:"([^"]+)"',proc.stdout)[1]+'_c'
    assert 'g_vColorTint"\n\t\t\tm_value = [ 1.0, 1.0, 1.0, 0.0 ]' in proc.stdout.replace('\r\n','\n')
    image=out/'body.png'
    if not image.exists():subprocess.run([str(tool),'-d','-i',str(vpk.path),'-f',texture,'-o',str(image),'--game',str(STEAM/'gameinfo.gi')],check=True,capture_output=True)
    model_data=ROOT/'private/source2/raw/complete_fountain_of_life_update/fountain_of_life_update.vmdl_c'
    proc=subprocess.run([str(tool),'-i',str(model_data),'-b','DATA'],capture_output=True,text=True,check=True)
    model_txt=out/'model-DATA.txt';model_txt.write_text(proc.stdout,encoding='utf8')
    assert re.search(r'm_name = "1"\s+m_materials =\s*\[\s*resource:"'+re.escape(entry[:-2])+r'"',proc.stdout)
    receipt=dict(source_material=vpk.identity(entry),source_texture=vpk.identity(texture),
        model_data_sha256=sha(model_data),model_dump_sha256=sha(model_txt),material_dump_sha256=sha(data),
        png=image.relative_to(ROOT).as_posix(),png_sha256=sha(image),
        material_group='1',replacement='fountain_of_life_update',colour_tint=[1,1,1],alpha_mode='OPAQUE')
    (out/'provenance.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
    return dict(path=(out/'provenance.json').relative_to(ROOT).as_posix(),sha256=sha(out/'provenance.json'))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--workers',type=int,default=3)
    ap.add_argument('--inventory-only',action='store_true')
    args=ap.parse_args()
    scene_path=ROOT/'private/source2/exports/map-geometry/dota.gltf'
    scene=Gltf(scene_path); matrices=scene.globals()
    entities_path=ROOT/'private/source2/reports/entity-default-parsed.json'
    entities=read(entities_path); world=read_world(); ordinary=read_scene_tints()
    old={e['stem'] for e in read(ROOT/'private/source2/reports/map-static-manifest.json')['assets']}
    jobs={}
    for short in AGGREGATES:
        stem='n0_lr0_agg_prop_'+short+'_0'
        assert stem in world,stem
        jobs[stem]=dict(name=stem,kind='aggregate',stem=stem,
                        entry='maps/dota/worldnodes/'+stem+'.vmdl_c',archive=str(STEAM/'maps/dota.vpk'))
    for stem in ('n0_lr0_agg_merge_props_generic_02_0','n0_lr0_agg_merge_tile_stone_000_50_0'):
        jobs[stem]=dict(name=stem,kind='aggregate',stem=stem,
                        entry='maps/dota/worldnodes/'+stem+'.vmdl_c',archive=str(STEAM/'maps/dota.vpk'))
    for stem, records in ordinary.items():
        if stem not in old and stem.startswith('ramp_'):
            jobs[stem]=dict(name=stem,kind='ordinary',stem=stem,
                           entry=records[0]['resource']+'_c',archive=str(STEAM/'pak01_dir.vpk'))
    jobs['tree_cine_02_low']=dict(name='tree_cine_02_low',kind='entity_tree',stem='tree_cine_02_low',
                        entry='models/props_tree/tree_cine_02_low.vmdl_c',archive=str(STEAM/'pak01_dir.vpk'))
    for e in entities:
        if e.get('classname') not in ENTITY_CLASSES: continue
        resource=model_path(e); stem=Path(resource).stem
        jobs.setdefault(stem,dict(name=stem,kind='entity',stem=stem,entry=resource+'_c',archive=str(STEAM/'pak01_dir.vpk')))
    if args.inventory_only:
        print(json.dumps(dict(resources=len(jobs),jobs=list(jobs.values()),
             entities={c:sum(e.get('classname')==c for e in entities) for c in ENTITY_CLASSES}),indent=2));return
    def export(j):
        output=EXPORT/j['name']/(j['name']+'.gltf')
        command=[sys.executable,str(ROOT/'tools/source2_export.py'),'--name','complete_'+j['name'],
                 '--entry',j['entry'],'--archive',j['archive'],'--output',str(output)]
        if output.exists(): command+=['--inspect-existing']
        proc=subprocess.run(command,capture_output=True,text=True,encoding='utf8',errors='replace')
        if proc.returncode: raise RuntimeError(proc.stdout+proc.stderr)
        print(json.dumps(dict(exported=j['name'])),flush=True)
        return dict(j,gltf=output.relative_to(ROOT).as_posix(),sha256=sha(output),
                    provenance='private/source2/reports/complete_'+j['name']+'-provenance.json')
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        assets=list(pool.map(export,jobs.values()))
    grouped={}
    for i,n in enumerate(scene.doc['nodes']):
        if 'mesh' not in n: continue
        stem=n.get('name','').split('.')[0]
        mesh=scene.doc['meshes'][n['mesh']]
        for name in jobs:
            if stem==name or mesh.get('name','').startswith(name+'_fragment'):
                grouped.setdefault(name,[]).append(i);break
    filler=0
    for asset in assets:
        name=asset['name']; prototype=Gltf(ROOT/asset['gltf']); parts=list(prototype.primitives())
        asset['buffers']=prototype.buffer_sha256
        asset['source_triangles']=sum(len(p['faces']) for p in parts)
        asset['placements']=[]
        nodes=grouped.get(name,[])
        if asset['kind']=='aggregate':
            for ni in nodes:
                n=scene.doc['nodes'][ni]; mesh=scene.doc['meshes'][n['mesh']]
                fragment=int(re.search(r'_fragment(\d+)$',mesh['name'])[1])-1
                original=world[name][fragment]
                error=float(np.abs(original['matrix']-matrices[ni]).max())
                assert error<.001,(name,fragment,error)
                p=mesh['primitives'];assert len(p)==1
                xyz=scene.accessor(p[0]['attributes']['POSITION'])
                draw=parts[original['draw']]
                assert xyz.shape==draw['points'].shape and np.max(np.abs(xyz-draw['points']))<1e-4
                faces=scene.accessor(p[0]['indices']).reshape(-1,3).astype(np.int32)
                assert set(map(tuple,faces.tolist())).issubset(set(map(tuple,draw['faces'].tolist())))
                asset['placements'].append(dict(node=ni,fragment=fragment,draw=original['draw'],
                    matrix_column_major=matrices[ni].T.reshape(-1).tolist(),tint=original['tint'].tolist(),
                    original_face_count=len(faces),matrix_error_metres=error))
        else:
            unique={}
            for ni in nodes:
                key=tuple(np.rint(matrices[ni].reshape(-1)*1e6).astype(int))
                unique.setdefault(key,dict(matrix_column_major=matrices[ni].T.reshape(-1).tolist(),nodes=[]))['nodes'].append(ni)
            for p in unique.values():
                matrix=np.asarray(p['matrix_column_major']).reshape(4,4).T
                if asset['kind']=='ordinary':
                    choices=ordinary[name];errors=[float(np.abs(v['matrix']-matrix).max()) for v in choices]
                    at=int(np.argmin(errors));assert errors[at]<.001
                    p['tint']=choices[at]['tint'].tolist();p['source_index']=choices[at]['source_index']
                else:
                    candidates=[e for e in entities if ('tree_cine_02_low.vmdl' in e.get('model','') if asset['kind']=='entity_tree' else e.get('classname') in ENTITY_CLASSES and model_path(e)==asset['entry'][:-2])]
                    origin=matrix[:3,3];errors=[float(np.abs(np.asarray(e['origin'])[[1,2,0]]*.0254-origin).max()) for e in candidates]
                    at=int(np.argmin(errors));assert errors[at]<.001,(name,min(errors))
                    entity=candidates[at]
                    p['entity']={k:entity.get(k) for k in ('classname','targetname','MapUnitName','hammerUniqueId','origin','angles','scales','skin','rendercolor')}
                    p['tint']=(np.asarray(entity['rendercolor'])/255).tolist()
                    p['world_xyz']=(np.asarray(entity['origin'])*[.25,-.25,.25]+[2048,2048,0]).tolist()
                    if entity['classname']=='npc_dota_building':
                        # Preserve source entity order, not spatial or model-group order.
                        buildings=[e for e in entities if e.get('classname')=='npc_dota_building']
                        index=next(i for i,e in enumerate(buildings) if e['hammerUniqueId']==entity['hammerUniqueId'])
                        p['model_id']=59+index;p['filler_index']=index;filler+=1
                asset['placements'].append(p)
        assert asset['placements'],name
    assert filler==14,filler
    skin=lotus_skin()
    result=dict(complete=True,assets=assets,lotus_dire_skin=skin,scene_sha256=sha(scene_path),entities_sha256=sha(entities_path),
         source_sha256={p.relative_to(ROOT).as_posix():sha(p) for p in (Path(__file__),scene_path,entities_path,ROOT/'private/source2/reports/worldnode-DATA.txt')},
         scope='Exact original map props omitted by the ROI exporter.14 filler buildings stay dynamic; static objectives use original entity matrices and tints.')
    REPORT.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(manifest=REPORT.relative_to(ROOT).as_posix(),resources=len(assets),placements=sum(len(a['placements']) for a in assets),dynamic_fillers=filler)))

if __name__=='__main__':main()
