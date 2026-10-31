"""Prepare four original Dota heroes and their verified default equipment.

The default item choices were read from default_item/used_by_heroes entries in
the installed scripts/items/items_game.txt, excluding persona cosmetics.
Uses Source 2 Viewer / ValveResourceFormat (https://s2v.app).
"""
from __future__ import annotations
import argparse
import concurrent.futures
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys

from source2_vpk import VpkArchive

ROOT=Path(__file__).resolve().parents[1]
CONFIG={
    'axe':{'clips':{'idle':'idle','run':'run'},'parts':['axe_weapon','axe_armor','axe_ponytail','axe_belt']},
    'lina':{'clips':{'idle':'idle','run':'run'},'parts':['lina_neck','lina_head','lina_arms','lina_belt']},
    'juggernaut':{'clips':{'idle':'idle','run':'run_run'},'parts':['jugg_mask','jugg_sword','jugg_cape','jugg_bracers','juggernaut_pants']},
    'crystal_maiden':{'clips':{'idle':'cm_idle','run':'cm_run'},'parts':['crystal_maiden_cape','crystal_maiden_shoulders','crystal_maiden_staff','crystal_maiden_cuffs','head_item']},
}
PROPS={
    'radiant_tower':'models/props_structures/radiant_tower002.vmdl_c',
    'dire_tower':'models/props_structures/dire_tower002.vmdl_c',
    'radiant_base':'models/props_structures/radiant_ancient001.vmdl_c',
    'dire_base':'models/props_structures/dire_ancient_base001.vmdl_c',
    'radiant_creep':'models/creeps/lane_creeps/creep_radiant_melee/radiant_melee.vmdl_c',
    'dire_creep':'models/creeps/lane_creeps/creep_bad_melee/creep_bad_melee.vmdl_c',
    'radiant_ranged':'models/creeps/lane_creeps/creep_radiant_ranged/radiant_ranged.vmdl_c',
    'dire_ranged':'models/creeps/lane_creeps/creep_bad_ranged/lane_dire_ranged.vmdl_c',
    'radiant_tree':'models/props_tree/tree_oak_01.vmdl_c',
    'dire_tree':'models/props_tree/dire_tree004.vmdl_c',
}
PROP_VISIBLE_MESH_HINTS={
    'dire_creep':['creep_bad_melee_model'],
    'radiant_tree':['tree_oak_01_model'],
    'dire_tree':['dire_tree004_model'],
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota'))
    parser.add_argument('--tool',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--workers',type=int,default=3)
    parser.add_argument('--hero',action='append',choices=list(CONFIG),help='Limit to selected hero; repeat as needed.')
    parser.add_argument('--refresh',action='store_true',help='Re-export existing intermediate files as well.')
    parser.add_argument('--props-only',action='store_true',help='Export original towers/bases/creeps/trees instead of heroes.')
    parser.add_argument('--rigged-parts-only',action='store_true',help='Re-export all default wearable parts with explicit skeletons to exports/rigged/.')
    args=parser.parse_args();heroes=[] if args.props_only else args.hero or list(CONFIG);jobs=[]
    for hero in heroes:
        model_dir=ROOT/'private/source2/exports'/hero
        if not args.rigged_parts_only:
            jobs.append({'hero':hero,'name':hero,'part':False,'entry':f'models/heroes/{hero}/{hero}.vmdl_c',
                         'output':model_dir/(hero+'.gltf'),'animations':','.join(CONFIG[hero]['clips'].values())})
        for part in CONFIG[hero]['parts']:
            jobs.append({'hero':hero,'name':part+'_rigged' if args.rigged_parts_only else part,'part':True,'entry':f'models/heroes/{hero}/{part}.vmdl_c',
                         'output':ROOT/'private/source2/exports/rigged'/hero/(part+'.gltf') if args.rigged_parts_only else model_dir/'parts'/(part+'.gltf'),
                         'animations':None,'skeleton':args.rigged_parts_only})
    if args.props_only:
        for name,entry in PROPS.items():
            jobs.append({'hero':'props','name':name,'part':False,'entry':entry,
                         'output':ROOT/'private/source2/exports/props'/name/(name+'.gltf'),
                         'animations':'idle,run' if 'creep' in name or 'ranged' in name else None})
    def export(job):
        command=[sys.executable,str(ROOT/'tools/source2_export.py'),'--name',job['name'],'--entry',job['entry'],
                 '--output',str(job['output']),'--archive',str(args.game/'pak01_dir.vpk'),
                 '--gameinfo',str(args.game/'gameinfo.gi'),'--tool',str(args.tool)]
        if job['animations']:command+=['--animations',job['animations']]
        if job.get('skeleton'):command+=['--skeleton']
        if job['output'].is_file() and not args.refresh:command+=['--inspect-existing']
        process=subprocess.run(command,capture_output=True,text=True,encoding='utf8',errors='replace',check=False)
        result={'hero':job['hero'],'name':job['name'],'part':job['part'],'entry':job['entry'],
                'output':str(job['output']),'exit_code':process.returncode,'result':process.stdout.strip(),
                'error':process.stderr[-3000:] if process.returncode else None}
        print(json.dumps({'name':job['name'],'exit_code':process.returncode,'part':job['part']}),flush=True)
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.workers,4))) as pool:
        results=list(pool.map(export,jobs))
    vpk=VpkArchive(args.game/'pak01_dir.vpk');items_identity=vpk.identity('scripts/items/items_game.txt')
    appmanifest=args.game.parents[3]/'appmanifest_570.acf'
    build=None
    if appmanifest.exists():
        match=re.search(r'"buildid"\s+"(\d+)"',appmanifest.read_text(encoding='utf8'))
        if match:build=match[1]
    manifest={'prepared_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'steam_game':str(args.game),'steam_build_id':build,'default_items_source':items_identity,
              'default_equipment_rule':'items_game default_item + used_by_heroes; base persona, not persona_1.',
              'heroes':{},'props':{},'jobs':results,'complete':all(r['exit_code']==0 for r in results),
              'scope':'Original glTF geometry, materials and animations only. Full-body default models, not portrait billboards.'}
    for hero in heroes:
        hero_jobs=[r for r in results if r['hero']==hero]
        manifest['heroes'][hero]={'body':str(ROOT/'private/source2/exports'/hero/(hero+'.gltf')),
                                 'animation_names':CONFIG[hero]['clips'],
                                 'parts':[r['output'] for r in hero_jobs if r['part'] and r['exit_code']==0],
                                 'complete':all(r['exit_code']==0 for r in hero_jobs)}
    if args.props_only:
        manifest['default_units_source']=vpk.identity('scripts/npc/npc_units.txt')
        manifest['props']={r['name']:{'gltf':r['output'],'source_entry':r['entry'],'complete':r['exit_code']==0,
                                    'visible_mesh_name_suffixes':PROP_VISIBLE_MESH_HINTS.get(r['name']),
                                    'selection_note':'Filter alternate appearances/stumps/inspector meshes when suffixes are specified; otherwise keep original primary model meshes.'}
                           for r in results}
        manifest['scope']='Original installed structures, creeps and trees exported to glTF, with material textures. No runtime conversion.'
    report=ROOT/'private/source2/reports'/('props-manifest.json' if args.props_only else 'rigged-parts-manifest.json' if args.rigged_parts_only else 'heroes-manifest.json')
    report.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'manifest':str(report),'complete':manifest['complete'],'jobs':len(results)}))
    if not manifest['complete']:raise SystemExit(1)


if __name__=='__main__':main()
