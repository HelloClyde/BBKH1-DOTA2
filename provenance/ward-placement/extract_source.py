"""Read original Dota map triggers/PHYS; write only a new local audit folder.

No game source, Steam resource, old project, USB or release is modified.
The VPK reader and pre-extracted entity records are explicit source inputs.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def convex(points):
    p = sorted(set(points))
    def cross(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    lower, upper = [], []
    for q in p:
        while len(lower) > 1 and cross(lower[-2], lower[-1], q) <= 0:
            lower.pop()
        lower.append(q)
    for q in reversed(p):
        while len(upper) > 1 and cross(upper[-2], upper[-1], q) <= 0:
            upper.pop()
        upper.append(q)
    return lower[:-1]+upper[:-1]


def inside(polygon, x, y):
    return all((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]) >= 0
               for a,b in zip(polygon, polygon[1:]+polygon[:1]))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpk', required=True, type=Path)
    ap.add_argument('--entities', required=True, type=Path)
    ap.add_argument('--entity-manifest', required=True, type=Path)
    ap.add_argument('--entity-data-text', required=True, type=Path)
    ap.add_argument('--vpk-reader', required=True, type=Path)
    ap.add_argument('--viewer', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    a=ap.parse_args()
    out=a.output.resolve()
    if out.exists():
        raise SystemExit('Refuse to overwrite an existing evidence directory: '+str(out))
    spec=importlib.util.spec_from_file_location('ward_audit_vpk_reader', a.vpk_reader)
    module=importlib.util.module_from_spec(spec)
    sys.modules[spec.name]=module
    spec.loader.exec_module(module)
    vpk=module.VpkArchive(a.vpk)
    manifest=json.loads(a.entity_manifest.read_text(encoding='utf8'))
    assert sha(a.entities)==manifest['files']['entities.json']
    assert sha(a.entity_data_text)==manifest['files']['default_ents-DATA.txt']
    source=vpk.identity('maps/dota/entities/default_ents.vents_c')
    assert source['sha256']==manifest['sources']['default_ents']['sha256']
    entities=[e for e in json.loads(a.entities.read_text(encoding='utf8'))
              if e.get('classname')=='trigger_no_wards']
    assert len(entities)==5
    assert all(e['_source_layer']=='default' and e['StartDisabled'] is False
               and not e.get('filtername') and not e.get('parentname')
               and not e.get('targetname') and not e.get('vscripts')
               and not e.get('useLocalOffset') for e in entities)
    out.mkdir(parents=True)
    source_inputs={str(p.resolve()):sha(p) for p in [a.entities,a.entity_manifest,
                  a.entity_data_text,a.vpk_reader,a.viewer,Path(__file__)]}
    items=[]
    all_hulls=[]
    for index,e in enumerate(entities):
        entry=re.search(r'"([^"]+)"',e['model'])[1]+'_c'
        identity=vpk.identity(entry)
        raw=vpk.read(entry)
        stem=Path(entry).name
        (out/stem).write_bytes(raw)
        result=subprocess.run([str(a.viewer.resolve()),'-i',str(a.vpk.resolve()),
                               '-f',entry,'-b','PHYS'],capture_output=True,
                              encoding='utf8',check=True)
        dump=result.stdout
        assert '--- Data for block "PHYS" ---' in dump
        phys=dump.split('--- Data for block "PHYS" ---',1)[1].strip()+'\n'
        phys_path=out/(stem+'.PHYS.txt')
        phys_path.write_text(phys,encoding='utf8',newline='\n')
        arrays=re.findall(r'm_VertexPositions\s*=\s*#\[([^]]+)\]',phys)
        assert arrays and 'm_spheres = [  ]' in phys and 'm_capsules = [  ]' in phys
        assert 'm_meshes = [  ]' in phys and 'm_compounds = [  ]' in phys
        pitch,yaw,roll=e['angles']
        assert pitch==roll==0 and e['scales']==[1.0,1.0,1.0]
        c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
        o=e['origin']
        hulls=[]
        for local_index,blob in enumerate(arrays):
            data=bytes.fromhex(blob)
            assert len(data)%12==0
            local=[struct.unpack_from('<fff',data,i) for i in range(0,len(data),12)]
            world_source=[(o[0]+c*x-s*y,o[1]+s*x+c*y,o[2]+z) for x,y,z in local]
            projected=[(2048+x/4,2048-y/4) for x,y,z in world_source]
            raw_outline=convex(projected)
            q8=convex([(round(x*256),round(y*256)) for x,y in projected])
            assert len(q8)>=3
            assert sum(x*y2-y*x2 for (x,y),(x2,y2) in zip(q8,q8[1:]+q8[:1]))>0
            bound=[min(p[0] for p in q8),max(p[0] for p in q8),
                   min(p[1] for p in q8),max(p[1] for p in q8)]
            # Quantization changes only sub-world boundary detail. Count any
            # integer-position difference rather than claim float identity.
            mismatches=[]
            for y in range(math.floor(bound[2]/256)-1, math.ceil(bound[3]/256)+2):
                for x in range(math.floor(bound[0]/256)-1,math.ceil(bound[1]/256)+2):
                    if inside(raw_outline,x,y)!=inside(q8,x*256,y*256):
                        mismatches.append([x,y])
            h={'source_entity_index':index,'source_compile_id':e['compile_source_id'],
               'model_hull_index':local_index,'local_xyz_vertices':local,
               'source_xy_polygon':convex([(x,y) for x,y,z in world_source]),
               'source_z_bounds':[min(z for x,y,z in world_source),max(z for x,y,z in world_source)],
               'world_xy_polygon':raw_outline,'world_q8_ccw':q8,'bounds_q8':bound,
               'integer_xy_quantization_differences':mismatches}
            hulls.append(h)
            all_hulls.append(h)
        items.append({'entity':e,'source_resource':identity,
                      'compiled_snapshot':{'path':stem,'sha256':sha(out/stem)},
                      'phys_text':{'path':phys_path.name,'sha256':sha(phys_path)},
                      'hulls':hulls})
    d={'format_version':1,'passed':True,'read_only_original_sources':True,
       'purpose':'Original enabled trigger_no_wards geometry for H1 ward placement',
       'source_default_entities':source,'source_inputs':source_inputs,
       'coordinate_mapping':{'x':'2048 + SourceX / 4','y':'2048 - SourceY / 4',
                             'z':'SourceZ / 4','entity_rotation':'Rz(yaw), pitch=roll=0, scale=1'},
       'runtime_adaptation':'2D XY projection of each original convex PHYS hull; membership is union, not convex hull of all brushes. Q8 nearest rounding (1/256 world).',
       'height_limitation':'Original Source Z intervals retained; H1 placement query has XY only and does not emulate arbitrary 3D flying/ward-height overlap.',
       'activation':{'active_layer':'default','StartDisabled':False,
                     'filters_parent_target_vscripts':'empty in all five records',
                     'team_field_present':False,
                     'spawnflags':[e['spawnflags'] for e in entities],
                     'native_spawnflag_bit_semantics':'Not reverse engineered; raw values retained, not invented.'},
       'scope':'All five actual default-active no-wards triggers, including the small fifth region; not fountain healing circles, walkability or FOW pit polygons.',
       'entities':items,'hulls':all_hulls,'entity_count':len(items),'hull_count':len(all_hulls),
       'vertex_count':sum(len(h['world_q8_ccw']) for h in all_hulls),
       'integer_quantization_difference_count':sum(len(h['integer_xy_quantization_differences']) for h in all_hulls)}
    (out/'source-audit.json').write_text(json.dumps(d,indent=2,ensure_ascii=True)+'\n',encoding='utf8',newline='\n')
    print(json.dumps({'path':str(out/'source-audit.json'),'sha256':sha(out/'source-audit.json'),
                      'entities':d['entity_count'],'hulls':d['hull_count'],'vertices':d['vertex_count'],
                      'integer_quantization_difference_count':d['integer_quantization_difference_count']}))


if __name__=='__main__':
    main()
