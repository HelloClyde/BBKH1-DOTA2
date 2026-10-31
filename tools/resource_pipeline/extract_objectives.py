"""CRC-checked installed shrine, pool and Watcher definitions and map anchors.

Reads the installed game; never executes or modifies the game. Explicit H1
adaptations are separate from original definitions and Valve patch snapshots.
"""
from pathlib import Path
import datetime,hashlib,importlib.util,json,re,struct,sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'private/objectives/source-rules'
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
spec=importlib.util.spec_from_file_location('source_kv',ROOT/'private/passives/extract_sources.py')
kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v):
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def rel(p):return Path(p).resolve().relative_to(ROOT).as_posix()
def text_pointer(binary,pe,va):
    off=pe.get_offset_from_rva(va-pe.OPTIONAL_HEADER.ImageBase)
    return binary[off:binary.index(0,off)].decode('ascii')

def native_defaults():
    import pefile
    path=STEAM/'bin/win64/server.dll';binary=path.read_bytes();digest=sha(path)
    assert digest=='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
    pe=pefile.PE(data=binary,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase;rows=[]
    # Original static constructors pair each exact convar-name address with
    # its default, then call the same registration API. No broad byte scan.
    for name,lea_off,value_off,size,value,begin,end in (
        ('dota_xp_fountain_activation_interval',0x274882,0x3b96eb0,'<f',420.0,0x274870,0x274907),
        ('dota_lotus_respawn_time',0x270702,0x2706fe,'<I',180,0x270680,0x270728),
        ('dota_lotuses_max',0x270862,0x27085e,'<I',6,0x2707e0,0x270888)):
        assert binary[lea_off:lea_off+3]==bytes.fromhex('488d15')
        disp=struct.unpack_from('<i',binary,lea_off+3)[0]
        name_va=base+pe.get_rva_from_offset(lea_off+7)+disp
        assert text_pointer(binary,pe,name_va)==name
        actual=struct.unpack_from(size,binary,value_off)[0];assert actual==value
        rows.append(dict(name=name,default=actual,default_file_offset=value_off,
                         name_lea_file_offset=lea_off,name_va=hex(name_va),
                         constructor_file_offset=begin,constructor_bytes_hex=binary[begin:end].hex()))
    return dict(server_dll=str(path),server_sha256=digest,read_only=True,convars=rows,
                interpretation='Original constructors bind420s shrine interval,180s lotus interval and6 pool capacity. Native XP-award algorithm and full engine fog are not executed.')

def main():
    OUT.mkdir(parents=True,exist_ok=True);vpk=VpkArchive(STEAM/'pak01_dir.vpk');checks=0;receipts=[];docs={}
    for name in ('npc_units','npc_abilities','items','neutral_items'):
        entry=f'scripts/npc/{name}.txt';raw=vpk.read(entry);path=OUT/(name+'.txt');path.write_bytes(raw)
        receipts.append(vpk.identity(entry));docs[name]=next(iter(kv.parse(raw.decode('utf-8-sig')).values()))
    units={k:docs['npc_units'][k] for k in ('npc_dota_xp_fountain','npc_dota_lotus_pool','npc_dota_lantern')}
    abilities={k:docs['npc_abilities'][k] for k in ('ability_xp_fountain','ability_lotus_pool','ability_pluck_lotus','ability_lamp_use')}
    items={k:docs['items'][k] for k in ('item_famango','item_great_famango','item_greater_famango','item_recipe_great_famango','item_recipe_greater_famango')}
    entities_path=ROOT/'private/source2/reports/entity-default-parsed.json';entities=json.loads(entities_path.read_text(encoding='utf8'))
    rows=[]
    for kind,count in (('npc_dota_xp_fountain',2),('npc_dota_lotus_pool',2),('npc_dota_lantern',10)):
        selected=[e for e in entities if e.get('classname')==kind];assert len(selected)==count;checks+=1
        for e in selected:
            x,y,z=e['origin'];pos=[int(2048+x/4+.5),int(2048-y/4+.5),int(z/4+.5)]
            rows.append({k:e[k] for k in ('classname','origin','angles','scales','model','MapUnitName','teamnumber','hammerUniqueId')}|dict(world_xyz=pos))
    game_text=(ROOT/'game/objectives.c').read_text(encoding='utf8')
    position_block=game_text.split('dota_objective_positions[DOTA_OBJECTIVES]={',1)[1].split('};',1)[0]
    positions=[list(map(int,m)) for m in re.findall(r'\{(-?\d+),(-?\d+)\}',position_block)]
    assert positions==[r['world_xyz'][:2] for r in rows];checks+=14
    heights=list(map(int,game_text.split('dota_objective_heights[DOTA_OBJECTIVES]={',1)[1].split('}',1)[0].split(',')))
    assert heights==[r['world_xyz'][2] for r in rows];checks+=14
    av=lambda name,key:abilities[name]['AbilityValues'][key]
    for name,key,value in [('ability_xp_fountain','radius','300'),('ability_xp_fountain','countdown_time','3'),
        ('ability_lotus_pool','radius','350'),('ability_lotus_pool','first_lotus_pickup_time','1.5'),
        ('ability_lotus_pool','pickup_time_reduction_pct','70'),('ability_lotus_pool','min_lotus_pickup_time','0.3'),
        ('ability_lamp_use','active_duration','420'),('ability_lamp_use','inactive_duration','120')]:
        assert av(name,key)==value;checks+=1
    assert abilities['ability_lamp_use']['AbilityCastRange']=='200';checks+=1
    assert abilities['ability_lamp_use']['AbilityChannelTime']=='1.0';checks+=1
    for item,value in [('item_famango','125'),('item_great_famango','400'),('item_greater_famango','900')]:
        assert items[item]['AbilityValues']['replenish_amount']==value;assert items[item]['AbilityCooldown']=='5';checks+=2
    assert units['npc_dota_lantern']['VisionDaytimeRange']=='800';checks+=1
    assert units['npc_dota_lantern']['VisionNighttimeRange']=='450';checks+=1
    tier_starts={key:value['start_time'] for key,value in docs['neutral_items']['neutral_tiers'].items()}
    assert tier_starts['4']=='35:00';assert tier_starts['5']=='60:00';checks+=2
    default=native_defaults();save(OUT/'native-defaults.json',default);checks+=6
    map_archive=VpkArchive(STEAM/'maps/dota.vpk');entity_receipt=map_archive.identity('maps/dota/entities/default_ents.vents_c')
    official=[];notes=[]
    for version in ('7.38','7.41'):
        p=ROOT/f'private/jungle/official-patches/{version}.json';doc=json.loads(p.read_text(encoding='utf8'))
        receipt=next(r for r in json.loads((ROOT/'private/jungle/official-patches/receipt.json').read_text(encoding='utf8'))['sources'] if r['patch']==version)
        assert sha(p)==receipt['sha256'];checks+=1;official.append(receipt)
        def walk(value,path=''):
            if isinstance(value,str) and any(t in value.lower() for t in ('lotus pools','lotuses','lotus takes','lotus in','shrines of wisdom','shrine will','shrines will','experience will be granted','wisdom shrine experience','wisdom shrines and lotus pools','within 300 units','first lotus')):
                notes.append(dict(patch=version,json_pointer=path,text=value))
            elif isinstance(value,list):
                for i,v in enumerate(value):walk(v,path+'/'+str(i))
            elif isinstance(value,dict):
                for k,v in value.items():walk(v,path+'/'+k)
        walk(doc)
    assert any('200/500/800' in r['text'] for r in notes);checks+=1
    assert any('reverse their countdowns' in r['text'] for r in notes);checks+=1
    save(OUT/'selected-original-entities.json',rows)
    result=dict(complete=True,observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        definitions=dict(units=units,abilities=abilities,items=items,neutral_tier_start_times=tier_starts),entity_source=entity_receipt,
        entities=rows,definition_sources=receipts,native_defaults=default,official_sources=official,official_notes=notes,
        source_entity_classifications='npc_dota_lantern is the fixed Watcher. npc_dota_watch_tower is a separate Outpost, not one of these10 Watchers. Lotus entity model overrides the misleading npc_units default outpost model.',
        h1_adaptations=dict(lotus_pouch='Separate bounded18-basic-equivalent pouch; combining3basic into1Great and2Great into1Greater follows original item recipes; use consumes greatest available grade. No sharing/ground-drop/inventory-slot machinery.',
            pool_late_game='At35:00 pools upgrade to Great Lotuses and existing basic count rounds up/3; at60:00 existing Great count rounds up/2 and pools spawn Greater Lotuses. Times are pinned by original neutral_items Tier4/Tier5 start_time. The60min simultaneous scheduled spawn occurs after conversion in H1; native tie ordering is not recovered.',
            missed_shrine='An unclaimed interval is replaced by the newest interval value, never accumulated.',
            tie_breaks='Lowest XP uses existing H1 level/current-XP ordering with lower slot tie-break; original engine tie-break and RandomInt distribution are not reconstructed.',
            vision='Owned Watchers expose an800Source day/450Source night vision query, without crossing pit regions. Existing replica only has pit fog; this is not complete shared-team global fog.',
            capture='Watcher uses ordinary source navigation and explicit player click+20tick channel; normal manual orders and disables interrupt. Nearby Wisdom/Lotus claims use passive area rules, not outdated hidden pluck ability.'),
        checks=checks,extractor_sha256=sha(__file__))
    save(OUT/'manifest.json',result)
    sources={rel(p):sha(p) for p in (Path(__file__),ROOT/'tools/source2_vpk.py',ROOT/'private/passives/extract_sources.py',entities_path,ROOT/'game/objectives.c',ROOT/'game/objectives.h',ROOT/'game/objective_data.h',OUT/'native-defaults.json',OUT/'selected-original-entities.json',*[OUT/(name+'.txt') for name in ('npc_units','npc_abilities','items','neutral_items')],*[ROOT/f'private/jungle/official-patches/{v}.json' for v in ('7.38','7.41')])}
    save(OUT/'verification.json',dict(passed=True,checks=checks,wisdom_shrines=2,lotus_pools=2,watchers=10,manifest_sha256=sha(OUT/'manifest.json'),source_sha256=sources,physical_h1_tested=False))
    print(json.dumps(dict(passed=True,checks=checks,wisdom_shrines=2,lotus_pools=2,watchers=10)))

if __name__=='__main__':main()
