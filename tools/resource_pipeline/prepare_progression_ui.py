"""Append original selected-innate and shard icons to the v0.22 UIR table."""
from pathlib import Path
import json,shutil,struct
import bake_extra_heroes as original
from source2_vpk import VpkArchive
from prepare_item_assets import pixels
ROOT=Path(__file__).resolve().parents[1]
ENTRIES={0:{5:'axe_one_man_army'},1:{5:'lina_slow_burn'},2:{5:'juggernaut_bladeform'},3:{5:'crystal_maiden_glacial_guard'},4:{4:'slark_depth_shroud',5:'slark_essence_shift'},5:{4:'necrolyte_death_seeker',5:'necrolyte_sadist'}}
def main():
 source=ROOT/'private/items/assets-v022';dest=ROOT/'private/items/assets-v023';dest.mkdir(exist_ok=True)
 base=json.loads((source/'manifest.json').read_text(encoding='utf8'));v=VpkArchive(original.STEAM/'pak01_dir.vpk')
 original.OUT=ROOT/'private/baked/roster-heroes';rows={};raw=(source/'heroes.uir').read_bytes();h=struct.unpack_from('<8I',raw)
 for i in range(h[3]):
  key,w,height,offset,size=struct.unpack_from('<IHHII',raw,h[4]+i*16);assert w==height==24 and size==1728;rows[key]=raw[offset:offset+size]
 receipts=[]
 for hero,entries in ENTRIES.items():
  for skill,name in entries.items():
   entry=f'panorama/images/spellicons/{name}_png.vtex_c';fallback=False
   if entry not in v.entries:
    assert name in ('axe_one_man_army','lina_slow_burn','juggernaut_bladeform')
    entry='panorama/images/hud/facets/innate_icon_large_png.vtex_c';fallback=True
   paths,receipt=original.decode_texture(v,'ui-'+name,entry);key=0x20000|(hero<<8)|skill;assert key not in rows
   rows[key]=pixels(paths[0]);receipts.append(dict(key=key,hero=hero,skill=skill,name=name,source=receipt,original_generic_innate_icon=fallback,fallback_reason='Selected ability has no standalone compiled icon in installed VPK; use original innate HUD icon' if fallback else None,png_sha256=original.sha(paths[0])))
 table=[];data=bytearray();start=32+16*len(rows)
 for key,payload in sorted(rows.items()):table.append(struct.pack('<IHHII',key,24,24,start+len(data),len(payload)));data.extend(payload)
 blob=struct.pack('<8I',0x31524955,1,start+len(data),len(rows),32,16,start,0)+b''.join(table)+data
 for name in ('catalog.hit','catalog.fnt'):shutil.copy2(source/name,dest/name)
 (dest/'heroes.uir').write_bytes(blob);base.update(ui_icons=len(rows),base_provider_manifest_sha256=original.sha(source/'manifest.json'),progression_ui_source=receipts,progression_ui_generator_sha256=original.sha(__file__))
 base['files']={name:dict(bytes=(dest/name).stat().st_size,sha256=original.sha(dest/name)) for name in ('catalog.hit','heroes.uir','catalog.fnt')}
 (dest/'manifest.json').write_text(json.dumps(base,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,icons=len(rows),bytes=len(blob))))
if __name__=='__main__':main()
