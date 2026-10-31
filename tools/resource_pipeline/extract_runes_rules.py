"""Pin local Steam rune anchors, localized values and native timing/effects.

Reads Valve archives/server PE; never launches Dota or opens H1 USB. H1
interaction/random/storage limits remain separate from recovered source facts.
"""
from pathlib import Path
import hashlib,json,struct,sys,re
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'private/runes/source-rules'
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SERVER_SHA='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
    OUT.mkdir(parents=True,exist_ok=True);vpk=VpkArchive(STEAM/'pak01_dir.vpk');receipts=[];localized={}
    for entry in ('resource/localization/abilities_english.txt','resource/localization/dota_english.txt'):
        data=vpk.read(entry);(OUT/Path(entry).name).write_bytes(data);receipts.append(vpk.identity(entry))
        text=data.decode('utf-16') if data[:2]==b'\xff\xfe' else data.decode('utf-8-sig')
        for key,value in re.findall(r'^\s*"([^"]+)"\s*"([^\n]*)"',text,re.M):
            if key.startswith(('DOTA_Tooltip_rune_','DOTA_Tooltip_modifier_rune_','DOTA_Glossary_Map_Runes')):localized[key]=value
    for key,expected in {'DOTA_Tooltip_rune_water_description':'Instantly restores 40 Health and 80 Mana.','DOTA_Tooltip_rune_doubledamage_description':'Increases base damage by 80% and provides 15% Spell Amplification.','DOTA_Tooltip_rune_arcane_description':'Reduces cooldowns by 25% and mana costs by 30%.','DOTA_Tooltip_rune_illusion_description':'Conjures 2 illusions of your hero which deal 35% damage.  Melee illusions take 200% damage.  Ranged illusions take 300% damage.  Illusions last 75 seconds.','DOTA_Tooltip_rune_shield_description':'Grants an All Damage Barrier for 75s equal to 50% of your max HP.'}.items():assert localized[key]==expected,(key,localized.get(key))
    assert 'At minute zero' in localized['DOTA_Glossary_Map_Runes_Bounty_Desc']
    assert 'Jungle and River' in localized['DOTA_Glossary_Map_Runes_Bounty_Desc']
    server=STEAM/'bin/win64/server.dll';data=server.read_bytes();assert sha(server)==SERVER_SHA
    pe=pefile.PE(data=data,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase;md=Cs(CS_ARCH_X86,CS_MODE_64)
    def raw(va):return pe.get_offset_from_rva(va-base)
    def block(va,size):
        at=raw(va);b=data[at:at+size]
        return {'va':hex(va),'file_offset':hex(at),'bytes_hex':b.hex(),'asm':[f'{i.address:x} {i.mnemonic} {i.op_str}' for i in md.disasm(b,va)]}
    def rip_target(va):
        i=next(md.disasm(data[raw(va):raw(va)+15],va));assert 'rip' in i.op_str
        return i.address+i.size+struct.unpack_from('<i',i.bytes,len(i.bytes)-4)[0]
    def floating(va):return struct.unpack_from('<f',data,raw(rip_target(va)))[0]
    def string(va):
        at=raw(va);return data[at:data.index(0,at)].decode('ascii')
    convars=[]
    for name,start,load,lea,want in (
        ('dota_powerup_rune_spawn_time',0x180272850,0x18027285a,0x180272862,120),
        ('dota_powerup_rune_spawn_time_initial',0x1802728f0,0x1802728fa,0x180272902,360),
        ('dota_water_rune_spawn_time_initial',0x180274ea0,0x180274eaa,0x180274eb2,120),
        ('dota_bounty_rune_spawn_time',0x18026db10,0x18026db1a,0x18026db22,240)):
        assert string(rip_target(lea))==name and floating(load)==want
        convars.append({'name':name,'seconds':want,'default_va':hex(rip_target(load)),'constructor':block(start,145)})
    table=struct.unpack_from('<10I',data,raw(base+0x253510c));cases={}
    for kind,want,name in ((0,45,'modifier_rune_doubledamage'),(1,22,'modifier_rune_haste'),(6,50,'modifier_rune_arcane'),(9,75,'modifier_rune_shield')):
        va=base+table[kind];assert floating(va)==want and string(rip_target(va+8))==name
        cases[str(kind)]={'seconds':want,'modifier':name,'activation':block(va,115 if kind!=9 else 161)}
    assert base+table[3]==0x1825337f1 and floating(0x182533808)==45 and floating(0x182533821)==2
    assert string(rip_target(0x1825337f4))=='modifier_rune_invis'
    assert floating(0x18253362b)==30 and string(rip_target(0x182533958))=='modifier_rune_regen'
    assert data[raw(0x182533962):raw(0x182533969)]==bytes.fromhex('f3440f11542420')
    assert floating(0x182533a32)==80 and floating(0x182533a75)==40
    assert struct.unpack_from('<I',data,raw(0x1816736e5)+4)[0]==0x44098000 #550.0
    assert data[raw(0x1816739e3):raw(0x1816739ea)]==bytes.fromhex('c743040000803f') #1%
    assert data[raw(0x1816739f5):raw(0x1816739fc)]==bytes.fromhex('c743040000c040') #6%
    assert floating(0x1816739d6)==3
    assert floating(0x182533c55)==60 and floating(0x182533c74)==1.5
    assert data[raw(0x182533c64):raw(0x182533c6d)]==bytes.fromhex('9983e20303c283e0fc')
    assert data[raw(0x182533c8d):raw(0x182533c90)]==bytes.fromhex('83c028') #+40gold
    assert struct.unpack_from('<Q',data,0x3bbe870+295*8)[0]==0x1816c9e00
    assert rip_target(0x1816c9e10)==rip_target(0x180272968) #same power-initial convar
    assert data[raw(0x181852634):raw(0x181852644)]==bytes.fromhex('440f2fc0c7442440ffffffff0f974567')
    entities_path=ROOT/'private/source2/reports/entity-default-parsed.json';entities=json.loads(entities_path.read_bytes())
    points=[]
    for classname in ('dota_item_rune_spawner_powerup','dota_item_rune_spawner_bounty'):
        selected=[e for e in entities if e.get('classname')==classname];assert len(selected)==2
        for e in selected:
            x,y,z=e['origin'];points.append({'classname':classname,'hammer_id':e['hammerUniqueId'],'source_xyz':e['origin'],'world_xyz':[int(2048+x/4+.5),int(2048-y/4+.5),int(z/4+.5)]})
    assert [p['world_xyz'] for p in points]==[[1638,1770,10],[2343,2352,14],[1799,940,42],[2197,3213,42]]
    native={'server_path':str(server),'server_sha256':SERVER_SHA,'image_base':hex(base),'read_only':True,'convars':convars,'activation_switch_table_va':hex(base+0x253510c),'activation_switch_targets':[hex(base+x) for x in table],'simple_duration_cases':cases,'invisibility_fade':block(0x1825337f1,193),'regeneration_duration_setup':block(0x18253361f,30),'regeneration_activation':block(0x182533953,33),'regeneration_percentage_getter':block(0x181673820,487),'regeneration_after_damage_seconds':3,'regeneration_percent':6,'regeneration_during_damage_percent':1,'haste_speed_getter':block(0x1816736d5,61),'haste_source_speed':550,'water_activation':block(0x182533a32,115),'bounty_growth':block(0x182533c55,64),'bounty_formula':'40 +6*floor(spawn_game_seconds/240), then current Turbo ModifyGold filter','turbo_water_callback_slot':295,'turbo_water_callback':block(0x1816c9e00,32),'water_gate':block(0x1818525fc,72),'type_bag_selection':block(0x181852ce5,436)}
    save(OUT/'native-rules.json',native)
    header=ROOT/'private/generated/dota_runes_data.h';header_text=header.read_text(encoding='utf8')
    for macro,value in (('DOTA_RUNE_POWER_INTERVAL_SECONDS',120),('DOTA_RUNE_POWER_FIRST_SECONDS',360),('DOTA_RUNE_WATER_FIRST_SECONDS',120),('DOTA_RUNE_BOUNTY_INTERVAL_SECONDS',240)):
        assert re.search(r'^#define '+macro+r' '+str(value)+r'$',header_text,re.M)
    assert '{45,22,0,45,30,0,50,0,0,75}' in header_text
    entity_receipt=VpkArchive(STEAM/'maps/dota.vpk').identity('maps/dota/entities/default_ents.vents_c')
    save(OUT/'source-identities.json',{'definition_sources':receipts,'map_entity_source':entity_receipt,'parsed_entities_file':'private/source2/reports/entity-default-parsed.json','parsed_entities_sha256':sha(entities_path),'generated_header_sha256':sha(header),'native_proof_sha256':sha(OUT/'native-rules.json'),'extractor_sha256':sha(__file__)})
    save(OUT/'manifest.json',{'complete':True,'read_only_source_extraction':True,'source_version':'installed server.dll pinned SHA; no wiki-derived numeric rules','source_archives':receipts,'server_sha256':SERVER_SHA,'native_proof_sha256':sha(OUT/'native-rules.json'),'native_proof':'private/runes/source-rules/native-rules.json','entities':points,'selected_localization':localized,'spawn_policy':{'initial':'four Bounty: two river plus two jungle, minute zero','water_seconds':[120,240],'power_first_seconds':360,'power_interval_seconds':120,'jungle_bounty_interval_seconds':240,'late_dual_spawns':'No local proof adopted; no claim of full late-game rune engine parity.'},'effects':{'amplify':{'duration_seconds':45,'base_damage_percent':80,'spell_amplification_percent':15},'haste':{'duration_seconds':22,'speed_source':550},'illusion':{'units':2,'duration_seconds':75,'outgoing_percent':35,'incoming_melee_percent':200,'incoming_ranged_percent':300},'invisibility':{'duration_seconds':45,'fade_seconds':2,'breaks':'attack, ability or item activation; source IGNORE_INVIS exceptions retained'},'regeneration':{'duration_seconds':30,'max_hp_mp_percent_per_second':6,'enemy_hero_damage_percent_per_second':1,'reduced_rate_seconds':3,'ends_when':'both current HP and MP reach maximum'},'arcane':{'duration_seconds':50,'cooldown_multiplier_percent':75,'mana_cost_multiplier_percent':70},'shield':{'duration_seconds':75,'barrier_percent_of_pickup_max_hp':50},'water':{'health':40,'mana':80},'bounty':{'base_gold':40,'gold_per_240_seconds':6,'clock':'original rune birth time; team reward; current Turbo gold filter'}},'h1_limits':{'pickup':'Explicit hero order,100 Source contact radius(25 world),real source navigation; no wide automatic pickup or NPC pickup. Original engine item capsule/pickup distance is not claimed.','random':'Existing bounded H1 LCG/high-bit location draw; active seven-type bag,not exact Valve RNG seed/distribution.','world_slots':'One visible record per original spot; unclaimed records are replaced at the next corresponding scheduled spawn. Full physical bounty piles are not reconstructed.','bottle':'Direct activation on pickup; full Bottle rune-storage/automatic90s activation is not implemented in this bounded module.','late_game':'No unsupported40-minute dual-spawn or super-rune rule is invented.','turbo':'Original native120/360/240 defaults and inherited water gate are used; no unsupported Turbo rune-clock multiplier is added.'},'extractor_sha256':sha(__file__)})
    print(json.dumps({'passed':True,'manifest':'private/runes/source-rules/manifest.json','points':4,'source_server_sha256':SERVER_SHA}))
if __name__=='__main__':main()
