"""CRC-check current installed four-hero definitions and generate integer data."""
from pathlib import Path
from decimal import Decimal
import datetime,hashlib,importlib.util,json,sys,re
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
spec=importlib.util.spec_from_file_location('roster_kv',ROOT/'private/passives/extract_sources.py');kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SLOTS=[['pudge_meat_hook','pudge_rot','pudge_flesh_heap','pudge_dismember'],['sniper_shrapnel','sniper_headshot','sniper_take_aim','sniper_assassinate'],['sven_storm_bolt','sven_great_cleave','sven_warcry','sven_gods_strength'],['tidehunter_gush','tidehunter_kraken_shell','tidehunter_anchor_smash','tidehunter_ravage']]
def raw(a,k,default='0'):
 v=a.get('AbilityValues',{}).get(k,a.get(k,default));return v.get('value',default) if isinstance(v,dict) else v
def nums(a,k,scale=1,count=4,default='0'):
 v=[int(Decimal(x)*scale) for x in raw(a,k,default).split()];return (v+[v[-1]]*count)[:count]
def row(v):return '{'+','.join(row(x) if isinstance(x,list) else str(x) for x in v)+'}'
def main():
 out=ROOT/'private/roster-heroes/source';out.mkdir(parents=True,exist_ok=True);v=VpkArchive(STEAM/'pak01_dir.vpk');units=[];abilities={};receipts=[]
 for slug in ['pudge','sniper','sven','tidehunter']:
  path=f'scripts/npc/heroes/npc_dota_hero_{slug}.txt';b=v.read(path);(out/(slug+'.txt')).write_bytes(b);u=kv.parse(b.decode('utf-8-sig'))['DOTAHeroes']['npc_dota_hero_'+slug];abilities.update(u['AbilityDefinitions']);units.append(u);receipts.append(v.identity(path))
 tooltip_path='resource/localization/abilities_english.txt';tooltip_raw=v.read(tooltip_path);receipts.append(v.identity(tooltip_path))
 tokens={k:t for k,t in re.findall(r'"(DOTA_Tooltip_(?:ability|modifier)_(?:pudge|sniper|sven|tidehunter)_[^"\r\n]+)"[ \t]+"((?:\\.|[^"\\\r\n])*)"',tooltip_raw.decode('utf-8-sig'))}
 (out/'tooltips.json').write_text(json.dumps(tokens,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 tables={}
 for field,key,scale in [('mana','AbilityManaCost',1),('cd','AbilityCooldown',20),('castpoint','AbilityCastPoint',20)]:tables['roster_'+field]=[[nums(abilities[n],key,scale) if 'PASSIVE' not in abilities[n]['AbilityBehavior'] else [0]*4 for n in slots] for slots in SLOTS]
 scalar={
 'pudge_hook_damage':('pudge_meat_hook','damage',1),'pudge_block':('pudge_flesh_heap','damage_block',1),'pudge_block_life':('pudge_flesh_heap','duration',20),'pudge_rot_damage':('pudge_rot','rot_damage',1),'pudge_rot_slow':('pudge_rot','rot_slow',-1),'pudge_dismember_damage':('pudge_dismember','dismember_damage',1),'pudge_dismember_str_pct':('pudge_dismember','strength_damage',100),
 'sniper_shrapnel_damage':('sniper_shrapnel','shrapnel_damage',1),'sniper_shrapnel_radius':('sniper_shrapnel','radius',1),'sniper_shrapnel_slow':('sniper_shrapnel','slow_movement_speed',-1),'sniper_headshot_damage':('sniper_headshot','damage',1),'sniper_headshot_life':('sniper_headshot','slow_duration',20),'sniper_aim_range':('sniper_take_aim','passive_attack_range_bonus',1),'sniper_aim_active_range':('sniper_take_aim','active_attack_range_bonus',1),'sniper_aim_vision':('sniper_take_aim','bonus_vision',1),'sniper_ult_damage':('sniper_assassinate','damage',1),
 'sven_bolt_damage':('sven_storm_bolt','AbilityDamage',1),'sven_bolt_stun':('sven_storm_bolt','bolt_stun_duration',20),'sven_bolt_radius':('sven_storm_bolt','bolt_aoe',1),'sven_cleave_pct':('sven_great_cleave','great_cleave_damage',1),'sven_cleave_width':('sven_great_cleave','cleave_ending_width',1),'sven_cleave_distance':('sven_great_cleave','cleave_distance',1),'sven_warcry_armor':('sven_warcry','bonus_armor',1),'sven_warcry_move':('sven_warcry','movespeed',1),'sven_gods_pct':('sven_gods_strength','gods_strength_damage',1),
 'tide_gush_damage':('tidehunter_gush','gush_damage',1),'tide_gush_armor':('tidehunter_gush','negative_armor',1),'tide_block':('tidehunter_kraken_shell','damage_reduction',1),'tide_cleanse':('tidehunter_kraken_shell','damage_cleanse',1),'tide_smash_damage':('tidehunter_anchor_smash','attack_damage',1),'tide_smash_reduction':('tidehunter_anchor_smash','damage_reduction',-1),'tide_ravage_damage':('tidehunter_ravage','AbilityDamage',1),'tide_ravage_stun':('tidehunter_ravage','duration',20)}
 for name,(ability,key,scale) in scalar.items():tables[name]=nums(abilities[ability],key,scale)
 lines=['/* Generated from CRC-checked installed Steam definitions. */','#ifndef H1_DOTA_ROSTER_HEROES_DATA_H','#define H1_DOTA_ROSTER_HEROES_DATA_H','static const DotaRosterBase roster_bases[4]={']
 for u in units:
  n=lambda k,scale=1,default='0':int(Decimal(u.get(k,default))*scale)
  values=[0 if u['AttributePrimary'].endswith('STRENGTH') else 1,n('AttributeBaseStrength',100),n('AttributeBaseAgility',100),n('AttributeBaseIntelligence',100),n('AttributeStrengthGain',100),n('AttributeAgilityGain',100),n('AttributeIntelligenceGain',100),(n('AttackDamageMin')+n('AttackDamageMax'))//2,n('ArmorPhysical'),n('AttackRange'),n('MovementSpeed'),n('BaseAttackSpeed',100,'100'),n('AttackRate',20),n('AttackAnimationPoint',1000),n('ProjectileSpeed'),n('StatusHealthRegen',100,'0.25')];lines.append(row(values)+',')
 lines.append('};')
 for name,values in tables.items():
  shape='[4][4][4]' if name in ('roster_mana','roster_cd','roster_castpoint') else '[4]';lines.append('static const int '+name+shape+'='+row(values)+';')
 lines.append('#endif');header=ROOT/'game/roster_heroes_data.h';header.write_text('\n'.join(lines)+'\n',encoding='utf8')
 report=dict(observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),archives=receipts,slots=SLOTS,passive_skill_masks=[0,2,2,0],units=units,tables=tables,header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),innates=['pudge_innate_graft_flesh','sniper_keen_scope','sven_wrath_of_god','tidehunter_leviathans_catch'],excluded_deprecated_facets=True,tooltips=tokens,semantic_conflicts={"sniper_assassinate_attack_factor":"Current numeric attack_factor=0 takes precedence over tooltip100-percent attack text; bonus damage follows300/400/500.","sniper_assassinate_kill":"Current numeric cooldown_reduction_on_kill=0/5/10seconds takes precedence over tooltip full-refresh text; hero-kill callback applies the numeric reduction.","sniper_headshot_distance":"Current numeric knockback_distance50 has no gradient parameters; maximum50Source distance is preserved without inventing a falloff formula."},scope='Source four regular skills plus actual selected unit innates. Deprecated facet alternatives are retained in the raw definition but are not silently added as extra passives.20Hz durations and Source distance units are integers; no numeric placeholders.')
 (out.parent/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print('Generated four current hero tables and CRC receipts')
if __name__=='__main__':main()
