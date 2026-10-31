"""Resolve installed NPC/hero sight inheritance into a small numeric header."""
from pathlib import Path
import hashlib,json
from gltf_assets import ROOT
from source2_vpk import VpkArchive
from bake_extra_heroes import kvparse
def main():
 v=VpkArchive(Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk'));entry='scripts/npc/npc_units.txt';units=kvparse(v.read(entry).decode('utf-8-sig'))['DOTAUnits'];unit_base=units['npc_dota_units_base'];he='scripts/npc/heroes/npc_dota_hero_base.txt';hero_doc=kvparse(v.read(he).decode('utf-8-sig'))['DOTAHeroes'];hero_base=hero_doc['npc_dota_hero_base'];receipts=[v.identity(entry),v.identity(he)];rows={}
 def resolved(name,seen=None):
  seen=set() if seen is None else seen
  if name in seen:raise ValueError('NPC inheritance cycle')
  seen.add(name);d=units[name];out=dict(unit_base)
  if d.get('include_keys_from'):out.update(resolved(d['include_keys_from'],seen))
  out.update(d);return out
 def pair(d):return [int(float(d[k])) for k in ('VisionDaytimeRange','VisionNighttimeRange')]
 heroes=[]
 for name in ['axe','lina','juggernaut','crystal_maiden','slark','necrolyte','pudge','sniper','sven','tidehunter']:
  e='scripts/npc/heroes/npc_dota_hero_'+name+'.txt';d=dict(unit_base);d.update(hero_base);d.update(kvparse(v.read(e).decode('utf-8-sig'))['DOTAHeroes']['npc_dota_hero_'+name]);heroes.append(pair(d));rows[name]=pair(d);receipts.append(v.identity(e))
 wanted={'tower':'npc_dota_goodguys_tower1_top','base':'npc_dota_goodguys_fort','melee_creep':'npc_dota_creep_goodguys_melee','ranged_creep':'npc_dota_creep_goodguys_ranged','satyr':'npc_dota_neutral_satyr_hellcaller','centaur':'npc_dota_neutral_centaur_khan','observer':'npc_dota_observer_wards','sentry':'npc_dota_sentry_wards','necro_warrior':'npc_dota_necronomicon_warrior_3','necro_archer':'npc_dota_necronomicon_archer_3','fountain':'dota_fountain','melee_barracks':'npc_dota_goodguys_melee_rax_top','ranged_barracks':'npc_dota_goodguys_range_rax_top','roshan':'npc_dota_roshan','watcher':'npc_dota_lantern'}
 lines=['/* Original Steam NPC sight; inherited from unit/hero bases, no guessed defaults. */','#ifndef H1_DOTA_UNIT_VISION_DATA_H','#define H1_DOTA_UNIT_VISION_DATA_H','static const unsigned short unit_hero_vision[10][2]={'+','.join('{%d,%d}'%tuple(p) for p in heroes)+'};','static const unsigned short unit_default_vision[2]={%d,%d};'%tuple(pair(unit_base))]
 for key,name in wanted.items():
  if name not in units:raise KeyError(name)
  rows[name]=pair(resolved(name));lines.append('static const unsigned short unit_'+key+'_vision[2]={%d,%d};'%tuple(rows[name]))
 lines+=['#endif',''];p=ROOT/'game/unit_vision_data.h';p.write_text('\n'.join(lines),encoding='ascii');report=dict(complete=True,units=rows,source_identities=receipts,inheritance='NPC unit default, include_keys_from recursively; hero also inherits npc_dota_hero_base before leaf.',header_sha256=hashlib.sha256(p.read_bytes()).hexdigest());out=ROOT/'private/items/special-source';out.mkdir(parents=True,exist_ok=True);(out/'unit-vision-data.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(rows))
if __name__=='__main__':main()
