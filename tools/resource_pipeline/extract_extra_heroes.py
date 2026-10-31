"""CRC-check installed Slark/Necrophos definitions and verify H1 constants.

Reads Steam archives without executing or modifying Steam. The game keeps its
existing rank/level cap; this receipt distinguishes those H1 choices from the
original hero stats, ability values, targeting flags and descriptions.
"""
from pathlib import Path
from decimal import Decimal
import datetime,hashlib,importlib.util,json,re,sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'private/extra-heroes'
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
spec=importlib.util.spec_from_file_location('source_kv',ROOT/'private/passives/extract_sources.py')
kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)

def numbers(value,scale=1):
    if isinstance(value,dict):value=value['value']
    return [int(Decimal(s)*scale) for s in value.split()]
def raw_values(ability,key):
    return ability.get('AbilityValues',{}).get(key,ability.get(key))
def values(ability,key,scale=1,count=None):
    result=numbers(raw_values(ability,key),scale)
    if count:result=(result+[result[-1]]*count)[:count]
    return result
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    (OUT/'source').mkdir(parents=True,exist_ok=True)
    vpk=VpkArchive(STEAM/'pak01_dir.vpk');receipts=[];heroes={};abilities={}
    for name in ('slark','necrolyte'):
        entry=f'scripts/npc/heroes/npc_dota_hero_{name}.txt';raw=vpk.read(entry)
        (OUT/'source'/f'{name}.txt').write_bytes(raw);receipts.append(vpk.identity(entry))
        hero=next(iter(next(iter(kv.parse(raw.decode('utf-8-sig')).values())).values()))
        heroes[name]={k:v for k,v in hero.items() if k!='AbilityDefinitions'}
        abilities.update(hero['AbilityDefinitions'])
    selected=[['slark_dark_pact','slark_pounce','slark_saltwater_shiv','slark_shadow_dance'],
              ['necrolyte_death_pulse','necrolyte_ghost_shroud','necrolyte_heartstopper_aura','necrolyte_reapers_scythe']]
    # Every gameplay table below is compared directly to the CRC-checked text.
    expected={
      'extra_mana':[values(abilities[n],'AbilityManaCost',count=4) if 'heartstopper' not in n else [0]*4 for row in selected for n in row],
      'extra_cd':[values(abilities[n],'AbilityCooldown',20,count=4) if 'heartstopper' not in n else [0]*4 for row in selected for n in row],
      'extra_ranges':[[0,0,*values(abilities['slark_saltwater_shiv'],'AbilityCastRange'),0],
                      [0,0,0,*values(abilities['necrolyte_reapers_scythe'],'AbilityCastRange')]],
      'extra_radii':[[*values(abilities['slark_dark_pact'],'radius'),*values(abilities['slark_pounce'],'pounce_radius'),0,0],
                     [*values(abilities['necrolyte_death_pulse'],'area_of_effect'),*values(abilities['necrolyte_ghost_shroud'],'slow_aoe'),*values(abilities['necrolyte_heartstopper_aura'],'aura_radius'),0]],
      'extra_pact_damage':values(abilities['slark_dark_pact'],'total_damage'),
      'extra_pounce_leash':values(abilities['slark_pounce'],'leash_duration',20),
      'extra_shadow_life':values(abilities['slark_shadow_dance'],'duration',20),
      'extra_pulse_damage':values(abilities['necrolyte_death_pulse'],'AbilityDamage'),
      'extra_pulse_heal':values(abilities['necrolyte_death_pulse'],'heal'),
      'extra_ghost_life':values(abilities['necrolyte_ghost_shroud'],'duration',20),
      'extra_ghost_heal':values(abilities['necrolyte_ghost_shroud'],'heal_bonus'),
      'extra_ghost_slow':values(abilities['necrolyte_ghost_shroud'],'movement_speed'),
      'extra_heartstopper_per_mille':values(abilities['necrolyte_heartstopper_aura'],'aura_damage',10),
      'extra_scythe_damage_pct':values(abilities['necrolyte_reapers_scythe'],'damage_per_health',100),
    }
    def flatten(a):return sum((flatten(x) if isinstance(x,list) else [x] for x in a),[])
    header=(ROOT/'game/extra_heroes_data.h').read_text(encoding='utf8');checks=0
    for name,row in expected.items():
        match=re.search(r'\b'+re.escape(name)+r'\[[^;=]+=(.*?);',header,re.S)
        assert match,name
        actual=list(map(int,re.findall(r'\d+',match[1])))
        assert actual==flatten(row),(name,actual,flatten(row));checks+=len(actual)
    numeric_checks=checks
    # The E behavior is deliberately taken from the installed current build.
    assert heroes['slark']['Ability3']=='slark_saltwater_shiv'
    assert heroes['slark']['Ability5']=='slark_essence_shift'
    assert 'PASSIVE' not in abilities['slark_saltwater_shiv']['AbilityBehavior']
    assert abilities['slark_essence_shift']['Innate']=='1'
    assert abilities['necrolyte_sadist']['Innate']=='1';checks+=5
    for name,key in [('slark_dark_pact','radius'),('slark_essence_shift','steal_radius'),
      ('slark_depth_shroud','radius'),('necrolyte_death_pulse','area_of_effect'),
      ('necrolyte_ghost_shroud','slow_aoe'),('necrolyte_heartstopper_aura','aura_radius')]:
        assert abilities[name]['AbilityValues'][key]['affected_by_aoe_increase']=='1';checks+=1
    assert abilities['necrolyte_heartstopper_aura']['SpellImmunityType']=='SPELL_IMMUNITY_ENEMIES_YES'
    assert abilities['slark_essence_shift']['SpellImmunityType']=='SPELL_IMMUNITY_ENEMIES_YES';checks+=2
    for name in ('slark_dark_pact','slark_pounce','slark_saltwater_shiv','necrolyte_ghost_shroud','necrolyte_reapers_scythe'):
        assert abilities[name]['SpellImmunityType']=='SPELL_IMMUNITY_ENEMIES_NO';checks+=1
    combat_heroes={n:{k:heroes[n].get(k) for k in ('AttackDamageMin','AttackDamageMax','ArmorPhysical','BaseAttackSpeed','AttackRate','AttackRange','AttackAnimationPoint','ProjectileSpeed','AttributePrimary','AttributeBaseStrength','AttributeStrengthGain','AttributeBaseAgility','AttributeAgilityGain','AttributeBaseIntelligence','AttributeIntelligenceGain','MovementSpeed')} for n in heroes}
    report=dict(passed=True,observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        verified_constants=checks,verified_numeric_constants=numeric_checks,
        verified_flags_and_ability_slots=checks-numeric_checks,archives=receipts,hero_stats=combat_heroes,
        ability_definitions={k:abilities[k] for row in selected for k in row}|
          {k:abilities[k] for k in ('slark_essence_shift','necrolyte_sadist','slark_depth_shroud','necrolyte_death_seeker')},
        h1_adaptations=['20Hz integer combat and Source distances divided by four.',
          'Existing 15-level game cap and skill ranks at1/5/9/13; ultimates3/6/9.',
          'Ten hero slots;768 independent temporary-stat expiry records;32 independent Shiv debuffs;159 generation-guarded unit slots.',
          'Base four abilities plus Essence Shift/Sadist innate, Slark/Necrophos Scepter and extra Shard abilities. Talents are not selected.',
          'Extra Shard abilities use T/the HUD button; their CAST stream reuses skill3/0 with step=-4, without changing the four-skill ABI.',
          'Shadow Dance passive uses source day/night vision plus equipped vision bonuses and the H1 radial team visibility/pit boundary. Tree/highground line-of-sight and terrain fog are not executed.',
          'Integer Heartstopper damage carries fractional low-health damage before emitting; no ancient creeps or structures are affected.'],
        refresher_contract=dict(primary='https://www.dota2.com/706',
          interpretation='Refresher replenishes charge-based abilities. New hero Shard cooldowns and Slark Scepter charges reset through dota_extra_refresh. Charge restoration uses the same equipped cooldown-reduction multiplier as ordinary cooldowns.',
          overlap_storage='Two independent Dark Pact/Scythe clocks per caster and four Death Pulse records per caster preserve concurrent refreshed casts.'),
        debuff_immunity_contract=dict(primary='https://www.dota2.com/newfrontiers?l=tchinese',
          interpretation='Non-piercing spells can be applied while immune. Their negative effects are dormant while their duration continues, and resume if immunity ends first. Damage still resolves. Current item resistance values come from the installed item definitions, not the historical7.33 resistance percentage.',
          source_piercing=['slark_essence_shift','necrolyte_heartstopper_aura']),
        official_reference_urls=['https://www.dota2.com/hero/slark','https://www.dota2.com/hero/necrophos',
          'https://www.dota2.com/newfrontiers',
          'https://www.dota2.com/706',
          'https://www.dota2.com/datafeed/herodata?language=english&hero_id=36'],
        source_sha256={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in
          [ROOT/'game/extra_heroes.c',ROOT/'game/extra_heroes.h',ROOT/'game/extra_heroes_state.h',ROOT/'game/extra_heroes_data.h',Path(__file__).resolve()]})
    (OUT/'source-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'passed':True,'verified_constants':checks,'verified_numeric_constants':numeric_checks,'hero_count':2,'primary_abilities':8,'innates':2}))

if __name__=='__main__':main()
