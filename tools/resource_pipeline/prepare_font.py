"""Bake only the renderer's glyphs from Windows fonts into a small C bitmap.

Build-time Pillow only; the H1 runtime has no font or image decoding dependency.
Microsoft YaHei font files are not copied or redistributed by this tool.
"""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'platform/render.c')
    parser.add_argument('--output',type=Path,default=ROOT/'private/generated/dota_font.h')
    parser.add_argument('--cjk-font',type=Path,default=Path('C:/Windows/Fonts/msyh.ttc'))
    parser.add_argument('--ascii-font',type=Path,default=Path('C:/Windows/Fonts/consola.ttf'))
    args=parser.parse_args()
    chinese=ImageFont.truetype(str(args.cjk_font),12)
    ascii_font=ImageFont.truetype(str(args.ascii_font),12)
    font_sources=[args.source]
    status_source=ROOT/'platform/status_effects.c'
    if args.source.resolve()==(ROOT/'platform/render.c').resolve() and status_source.exists():font_sources.append(status_source)
    if args.source.resolve()==(ROOT/'platform/render.c').resolve():font_sources.append(ROOT/'platform/extra_ability.h')
    if args.source.resolve()==(ROOT/'platform/render.c').resolve() and (ROOT/'game/progression_data.h').exists():font_sources.append(ROOT/'game/progression_data.h')
    source_text='\n'.join(p.read_text(encoding='utf8') for p in font_sources)
    codes=sorted(set(range(32,127))|{ord(c) for c in source_text if ord(c)>127})
    lines=['/* Generated subset of local Windows font glyphs for H1, 14 rows, LSB is left. */',
           '#ifndef H1_DOTA_FONT_H','#define H1_DOTA_FONT_H',
           '#ifndef H1_DOTA_GLYPH_TYPE_DEFINED','#define H1_DOTA_GLYPH_TYPE_DEFINED',
           'typedef struct DotaGlyph {unsigned short code;unsigned char width;unsigned short rows[14];} DotaGlyph;','#endif',
           'static const DotaGlyph dota_glyphs[] = {']
    for code in codes:
        width=7 if code<128 else 12
        canvas=Image.new('L',(width,14))
        # Consolas starts close to its origin; the CJK font needs its own
        # baseline offset. Sharing -2 clipped the top of ASCII digits/letters.
        offset_y=1 if code<128 else -2
        ImageDraw.Draw(canvas).text((0,offset_y),chr(code),font=ascii_font if code<128 else chinese,fill=255)
        rows=[]
        for y in range(14):rows.append(sum((1<<x) for x in range(width) if canvas.getpixel((x,y))>=90))
        lines.append('    {%d,%d,{%s}},'%(code,width,','.join('0x%x'%r for r in rows)))
    lines.extend(['};','static const DotaGlyph *dota_font_find(unsigned code) {',
                  '    int low=0,high=(int)(sizeof(dota_glyphs)/sizeof(dota_glyphs[0]))-1;',
                  '    while(low<=high){int mid=(low+high)/2;unsigned c=dota_glyphs[mid].code;',
                  '        if(c==code)return &dota_glyphs[mid];',
                  '        if(c<code)low=mid+1;else high=mid-1;}',
                  '    return (const DotaGlyph *)0;','}','#endif',''])
    output='\n'.join(lines).encode('utf8');args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(output)
    report={'glyphs':len(codes),'glyph_height':14,'ascii_advance':7,'chinese_advance':12,'header_sha256':hashlib.sha256(output).hexdigest(),
            'cjk_font':str(args.cjk_font),'ascii_font':str(args.ascii_font),'ascii_offset_y':1,'cjk_offset_y':-2,
            'source':str(args.source),'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in font_sources},'font_files_redistributed':False}
    dest=ROOT/'private/reports/font-provenance.json';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':main()
