"""Read-only installed courier scripts and exact native Turbo callback proof."""
from pathlib import Path
import datetime,hashlib,importlib.util,json,struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gltf_assets import np  # Also locates the bundled PE inspection dependency.
from source2_vpk import VpkArchive
import pefile
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
OUT=ROOT/'private/courier/source-rules'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
    OUT.mkdir(parents=True,exist_ok=True);checks=0
    path=STEAM/'bin/win64/server.dll';binary=path.read_bytes();digest=sha(path)
    assert digest=='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834';checks+=1
    pe=pefile.PE(data=binary,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase
    def offset(va):return pe.get_offset_from_rva(va-base)
    def primary(name):
        nonlocal checks
        name_raw=binary.index(('.?AV'+name+'@@').encode());td=pe.get_rva_from_offset(name_raw-16);cursor=0;matches=[]
        while (cursor:=binary.find(struct.pack('<I',td),cursor))>=0:
            if cursor>=12:
                values=struct.unpack_from('<6I',binary,cursor-12)
                if values[0:3]==(1,0,0) and values[3]==td and values[5]==pe.get_rva_from_offset(cursor-12):
                    ptr=binary.find(struct.pack('<Q',base+values[5]));assert ptr>=0
                    matches.append(dict(class_name=name,type_descriptor_raw=name_raw-16,type_descriptor_rva=td,
                        complete_object_locator_raw=cursor-12,locator=list(values),vtable_raw=ptr+8))
            cursor+=1
        assert len(matches)==1;checks+=1;return matches[0]
    def function(table,slot):
        va=struct.unpack_from('<Q',binary,table['vtable_raw']+slot*8)[0];return va,offset(va)
    def modifier_name(table,expected):
        nonlocal checks
        va,raw=function(table,9);assert binary[raw:raw+3]==bytes.fromhex('488d05')
        target=va+7+struct.unpack_from('<i',binary,raw+3)[0];at=offset(target)
        assert binary[at:binary.index(0,at)].decode()==expected;checks+=1
        return dict(slot=9,function_va=hex(va),function_raw=raw,name_raw=at,bytes=binary[raw:raw+8].hex())
    haste=primary('CDOTA_Modifier_Turbo_Courier_Haste');haste['name_getter']=modifier_name(haste,'modifier_turbo_courier_haste')
    va,raw=function(haste,94);assert va==0x180de0c70 and binary[raw:raw+7]==bytes.fromhex('4883ec18488d05');checks+=1
    getter_va=va+4+7+struct.unpack_from('<i',binary,raw+4+3)[0];getter_raw=offset(getter_va)
    assert getter_va==0x180e3df20 and binary[getter_raw+19:getter_raw+22]==bytes.fromhex('c74204');checks+=1
    speed=struct.unpack_from('<f',binary,getter_raw+22)[0];assert speed==1100;checks+=1
    haste['property_registration']=dict(slot=94,function_va=hex(va),function_raw=raw,bytes=binary[raw:raw+63].hex(),
        property_callback_offset=0x280,getter_va=hex(getter_va),getter_raw=getter_raw,getter_bytes=binary[getter_raw:getter_raw+27].hex(),movement_speed_source=speed)
    invulnerable=primary('CDOTA_Modifier_Turbo_Courier_Invulnerable');invulnerable['name_getter']=modifier_name(invulnerable,'modifier_turbo_courier_invulnerable')
    transfer=primary('CDOTA_Modifier_Courier_TransferItems');va,raw=function(transfer,84)
    assert va==0x180e13e00 and raw==0xe13200;checks+=1
    compare=0xe13559;compare_va=base+pe.get_rva_from_offset(compare)
    assert binary[compare:compare+4]==bytes.fromhex('f30f1005');checks+=1
    constant_va=compare_va+8+struct.unpack_from('<i',binary,compare+4)[0];constant_raw=offset(constant_va)
    handoff=struct.unpack_from('<f',binary,constant_raw)[0];assert handoff==400;checks+=1
    # sqrtss xmm1,xmm2; comiss xmm0,xmm1; jbe(return):400<=distance rejects.
    assert binary[compare+8:compare+16]==bytes.fromhex('f30f51ca0f2fc10f');checks+=1
    assert binary[compare+16]==0x86;checks+=1
    transfer['distance_test']=dict(interval_slot=84,function_va=hex(va),function_raw=raw,branch_raw=0xe13520,
        branch_bytes=binary[0xe13520:0xe135a1].hex(),constant_raw=constant_raw,constant_va=hex(constant_va),handoff_source=handoff,
        comparison='2D origin Euclidean distance strictly less than400 Source units; sqrtss/comiss/jbe return branch.')
    native=dict(server_path=str(path),server_sha256=digest,image_base=hex(base),read_only=True,
        haste=haste,invulnerable=invulnerable,transfer=transfer)
    save(OUT/'native-callbacks.json',native)
    spec=importlib.util.spec_from_file_location('original_kv',ROOT/'private/passives/extract_sources.py');kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
    archive=VpkArchive(STEAM/'pak01_dir.vpk');documents={};receipts=[]
    for name in ('npc_units','npc_abilities'):
        entry='scripts/npc/'+name+'.txt';data=archive.read(entry);(OUT/(name+'.txt')).write_bytes(data)
        documents[name]=next(iter(kv.parse(data.decode('utf-8-sig')).values()));receipts.append(archive.identity(entry));checks+=1
    courier=documents['npc_units']['npc_dota_courier'];auto=documents['npc_abilities']['courier_autodeliver']
    assert courier['Model']=='models/props_gameplay/donkey.vmdl' and courier['HasInventory']=='1';checks+=1
    flying=documents['npc_units']['npc_dota_flying_courier']
    assert flying['Model']=='models/props_gameplay/donkey_wings.vmdl';checks+=1
    assert 'AUTOCAST' in auto['AbilityBehavior'] and 'TOGGLE' in auto['AbilityBehavior'];checks+=1
    manifest=dict(complete=True,observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        original_sources=receipts,source_unit=courier,source_autodeliver=auto,native_proof='private/courier/source-rules/native-callbacks.json',native_proof_sha256=sha(OUT/'native-callbacks.json'),
        confirmed=dict(speed_source=1100,handoff_source=400,handoff_comparison='strictly less',turbo_invulnerable_modifier=True,
            source_flying_model=flying['Model'],source_flying_unit_deprecated=True,buy_anywhere=True),
        official_mode_description=dict(url='https://www.dota2.com/duelingfates',section='Turbo Mode',
            supports='Official buy-anywhere shop system; accelerated mode. Precise courier constants come from the installed native callbacks, not this historical summary.'),
        h1_adaptations=dict(dispatch='Global5second fountain-stash poll; native precise scheduling has not been recovered.',
            home='Original hero start anchors, one per player, separate visual berths; fountain shop proximity retains existing300-world H1 radius.',
            carry='One6-slot courier and one6-slot fountain stash per hero; no item sharing, ground retrieval, sell orders or manual courier control.',
            full_inventory='Undeliverable cargo remains safely on courier, which follows and retries when inventory space opens.',
            death='Return cargo to own berth, retain safely while dead; resume after owner respawns. No death/upgrade effects are triggered.',
            unified_shop='All six supported items, including previous secret-shop items, are ordered at own fountain; no secret-shop detour.'),
        unknown=['Exact native autodelivery polling/scheduling logic','Complete native inventory locks/backpack/TP/neutral slots and item sharing'],
        source_sha256={'tools/extract_courier_rules.py':sha(__file__)})
    save(OUT/'manifest.json',manifest)
    save(OUT/'verification.json',dict(passed=True,checks=checks,manifest_sha256=sha(OUT/'manifest.json'),
        native_sha256=sha(OUT/'native-callbacks.json'),source_sha256=manifest['source_sha256'],physical_h1_tested=False))
    print(json.dumps(dict(passed=True,checks=checks,speed=speed,handoff=handoff)))
if __name__=='__main__':main()
