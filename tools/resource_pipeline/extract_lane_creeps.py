"""CRC/SHA-bound installed lane creep data and native normal/Turbo upgrades.

Read-only Steam inspection; writes only a generated table and local receipts.
The interval is450 seconds in both modes. Turbo multiplies each health/damage
upgrade by3 at the actual mode callback, rather than changing it to225 seconds.
"""
from pathlib import Path
from decimal import Decimal
import hashlib,importlib.util,json,struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SERVER_SHA='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 spec=importlib.util.spec_from_file_location('lane_original_kv',ROOT/'private/passives/extract_sources.py');kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
 vpk=VpkArchive(STEAM/'pak01_dir.vpk');receipts=[]
 def read(name):
  raw=vpk.read(name);receipts.append(vpk.identity(name));return kv.parse(raw.decode('utf-8-sig'))
 units=read('scripts/npc/npc_units.txt')['DOTAUnits'];abilities=read('scripts/npc/npc_abilities.txt')['DOTAAbilities'];rows=[];npc=[]
 fields=[('StatusHealth',1),('StatusHealthRegen',100),('AttackDamageMin',1),('AttackDamageMax',1),('ArmorPhysical',100),('MagicalResistance',1),('AttackRate',20),('BaseAttackSpeed',100),('AttackAnimationPoint',1000),('ProjectileSpeed',1),('AttackRange',1),('MovementSpeed',1),('Level',1),('StatusMana',1),('StatusManaRegen',100)]
 for tier,kind in enumerate(('melee','ranged')):
  original=units['npc_dota_creep_goodguys_'+kind];other=units['npc_dota_creep_badguys_'+kind];effective={**units['npc_dota_units_base'],**original}
  for key,_ in fields:assert original.get(key)==other.get(key),(kind,key)
  def number(key,scale):return int(Decimal(effective.get(key,'0') or '0')*scale)
  row=[number(key,scale) for key,scale in fields]
  if not tier:row[9]=0
  row+=[12,1+tier,36,3*(1+tier),30]
  rows.append(row);npc.append(dict(tier=tier,name=kind,raw_radiant=original,raw_dire=other,effective_base=effective,values=row))
 assert abilities['creep_irresolute']['AbilityValues']['hero_damage_penalty']=='-25'
 assert abilities['creep_piercing']['AbilityValues']=={'creep_damage_bonus':'50','hero_damage_penalty':'-50','heavy_damage_penalty':'-50'}
 import pefile
 from capstone import Cs,CS_ARCH_X86,CS_MODE_64
 server=STEAM/'bin/win64/server.dll';assert sha(server)==SERVER_SHA;binary=server.read_bytes();pe=pefile.PE(data=binary,fast_load=True);image=pe.OPTIONAL_HEADER.ImageBase;md=Cs(CS_ARCH_X86,CS_MODE_64)
 def at(va):return pe.get_offset_from_rva(va-image)
 def check(va,hexbytes):assert binary[at(va):at(va)+len(bytes.fromhex(hexbytes))]==bytes.fromhex(hexbytes),hex(va)
 def capture(va,size):
  pos=at(va);raw=binary[pos:pos+size];return dict(va=hex(va),raw=hex(pos),bytes=raw.hex(),asm=[f'{i.address:x} {i.mnemonic} {i.op_str}' for i in md.disasm(raw,va)])
 check(0x180ff1bc9,'f30f5e052fb82902');assert struct.unpack_from('<f',binary,0x328ba00)[0]==450.0
 check(0x180ff1bdd,'41be1e000000');check(0x180ff1c20,'413bde7c03');check(0x180ff1c25,'418bde')
 check(0x1817c9e43,'c745000c000000');check(0x1817c9e4a,'41c70702000000');check(0x1817c9e7e,'c745000c000000');check(0x1817c9e85,'41c70701000000')
 check(0x1817c9f65,'ff90a0070000');check(0x1816e8150,'c20000');check(0x1818e75c0,'418b008d0c40418908418b018d0c40418909c3')
 check(0x1816b5b4b,'c7865406000032000000');check(0x1816b5b75,'c7865806000010000000');check(0x1816b509f,'33ff');check(0x1816b50a9,'33ff');check(0x1816b5b9f,'89be5c060000')
 # Bound the schema names/offsets as well as the actual native branch.
 for pos,name,offset in [(0x4a1d460,'m_nInnateMeleeDamageBlockPct',0x654),(0x4a1d480,'m_nInnateMeleeDamageBlockAmount',0x658),(0x4a1d4a0,'m_nInnateMeleeDamageBlockPerLevelAmount',0x65c),(0x4b0c7b0,'m_iUnitType',0x9fc),(0x4b0d310,'m_iAttackCapabilities',0xd18),(0x4b0d650,'m_nUnitState64',0xf00)]:
  ptr,_,member_offset=struct.unpack_from('<3Q',binary,pos);assert member_offset==offset;name_raw=pe.get_offset_from_rva(ptr-image);assert binary[name_raw:name_raw+len(name)+1]==name.encode()+b'\0'
 assert struct.unpack_from('<Q',binary,0x3b986e8)[0]==12 # MODIFIER_STATE_BLOCK_DISABLED
 assert struct.unpack_from('<Q',binary,0x3b98928)[0]==30 # MODIFIER_STATE_PASSIVES_DISABLED is distinct.
 assert struct.unpack_from('<f',binary,0x2ef34fc)[0]==30.0
 check(0x1826060c9,'488bcfe82fb4ffff');check(0x1826060d1,'f30f100d23ee8e00');check(0x1826060dc,'e8afa6ffff')
 native=dict(read_only=True,server_path=str(server),server_sha256=SERVER_SHA,upgrade_period_seconds=dict(normal=450,turbo=450),max_health_damage_upgrades=30,normal_upgrade_health=12,normal_upgrade_damage=[1,2],turbo_upgrade_multiplier=3,turbo_upgrade_health=36,turbo_upgrade_damage=[3,6],period_float_raw='0x328ba00',mode_slot=244,
  clock_and_cap=capture(0x180ff1b90,668),baseline_upgrade_outputs=capture(0x1817c9db0,476),base_mode_callback=capture(0x1816e8150,3),turbo_mode_callback=capture(0x1818e75c0,19),
  innate_melee_block=dict(chance=50,amount=16,per_level=0,constructor=capture(0x1816b4b00,0x10dd),damage_branch=capture(0x18253fb4e,0x247),
   outer_gate='nonnull attacker; attacker m_iUnitType&0x193; victim BLOCK_DISABLED bit12 clear; positive physical damage bucket; no damage_flags0x8 or bit19',
   innate_gate='victim m_iUnitType&1 (hero), victim attack-capability bit1 clear (not ranged), nonnull attacker',
   source_filter='The native innate branch does not exclude hero attackers or player-controlled attackers; no HeroControlled/IsHero attacker call occurs between gate and roll.',
   break_behavior='BLOCK_DISABLED=12 differs from PASSIVES_DISABLED=30. This branch does not test Break.',
   stacking='native takes maximum of inherent roll and ordinary physical-constant-block value, then adds separate stacking block; do not sum standard Vanguard/inherent blocks'),
  default_wave_seconds=30,wave_time_float_raw='0x2ef34fc',lane_spawner=capture(0x18260608d,100),wave_timer_helper=capture(0x182600790,0x313),
  scope='Non-upgraded ordinary melee/ranged lane units. Health and min/max attack upgrades cap30. The separate gold-growth calculation and super/mega units are not implemented by this table. Native lane spawner supplies30 seconds to scheduling; H1 bounded3-unit composition remains a separate adaptation. No225-second assumption.')
 out=ROOT/'private/lane-creeps/source-rules';out.mkdir(parents=True,exist_ok=True);(out/'native-rules.json').write_text(json.dumps(native,indent=2)+'\n',encoding='utf8')
 lines=['/* Generated by tools/extract_lane_creeps.py from installed Valve data. */','#ifndef DOTA_LANE_CREEP_DATA_H','#define DOTA_LANE_CREEP_DATA_H',
 '/* Gameplay Source units; regen/armor/attack_speed100 use x100, BAT uses20Hz ticks, point_ms uses milliseconds. */',
 'typedef struct {short health,regen100,damage_min,damage_max,armor100,magic_resist,bat_ticks,attack_speed100,point_ms,projectile_speed,range,speed,level,mana,mana_regen100,health_upgrade,damage_upgrade,turbo_health_upgrade,turbo_damage_upgrade,max_upgrades;} DotaLaneCreepSource;',
 '#define DOTA_LANE_UPGRADE_NORMAL_SECONDS 450',
 '#define DOTA_LANE_UPGRADE_TURBO_SECONDS 450',
 '#define DOTA_LANE_MAX_UPGRADES 30',
 '#define DOTA_LANE_WAVE_SECONDS 30',
 '#define DOTA_LANE_TURBO_UPGRADE_MULTIPLIER 3',
 '#define DOTA_LANE_MELEE_HERO_DAMAGE_PERCENT 75',
 '#define DOTA_LANE_RANGED_HERO_DAMAGE_PERCENT 50',
 '#define DOTA_LANE_RANGED_BASIC_DAMAGE_PERCENT 150',
 '#define DOTA_LANE_RANGED_BUILDING_DAMAGE_PERCENT 50',
 '#define DOTA_MELEE_BLOCK_CHANCE 50',
 '#define DOTA_MELEE_BLOCK_AMOUNT 16',
 '#define DOTA_MELEE_BLOCK_PER_LEVEL 0',
 '/*[0]melee,[1]ranged. First wave has zero upgrades. */','static const DotaLaneCreepSource dota_lane_creep_source[2]={']
 lines += [' {'+','.join(map(str,row))+'},' for row in rows];lines += ['};','#endif']
 header=ROOT/'game/lane_creep_data.h';header.write_text('\n'.join(lines)+'\n',encoding='utf8')
 result=dict(complete=True,archives=receipts,units=npc,passives={name:abilities[name] for name in ['creep_irresolute','creep_piercing','creep_siege']},native_rules_sha256=sha(out/'native-rules.json'),header_sha256=sha(header),extractor_sha256=sha(__file__),wave_cadence_status='native lane spawner passes30 seconds to its scheduling helper; H1 bounded3-unit wave composition remains a separate adaptation')
 (out/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,units=2,upgrade_seconds=450,turbo_growth_multiplier=3,max_upgrades=30)))
if __name__=='__main__':main()
