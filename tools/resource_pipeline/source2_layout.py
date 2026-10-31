"""Make a source-entity layout receipt; never edit H1 game/runtime coordinates."""
from pathlib import Path
import hashlib
import json
import re

from source2_vpk import VpkArchive
from source2_prepare import ROOT


def compact(entity):
    xyz=entity['origin'];x,y,z=xyz
    row={k:entity[k] for k in ('classname','targetname','MapUnitName','model','teamnumber','angles','scales','hammerUniqueId','compile_source_id','Disabled','target','NPCFirstWaypoint') if k in entity}
    row.update(source_xyz=xyz,gltf_xyz_metres=[y*.0254,z*.0254,x*.0254],
               canonical_game_xyz=[2048+x/4,2048-y/4,z/4],legacy_game_xyz=[2048+y/4,2048-x/4,z/4])
    return row


def main():
    reports=ROOT/'private/source2/reports'
    parsed=reports/'entity-default-parsed.json';entities=json.loads(parsed.read_text(encoding='utf8'))
    corners={p['targetname']:p for p in entities if p.get('classname')=='path_corner'}
    teams=[]
    for team_id,short,word in ((0,'good','goodguys'),(1,'bad','badguys')):
        records=[p for p in entities if str(p.get('teamnumber',''))==str(team_id+2)]
        team={'id':team_id,'name':'radiant' if team_id==0 else 'dire',
              'hero_spawns':[compact(p) for p in entities if p.get('classname')=='info_player_start_'+word],
              'ancient':next(compact(p) for p in records if p.get('classname')=='npc_dota_fort'),
              'fountain':next(compact(p) for p in records if p.get('classname')=='ent_dota_fountain'),
              'towers':[],'lanes':{}}
        for p in records:
            if p.get('classname')!='npc_dota_tower':continue
            r=compact(p);name=p.get('MapUnitName',p.get('targetname',''))
            tier=re.search(r'_tower([1-4])',name);lane=re.search(r'_(top|mid|bot)(?:\d+)?$',name)
            r['tier']=int(tier[1]) if tier else None;r['lane']=lane[1] if lane else None
            team['towers'].append(r)
        for lane in ('top','mid','bot'):
            spawner=next(p for p in entities if p.get('classname')=='npc_dota_spawner_'+short+'_'+lane)
            name=spawner['NPCFirstWaypoint'];seen=set();chain=[]
            while name and name not in seen:
                if name not in corners:raise ValueError('Missing original target path_corner: '+name)
                seen.add(name);node=corners[name];chain.append(compact(node));name=node.get('target','')
            team['lanes'][lane]={'spawner':compact(spawner),'first_waypoint':spawner['NPCFirstWaypoint'],
                                'ordered_path':chain,'point_count':len(chain),'terminal_loop_target':name or None,
                                'terminal_kind':'loop' if name in seen else 'empty_target'}
        teams.append(team)
    archive=VpkArchive('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/maps/dota.vpk')
    source=archive.identity('maps/dota/entities/default_ents.vents_c')
    raw=ROOT/'private/source2/raw/entities/default_ents.vents_c';raw.parent.mkdir(parents=True,exist_ok=True)
    data=archive.read(source['path'])
    if not raw.exists():raw.write_bytes(data)
    elif hashlib.sha256(raw.read_bytes()).hexdigest()!=source['sha256']:raise ValueError('Saved entity raw bytes differ')
    layers=[{k:p.get(k) for k in ('classname','targetname','layerName','spawnflags','childSpawnGroup','origin','hammerUniqueId')} for p in entities if p.get('classname')=='info_world_layer']
    result={'source':source,'raw_source_file':str(raw),'entity_dump':str(reports/'entity-default-DATA.txt'),
            'entity_dump_sha256':hashlib.sha256((reports/'entity-default-DATA.txt').read_bytes()).hexdigest(),
            'parsed_entities_file':str(parsed),'parsed_entity_count':len(entities),
            'coordinate_formulas':{'source':'Valve Source XYZ, units=inches; original entity origins unchanged',
                'gltf':'(X,Y,Z) = 0.0254 * (SourceY,SourceZ,SourceX), metres/Y-up',
                'canonical_game':'(x,y,height)=(2048+SourceX/4,2048-SourceY/4,SourceZ/4)',
                'canonical_from_gltf':'x=2048+glTF_Z*MAP_SCALE; y=2048-glTF_X*MAP_SCALE',
                'legacy_game':'(x,y,height)=(2048+SourceY/4,2048-SourceX/4,SourceZ/4), mirrored lane orientation',
                'MAP_SCALE':4096/(16384*.0254)},
            'teams':teams,'world_layer_entities':layers,
            'waypoint_note':'Each team follows its own exact target chain. Stop before repeated targets; preserve terminal self-loops as metadata. Do not reverse the other team path.',
            'spawn_note':'All ten original hero start entities have Disabled=false. Team slots retain original entity order; fountain and Ancient are distinct anchors.'}
    dest=reports/'layout.json';dest.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'layout':str(dest),'teams':[{'name':t['name'],'spawns':len(t['hero_spawns']),
         'ancient':t['ancient']['canonical_game_xyz'],'lane_points':{k:v['point_count'] for k,v in t['lanes'].items()}} for t in teams]}))


if __name__=='__main__':main()
