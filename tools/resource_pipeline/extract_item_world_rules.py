"""CRC-verified installed NPC/Wind Waker/Dominate rules; no Steam writes."""
from pathlib import Path
import argparse,datetime,hashlib,importlib.util,json,sys
from decimal import Decimal
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
spec=importlib.util.spec_from_file_location('kv',ROOT/'private/passives/extract_sources.py')
kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
NAMES=['centaur_outrunner','satyr_trickster','black_dragon','black_drake','granite_golem','rock_golem','big_thunder_lizard','small_thunder_lizard','ice_shaman','frostbitten_golem','prowler_shaman','prowler_acolyte']
ABILITIES=['satyr_trickster_purge','black_dragon_fireball','big_thunder_lizard_slam','big_thunder_lizard_frenzy','ice_shaman_incendiary_bomb','spawnlord_master_stomp','spawnlord_master_freeze']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def nums(value,scale=1):
    if isinstance(value,dict):value=value['value']
    a=[int(Decimal(s)*scale) for s in str(value).split()];return (a+[a[-1]]*4)[:4]
def row(a,key,default=0,scale=1):return nums(a.get('AbilityValues',{}).get(key,a.get(key,default)),scale)
def main():
    p=argparse.ArgumentParser();p.add_argument('--write-header',action='store_true');args=p.parse_args()
    out=ROOT/'private/items/world-source';out.mkdir(parents=True,exist_ok=True)
    vpk=VpkArchive(Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk'));sources={};docs={}
    for entry,key,top in [('scripts/npc/npc_units.txt','units','DOTAUnits'),('scripts/npc/npc_abilities.txt','abilities','DOTAAbilities'),('scripts/npc/items.txt','items','DOTAAbilities'),('resource/localization/abilities_english.txt','english','lang')]:
        raw=vpk.read(entry);sources[entry]=vpk.identity(entry);(out/(key+'.txt')).write_bytes(raw);docs[key]=kv.parse(raw.decode('utf-8-sig'))[top]
    units=docs['units'];abilities=docs['abilities'];items=docs['items'];selected=['npc_dota_neutral_'+n for n in NAMES]+['npc_dota_creep_goodguys_melee','npc_dota_creep_goodguys_ranged','npc_dota_creep_badguys_melee','npc_dota_creep_badguys_ranged']
    lines=['/* Generated and verified against original Steam VPK by extract_item_world_rules.py. */','#ifndef DOTA_ITEM_NPC_DATA_H','#define DOTA_ITEM_NPC_DATA_H',
      'typedef struct { short mana,hp_regen100,mp_regen100,level,xp,gold_min,gold_max,mr,bat_ticks,point_ms,base_as,projectile; } DotaNpcSourceDef;',
      'static const DotaNpcSourceDef npc_source[17]={',' {0},']
    for n in selected:
        u=units[n];values=[int(u.get('StatusMana') or 0),nums(u.get('StatusHealthRegen') or 0,100)[0],nums(u.get('StatusManaRegen') or 0,100)[0],int(u.get('Level') or 1),int(u.get('BountyXP') or 0),int(u.get('BountyGoldMin') or 0),int(u.get('BountyGoldMax') or 0),int(u.get('MagicalResistance') or 0),nums(u.get('AttackRate') or 1,20)[0],nums(u.get('AttackAnimationPoint') or 0,1000)[0],int(u.get('BaseAttackSpeed') or 100),int(u.get('ProjectileSpeed') or 0)]
        lines.append(' {'+','.join(map(str,values))+'}, /* '+n+' */')
    lines+=['};','enum { NPC_PURGE=1,NPC_FIREBALL,NPC_SLAM,NPC_FRENZY,NPC_ICEFIRE,NPC_DESECRATE,NPC_PETRIFY };',
      'typedef struct { short range[4],mana[4],cooldown[4],cast[4],radius[4],value[4],duration[4],second[4]; } DotaNpcAbilityDef;',
      'static const DotaNpcAbilityDef npc_abilities[8]={ {{0},{0},{0},{0},{0},{0},{0},{0}},']
    extra=[('summon_damage','duration','purge_rate'),('damage','duration','burn_interval'),('AbilityDamage','hero_duration','non_hero_duration'),('attackspeed_bonus','duration',None),('burn_damage','duration','building_damage_pct'),('damage','duration','armor_reduction_pct'),('damage','duration','health_threshold_pct')]
    for n,(damage,duration,second) in zip(ABILITIES,extra):
        a=abilities[n];fields=[row(a,'AbilityCastRange'),row(a,'AbilityManaCost'),row(a,'AbilityCooldown',scale=20),row(a,'AbilityCastPoint',scale=20),row(a,'radius'),row(a,damage),row(a,duration,scale=20),row(a,second,scale=20 if second in ('burn_interval','non_hero_duration') else 1) if second else [0]*4]
        lines.append(' {'+','.join('{'+','.join(map(str,x))+'}' for x in fields)+'}, /* '+n+' */')
    lines+=['};','enum { NPC_AURA_MAGIC=1,NPC_AURA_ARMOR,NPC_AURA_SPELL,NPC_AURA_HEALTH,NPC_AURA_WEAK,NPC_AURA_DRUMS,NPC_AURA_TIME,NPC_AURA_PROWLER };',
      'typedef struct { short kind,radius,first[4],second[4]; } DotaNpcAuraDef;','static const DotaNpcAuraDef npc_auras[13]={ {0},']
    aura=[('mudgolem_cloak_aura',1,'bonus_magical_armor','bonus_magical_armor_creeps'),None,('black_dragon_dragonhide_aura',2,'bonus_armor',None),('black_drake_magic_amplification_aura',3,'spell_amp',None),('granite_golem_hp_aura',4,'bonus_hp',None),('ancient_rock_golem_weakening_aura',5,'armor_reduction',None),None,('big_thunder_lizard_wardrums_aura',6,'speed_bonus','accuracy'),None,('frostbitten_golem_time_warp_aura',7,'bonus_cdr',None),None,('spawnlord_aura',8,'lifesteal','hp_regen')]
    for spec in aura:
        if not spec:lines.append(' {0},');continue
        n,kind,first,second=spec;a=abilities[n];lines.append(' {'+str(kind)+','+str(row(a,'radius')[0])+',{'+','.join(map(str,row(a,first)))+'},{'+','.join(map(str,row(a,second) if second else [0]*4))+'}}, /* '+n+' */')
    lines+=['};','#endif',''];header='\n'.join(lines);path=ROOT/'game/item_npc_data.h'
    if args.write_header:path.write_text(header,encoding='utf8')
    assert path.read_text(encoding='utf8')==header,'NPC header differs from installed Source'
    tooltips=docs['english']['Tokens'];native={name:units[name] for name in selected};ability_names={a for u in native.values() for k,a in u.items() if k.startswith('Ability') and isinstance(a,str) and a in abilities}
    report=dict(passed=True,observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source=sources,units=native,abilities={n:abilities[n] for n in sorted(ability_names)},items={n:items[n] for n in ['item_wind_waker','item_helm_of_the_dominator','item_helm_of_the_overlord']},tooltips={k:v for k,v in tooltips.items() if any(k.lower().startswith('dota_tooltip_ability_'+n.lower()) for n in [*ability_names,'item_wind_waker','item_helm_of_the_dominator','item_helm_of_the_overlord'])},header_sha256=sha(path),h1_adaptations=['Existing two ordinary neutral species, ten ancient species and four lane-creep types only; no substitute NPCs.','Original active abilities are automatically selected by the controlled creep; no new multi-unit input HUD.','Twenty-Hz integer clock and Source/world scale4; original scalar/vector values and Overlord+1 skill rank are preserved.','Original common effect sprites are reused; no claim of newly baked native-skill models.'])
    report.update(native_combat_abilities=16,retained_lane_passives=2,neutral_upgrade_scope='Existing H1 spawn/growth rules; no new global neutral levelling system.',runtime_source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'game/item_world.c',ROOT/'game/item_world.h',ROOT/'game/item_world_data.h',path,Path(__file__).resolve()]})
    (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({'passed':True,'unit_definitions':len(native),'native_abilities':len(ability_names),'header_sha256':sha(path)}))
if __name__=='__main__':main()
