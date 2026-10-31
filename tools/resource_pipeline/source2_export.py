"""Export an explicitly named installed Source 2 VPK asset with VRF CLI.

Intermediate glTF, PNGs, logs, original compiled bytes and provenance remain
private. This does not simplify geometry or claim a successful H1 conversion.
Uses Source 2 Viewer / ValveResourceFormat: https://s2v.app
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
from urllib.parse import unquote

from source2_vpk import VpkArchive

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')


def digest(path:Path)->str:
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def inspect_gltf(path:Path)->dict:
    gltf=json.loads(path.read_text(encoding='utf8'))
    accessors=gltf.get('accessors',[])
    meshes=[]
    for index,mesh in enumerate(gltf.get('meshes',[])):
        primitives=[]
        for primitive in mesh.get('primitives',[]):
            positions=accessors[primitive['attributes']['POSITION']]
            indices=accessors[primitive['indices']] if 'indices' in primitive else None
            primitives.append({'vertices':positions['count'],'triangles':(indices['count'] if indices else positions['count'])//3,
                               'attributes':list(primitive['attributes']),'material':primitive.get('material'),
                               'local_bounds_min':positions.get('min'),'local_bounds_max':positions.get('max')})
        meshes.append({'index':index,'name':mesh.get('name'),'primitives':primitives})
    animations=[]
    for animation in gltf.get('animations',[]):
        times=[accessors[s['input']] for s in animation.get('samplers',[])]
        lo=min((a.get('min',[0])[0] for a in times),default=0)
        hi=max((a.get('max',[0])[0] for a in times),default=0)
        animations.append({'name':animation.get('name'),'channels':len(animation.get('channels',[])),
                           'start_seconds':lo,'end_seconds':hi,'duration_seconds':hi-lo})
    files=[]
    for node in gltf.get('buffers',[])+gltf.get('images',[]):
        uri=node.get('uri','')
        if not uri or uri.startswith('data:'):continue
        file=(path.parent/unquote(uri)).resolve()
        if not file.is_relative_to(path.parent.resolve()):raise ValueError('glTF URI escapes export directory')
        files.append({'uri':uri,'bytes':file.stat().st_size,'sha256':digest(file)})
    position_indices={p['attributes']['POSITION'] for m in gltf.get('meshes',[]) for p in m.get('primitives',[])}
    return {'gltf_file':str(path),'gltf_sha256':digest(path),'gltf_bytes':path.stat().st_size,
            'mesh_count':len(meshes),'node_count':len(gltf.get('nodes',[])),'skin_count':len(gltf.get('skins',[])),
            'material_count':len(gltf.get('materials',[])),'image_count':len(gltf.get('images',[])),
            'stored_primitive_vertices_sum':sum(p['vertices'] for m in meshes for p in m['primitives']),
            'unique_position_accessor_vertices_sum':sum(accessors[i]['count'] for i in position_indices),
            'stored_primitive_triangles_sum':sum(p['triangles'] for m in meshes for p in m['primitives']),
            'animations':animations,'meshes':meshes,'output_files':files,
            'material_source_resources':[m.get('extras',{}).get('vmat',{}) for m in gltf.get('materials',[])]}


def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name',required=True)
    parser.add_argument('--entry',required=True)
    parser.add_argument('--archive',type=Path,default=STEAM/'pak01_dir.vpk')
    parser.add_argument('--gameinfo',type=Path,default=STEAM/'gameinfo.gi')
    parser.add_argument('--tool',type=Path,default=WORKSPACE/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--animations',help='Exact original animation names, comma separated. Omit to export no animations.')
    parser.add_argument('--skeleton',action='store_true',help='Enable VRF skeleton export for wearables, even when no intrinsic animation clips exist.')
    parser.add_argument('--mesh-list',help='VRF exact mesh-name filter, comma separated.')
    parser.add_argument('--no-materials',action='store_true')
    parser.add_argument('--inspect-existing',action='store_true',help='Inspect an existing export without rerunning VRF; command is recorded as reproduction instructions.')
    args=parser.parse_args()
    if any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in args.name):parser.error('Use a simple asset name')
    output=(args.output or ROOT/'private/source2/exports'/args.name/(args.name+'.gltf')).resolve()
    output.parent.mkdir(parents=True,exist_ok=True)
    reports=ROOT/'private/source2/reports';reports.mkdir(parents=True,exist_ok=True)
    log=reports/(args.name+'-export.log')
    command=[str(args.tool.resolve()),'-d','-i',str(args.archive.resolve()),'-f',args.entry,'-o',str(output),
             '--game',str(args.gameinfo.resolve()),'--gltf_export_format','gltf','--gltf_export_extras']
    if not args.no_materials:command+=['--gltf_export_materials','--gltf_textures_adapt']
    if args.skeleton or args.animations:command+=['--gltf_export_animations']
    if args.animations:command+=['--gltf_animation_list',args.animations]
    if args.mesh_list:command+=['--gltf_mesh_list',args.mesh_list]
    if not args.inspect_existing:
        with log.open('w',encoding='utf8') as stream:
            process=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,check=False)
        if process.returncode:raise RuntimeError(f'VRF failed with exit {process.returncode}; see {log}')
    if not output.is_file():raise FileNotFoundError('VRF did not create the requested glTF; an unmatched VPK filter can exit successfully')
    archive=VpkArchive(args.archive)
    identity=archive.identity(args.entry)
    raw=ROOT/'private/source2/raw'/args.name/Path(args.entry).name
    raw.parent.mkdir(parents=True,exist_ok=True);data=archive.read(args.entry)
    if raw.exists() and raw.read_bytes()!=data:raise FileExistsError('Existing original compiled bytes differ; choose another asset name')
    if not raw.exists():raw.write_bytes(data)
    summary=inspect_gltf(output)
    if not summary['mesh_count'] or not summary['stored_primitive_triangles_sum']:raise ValueError('Export contains no triangle geometry')
    dependency_archive=archive if args.archive.resolve()==(args.gameinfo.parent/'pak01_dir.vpk').resolve() else VpkArchive(args.gameinfo.parent/'pak01_dir.vpk')
    dependency_names=set()
    for material in summary['material_source_resources']:
        if material.get('Name'):dependency_names.add(material['Name']+'_c')
        dependency_names.update(name+'_c' for name in material.get('TextureParams',{}).values() if isinstance(name,str))
    dependencies=[]
    for entry in sorted(dependency_names):
        if entry in dependency_archive.entries:dependencies.append(dependency_archive.identity(entry))
        else:dependencies.append({'path':entry,'status':'not_found_in_base_pak01'})
    result={'recorded_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'owner':'Valve Corporation',
            'source':identity,'source_archive_sha256':digest(args.archive),'source_raw_file':str(raw),'tool':str(args.tool),'tool_sha256':digest(args.tool),
            'tool_project':'https://github.com/ValveResourceFormat/ValveResourceFormat','tool_release':'20.1',
            'reproduction_command':command,'command_executed_this_invocation':not args.inspect_existing,'log':str(log) if log.exists() else None,
            'dependencies':dependencies,**summary,
            'scope':'Original installed compiled asset exported by VRF to glTF/PNG. No invented mesh, animation, simplification or H1 runtime conversion is performed here.'}
    report=reports/(args.name+'-provenance.json');report.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'report':str(report),'gltf':str(output),'vertices':summary['unique_position_accessor_vertices_sum'],
                      'triangles':summary['stored_primitive_triangles_sum'],'animations':[a['name'] for a in summary['animations']],
                      'source_sha256':identity['sha256']},ensure_ascii=False))


if __name__=='__main__':main()
