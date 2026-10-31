"""Build external HIT1/UIR1/FNT1 assets using original Steam UI PNGs.

No 3D icon renders. Recipes use Valve's original shared recipe scroll icon.
Only selected compiled textures are decoded in one bounded VRF folder batch.
"""
from pathlib import Path
import concurrent.futures,hashlib,html,json,re,struct,subprocess
from PIL import Image,ImageFont,ImageDraw
from gltf_assets import np,ROOT
from source2_vpk import VpkArchive
from bake_extra_heroes import kvparse
SOURCE=ROOT/'private/items/current-source'
OUT=ROOT/'private/items/assets'
TOOL=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
ARCHIVE=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def pixels(path):
 im=np.asarray(Image.open(path).convert('RGBA').resize((24,24),Image.Resampling.LANCZOS));a=im[:,:,:3].astype(np.uint16)
 colour=(((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)).astype('<u2')
 return colour.tobytes()+im[:,:,3].tobytes()
def expanded_description(item):
 values={};definition=item['definition'];rank=max(0,int(item['level'])-1)
 for key,value in {**definition,**definition.get('AbilityValues',{})}.items():
  if isinstance(value,dict):value=value.get('value','')
  if not isinstance(value,(str,int,float)):continue
  numbers=str(value).split()
  try:parsed=[float(n) for n in numbers]
  except ValueError:continue
  if parsed:values[key.casefold()]=format(parsed[min(rank,len(parsed)-1)],'g')
 text=item['description'].replace('\\n','\n');text=re.sub(r'<br\s*/?>','\n',text,flags=re.I);text=html.unescape(re.sub('<[^>]*>','',text))
 text=re.sub(r'%([A-Za-z_][A-Za-z0-9_]*)%',lambda m:values.get(m[1].casefold(),m[0]),text);text=re.sub(r'%{2,}','%',text)
 missing=re.findall(r'%[A-Za-z_][A-Za-z0-9_]*%',text)
 if missing:raise ValueError('Unexpanded Source item tooltip '+item['name']+' '+str(missing))
 return text
def main():
 OUT.mkdir(parents=True,exist_ok=True);rawfolder=OUT/'source-textures';pngfolder=OUT/'png';rawfolder.mkdir(exist_ok=True);pngfolder.mkdir(exist_ok=True)
 catalogue=read(SOURCE/'catalog.json');v=VpkArchive(ARCHIVE);chosen={};sources={};names={}
 localizations={lang:{k.lower():value for k,value in kvparse((SOURCE/('abilities_'+lang+'.txt')).read_text(encoding='utf-8-sig'))['lang']['Tokens'].items()} for lang in ('schinese','english')}
 for item in catalogue['items']:
  key='dota_tooltip_ability_'+item['source_name'];resolved=[]
  for lang,fallback in [('schinese',item['chinese']),('english',item['english'])]:
   value=localizations[lang].get(key+':n',localizations[lang].get(key,fallback))
   if '#' in item['name'] and lang=='schinese' and '阶）' not in value:value+='（'+str(item['tier'])+'阶）'
   resolved.append(value)
  names[item['id']]=resolved
 for item in catalogue['items']:
  entry='panorama/images/items/'+item['texture']+'_png.vtex_c'
  if entry not in v.entries:
   if not item['flags']&2:raise ValueError('Original icon missing '+entry)
   entry='panorama/images/items/recipe_png.vtex_c'
  key=Path(entry).name.removesuffix('.vtex_c');chosen[item['id']]=key
  if key not in sources:
   raw=rawfolder/(key+'.vtex_c');source=v.read(entry)
   if not raw.exists():raw.write_bytes(source)
   elif raw.read_bytes()!=source:raise ValueError('Installed original icon changed '+entry)
   sources[key]=v.identity(entry)
 missing=[key for key in sources if not (pngfolder/(key+'.png')).exists()]
 if missing:
  process=subprocess.run([str(TOOL),'-d','-i',str(rawfolder),'-o',str(pngfolder),'--threads','3'],capture_output=True,text=True,encoding='utf8',errors='replace')
  (OUT/'texture-export.log').write_text(process.stdout+process.stderr,encoding='utf8')
  if process.returncode:raise RuntimeError('Original Steam icon export failed')
 for key in sources:
  if not (pngfolder/(key+'.png')).exists():raise FileNotFoundError(pngfolder/(key+'.png'))
 original=bytearray((SOURCE/'catalog.hit').read_bytes());header=list(struct.unpack_from('<16I',original));assert header[0]==0x31544948 and header[5]==96
 # Rebuild only the external text area, retaining every binary rule/recipe row.
 texts=bytearray();text_index=[];expanded={}
 for item in catalogue['items']:
  expanded[item['id']]=expanded_description(item);offsets=[]
  for value in (names[item['id']][0],expanded[item['id']],item['name'],names[item['id']][1]):offsets.append(len(texts));texts.extend(value.encode('utf8')+b'\0')
  text_index.append(struct.pack('<4I',*offsets))
 original=original[:header[10]]+bytearray(b''.join(text_index))+texts+bytearray(header[3]*8);header[11]=header[10]+header[3]*16;header[12]=header[11]+len(texts);header[13]=header[12]+header[3]*8
 icondata=bytearray();index=[];dedup={};iconrows=[]
 for item in catalogue['items']:
  key=chosen[item['id']]
  if key not in dedup:dedup[key]=len(icondata);icondata.extend(pixels(pngfolder/(key+'.png')))
  off=dedup[key];index.append(struct.pack('<2I',off,1728));struct.pack_into('<H',original,header[4]+item['id']*96+90,item['id'])
  iconrows.append(dict(item=item['id'],source_texture=key,icon_relative_offset=off,bytes=1728))
 original[header[12]:header[13]]=b''.join(index);original.extend(icondata);header[2]=len(original);struct.pack_into('<16I',original,0,*header);(OUT/'catalog.hit').write_bytes(original)
 # Ten external hero/ability icons, including the actual Saltwater Shiv icon.
 extras=ROOT/'private/baked/extra-heroes';ui=read(extras/'ui-manifest.json');uirecords=[];uidata=bytearray();hids={'slark':4,'necrolyte':5}
 abilities=['slark_dark_pact','slark_pounce','slark_saltwater_shiv','slark_shadow_dance','necrolyte_death_pulse','necrolyte_ghost_shroud','necrolyte_heartstopper_aura','necrolyte_reapers_scythe']
 for icon in ui['icons']:
  name=icon['name'];key=(0x10000|hids[name]) if name in hids else (0x20000|((4+abilities.index(name)//4)<<8)|(abilities.index(name)%4))
  uirecords.append((key,24,24,len(uidata),1728));uidata.extend(pixels(ROOT/icon['path']))
 uirecords.sort();uibase=32+len(uirecords)*16
 uiblob=struct.pack('<8I',0x31524955,1,uibase+len(uidata),len(uirecords),32,16,uibase,0)+b''.join(struct.pack('<IHHII',k,w,h,uibase+o,n) for k,w,h,o,n in uirecords)+uidata
 (OUT/'heroes.uir').write_bytes(uiblob)
 # Exactly the existing 14-row bitmap layout, with a bounded runtime glyph cache.
 texts=''.join(names[item['id']][0]+names[item['id']][1]+expanded[item['id']]+item['name'] for item in catalogue['items'])
 texts+='斯拉克瘟疫法师鱼人夜行者死神镰刀死亡脉冲幽魂护罩竭心光环黑暗契约突袭海水短匕暗影之舞深海迷雾死亡搜寻阿哈利姆神杖魔晶技能'
 codes=sorted(set(range(32,127))|{ord(c) for c in texts if 127<ord(c)<=65535})
 fonts=[Path('C:/Windows/Fonts/msyh.ttc'),Path('C:/Windows/Fonts/consola.ttf')];cjk=ImageFont.truetype(str(fonts[0]),12);ascii_font=ImageFont.truetype(str(fonts[1]),12);glyphs=[]
 for code in codes:
  width=7 if code<128 else 12;im=Image.new('L',(width,14));ImageDraw.Draw(im).text((0,1 if code<128 else -2),chr(code),font=ascii_font if code<128 else cjk,fill=255)
  rows=[sum((1<<x) for x in range(width) if im.getpixel((x,y))>=90) for y in range(14)];glyphs.append(struct.pack('<HBB14H',code,width,0,*rows))
 fontblob=struct.pack('<8I',0x31544e46,1,32+len(glyphs)*32,len(glyphs),32,32,14,0)+b''.join(glyphs);(OUT/'catalog.fnt').write_bytes(fontblob)
 report=dict(complete=True,items=len(chosen),unique_original_icons=len(sources),recipes_use_original_shared_scroll=True,source_nominative_name_tokens_resolved=True,tooltip_source_tokens_expanded=True,tooltip_literal_percent_normalized=True,glyphs=len(codes),glyph_height=14,glyph_record_bytes=32,static_font_unchanged=True,
  files={n:dict(bytes=(OUT/n).stat().st_size,sha256=sha(OUT/n)) for n in ['catalog.hit','heroes.uir','catalog.fnt']},original_catalog_sha256=sha(SOURCE/'catalog.hit'),source_catalog_sha256=sha(SOURCE/'catalog.json'),
  original_icons=[dict(key=k,source=s,png_sha256=sha(pngfolder/(k+'.png'))) for k,s in sorted(sources.items())],item_icons=iconrows,font_sources=[dict(path=str(p),sha256=sha(p)) for p in fonts],font_files_redistributed=False,
  tool_sha256=sha(TOOL),generator_sha256=sha(__file__),ui_manifest_sha256=sha(extras/'ui-manifest.json'))
 (OUT/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({k:report[k] for k in ['complete','items','unique_original_icons','glyphs','files']}))
if __name__=='__main__':main()
