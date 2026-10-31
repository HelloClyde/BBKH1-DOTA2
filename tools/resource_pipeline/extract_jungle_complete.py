"""Read all four original native neutral pools and28 original map spawners.

Only research/output files are written; Steam data and gameplay are read-only.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,re,struct,sys
from source2_vpk import VpkArchive
from extract_roshan_rules import native_spawn_table,kv
ROOT=Path(__file__).resolve().parents[1]
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,default=ROOT/'build/audit-v022/complete-jungle');a=ap.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    original=out/'original';original.mkdir(exist_ok=True)
    proof=native_spawn_table() # Preserves all existing exact getter/dispatch/reflection assertions.
    import pefile
    server=STEAM/'bin/win64/server.dll';binary=server.read_bytes();image=pefile.PE(data=binary,fast_load=True);base=image.OPTIONAL_HEADER.ImageBase
    assert sha(server)==proof['server_sha256']
    start=0x4a84180;stride=proof['category_stride_bytes'];categories=[];unit_names=set();checks=proof['checks']
    for category in range(proof['category_count_getter']['value']):
        offset=start+category*stride;count=struct.unpack_from('<I',binary,offset)[0]
        assert 0<count<=8 and 8+count*40<=stride
        rows=[]
        for group in range(count):
            names=[];pointers=[]
            for slot in range(5):
                field=offset+8+group*40+slot*8;address=struct.unpack_from('<Q',binary,field)[0]
                target=image.get_offset_from_rva(address-base);end=binary.index(0,target);name=binary[target:end].decode('ascii')
                assert not name or name.startswith('npc_dota_neutral_')
                names.append(name);pointers.append(dict(field_file_offset=field,pointer_va=hex(address),string_file_offset=target,string_bytes_hex=binary[target:end+1].hex()))
                if name:unit_names.add(name)
                checks+=1
            rows.append(dict(subtype_index=group,source_spawn_type=group+1,entries=names,unit_counts=dict(__import__('collections').Counter(n for n in names if n)),pointers=pointers))
        categories.append(dict(neutral_type=category,category_file_offset=offset,declared_subtype_count=count,data_bytes_hex=binary[offset:offset+stride].hex(),subtypes=rows))
    assert categories[3]['subtypes']==[{**r,'source_spawn_type':r['subtype_index']+1,'unit_counts':dict(__import__('collections').Counter(n for n in r['entries'] if n)),
        'pointers':[dict(field_file_offset=p['file_offset'],pointer_va=p['pointer_va'],string_file_offset=p['string_file_offset'],string_bytes_hex=binary[p['string_file_offset']:binary.index(0,p['string_file_offset'])+1].hex()) for p in r['pointers']]} for r in proof['subtypes']]
    pak=VpkArchive(STEAM/'pak01_dir.vpk');mp=VpkArchive(STEAM/'maps/dota.vpk');entry='scripts/npc/npc_units.txt';raw=pak.read(entry);(original/'npc_units.txt').write_bytes(raw)
    definitions=kv.parse(raw.decode('utf-8-sig'))['DOTAUnits'];defaults=definitions['npc_dota_units_base'];units={}
    for name in sorted(unit_names):
        d=definitions[name];effective={**defaults,**d};model=effective['Model'];compiled=model+'_c'
        assert compiled in pak.entries,(name,compiled)
        stats={k:v for k,v in effective.items() if re.match(r'^(Attack|BaseAttack|Bounds|Status|Movement|Armor|Magical|Bounty|Vision|Projectile|Model|Level|IsAncient|Ability|HasInventory)',k)}
        units[name]=dict(source_key='DOTAUnits/'+name,definition=d,effective_stats_with_script_defaults=stats,model_resource=compiled,model_source=pak.identity(compiled));checks+=1
    entities_path=ROOT/'private/source2/reports/entity-default-parsed.json';entities=json.loads(entities_path.read_text(encoding='utf8'));camps=[]
    for e in entities:
        if e.get('classname')!='npc_dota_neutral_spawner':continue
        row=dict(e);category=int(e['NeutralType']);forced=int(e['ForcedSubType']);lo=int(e['MinSpawnType'])-1;hi=int(e['MaxSpawnType'])-1
        indices=[forced-1] if forced>0 else list(range(lo,hi+1));rows=categories[category]['subtypes'];assert all(0<=i<len(rows) for i in indices)
        row['world_xyz']=[2048+e['origin'][0]/4,2048-e['origin'][1]/4,e['origin'][2]/4]
        row['possible_subtype_indices']=indices;row['possible_combinations']=[dict(subtype_index=i,source_spawn_type=i+1,entries=rows[i]['entries'],unit_counts=rows[i]['unit_counts']) for i in indices]
        row['native_reject_immediate_repeat']=forced<=0 and lo!=hi
        row['volume_entities']=[v for v in entities if v.get('targetname')==e.get('VolumeName')]
        camps.append(row);checks+=1
    assert len(camps)==28
    identities={'npc_units':pak.identity(entry),'map_entities':mp.identity('maps/dota/entities/default_ents.vents_c')}
    report=dict(complete=True,observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),checks=checks,native_proof=proof,categories=categories,units=units,camps=camps,source=identities,
        source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),ROOT/'tools/extract_roshan_rules.py',entities_path]},
        scope='All original native4category data,5pointer rows, original source NPC models/effective stats, all28 map Min/Max/Forced spawners and per-camp allowed compositions; no gameplay or source assets changed.',
        not_proven='Imported RandomInt distribution beyond inclusive range API; exact native camp upgrade timing and world trigger collision volume geometry are not reconstructed here.')
    (out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(complete=True,camps=len(camps),categories=[dict(type=c['neutral_type'],rows=c['declared_subtype_count']) for c in categories],units=len(units),checks=checks,manifest=str(out/'manifest.json'))))
if __name__=='__main__':main()
