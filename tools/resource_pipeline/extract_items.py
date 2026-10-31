"""Extract the installed Valve item catalogue into the external HIT1 table.

Read-only VPK input, CRC verified. No obsolete/internal item is sold. Recipes
and neutral/enchantment records keep their distinct flags. All text is UTF-8
outside the executable; numeric parameters retain every Source rank (x100).
"""
from pathlib import Path
import argparse,datetime,hashlib,importlib.util,json,re,struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
spec=importlib.util.spec_from_file_location('item_kv',ROOT/'private/passives/extract_sources.py')
kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
OUT=ROOT/'private/items/current-source'
ARCHIVE=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk')
OLD=['item_boots','item_blades_of_attack','item_chainmail','item_vitality_booster','item_energy_booster','item_flask']
EXTRA=['item_aegis','item_cheese','item_refresher_shard','item_ultimate_scepter_roshan','item_aghanims_shard_roshan','item_famango','item_great_famango','item_greater_famango','item_ward_dispenser']
ACTIONS={
 'BLINK':'blink overwhelming_blink swift_blink arcane_blink fallen_sky',
 'TREE':'quelling_blade bfury tango foragers_kit', 'TREE_PLANT':'branches',
 'HEAL':'faerie_fire enchanted_mango cheese famango great_famango greater_famango mana_draught',
 'REGEN':'flask clarity bottle', 'WARD':'ward_observer ward_sentry ward_dispenser',
 'CHARGES':'magic_stick magic_wand holy_locket', 'TELEPORT':'tpscroll',
 'PHASE':'phase_boots', 'TREADS':'power_treads','MIDAS':'hand_of_midas',
 'TEAM_RESTORE':'mekansm arcane_boots guardian_greaves', 'BARRIER':'pipe crimson_guard solar_crest pavise glimmer_cape',
 'SOUL':'urn_of_shadows spirit_vessel essence_distiller','HEX':'sheepstick',
 'SILENCE':'orchid bloodthorn','CYCLONE':'cyclone wind_waker','FORCE':'force_staff hurricane_pike psychic_headband pogo_stick',
 'DAMAGE':'dagon dagon_2 dagon_3 dagon_4 dagon_5 blood_grenade',
 'REFRESH':'refresher refresher_shard','IMMUNITY':'black_king_bar minotaur_horn',
 'SHIVA':'shivas_guard','LIFESTEAL':'satanic bloodstone', 'SPELLBLOCK':'sphere','REFLECT':'lotus_orb',
 'METEOR':'meteor_hammer','PURGE':'nullifier diffusal_blade disperser','GHOST':'ghost ethereal_blade crellas_crozier',
 'BLADEMAIL':'blade_mail','TOGGLE':'rapier radiance armlet','ILLUSION':'manta riftshadow_prism',
 'INVISIBLE':'invis_sword silver_edge shadow_amulet smoke_of_deceit', 'STATIC':'mjollnir',
 'DOMINATE':'helm_of_the_dominator helm_of_the_overlord','ROOT':'gungir rod_of_atos',
 'BERSERK':'mask_of_madness','SOULRING':'soul_ring','DRUM':'ancient_janggo boots_of_bearing',
 'VEIL':'veil_of_discord','STUN':'abyssal_blade','DISARM':'heavens_halberd crippling_crossbow',
 'PULL':'harpoon','REVEAL':'dust gem','MOON':'moon_shard','SUMMON':'demonicon',
 'NEUTRAL':'essence_ring seeds_of_serenity spider_legs metamorphic_mandible idol_of_screeauk enchanters_bauble flayers_bota dezun_bloodrite harmonizer kobold_cup polliwog_charm ash_legion_shield dagger_of_ristul stonefeather_satchel medallion_of_courage jidi_pollen_bag heavy_blade',
 'UPGRADE':'ultimate_scepter aghanims_shard ultimate_scepter_roshan aghanims_shard_roshan',
}
ACTION_NAMES=['NONE']+list(ACTIONS)
ACTION_MAP={name:i for i,k in enumerate(ACTION_NAMES) if k!='NONE' for name in ACTIONS[k].split()}
STATS=['HEALTH','MANA','DAMAGE','ARMOR','STRENGTH','AGILITY','INTELLECT','ATTACK_SPEED','MOVE_FLAT','MOVE_PERCENT','HEALTH_REGEN','MANA_REGEN','MAGIC_RESIST','EVASION','LIFESTEAL','SPELL_AMP','COOLDOWN_REDUCTION','CAST_RANGE','ATTACK_RANGE','STATUS_RESIST','SPELL_LIFESTEAL','HEAL_AMP','ARMOR_REDUCTION','AOE_BONUS']
STAT_KEYS={
 'HEALTH':['bonus_health','bonus_hp','health_bonus'],'MANA':['bonus_mana'],'DAMAGE':['bonus_damage','base_attack_damage'],
 'ARMOR':['bonus_armor','armor'],'STRENGTH':['bonus_strength','bonus_str'],'AGILITY':['bonus_agility'],'INTELLECT':['bonus_intellect','bonus_int'],
 'ATTACK_SPEED':['bonus_attack_speed','attack_speed'],'HEALTH_REGEN':['bonus_health_regen','bonus_hp_regen','health_regen','hp_regen','bonus_regen'],
 'MANA_REGEN':['bonus_mana_regen','bonus_mp_regen','mana_regen','mp_regen'],'MAGIC_RESIST':['magic_resistance','bonus_magical_armor','bonus_magic_resistance','magic_res'],
 'EVASION':['evasion','bonus_evasion'],'LIFESTEAL':['lifesteal_percent','lifesteal','attack_lifesteal'],'SPELL_AMP':['spell_amp','bonus_spell_amp'],
 'COOLDOWN_REDUCTION':['cooldown_reduction'],'CAST_RANGE':['cast_range_bonus'],'ATTACK_RANGE':['bonus_attack_range','base_attack_range'],
 'STATUS_RESIST':['status_resistance'],'SPELL_LIFESTEAL':['spell_lifesteal'],'HEAL_AMP':['heal_increase_passive','hp_regen_amp'],
 'ARMOR_REDUCTION':['corruption_armor'],'AOE_BONUS':['bonus_aoe','aoe_bonus'],
}
UNSCALED={'HEALTH','MANA','DAMAGE','ATTACK_SPEED','MOVE_FLAT','CAST_RANGE','ATTACK_RANGE','AOE_BONUS'}
def vals(value):
 if isinstance(value,dict):value=value.get('value','')
 if not isinstance(value,(str,int,float)):return []
 try:return [float(x) for x in str(value).split()]
 except ValueError:return []
