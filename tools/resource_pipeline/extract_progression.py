"""CRC-bound current ten-hero skills, innates, talent bindings and base stats."""
from pathlib import Path
from decimal import Decimal
import datetime,hashlib,importlib.util,json,re,struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SLUGS=['axe','lina','juggernaut','crystal_maiden','slark','necrolyte','pudge','sniper','sven','tidehunter']
NAMES=['斧王','莉娜','主宰','水晶室女','斯拉克','瘟疫法师','帕吉','狙击手','斯温','潮汐猎人']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def esc(s):return json.dumps(str(s),ensure_ascii=False)
def text(raw):
 return raw.decode('utf-16') if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else raw.decode('utf-8-sig')
def scalar(value):
 if isinstance(value,dict):return value.get('value','0')
 return str(value)
def number(value,scale=1):return int(Decimal(str(value))*scale)
def plain(s):return re.sub(r'\s+',' ',re.sub('<[^>]+>','',s.replace('\\n',' '))).strip()
def main():
 sp=importlib.util.spec_from_file_location('progression_kv',ROOT/'private/passives/extract_sources.py');kv=importlib.util.module_from_spec(sp);sp.loader.exec_module(kv)
 v=VpkArchive(STEAM/'pak01_dir.vpk');out=ROOT/'private/progression';out.mkdir(parents=True,exist_ok=True);receipts=[]
 def doc(path):
  raw=v.read(path);receipts.append(v.identity(path));return kv.parse(text(raw))
 base=doc('scripts/npc/heroes/npc_dota_hero_base.txt')['DOTAHeroes']['npc_dota_hero_base'];common=doc('scripts/npc/npc_abilities.txt')['DOTAAbilities']
 locpath='resource/localization/abilities_schinese.txt';loc=v.read(locpath);receipts.append(v.identity(locpath));tokens={k.casefold():val for k,val in re.findall(r'"([^"\r\n]+)"[ \t]+"((?:\\.|[^"\\\r\n])*)"',text(loc))}
 heroes=[];effects=[];talents=[];base_rows=[];value_rows=[];value_effects=[];value_ranges=[]
 for h,slug in enumerate(SLUGS):
  path=f'scripts/npc/heroes/npc_dota_hero_{slug}.txt';u=doc(path)['DOTAHeroes']['npc_dota_hero_'+slug];effective={**base,**u};defs=u['AbilityDefinitions'];slots=[u['Ability1'],u['Ability2'],u['Ability3'],u['Ability6']]
  selected=[u.get('Ability'+str(i)) for i in range(1,10)];innates=[n for n in selected if n in defs and defs[n].get('Innate')=='1'];assert len(innates)==1,(slug,innates)
  innate=innates[0];ability_data=[]
  for skill,name in enumerate(slots):
   a={**common['ability_base'],**common.get(name,{}),**defs[name]};ultimate=a.get('AbilityType')=='ABILITY_TYPE_ULTIMATE';assert ultimate==(skill==3),(slug,name)
   ability_data.append(dict(name=name,max_level=int(a.get('MaxLevel',3 if ultimate else 4)),required_level=int(a.get('RequiredLevel',6 if ultimate else 1)),levels_between=int(a.get('LevelsBetweenUpgrades',6 if ultimate else 2)),definition=a))
  talent_names=[u['Ability'+str(i)] for i in range(10,18)];ht=[]
  for index,name in enumerate(talent_names):
   a={**common.get(name,{}),**defs.get(name,{})};bindings=[]
   for ability,definition in defs.items():
    for field,value in definition.get('AbilityValues',{}).items():
     if isinstance(value,dict) and name in value:
      binding=dict(ability=ability,field=field,expression=str(value[name]),base=str(value.get('value','0')),selected=ability in selected)
      bindings.append(binding);effects.append(dict(hero=h,index=index,talent=name,**binding))
   own={k:scalar(val) for k,val in a.get('AbilityValues',{}).items()};label=tokens.get(('DOTA_Tooltip_ability_'+name).casefold(),name);description=tokens.get(('DOTA_Tooltip_ability_'+name+'_Description').casefold(),'')
   substitutions=dict(own)
   for b in bindings:
    expr=b['expression'];substitutions[b['field']]=expr.lstrip('+-x%')
   def substitute(m):
    key=m.group(1);return substitutions.get(key,substitutions.get(key[6:] if key.startswith('bonus_') else key,substitutions.get('value',m.group())))
   label=plain(re.sub(r'\{s:([^}]+)\}',substitute,label));description=plain(re.sub(r'\{s:([^}]+)\}',substitute,description))
   if not description:description=label
   record=dict(hero=h,index=index,level=10+(index//2)*5,name=name,label=label,description=description,definition=a,bindings=bindings,own_values=own)
   talents.append(record);ht.append(record)
  n=lambda k,scale=1: number(effective.get(k,'0'),scale)
  primary={'DOTA_ATTRIBUTE_STRENGTH':0,'DOTA_ATTRIBUTE_AGILITY':1,'DOTA_ATTRIBUTE_INTELLECT':2,'DOTA_ATTRIBUTE_ALL':3}[effective['AttributePrimary']]
  values=[primary,n('AttributeBaseStrength',100),n('AttributeBaseAgility',100),n('AttributeBaseIntelligence',100),n('AttributeStrengthGain',100),n('AttributeAgilityGain',100),n('AttributeIntelligenceGain',100),n('StatusHealth'),n('StatusMana'),n('StatusHealthRegen',100),n('StatusManaRegen',100),(n('AttackDamageMin',100)+n('AttackDamageMax',100))//2,n('ArmorPhysical',100),n('BaseAttackSpeed',100),n('AttackRate',20),n('AttackAnimationPoint',1000),n('ProjectileSpeed'),n('AttackRange'),n('MovementSpeed')]
  assert all(-32768<=x<=32767 for x in values);base_rows.append(values)
  innate_values={k:scalar(vv).split()[0] for k,vv in defs[innate].get('AbilityValues',{}).items()}
  innate_desc=plain(tokens.get(('DOTA_Tooltip_ability_'+innate+'_Description').casefold(),''))
  innate_desc=re.sub(r'%([a-zA-Z0-9_]+)%',lambda m:innate_values.get(m.group(1),m.group()),innate_desc).replace('%%','%')
  heroes.append(dict(id=h,name=slug,label=NAMES[h],raw=u,effective_base=effective,skills=ability_data,innate=innate,innate_definition=defs[innate],innate_label=plain(tokens.get(('DOTA_Tooltip_ability_'+innate).casefold(),innate)),innate_description=innate_desc,talents=ht,base_values=values))
  names=slots+[u.get('Ability4','generic_hidden'),innate];hranges=[]
  for slot,name in enumerate(names):
   hranges.append(len(value_rows));definition={**common['ability_base'],**common.get(name,{}),**defs.get(name,{})};fields={**{k:v for k,v in definition.items() if k not in ('AbilityValues','AbilitySpecial')},**definition.get('AbilityValues',{})}
   for field,value in sorted(fields.items()):
    vv=value if isinstance(value,dict) else {'value':value};raw=scalar(vv);parts=raw.split()
    try:nums=[number(p,100) for p in parts];step=number(str(vv.get('hero_levelup','0')).lstrip('+'),100)
    except Exception:continue
    if not nums:continue
    first=len(value_effects)
    for talent in ht:
     if talent['name'] not in vv:continue
     expr=str(vv[talent['name']]);op=4
     if expr.startswith('x'):op=3;literal=expr[1:]
     elif expr.endswith('%'):op=2;literal=expr[:-1].lstrip('+')
     elif expr.startswith(('+','-')):op=1;literal=expr.lstrip('+')
     else:literal=expr
     value_effects.append((talent['index'],op,number(literal,100)))
    value_rows.append(dict(hero=h,slot=slot,field=field,values=(nums+[nums[-1]]*4)[:4],count=min(len(nums),4),hero_step100=step,first_effect=first,effect_count=len(value_effects)-first))
  hranges.append(len(value_rows));value_ranges.append(hranges)
 assert len(talents)==80
 actual_attribute_name=base['Ability25'];assert actual_attribute_name=='special_bonus_attributes';actual_attribute=common[actual_attribute_name];assert actual_attribute.get('AbilityType','ABILITY_TYPE_BASIC')=='ABILITY_TYPE_BASIC'
 import pefile
 from capstone import Cs,CS_ARCH_X86,CS_MODE_64
 server=STEAM/'bin/win64/server.dll';binary=server.read_bytes();assert sha(server)=='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
 pe=pefile.PE(data=binary,fast_load=True);image=pe.OPTIONAL_HEADER.ImageBase;md=Cs(CS_ARCH_X86,CS_MODE_64)
 def code(va,n):
  at=pe.get_offset_from_rva(va-image);data=binary[at:at+n];return dict(va=hex(va),raw=hex(at),bytes=data.hex(),asm=[f'{i.address:x} {i.mnemonic} {i.op_str}' for i in md.disasm(data,va)])
 def bytes_at(va,n):at=pe.get_offset_from_rva(va-image);return binary[at:at+n]
 assert bytes_at(0x1816e9eb8,6)==bytes.fromhex('41b803000000')
 assert bytes_at(0x1816e9ebe,10)==bytes.fromhex('c787a802000006000000')
 assert bytes_at(0x1816e9ecf,10)==bytes.fromhex('c787a402000006000000')
 assert bytes_at(0x1816e9de1,10)==bytes.fromhex('c787a802000001000000')
 assert bytes_at(0x1816e9deb,10)==bytes.fromhex('c787a402000002000000')
 assert bytes_at(0x1816e9e2a,6)==bytes.fromhex('41b804000000')
 assert bytes_at(0x180c6eb21,8)==bytes.fromhex('c744242800840000')
 assert bytes_at(0x180c6e9b0,6)==bytes.fromhex('b802000000c3')
 assert bytes_at(0x180c688d9,3)==bytes.fromhex('83c802')
 assert struct.unpack_from('<f',binary,0x2eb1870)[0]==1.0
 native=dict(read_only=True,server_path=str(server),server_sha256=sha(server),ordinary_default=dict(max_level=4,required_level=1,levels_between=2),ultimate_default=dict(max_level=3,required_level=6,levels_between=6),ability_parser=code(0x1816e9dde,270),lina_burn_interval_seconds=1,lina_burn_damage_flags=0x8400,lina_burn_modifier_multiple=2,lina_burn_create=code(0x180c6e9f0,162),lina_burn_interval=code(0x180c6eab0,392),cm_spent_mana_callback=code(0x180cffad0,698),cm_barrier_multiple=code(0x180c688d0,17),cm_barrier_create=code(0x180c68770,103),cm_barrier_refresh=code(0x180c68850,103))
 (out/'native-rules.json').write_text(json.dumps(native,indent=2)+'\n',encoding='utf8')
 lines=['/* Generated from current installed Valve KV, CRC/SHA receipts in private/progression. */','#ifndef DOTA_PROGRESSION_DATA_H','#define DOTA_PROGRESSION_DATA_H',
 '#define DOTA_ATTRIBUTE_MAX '+str(int(actual_attribute.get('MaxLevel',4))),
 '#define DOTA_ATTRIBUTE_REQUIRED '+str(int(actual_attribute.get('RequiredLevel',1))),
 '#define DOTA_ATTRIBUTE_GAP '+str(int(actual_attribute.get('LevelsBetweenUpgrades',2))),
 'static const unsigned char progression_attribute_value[7]={'+','.join(actual_attribute['AbilityValues']['value'].split())+'};',
 'static const DotaProgressionBase progression_bases[10]={']
 lines += [' {'+','.join(map(str,row))+'},' for row in base_rows];lines += ['};']
 for field,key in [('skill_max','max_level'),('skill_required','required_level'),('skill_gap','levels_between')]:lines.append('static const unsigned char progression_'+field+'[10][4]={'+','.join('{'+','.join(str(a[key]) for a in h['skills'])+'}' for h in heroes)+'};')
 for field in ('name','description'):
  lines.append('static const char *const progression_talent_'+field+'[10][8]={')
  lines += [' {'+','.join(esc(t['label' if field=='name' else 'description']) for t in h['talents'])+'},' for h in heroes];lines.append('};')
 for field in ('name','description'):
  lines.append('static const char *const progression_innate_'+field+'[10]={'+','.join(esc(h['innate_label' if field=='name' else 'innate_description']) for h in heroes)+'};')
 lines.append('static const DotaProgressionValue progression_values[]={')
 for r in value_rows:lines.append(' {'+esc(r['field'])+',{'+','.join(map(str,r['values']))+'},'+','.join(map(str,[r['hero_step100'],r['first_effect'],r['effect_count'],r['count']]))+'},')
 lines += ['};','static const unsigned short progression_value_ranges[10][7]={'+','.join('{'+','.join(map(str,r))+'}' for r in value_ranges)+'};','static const DotaProgressionValueEffect progression_value_effects[]={']
 lines += [' {'+','.join(map(str,r))+'},' for r in value_effects];lines.append('};')
 lines.append('#endif');header=ROOT/'game/progression_data.h';header.write_text('\n'.join(lines)+'\n',encoding='utf8')
 manifest=dict(complete=True,observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),archives=receipts,heroes=heroes,talents=talents,talent_effects=effects,query_rows=value_rows,query_effects=value_effects,query_ranges=value_ranges,attribute_bonus=actual_attribute,attribute_bonus_name=actual_attribute_name,excluded_legacy_attribute_bonus=common['attribute_bonus'],ability_base=common['ability_base'],native_rules_sha256=sha(out/'native-rules.json'),official_talent_pool_rules=json.loads((out/'official-rules.json').read_text(encoding='utf8')),official_rules_sha256=sha(out/'official-rules.json'),header_sha256=sha(header),extractor_sha256=sha(__file__),scope='Current installed selected Ability1/2/3/6, selected innate and HeroBase Ability25 special_bonus_attributes only; preserves unselected deprecated definitions as raw evidence, not additional active facets.')
 (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8');(out/'talent-contract.json').write_text(json.dumps(dict(talents=talents,effects=effects),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(complete=True,heroes=10,talents=80,bindings=len(effects),innates=[h['innate'] for h in heroes])))
if __name__=='__main__':main()
