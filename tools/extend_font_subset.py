"""Append missing UI glyphs from local fonts; preserve existing glyph bytes."""
from pathlib import Path
import argparse,hashlib,json,re
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cjk-font',type=Path,default=Path('C:/Windows/Fonts/msyh.ttc'));p.add_argument('--ascii-font',type=Path,default=Path('C:/Windows/Fonts/consola.ttf'));a=p.parse_args()
 dest=ROOT/'private/generated/dota_font.h';old=dest.read_text(encoding='utf8');old_sha=sha(dest)
 pattern=re.compile(r'^    \{(\d+),[^\n]+\},$',re.M);records={int(m[1]):m[0]for m in pattern.finditer(old)};assert len(records)>=646
 sources=[ROOT/x for x in ['platform/render.c','platform/status_effects.c','platform/extra_ability.h','game/progression_data.h']]
 needed=set(range(32,127))|{ord(c)for s in sources for c in s.read_text(encoding='utf8')if ord(c)>127};missing=sorted(needed-records.keys())
 chinese=ImageFont.truetype(str(a.cjk_font),12);latin=ImageFont.truetype(str(a.ascii_font),12)
 for code in missing:
  width=7 if code<128 else 12;canvas=Image.new('L',(width,14));ImageDraw.Draw(canvas).text((0,1 if code<128 else -2),chr(code),font=latin if code<128 else chinese,fill=255)
  rows=[sum(1<<x for x in range(width)if canvas.getpixel((x,y))>=90)for y in range(14)]
  records[code]='    {%d,%d,{%s}},'%(code,width,','.join('0x%x'%x for x in rows))
 matches=list(pattern.finditer(old));out=old[:matches[0].start()]+'\n'.join(records[x]for x in sorted(records))+old[matches[-1].end():];dest.write_bytes(out.encode('utf8'))
 report=dict(glyphs=len(records),added=missing,added_text=''.join(map(chr,missing)),existing_glyph_records_unchanged=True,old_header_sha256=old_sha,header_sha256=sha(dest),glyph_height=14,source_sha256={s.relative_to(ROOT).as_posix():sha(s)for s in sources},font_files_redistributed=False,local_font_sources=[dict(name=x.name,sha256=sha(x))for x in [a.cjk_font,a.ascii_font]],ownership='Generated bitmap subset from locally installed Microsoft fonts; no new font license is asserted.')
 folder=ROOT/'docs/provenance';folder.mkdir(parents=True,exist_ok=True);(folder/'fonts-v0.29.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf8'));print(json.dumps(report,ensure_ascii=False))
if __name__=='__main__':main()
