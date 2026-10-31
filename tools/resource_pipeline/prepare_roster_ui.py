"""Append original portrait/ability textures for hero IDs6..9 to UIR1."""
from pathlib import Path
import json,shutil,struct
import bake_extra_heroes as original
from source2_vpk import VpkArchive
from prepare_item_assets import pixels
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'private/baked/roster-heroes';DEST=ROOT/'private/items/assets-v022'
SPECS={6:('pudge',['pudge_meat_hook','pudge_rot','pudge_flesh_heap','pudge_dismember']),7:('sniper',['sniper_shrapnel','sniper_headshot','sniper_take_aim','sniper_assassinate']),8:('sven',['sven_storm_bolt','sven_great_cleave','sven_warcry','sven_gods_strength']),9:('tidehunter',['tidehunter_gush','tidehunter_kraken_shell','tidehunter_anchor_smash','tidehunter_ravage'])}
EXTRAS={6:{5:'pudge_innate_graft_flesh'},7:{4:'sniper_concussive_grenade',5:'sniper_headshot'},8:{5:'sven_wrath_of_god'},9:{4:'tidehunter_dead_in_the_water',5:'tidehunter_leviathans_catch'}}
def main():
 DEST.mkdir(exist_ok=True);v=VpkArchive(original.STEAM/'pak01_dir.vpk');original.OUT=OUT
 old=ROOT/'private/items/assets';base=json.loads((old/'manifest.json').read_text(encoding='utf8'))
 for name in ('catalog.hit','catalog.fnt'):shutil.copy2(old/name,DEST/name)
 raw=(old/'heroes.uir').read_bytes();h=struct.unpack_from('<8I',raw);rows={}
 for i in range(h[3]):
  key,w,height,at,size=struct.unpack_from('<IHHII',raw,h[4]+i*16);assert w==height==24 and size==1728;rows[key]=raw[at:at+size]
 sources=[]
 for hero,(slug,skills) in SPECS.items():
  entries=[(0x10000|hero,slug,f'panorama/images/heroes/icons/npc_dota_hero_{slug}_png.vtex_c')]+[(0x20000|(hero<<8)|skill,name,f'panorama/images/spellicons/{name}_png.vtex_c') for skill,name in enumerate(skills)]+[(0x20000|(hero<<8)|skill,name,f'panorama/images/spellicons/{name}_png.vtex_c') for skill,name in EXTRAS[hero].items()]
  for key,name,entry in entries:
   paths,receipt=original.decode_texture(v,'ui-'+name,entry);rows[key]=pixels(paths[0]);sources.append(dict(key=key,name=name,source=receipt,png_sha256=original.sha(paths[0])))
 table=[];data=bytearray();start=32+16*len(rows)
 for key,payload in sorted(rows.items()):table.append(struct.pack('<IHHII',key,24,24,start+len(data),len(payload)));data.extend(payload)
 blob=struct.pack('<8I',0x31524955,1,start+len(data),len(rows),32,16,start,0)+b''.join(table)+data
 (DEST/'heroes.uir').write_bytes(blob)
 base.update(ui_icons=len(rows),original_provider_manifest_sha256=original.sha(old/'manifest.json'),roster_ui_source=sources,roster_ui_generator_sha256=original.sha(__file__))
 base['files']={name:dict(bytes=(DEST/name).stat().st_size,sha256=original.sha(DEST/name)) for name in ('catalog.hit','heroes.uir','catalog.fnt')}
 (DEST/'manifest.json').write_text(json.dumps(base,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,ui_icons=len(rows),ui_bytes=len(blob))))
if __name__=='__main__':main()