def number(d,k,rank=0,default=0):
 v=vals(d.get(k,''));return v[min(rank,len(v)-1)] if v else default
def macro(s):return re.sub('[^A-Z0-9]','_',s.upper().removeprefix('ITEM_'))
def clean(s):return re.sub('<[^>]+>','',s.replace('<br>','\n').replace('\\n','\n'))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vpk',type=Path,default=ARCHIVE);p.add_argument('--cached',action='store_true');args=p.parse_args()
 OUT.mkdir(parents=True,exist_ok=True);sources=[];docs={}
 archive=None if args.cached else VpkArchive(args.vpk)
 for entry in ['scripts/npc/items.txt','scripts/npc/neutral_items.txt','resource/localization/abilities_schinese.txt','resource/localization/abilities_english.txt']:
  dest=OUT/Path(entry).name
  if archive:dest.write_bytes(archive.read(entry));sources.append(archive.identity(entry))
  docs[Path(entry).stem]=kv.parse(dest.read_text(encoding='utf-8-sig'))
 a={k:v for k,v in docs['items']['DOTAAbilities'].items() if isinstance(v,dict)}
 tiers=docs['neutral_items']['neutral_items']['neutral_tiers']
 normal=[k for k,v in a.items() if v.get('IsObsolete')!='1' and v.get('ItemPurchasable','1')!='0' and v.get('ItemRecipe')!='1']
 recipes=[k for k,v in a.items() if v.get('IsObsolete')!='1' and v.get('ItemPurchasable','1')!='0' and v.get('ItemRecipe')=='1']
 recipes+=['item_recipe_ward_dispenser']
 neutral={k:int(t) for t,v in tiers.items() for k in v['items']}
 enh=[]
 for t,v in tiers.items():
  for k in dict.fromkeys(k for group in v['enhancements'].values() for k in group):enh.append((k,int(t)))
 names=OLD+[k for k in normal if k not in OLD]+recipes+list(neutral)+[k+'#'+str(t) for k,t in enh]
 names += [k for k in EXTRA if k not in names]
 assert len(names)==len(set(names))
 ids={k:i for i,k in enumerate(names)}
 topkeys=('AbilityChannelTime','AbilityCooldown','AbilityManaCost','AbilityHealthCost','AbilityCastRange','AbilityCharges','AbilityChargeRestoreTime','ItemInitialCharges','ItemRequiresCharges')
 allkeys=sorted({key for name in names for key in a[name.split('#')[0]].get('AbilityValues',{}) if vals(a[name.split('#')[0]].get('AbilityValues',{})[key])}|set(topkeys))
 keys={k:i+1 for i,k in enumerate(allkeys)}
 params=[];recs=[];manifest=[];recipe_rows=[];text=bytearray();text_index=[]
 shared={name:i+1 for i,name in enumerate(sorted({v.get('AbilitySharedCooldown') for v in a.values() if v.get('AbilitySharedCooldown')}))}
 langs={lang:{k.lower():v for k,v in docs['abilities_'+lang]['lang']['Tokens'].items()} for lang in ('schinese','english')}
 def string(s):
  b=len(text);text.extend(s.encode('utf8')+b'\0');return b
 for id,name in enumerate(names):
  base=name.split('#')[0];d=a[base];av=d.get('AbilityValues',{});tier=int(name.split('#')[1]) if '#' in name else neutral.get(base,0)
  level=tier-min(t for k,t in enh if k==base)+1 if '#' in name else int(number(d,'ItemBaseLevel',default=1));rank=max(0,level-1)
  flags=0;is_recipe=d.get('ItemRecipe')=='1';cost=int(number(d,'ItemCost'))
  if name in normal or (name in recipes and cost>0):flags|=1
  if is_recipe:flags|=2
  if is_recipe and cost==0:flags|=4
  if base in neutral:flags|=8
  if '#' in name:flags|=16
  if d.get('ItemPermanent','1')=='0':flags|=32
  if d.get('ItemStackable')=='1':flags|=64
  if d.get('ItemSellable','1')!='0' and not(flags&(8|16)):flags|=128
  dis=d.get('ItemDisassembleRule','DOTA_ITEM_DISASSEMBLE_NEVER')
  disrule={'DOTA_ITEM_DISASSEMBLE_NEVER':0,'DOTA_ITEM_DISASSEMBLE_ALWAYS':1,'DOTA_ITEM_DISASSEMBLE_ONCE':2}.get(dis,0)
  if disrule:flags|=256
  slug=base[5:];boots=slug in ('boots','phase_boots','power_treads','arcane_boots','tranquil_boots','travel_boots','travel_boots_2','guardian_greaves','boots_of_bearing')
  if boots:flags|=512
  behavior=d.get('AbilityBehavior','');team=d.get('AbilityUnitTargetTeam','')
  if 'ENEMY' in team:flags|=1024
  if 'FRIENDLY' in team:flags|=2048
  for word,flag in [('POINT',4096),('TOGGLE',8192),('CHANNELLED',16384),('NO_TARGET',32768)]:
   if word in behavior:flags|=flag
  active=bool(behavior and 'PASSIVE' not in behavior and not is_recipe)
  action=ACTION_MAP.get(slug,0) if active else 0
  if active and action==0:raise ValueError('Unclassified active '+base+' '+behavior)
  first=len(params)
  for key,value in {**av,**{k:d[k] for k in topkeys if k in d}}.items():
   for r,value in enumerate(vals(value)):params.append((keys[key],r,round(value*100)))
  stats=[]
  for stat in STATS:
   value=sum(number(av,k,rank) for k in STAT_KEYS.get(stat,[]))
   if stat in ('STRENGTH','AGILITY','INTELLECT'):value+=number(av,'bonus_all_stats',rank)+number(av,'bonus_stats',rank)
   if stat=='MOVE_FLAT':
    value=number(av,'bonus_movement',rank)+number(av,'self_movement_speed',rank)
    if boots or slug in ('wind_lace','cyclone','wind_waker'):value+=number(av,'bonus_movement_speed',rank)+number(av,'movement_speed',rank)
    if slug=='power_treads':value=number(av,'bonus_movement_speed_melee',rank)
   if stat=='MOVE_PERCENT' and not boots and slug not in ('wind_lace','cyclone','wind_waker','smoke_of_deceit'):value=number(av,'bonus_movement_speed',rank)+number(av,'movement_speed_percent_bonus',rank)+number(av,'movespeed_pct',rank)
   if stat=='MOVE_FLAT' and '#' in name:value+=number(av,'movement_speed',rank)+number(av,'movespeed',rank)
   if stat=='MANA' and '#' in name and slug not in ('enhancement_feverish',):value+=number(av,'max_mana',rank)
   if stat=='HEALTH_REGEN' and slug in ('flask','tango','bottle'):value=0
   if stat=='HEALTH_REGEN' and slug=='heart':value=0
   if slug in ('specialists_array','hydras_breath'):
    if stat=='DAMAGE':value+=number(av,'damage',rank)
    if stat=='AGILITY':value+=number(av,'agility',rank)
    if stat=='STRENGTH':value+=number(av,'strength',rank)
   if stat=='MANA_REGEN' and slug in ('clarity','bottle'):value=0
   if stat=='DAMAGE' and slug=='rapier':value=number(av,'bonus_damage_base',rank)
   if stat=='SPELL_AMP' and slug=='rapier':value=0
   value=round(value*(1 if stat in UNSCALED else 100));assert -32768<=value<=32767,(name,stat,value);stats.append(value)
  cooldown=round(number(d,'AbilityCooldown',rank)*20);mana=round(number(d,'AbilityManaCost',rank));cast=round(number(d,'AbilityCastRange',rank))
  radius=next((round(number(av,k,rank)) for k in ['radius','aura_radius','replenish_radius','heal_radius','blast_radius','debuff_radius','application_radius','impact_radius'] if k in av),0)
  duration=next((round(number(av,k,rank)*20) for k in ['duration','buff_duration','active_duration','unholy_duration','phase_duration','windwalk_duration','sheep_duration','silence_duration','cyclone_duration','static_duration','berserk_duration','barrier_duration','disarm_melee','fade_duration','resist_debuff_duration','debuff_duration','restore_time','burn_duration','lifetime'] if k in av),0)
  initial=int(number(d,'ItemInitialCharges',default=0));maxcharge=int(number(av,'max_charges',default=number(d,'ItemMaxCharges',default=initial)))
  if flags&32 and initial==0:initial=1
  if slug=='bottle':initial=3;maxcharge=3
  if slug in ('ancient_janggo','boots_of_bearing'):initial=int(number(d,'ItemInitialCharges',default=8));maxcharge=8
  stock=[int(number(d,k,default=0)) for k in ['ItemStockInitial','ItemStockMax']]
  stock+=[round(number(d,k+'Turbo',default=number(d,k))*20) for k in ['ItemStockTime','ItemInitialStockTime']]
  stock=[min(65535,max(0,x)) for x in stock]
  category=next((i for i,k in enumerate(['consumable','attribute','weapon','armor','secret_shop','accessories','support','magical','armor','weapon','artifact']) if k in d.get('ItemShopTags','')),0)
  # Source's 32-bit ID is retained in the manifest; runtime uses stable compact IDs.
  row=dict(id=id,name=name,source_name=base,source_id=int(number(d,'ID',default=-1)),cost=cost,flags=flags,level=level,tier=tier,action=ACTION_NAMES[action],texture=d.get('AbilityTextureName',slug),behavior=behavior,stats=dict(zip(STATS,stats)),definition=d)
  key='dota_tooltip_ability_'+base
  zh=langs['schinese'].get(key,langs['english'].get(key,slug));en=langs['english'].get(key,slug)
  description=langs['schinese'].get(key+'_description',langs['english'].get(key+'_description',''))
  for k,v in av.items():description=re.sub('%'+re.escape(k)+'%',str(number(av,k,rank)).removesuffix('.0'),description,flags=re.I)
  if '#' in name:zh+='（'+str(tier)+'阶）'
  row.update(chinese=clean(zh),english=clean(en),description=clean(description))
  text_index.append([string(row['chinese']),string(row['description']),string(name),string(row['english'])])
  manifest.append(row)
  # Recipe offsets are filled in after all compact IDs are established.
  recs.append([cost,flags,cooldown,mana,cast,radius,duration,action,category,first,len(params)-first,0,0,level,shared.get(d.get('AbilitySharedCooldown'),0),min(255,initial),min(255,maxcharge),0,*stats,*stock,disrule,tier,id,0])
 for name in recipes:
  d=a[name];result=d.get('ItemResult');requirements=d.get('ItemRequirements',{})
  if result not in ids:raise ValueError(('recipe result missing',name,result))
  for text_req in requirements.values():
   if not text_req:continue
   components=[x for x in text_req.split(';') if x];count=len(components);assert count<=6
   star=sum((1<<i) for i,c in enumerate(components) if '*' in c)
   comp=[ids[c.replace('*','')] for c in components]
   recipe_rows.append([ids[name],ids[result],*(comp+[65535]*(6-count)),count,star,0])
 for i,row in enumerate(recs):
  indexes=[j for j,r in enumerate(recipe_rows) if r[0]==i or r[1]==i]
  if indexes:row[11]=min(indexes);row[12]=len(indexes)
 record_fmt='<7H2B4H4BI24h4H2BHI';assert struct.calcsize(record_fmt)==96
 records=b''.join(struct.pack(record_fmt,*r) for r in recs)
 parameter_data=b''.join(struct.pack('<HHi',*r) for r in params)
 recipe_data=b''.join(struct.pack('<8H2BH',*r) for r in recipe_rows)
 recordoff=64;paramoff=recordoff+len(records);recipeoff=paramoff+len(parameter_data);indexoff=recipeoff+len(recipe_data);textoff=indexoff+len(names)*16;iconindexoff=textoff+len(text);iconoff=iconindexoff+len(names)*8
 header=struct.pack('<16I',0x31544948,1,iconoff,len(names),recordoff,96,paramoff,len(params),recipeoff,len(recipe_rows),indexoff,textoff,iconindexoff,iconoff,0,0)
 blob=header+records+parameter_data+recipe_data+b''.join(struct.pack('<4I',*r) for r in text_index)+text+bytes(len(names)*8)
 (OUT/'catalog.hit').write_bytes(blob)
 (ROOT/'game/items_ids.h').write_text('#ifndef H1_DOTA_ITEMS_IDS_H\n#define H1_DOTA_ITEMS_IDS_H\n'+''.join('#define DOTA_ITEM_'+macro(name)+' '+str(i)+'\n' for i,name in enumerate(names))+'#define DOTA_ITEM_CATALOG_COUNT '+str(len(names))+'\n#endif\n',encoding='utf8')
 (ROOT/'game/items_params.h').write_text('#ifndef H1_DOTA_ITEMS_PARAMS_H\n#define H1_DOTA_ITEMS_PARAMS_H\n'+''.join('#define IP_'+macro(key)+' '+str(value)+'\n' for key,value in keys.items())+'#endif\n',encoding='ascii')
 (ROOT/'game/items_actions.h').write_text('#ifndef H1_DOTA_ITEMS_ACTIONS_H\n#define H1_DOTA_ITEMS_ACTIONS_H\n'+''.join('#define ITEM_ACTION_'+name+' '+str(i)+'\n' for i,name in enumerate(ACTION_NAMES))+'#endif\n',encoding='ascii')
 enchant_rows=[]
 for name,tier in enh:
  mask=0
  for group,bit in [('global',1),('strength',2),('agility',4),('intelligence',8),('universal',16)]:
   if name in tiers[str(tier)]['enhancements'].get(group,{}):mask|=bit
  enchant_rows.append((ids[name+'#'+str(tier)],tier,mask))
 (ROOT/'game/neutral_catalog.h').write_text('#ifndef H1_DOTA_NEUTRAL_CATALOG_H\n#define H1_DOTA_NEUTRAL_CATALOG_H\n/* Extracted current Source artifact pools and attribute eligibility. */\nstatic const short neutral_artifacts[49][2]={'+','.join('{'+str(ids[k])+','+str(t)+'}' for k,t in neutral.items())+'};\nstatic const short neutral_enchantments[44][3]={'+','.join('{'+','.join(map(str,r))+'}' for r in enchant_rows)+'};\n#endif\n',encoding='ascii')
 counts={'raw':len(a),'normal':len(normal),'paid_recipes':sum(number(a[k],'ItemCost')>0 for k in recipes),'auto_recipes':sum(number(a[k],'ItemCost')==0 for k in recipes),'neutral':len(neutral),'enhancement_tiers':len(enh),'total':len(names),'recipe_relations':len(recipe_rows),'parameters':len(params),'bytes':len(blob)}
 payload={'counts':counts,'source':sources,'format':{'record_bytes':96,'parameter_bytes':8,'recipe_bytes':20,'text_index_bytes':16,'text_fields':['name','description','source_slug','english_name'],'text_offsets':'relative to header.text_offset, UTF-8 NUL-terminated','icon_index_bytes':8,'icon_fields':['offset_relative_to_icon_offset','bytes'],'icons':'unfilled; parent supplies original icon RGB565'},'parameters':keys,'recipes':recipe_rows,'items':manifest}
 (OUT/'catalog.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 (OUT/'source-manifest.json').write_text(json.dumps({'generated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'counts':counts,'sources':sources,'catalog_sha256':hashlib.sha256(blob).hexdigest(),'excluded':{k:('obsolete' if v.get('IsObsolete')=='1' else 'internal or inactive neutral') for k,v in a.items() if k not in ids and k not in {x[0] for x in enh}}},ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(counts))
if __name__=='__main__':main()
