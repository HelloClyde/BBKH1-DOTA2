"""Small read-only primary-source tree-rule receipt, no binary redistribution."""
from pathlib import Path
import hashlib,json,re,struct
import pefile
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 dll=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/bin/win64/server.dll');raw=dll.read_bytes();assert sha(raw)=='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834';pe=pefile.PE(data=raw,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase
 def at(va,n):return raw[pe.get_offset_from_rva(va-base):pe.get_offset_from_rva(va-base)+n]
 snippets={}
 for name,va,n in [('cvar_load',0x180273cba,15),('default_override',0x18177ab24,11),('getter',0x1817d8bc0,29)]:snippets[name]={'va':hex(va),'bytes':at(va,n).hex()}
 assert at(0x18177ab24,11).hex()=='41c78678040000000080bf'
 assert struct.unpack('<f',at(0x182ec3fb0,4))[0]==180.0
 assert at(0x183b73818,22).startswith(b'dota_tree_regrow_time\0')
 src=ROOT.parent/'h1-dota2/private/items/current-source';itemraw=(src/'items.txt').read_bytes();text=itemraw.decode('utf-8-sig');items={}
 for name in ['item_tango','item_quelling_blade','item_bfury','item_branches']:
  m=re.search(r'"'+name+r'"\s*\{',text);assert m;start=m.end()-1;depth=1;end=start+1
  while depth:
   if text[end]=='{':depth+=1
   elif text[end]=='}':depth-=1
   end+=1
  block=text[start:end];items[name]={'block_sha256_utf8':sha(block.encode()),'fields':{k:v for k,v in re.findall(r'"([A-Za-z_]+)"\s+"([^"\r\n]*)"',block) if k in ['AbilityCastRange','AbilityCooldown','health_regen','buff_duration','tree_duration']}}
 assert items['item_tango']['fields']['health_regen']=='7.0' and items['item_tango']['fields']['buff_duration']=='16.0'
 ability=(src/'abilities_english.txt').read_bytes();assert b'Light Strike Array will destroy trees in its area of effect.' in ability
 turbo=ROOT.parent/'h1-dota2/private/turbo/source-rules/native-rules.json';out=ROOT/'provenance/tree-gameplay';out.mkdir(parents=True,exist_ok=True)
 report={'primary_source':'Read-only local installed Steam Dota2 server.dll and original item/localisation text','server_sha256':sha(raw),'server_bytes':len(raw),'native_snippets':snippets,'native_default_regrow_seconds':180,'native_default_constant':{'va':'0x182ec3fb0','bytes':at(0x182ec3fb0,4).hex()},'reasoning':'Rules constructor sets override +0x478 to -1. Getter returns nonnegative override, otherwise the dota_tree_regrow_time ConVar. Its registration default is 180. Generic MapInfo TreeRegrowTime parser has a separate 120-second fallback; it is not used as the standard default here. Audited Turbo initializer does not write +0x478.','turbo_audit_sha256':sha(turbo.read_bytes()),'items_text_sha256':sha(itemraw),'item_fields':items,'localisation_sha256':sha(ability),'implemented_skill_tree_action':'Lina Light Strike Array destroys trees in its actual area. No unverified tree interactions were invented for other skills.','geometry':{'tree_count':2113,'id_order':'provenance/vision/trees.json order, not sorted','sha256':sha((ROOT/'provenance/vision/trees.json').read_bytes()),'tree_radius_world':16,'navigation_extra_unit_clearance_world':6},'scope_limits':'Native timer/default and item values are verified; the fixed-grid H1 navigation/LOS and tree regrowth placement are not claimed to reproduce every continuous Source2 hull interaction.'}
 (out/'source-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({'passed':True,'report':str(out/'source-audit.json')}))
 slark=ROOT.parent/'h1-dota2/private/extra-heroes/source/slark.txt';slark_bytes=slark.read_bytes();assert re.search(rb'"pounce_radius"\s*\{\s*"value"\s*"120"',slark_bytes)
 assert at(0x18133d1a6,6).hex()=='8986e41a0000'
 assert at(0x18133dfe0,9).hex()=='66410f6eb6e41a0000'
 assert at(0x18133e021,5).hex()=='e84ad4ca00'
 chunks={name:{'va':hex(va),'bytes':at(va,count).hex()}for name,va,count in [('read_pounce_radius',0x18133d189,35),('landing_radius_and_destroy',0x18133dfe0,70),('vscript_DestroyTreesAroundPoint_registration',0x181fcc6e7,158),('destroy_helper_shared_tree_collection',0x181feb4e0,141),('vscript_tree_collection_and_destroy',0x1820156b4,100)]}
 pounce={'passed':True,'server_sha256':sha(raw),'hero_source_sha256':sha(slark_bytes),'localisation_sha256':sha(ability),'native_class':'CDOTA_Modifier_Slark_Pounce','rtti_primary_vtable':'0x183709cf0','field':'0x1ae4','installed_parameter':'pounce_radius','source_units':120,'world_units':30,'native_evidence':chunks,'reasoning':'The native initializer reads pounce_radius into integer field+1ae4. Landing converts that field to float and passes it as xmm2 to181feb470. This helper calls181ff10c0 and destroys each returned tree through vtable+0x50. The registered GridNav.DestroyTreesAroundPoint method182015650 uses the same collector and tree destruction slot. The original tooltip explicitly describes landing tree destruction. H1 performs this before nearest-point resolution at both natural landing and early hero collision.','tested_host_report':'build/tests/trees-pounce-final-1/verification.json','no_other_skills_changed':True}
 (out/'pounce-source-audit.json').write_text(json.dumps(pounce,indent=2)+'\n',encoding='utf-8')
 report['implemented_skill_tree_action']='Lina Light Strike Array cuts its actual AoE; Slark Pounce clears the original120-Source landing circle on natural expiry or hero impact.'
 report['pounce_audit_sha256']=sha((out/'pounce-source-audit.json').read_bytes());(out/'source-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
