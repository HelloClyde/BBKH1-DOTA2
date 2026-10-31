"""Read-only current Steam Turbo rules: native RTTI/callbacks and raw Valve KV.

No DLL is executed. Fixed offsets are accepted only for the bound SHA256, with
primary RTTI, opcode, table and script assertions; another build must be audited.
"""
from pathlib import Path
import datetime,hashlib,importlib.util,json,re,runpy,struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gltf_assets import np
from source2_vpk import VpkArchive
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
OUT=ROOT/'private/turbo/source-rules'
DIGEST='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
XP=[0,240,400,520,600,680,760,800,900,1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000,2200,2400,2600,2800,3000,4000,5000,6000,7000,8000]
RESPAWN=[12,15,18,21,24,26,28,30,32,34,36,44,46,48,50,52,54,65,70,75,80,85,90,95,100,100,100,100,100,100]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
    OUT.mkdir(parents=True,exist_ok=True);path=STEAM/'bin/win64/server.dll';b=path.read_bytes();assert sha(path)==DIGEST
    pe=pefile.PE(data=b,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase;md=Cs(CS_ARCH_X86,CS_MODE_64)
    def off(va):return pe.get_offset_from_rva(va-base)
    def primary(name):
        at=b.index(('.?AV'+name+'@@').encode());td=pe.get_rva_from_offset(at-16);cur=0;found=[]
        while (cur:=b.find(struct.pack('<I',td),cur))>=0:
            if cur>=12:
                v=struct.unpack_from('<6I',b,cur-12)
                if v[:4]==(1,0,0,td) and v[5]==pe.get_rva_from_offset(cur-12):
                    ptr=b.index(struct.pack('<Q',base+v[5]));found.append(dict(class_name=name,type_descriptor_raw=at-16,complete_object_locator_raw=cur-12,vtable_raw=ptr+8))
            cur+=1
        assert len(found)==1;return found[0]
    def code(va,size):
        raw=off(va);data=b[raw:raw+size]
        return dict(va=hex(va),raw=hex(raw),bytes=data.hex(),asm=[f'{i.address:x} {i.mnemonic} {i.op_str}' for i in md.disasm(data,va)])
    mode=primary('CDOTATurboGameMode');assert mode['vtable_raw']==0x3bbe870
    callbacks={}
    for slot,size in {243:8,245:8,253:8,254:16,255:16,256:8,257:16,311:220,313:48,315:110,317:48,320:48,321:48,324:8,325:8}.items():
        va=struct.unpack_from('<Q',b,mode['vtable_raw']+slot*8)[0];callbacks[str(slot)]=code(va,size)
    assert callbacks['320']['va']=='0x1818db900' and callbacks['321']['va']=='0x1818ddc70'
    assert b[0x18dad00:0x18dad07]==bytes.fromhex('80b98908000000')
    assert struct.unpack_from('<f',b,0x2eb6580)[0]==2
    assert struct.unpack_from('<30i',b,0x4a8a620)==tuple(XP)
    assert struct.unpack_from('<30i',b,0x4a8a6a0)==tuple(RESPAWN)
    # An exact table address loaded by the real default-level query and real
    # hero-respawn caller, rather than a coincidental integer sequence.
    assert b[0x198f559:0x198f55c]==bytes.fromhex('488d1d')
    assert b[0x1989eb3:0x1989eb6]==bytes.fromhex('488d0d')
    assert b[0x25796aa]==0xe8 and base+pe.get_rva_from_offset(0x25796aa)+5+struct.unpack_from('<i',b,0x25796ab)[0]==0x18198aab0
    assert b[0x30bbaa:0x30bbb2]==bytes.fromhex('c744243758020000') # dota_default_gold600
    assert b[0x1779de6:0x1779df1]==bytes.fromhex('41c7868803000001000000')
    assert b[0x1779df1:0x1779dfc]==bytes.fromhex('41c7868c0300000000403f')
    native=dict(server_path=str(path),server_sha256=DIGEST,image_base=hex(base),read_only=True,primary_rtti=mode,callbacks=callbacks,
        initialization=code(0x1818e9770,830),gold_filter=code(0x1818d8340,160),xp_rubberband=code(0x1818e2aa0,788),
        xp_query=code(0x1819900e0,180),xp_table_raw=hex(0x4a8a620),xp_required_non_cumulative=XP,
        respawn_table_raw=hex(0x4a8a6a0),respawn_seconds=RESPAWN,respawn_query=code(0x18198aab0,15),respawn_caller=code(0x18257a29e,36),
        starting_gold_constructor=code(0x18030c792,51),passive_normal_initialization=code(0x18177a9d5,39),
        lotus_interval_override=code(0x1816ffb05,760),item_mode_override=code(0x1818d8050,483))
    runpy.run_path(str(ROOT/'private/turbo/research/tower_flags_proof.py'))
    tower=json.loads((ROOT/'private/turbo/research/tower-flags-proof.json').read_text(encoding='utf8'));assert tower['passed'] and tower['server_sha256']==DIGEST
    save(OUT/'tower-flags-proof.json',tower)
    native['tower_flag_proof_sha256']=sha(OUT/'tower-flags-proof.json')
    save(OUT/'native-rules.json',native)
    spec=importlib.util.spec_from_file_location('original_kv',ROOT/'private/passives/extract_sources.py');kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
    archive=VpkArchive(STEAM/'pak01_dir.vpk');receipts=[];docs={}
    for name in ('items','npc_units'):
        entry='scripts/npc/'+name+'.txt';data=archive.read(entry);(OUT/(name+'.txt')).write_bytes(data)
        docs[name]=kv.parse(data.decode('utf8'));receipts.append(dict(entry=entry,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data)))
    items=docs['items']['DOTAAbilities'];names=['item_boots','item_blades_of_attack','item_chainmail','item_vitality_booster','item_energy_booster','item_flask'];prices=[500,450,500,1000,800,100]
    selected={name:items[name] for name in names};assert [int(selected[n]['ItemCost']) for n in names]==prices
    assert items['item_aegis']['AbilityValues']['disappear_time_turbo']=='240.0'
    save(OUT/'selected-items.json',selected)
    facts=dict(gold_positive_reward_multiplier=2,xp_multiplier=2,starting_gold=600,lose_gold_on_death=False,respawn_time_scale=[3,4],
        passive_base_gold=1,passive_base_seconds=[3,4],passive_turbo_time_scale=[3,4],passive_turbo_gold_per_minute=[640,3],
        xp_snap_min_level_gap=4,xp_snap_max_level_gap=6,xp_snap_cooldown_strict_seconds=5,lotus_spawn_seconds=90,aegis_lifetime_seconds=240,
        shop_item_costs=prices,item_blades_bonus_damage=9,hero_level_cap_h1=15,tower_damage_from_nonsummoned_creeps_multiplier=2)
    manifest=dict(observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),read_only=True,server_sha256=DIGEST,vpk_receipts=receipts,facts=facts,
        official_primary=[dict(url='https://www.dota2.com/duelingfates',scope='Original Turbo mode: higher gold/XP, shorter respawn, weaker towers and buy-anywhere; no precise current multipliers on this page.')],
        retained_h1_adaptations=['Hero base/growth stats and automatic skill-rank mapping1/5/9/13; level cap15.','Existing deterministic base kill bounties and XP sharing rounding; Turbo filter doubles actual positive awards.','Bounded lane unit capacity and simplified3-unit waves; existing lane and small-camp clocks pending complete native clock mapping.','Existing consumable healing mechanic; no unverified global Turbo healing multiplier.','H1 has building dependency protection but no active backdoor buff; targetable towers receive native2x nonsummoned-creep damage. No HP/armor reduction guessed.','Courier five-second polling, asset scale and delivery presentation documented separately.'],
        not_claimed=['No additional HP/armor reduction established for this current build; Roshan/Tormentor timing override semantics unresolved.','Every original item, hero, spell, talent, buyback, rune, draft or pregame phase.'],
        extractor_sha256=sha(__file__))
    save(OUT/'manifest.json',manifest);print('Turbo source assertions passed: RTTI, native tables/callbacks,600 gold, passive clock,6 unchanged prices,90s lotus and240s Aegis.')
if __name__=='__main__':main()
