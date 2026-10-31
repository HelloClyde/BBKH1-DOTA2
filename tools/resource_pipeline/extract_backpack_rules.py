"""Seal current installed item-specific backpack rules and Valve's base rules."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from source2_vpk import VpkArchive
from extract_items import kv
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 out=ROOT/'private/backpack-v023';out.mkdir(parents=True,exist_ok=True)
 v=VpkArchive(Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk'));entry='scripts/npc/items.txt';data=v.read(entry);doc=kv.parse(data.decode('utf-8-sig'))['DOTAAbilities']
 catalog=json.loads((ROOT/'private/items/current-source/catalog.json').read_text(encoding='utf8'))
 rules=[];forbidden=[];frozen=[]
 for item in catalog['items']:
  name=item['source_name'];d=doc[name];rule={k:d[k] for k in ('AllowedInBackpack','CooldownPausedOutOfInventory','CanSkipBackpackCooldown','BackpackCooldownOverride') if k in d}
  if rule:rules.append(dict(id=item['id'],name=name,definition=rule))
  if d.get('AllowedInBackpack','1')=='0':forbidden.append(item['id'])
  if d.get('CooldownPausedOutOfInventory','0')=='1':frozen.append(item['id'])
 assert forbidden==[43,130,398] and frozen==[110,400]
 h=ROOT/'game/backpack_data.h';h.write_text('/* Generated from current CRC-checked Steam items.txt. */\n#ifndef DOTA_BACKPACK_DATA_H\n#define DOTA_BACKPACK_DATA_H\nstatic int backpack_allowed(int id){return '+''.join(f'id!={x}&&' for x in forbidden)+'id>=0;}\nstatic int backpack_cd_paused(int id){return '+ '||'.join(f'id=={x}' for x in frozen)+';}\n#endif\n',encoding='utf8')
 report=dict(complete=True,source=v.identity(entry),catalog_sha256=sha(ROOT/'private/items/current-source/catalog.json'),item_rules=rules,
   confirmed=dict(main_slots=6,backpack_slots=3,return_inactivity_seconds=6,ordinary_cooldown_rate_percent=50,first_entry_grace_seconds=3,grace_once_per_item_per_hero=True),
   official_primary_sources=[dict(url='https://www.dota2.com/700/gameplay/',scope='3 additional slots, inactive passives/actives,6second return penalty, half cooldown rate, nearby-shop exception; old item exclusion list superseded by installed current definitions'),dict(url='https://www.dota2.com/datafeed/patchnotes?version=7.38&language=english',scope='Major Mechanics: first backpack entry has3second no-cooldown return grace, once per item per hero')],
   h1_adaptations=['Nearby-shop exception reuses existing300world-unit proximity to own fountain or original secret merchants; Turbo buy-anywhere does not waive inactivity.','Items that cannot enter backpack are never forced into it by auto-combination/delivery; an impossible transaction fails atomically and remains in courier cargo.'],
   generated_header_sha256=sha(h),source_sha256={'tools/extract_backpack_rules.py':sha(__file__)})
 (out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,forbidden=forbidden,paused=frozen)))
if __name__=='__main__':main()
