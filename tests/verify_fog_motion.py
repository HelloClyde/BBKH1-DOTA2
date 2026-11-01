"""Camera translation invariant pixels from the actual renderer, not an oracle shader."""
from pathlib import Path
import argparse,csv,hashlib,json,os,subprocess
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',type=Path);a=p.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    source=ROOT/'platform/baked_scene.c'
    if a.baseline:
        source=out/'baseline.c';source.write_text(a.baseline.read_text().replace('#include "../private/','#include "'+(ROOT/'private').as_posix()+'/'),encoding='utf8')
    sources=[ROOT/'tests/fog_motion_ui.c',source,*[ROOT/'platform'/n for n in ['render.c','baked_io.c','item_assets.c','shop_catalog.c','status_effects.c','map_codec.c','map_format.c','vision_assets.c']],*sorted((ROOT/'game').glob('*.c'))]
    if (ROOT/'platform/tree_visual.c').exists():sources.append(ROOT/'platform/tree_visual.c')
    flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DBAKED_IO_HOST','-DCACHE_MEMORY_CUSTOM','-DDOTA_BAKED','-Igame','-Iplatform','-Isdk/sdk/include']
    env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');exe=out/'fog-motion.exe'
    inputs=[*sources,Path(__file__),ROOT/'tests/vision_ui.c',ROOT/'platform/native.c',*sorted((ROOT/'game').glob('*.h')),*sorted((ROOT/'platform').glob('*.h')),*sorted((ROOT/'private/generated').glob('*.h'))]
    identities=lambda:{str(s.relative_to(ROOT)):sha(s) for s in inputs};before=identities()
    report={'passed':False,'scope':__doc__,'source_sha256':before,'physical_h1_tested':False}
    try:
        # Exhaust every viewport pixel phase relative to a map tile. The real
        # draw and halo footprint must fit without evicting any needed tile.
        max_tiles=0
        for oy in range(128):
            for ox in range(128):
                gx=ox//8*8;gy=oy//8*8
                xs=[ox,ox+479,gx-4,gx+492];ys=[oy,oy+191,gy-4,gy+204]
                tile_count=(max(xs)//128-min(xs)//128+1)*(max(ys)//128-min(ys)//128+1)
                max_tiles=max(max_tiles,tile_count);assert tile_count<=15
        report.update(tile_phase_cases=128*128,max_live_map_tiles=max_tiles)
        for name,cmd in [('compile',['C:/msys64/ucrt64/bin/gcc.exe',*flags,*sources,'-o',exe]),('run',[exe,ROOT/'assets/runtime/SCENE.H1P',out])]:
            r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=300)
            (out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stdout+r.stderr
        rows=list(csv.DictReader((out/'frames.csv').open()));frames=np.fromfile(out/'frames.bin',dtype='<u2').reshape(-1,192,480);assert len(rows)==len(frames)==390
        groups=[];total=0;changes=0;gifs=[]
        for group in range(6):
            mismatch=0;compared=0;worst=0
            for i in range(1,65):
                at=group*65+i;prev=rows[at-1];now=rows[at];dx=int(now['origin_x'])-int(prev['origin_x']);dy=int(now['origin_y'])-int(prev['origin_y'])
                left,right=max(0,-dx),min(480,480-dx);top,bottom=max(0,-dy),min(192,192-dy)
                a0=frames[at,top:bottom,left:right];b0=frames[at-1,top+dy:bottom+dy,left+dx:right+dx]
                count=int(np.count_nonzero(a0!=b0));mismatch+=count;worst=max(worst,count);compared+=a0.size;changes+=bool(dx or dy)
            groups.append(dict(group=group,compared=compared,mismatched=mismatch,worst_pair=worst));total+=compared
            # Consecutive real frames, doubled with nearest pixels for inspection.
            gif=[]
            for i in range(65):
                f=frames[group*65+i].astype(np.uint32);rgb=np.stack(((f>>11)*255//31,((f>>5)&63)*255//63,(f&31)*255//31),axis=-1).astype('uint8')
                im=Image.fromarray(rgb).resize((960,384),Image.Resampling.NEAREST);gif.append(im)
            gif[0].save(out/f'camera-{group}.gif',save_all=True,append_images=gif[1:],duration=70,loop=0)
            gif[0].save(out/f'camera-{group}-first.png');gif[-1].save(out/f'camera-{group}-last.png');gifs.append(gif[32])
        sheet=Image.new('RGB',(960,384*3))
        for i,im in enumerate(gifs[::2]):sheet.paste(im,(0,i*384))
        sheet.save(out/'contact.png')
        walking=np.fromfile(out/'walking.bin',dtype='<u2').reshape(65,2,192,480);walk_rows=list(csv.DictReader((out/'walking.csv').open()));walk_compared=walk_mismatch=0;walk_gif=[]
        for i,r in enumerate(walk_rows):
            dx=int(r['origin_x'])-int(r['reference_x']);dy=int(r['origin_y'])-int(r['reference_y'])
            left,right=max(0,-dx),min(480,480-dx);top,bottom=max(0,-dy),min(192,192-dy)
            actual=walking[i,0,top:bottom,left:right];reference=walking[i,1,top+dy:bottom+dy,left+dx:right+dx]
            walk_compared+=actual.size;walk_mismatch+=int(np.count_nonzero(actual!=reference))
            f=walking[i,0].astype(np.uint32);rgb=np.stack(((f>>11)*255//31,((f>>5)&63)*255//63,(f&31)*255//31),axis=-1).astype('uint8');walk_gif.append(Image.fromarray(rgb).resize((960,384),Image.Resampling.NEAREST))
        walk_gif[0].save(out/'walking.gif',save_all=True,append_images=walk_gif[1:],duration=70,loop=0)
        report.update(groups=groups,frames=len(frames),compared_pixels=total,translated_pairs=changes,walking_pairs=65,walking_compared=walk_compared,walking_mismatched=walk_mismatch,source_current=before==identities(),immediate_visibility_states=3,baseline=bool(a.baseline))
        report['passed']=all(g['mismatched']==0 for g in groups) and not walk_mismatch and report['source_current']
        assert report['passed'] or a.baseline,dict(source_current=report['source_current'],camera_mismatches=sum(g['mismatched'] for g in groups),walking_mismatches=walk_mismatch)
    finally:(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))
if __name__=='__main__':main()
