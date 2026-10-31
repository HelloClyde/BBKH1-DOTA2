"""Small compiled original Steam rune meshes and HUD icons; no scene repack."""
from pathlib import Path
import hashlib,json,math,struct,subprocess,sys
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from source2_vpk import VpkArchive
from bake_sprites import Model,source_normals

STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
TOOL=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
OUT=ROOT/'private/baked/runes';SOURCE=ROOT/'private/source2/exports/runes'
MODELS={0:'rune_doubledamage01',1:'rune_haste01',2:'rune_illusion01',3:'rune_invisibility01',4:'rune_regeneration01',5:'rune_goldxp',6:'rune_arcane',7:'rune_water',9:'rune_shield01'}
ICONS={0:'spellicons/rune_doubledamage',1:'spellicons/rune_haste',3:'spellicons/rune_invis',4:'spellicons/rune_regen',6:'spellicons/rune_arcane',7:'emoticons/water_rune',9:'spellicons/rune_shield'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def rel(p):return Path(p).resolve().relative_to(ROOT).as_posix()
def run(args,log):
    r=subprocess.run([str(x) for x in args],capture_output=True,text=True,encoding='utf8',errors='replace');log.write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,(args,log)

class Rune(Model):
    def __init__(self,slug,path):
        self.slug=slug;self.paths=[path];self.docs=[Gltf(path)];self.body=self.docs[0];self.textures={};self.scale=100/2.54/4
        self.names={a['name']:i for i,a in enumerate(self.body.doc.get('animations',[]))}
        self.clip=next(iter(self.names),None)
    def pose(self,seconds):
        ai=self.names.get(self.clip);matrices=self.body.globals(ai,seconds);out=[]
        for original in self.body.primitives(ai,seconds):
            if any(k in original['name'].lower() for k in ('physics','collision','inspector')):continue
            p=dict(original,doc=0);q=p['points'];n=source_normals(self.body,p,matrices)
            p['points']=np.column_stack((-q[:,0],q[:,2],q[:,1]))*self.scale;p['normals']=np.column_stack((-n[:,0],n[:,2],n[:,1]));out.append(p)
        return out

def main():
    OUT.mkdir(parents=True,exist_ok=True);SOURCE.mkdir(parents=True,exist_ok=True);v=VpkArchive(STEAM/'pak01_dir.vpk');header=['#ifndef DOTA_RUNE_ART_H','#define DOTA_RUNE_ART_H','#include "../../platform/art_types.h"',
        'typedef struct {unsigned short w,h,count;short ax,ay;const unsigned char *data;} DotaRuneFrame;'];frames={};icons={};records=[];stored=0
    for kind,slug in MODELS.items():
        folder=SOURCE/slug;path=folder/(slug+'.gltf');folder.mkdir(exist_ok=True)
        entry='models/props_gameplay/'+slug+'.vmdl_c'
        if not path.exists():run([sys.executable,ROOT/'tools/source2_export.py','--name','rune_'+slug,'--entry',entry,'--output',path,'--skeleton'],folder/'export.log')
        m=Rune(slug,path);rows=[];previews=[];duration=m.body.duration(m.names[m.clip]) if m.clip else 2
        for frame in range(8):
            rgba,depth,ax,ay=m.frame(m.pose(duration*frame/8),0 if m.clip else frame*2,2)
            height,width=rgba.shape[:2];data=bytearray()
            for y,x in zip(*np.where(rgba[:,:,3]>0)):
                r,g,b,a=map(int,rgba[y,x]);color=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
                data+=struct.pack('<HHhB',int(y*width+x),color,int(depth[y,x]),a)
            assert width*height<65536
            name=f'rune_frame_{kind}_{frame}';header.append('static const unsigned char '+name+'[]={'+','.join(map(str,data))+'};')
            rows.append(f'{{{width},{height},{len(data)//7},{ax},{ay},{name}}}');stored+=len(data)
            image=Image.fromarray(rgba);image.save(OUT/f'{kind}-{frame}.png');previews.append(image)
        header.append(f'static const DotaRuneFrame rune_frames_{kind}[8]={{'+','.join(rows)+'};');frames[kind]=f'rune_frames_{kind}'
        previews[0].save(OUT/f'{kind}.gif',save_all=True,append_images=previews[1:],duration=125,loop=0,disposal=2)
        if kind in ICONS:
            icon_entry='panorama/images/'+ICONS[kind]+'_png.vtex_c';raw=folder/'icon.vtex_c';png=folder/'icon.png';raw.write_bytes(v.read(icon_entry))
            if not png.exists():run([TOOL,'-d','-i',raw,'-o',png],folder/'icon.log')
            im=Image.open(png).convert('RGBA').resize((24,24),Image.Resampling.LANCZOS)
        else:
            im=previews[0].copy();im.thumbnail((22,22),Image.Resampling.LANCZOS);canvas=Image.new('RGBA',(24,24));canvas.alpha_composite(im,((24-im.width)//2,(24-im.height)//2));im=canvas;icon_entry=None
        colors=[];alpha=[]
        for r,g,b,a in np.asarray(im,dtype=np.uint8).reshape(-1,4).tolist():colors.append(((r>>3)<<11)|((g>>2)<<5)|(b>>3));alpha.append(a)
        name=f'rune_icon_{kind}';header.append('static const unsigned short '+name+'_pixels[]={'+','.join(map(str,colors))+'};');header.append('static const unsigned char '+name+'_alpha[]={'+','.join(map(str,alpha))+'};');header.append(f'static const DotaArt {name}={{24,24,{name}_pixels,{name}_alpha}};');icons[kind]='&'+name;stored+=24*24*3;im.save(OUT/f'icon-{kind}.png')
        receipt=ROOT/'private/source2/reports'/('rune_'+slug+'-provenance.json');export=json.loads(receipt.read_text(encoding='utf8'))
        for item in export['output_files']:assert sha(folder/item['uri'])==item['sha256']
        records.append({'kind':kind,'model':v.identity(entry),'gltf':rel(path),'gltf_sha256':sha(path),'buffers':m.body.buffer_sha256,'export_receipt':{'path':rel(receipt),'sha256':sha(receipt)},'source_clip':m.clip,'duration_seconds':duration,'frames':8,'icon_source':v.identity(icon_entry) if icon_entry else 'Original mesh rendered as the UI symbol'})
    header+=['static const DotaRuneFrame *const dota_rune_frames[10]={'+','.join(frames.get(i,'0') for i in range(10))+'};','static const DotaArt *const dota_rune_icons[10]={'+','.join(icons.get(i,'0') for i in range(10))+'};','#endif','']
    target=ROOT/'private/generated/dota_rune_art.h';target.write_text('\n'.join(header),encoding='utf8')
    assert stored<90000,stored
    report={'complete':True,'stored_asset_bytes':stored,'header_sha256':sha(target),'records':records,'source_sha256':{rel(p):sha(p) for p in (Path(__file__),ROOT/'tools/bake_sprites.py',ROOT/'tools/gltf_assets.py',ROOT/'tools/source2_export.py',ROOT/'tools/source2_vpk.py',ROOT/'tools/bake_sprite_raster.py',ROOT/'tools/bake_sprite_raster.c')},'scope':'Original Steam rune meshes, native Source/4 scale and source animation if present; otherwise an offline turntable. Eight fixed-camera depth sprites per type and original HUD textures. No runtime mesh or new external pack.'}
    (OUT/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({'complete':True,'stored_asset_bytes':stored,'models':len(MODELS),'header_sha256':sha(target)}),flush=True)
if __name__=='__main__':main()
