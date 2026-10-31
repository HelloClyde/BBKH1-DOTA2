"""Reduce large persistent FX to fit H1's bounded sprite working set.

RGB/alpha are resolved together in premultiplied space. Anchors and relative
depth use the same half-world canonical scale; the renderer restores the
gameplay radius from frame.height_world, without a new runtime image format.
"""
from pathlib import Path
import argparse,hashlib,json
from PIL import Image,ImageDraw
from gltf_assets import np,ROOT

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def half(rgba,depth,ax,ay):
    top=ay&1;left=ax&1
    rgba=np.pad(rgba,((top,0),(left,0),(0,0)));depth=np.pad(depth,((top,0),(left,0)))
    ax+=left;ay+=top
    h,w=rgba.shape[:2];bottom=h&1;right=w&1
    rgba=np.pad(rgba,((0,bottom),(0,right),(0,0)));depth=np.pad(depth,((0,bottom),(0,right)))
    h,w=rgba.shape[:2]
    alpha=rgba[:,:,3]>0
    front=np.where(alpha,depth.astype(np.int32),32767).reshape(h//2,2,w//2,2).min((1,3))
    image=Image.fromarray(rgba).convert('RGBa').resize((w//2,h//2),Image.Resampling.BOX).convert('RGBA')
    output=np.asarray(image).copy();z=np.where(output[:,:,3]>0,np.rint(front/2),0).clip(-32768,32767).astype('<i2')
    return output,z,ax//2,ay//2

def compact(path):
    path=Path(path);m=json.loads(path.read_text(encoding='utf8'));report=[]
    for skill in m['skills']:
        rows=[r for r in m['frames'] if r['model']==skill['model']]
        original=skill.get('original_reference_radius_world',skill['reference_radius_world'])
        sizes=[r['width']*r['height']*5 for r in rows]
        loops=[r for r in rows if r['clip']==1 and r['direction']==0]
        loop_bytes=sum(r['width']*r['height']*5 for r in loops)
        before=max(sizes);scale=skill.get('raster_scale_q8',256)
        if loops and scale==256 and (before>110*1024 or loop_bytes>640*1024):
            for r in rows:
                file=ROOT/r['npz']
                with np.load(file) as f:rgba=f['rgba'];depth=f['depth']
                rgba,depth,ax,ay=half(rgba,depth,r['anchor_x'],r['anchor_y'])
                np.savez_compressed(file,rgba=rgba,depth=depth)
                r.update(width=rgba.shape[1],height=rgba.shape[0],anchor_x=ax,anchor_y=ay,height_world=original//2,sha256=sha(file),raster_scale_q8=128)
            scale=128
        skill.update(original_reference_radius_world=original,baked_reference_radius_world=original*scale//256,
                     reference_radius_world=original*scale//256,raster_scale_q8=scale)
        loop_bytes=sum(r['width']*r['height']*5 for r in loops)
        assert loop_bytes<=640*1024,(skill['name'],loop_bytes)
        report.append({'model':skill['model'],'skill':skill['name'],'raster_scale_q8':scale,'original_reference_radius_world':original,
                       'baked_reference_radius_world':skill['reference_radius_world'],'maximum_frame_payload_bytes':max(r['width']*r['height']*5 for r in rows),
                       'loop_working_set_bytes':loop_bytes})
    m['compaction']={'method':'Large persistent models use half canonical world scale; premultiplied alpha resolve, nearest visible depth, even source anchors; runtime radius restores world footprint',
                     'maximum_loop_working_set_bytes':max(r['loop_working_set_bytes'] for r in report),'models':report}
    m['source_sha256']={str(name).replace('\\','/'):digest for name,digest in m['source_sha256'].items()}
    m['source_sha256']['tools/compact_skill_fx.py']=sha(__file__);m['source_sha256']['tools/bake_skills.py']=sha(ROOT/'tools/bake_skills.py')
    path.write_text(json.dumps(m,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    # Preview the physical reference size, matching runtime nearest scaling.
    sheet=Image.new('RGB',(960,1024),(20,28,28));draw=ImageDraw.Draw(sheet)
    for number,skill in enumerate(m['skills']):
        rows=sorted([r for r in m['frames'] if r['model']==skill['model'] and r['clip']==0 and r['direction']==0],key=lambda r:r['frame'])
        pictures=[];factor=256//skill['raster_scale_q8']
        for r in rows:
            with np.load(ROOT/r['npz']) as f:src=Image.fromarray(f['rgba'])
            if factor>1:src=src.resize((src.width*factor,src.height*factor),Image.Resampling.NEAREST)
            im=Image.new('RGB',(240,224),(24,34,33));im.paste(src,(120-r['anchor_x']*factor,132-r['anchor_y']*factor),src);pictures.append(im)
        folder=path.parent/skill['name'];pictures[0].save(folder/'preview.gif',save_all=True,append_images=pictures[1:],duration=100,loop=0,optimize=False)
        pictures[len(pictures)//2].save(folder/'preview.png')
        x=number%4*240;y=number//4*256;sheet.paste(pictures[len(pictures)//2],(x,y+24));draw.text((x+5,y+4),skill['name'],fill='white')
    sheet.save(path.parent/'contact.png')
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--manifest',type=Path,default=ROOT/'private/baked/skills/manifest.json');a=parser.parse_args()
    print(json.dumps(compact(a.manifest)))
