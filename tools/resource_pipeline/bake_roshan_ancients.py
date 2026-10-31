"""Bake original Roshan and all five current Ancient-camp species groups.

Only new model IDs48..58 are emitted. Original compiled models, complete
original skeletons and material alpha are evaluated on the PC; every output
pose includes its own compact projected soft shadow, with unchanged body
RGBA/depth. No body triangles or skeletons are needed on H1.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, math, re, subprocess, sys
from PIL import Image, ImageDraw
from gltf_assets import Gltf, np, ROOT
from source2_vpk import VpkArchive
from bake_sprites import Model
from bake_jungle import original_normals
from bake_hero_shadows import project_shadow, compact_shadow, projection_metadata

SOURCE = ROOT/'private/source2/exports/roshan-ancients'
OUT = ROOT/'private/baked/roshan'
STEAM = Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SOURCE_TO_WORLD = 100/2.54/4
BASE_CLIPS = (0, 1, 2, 4)
SPECS = [
    dict(id=48,name='roshan',unit='npc_dota_roshan',scale=1.,model='models/creeps/roshan/roshan.vmdl_c',
         clips=['roshan_idle','roshan_run','roshan_attack','roshan_die'],extra={3:'roshan_slam',5:'roar',6:'roshan_bash'}),
    dict(id=49,name='black_dragon',scale=1.,model='models/creeps/neutral_creeps/n_creep_black_dragon/n_creep_black_dragon.vmdl_c',clips=['idle','run','attack','death']),
    dict(id=50,name='black_drake',scale=.6,model='models/creeps/neutral_creeps/n_creep_black_drake/n_creep_black_drake.vmdl_c',clips=['idle','run','attack','death']),
    dict(id=51,name='granite_golem',scale=1.3,model='models/creeps/neutral_creeps/n_creep_golem_a/neutral_creep_golem_a.vmdl_c',clips=['idle','run','attack','dieAlt']),
    dict(id=52,name='rock_golem',export='granite_golem',scale=1.,model='models/creeps/neutral_creeps/n_creep_golem_a/neutral_creep_golem_a.vmdl_c',clips=['idle','run','attack','dieAlt']),
    dict(id=53,name='big_thunder_lizard',scale=.8,model='models/creeps/neutral_creeps/n_creep_thunder_lizard/n_creep_thunder_lizard_big.vmdl_c',clips=['n_creep_thunder_lizard_big_idle','n_creep_thunder_lizard_big_run','n_creep_thunder_lizard_big_attack','n_creep_thunder_lizard_big_death']),
    dict(id=54,name='small_thunder_lizard',scale=.65,model='models/creeps/neutral_creeps/n_creep_thunder_lizard/n_creep_thunder_lizard_small.vmdl_c',clips=['n_creep_thunder_lizard_small_idle','n_creep_thunder_lizard_small_run','n_creep_thunder_lizard_small_attack','n_creep_thunder_lizard_small_death']),
    dict(id=55,name='ice_shaman',scale=1.3,model='models/creeps/ice_biome/giant/ice_giant01.vmdl_c',clips=['witchdoctor_idle_anim','witchdoctor_run_anim','ice_giant_attack_ranged','witchdoctor_death']),
    dict(id=56,name='frostbitten_golem',scale=1.,model='models/creeps/ice_biome/frostbitten/n_creep_frostbitten_swollen01.vmdl_c',clips=['idle','run','attack','death']),
    dict(id=57,name='prowler_shaman',scale=1.15,model='models/creeps/neutral_creeps/n_creep_satyr_spawn_a/n_creep_satyr_spawn_a.vmdl_c',clips=['idle','run','attack','death']),
    dict(id=58,name='prowler_acolyte',scale=1.,model='models/creeps/neutral_creeps/n_creep_satyr_spawn_a/n_creep_satyr_spawn_b.vmdl_c',clips=['idle','run','attack','death']),
]
for _spec in SPECS:
    _spec.setdefault('unit','npc_dota_neutral_'+_spec['name'])


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def relative(path):return Path(path).resolve().relative_to(ROOT).as_posix()


def unit_block(text, name):
    """Take one balanced, comment-free original KV unit without guessing stats."""
    text=re.sub(r'//[^\r\n]*','',text)
    match=re.search('"'+re.escape(name)+r'"\s*\{',text)
    if not match:raise ValueError('Missing original unit '+name)
    depth=1;end=match.end()
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return dict(re.findall(r'"([^"\r\n]+)"\s*"([^"\r\n]*)"',text[match.end():end-1]))


class AncientModel(Model):
    def __init__(self,spec):
        self.spec=spec;self.slug=spec['name'];self.model_id=spec['id'];self.textures={}
        name=spec.get('export',self.slug);self.paths=[SOURCE/name/(name+'.gltf')]
        self.docs=[Gltf(self.paths[0])];self.body=self.docs[0]
        self.names={a['name']:i for i,a in enumerate(self.body.doc.get('animations',[]))}
        self.clips=dict(zip(BASE_CLIPS,spec['clips']));self.clips.update(spec.get('extra',{}))
        for clip in self.clips.values():
            if clip not in self.names:raise ValueError(self.slug+': missing original animation '+clip)
        self.scale=SOURCE_TO_WORLD*spec['scale'];self.floor=0.
        root=self.body.doc['skins'][0]['joints'][0]
        self.root_name=self.body.doc['nodes'][root]['name']
        self.root0=self.body.globals(self.names[self.clips[0]],0)[root][:3,3]
        reference=self.pose(self.clips[0],0)
        pts=np.concatenate([p['points'] for p in reference]);self.bounds=[pts.min(0).tolist(),pts.max(0).tolist()]
        self.height=max(1,int(math.ceil(pts[:,2].max())))
        self.original_triangles=sum(len(p['faces']) for p in reference)

    def pose(self,clip,seconds):
        animation=self.names[clip];matrices=self.body.globals(animation,seconds)
        bones={n.get('name'):m for n,m in zip(self.body.doc['nodes'],matrices)}
        motion=bones[self.root_name][:3,3]-self.root0;result=[]
        for original in self.body.primitives(animation,seconds):
            if any(x in original['name'].lower() for x in ('physics','collision','inspector')):continue
            p=dict(original,doc=0);xyz=p['points']
            p['points']=np.column_stack((-xyz[:,0]+motion[0],xyz[:,2]-motion[2],xyz[:,1]))*self.scale
            n=original_normals(self.body,p,matrices)
            p['normals']=np.column_stack((-n[:,0],n[:,2],n[:,1]));result.append(p)
        return result


def preview_frame(rgba,ax,ay,shadow,sax,say,scale=2):
    image=Image.new('RGBA',(240,200),(70,84,72,255));foot=(120,150)
    for y in range(0,200,20):ImageDraw.Draw(image).line((0,y,240,y),fill=(76,90,79,255))
    sh=Image.new('RGBA',(shadow.shape[1],shadow.shape[0]));sh.putalpha(Image.fromarray(shadow))
    sh=sh.resize((sh.width*scale,sh.height*scale),Image.Resampling.NEAREST)
    image.alpha_composite(sh,(foot[0]-sax*scale,foot[1]-say*scale))
    body=Image.fromarray(rgba).resize((rgba.shape[1]*scale,rgba.shape[0]*scale),Image.Resampling.NEAREST)
    image.alpha_composite(body,(foot[0]-ax*scale,foot[1]-ay*scale))
    return image.convert('RGB')


def bake_model(item):
    spec,output,ss,count,preview_only,identities=item
    model=AncientModel(spec);folder=output/spec['name'];folder.mkdir(parents=True,exist_ok=True)
    records=[];motions=[];contacts=[]
    for clip,source_clip in sorted(model.clips.items()):
        duration=model.body.duration(model.names[source_clip]);ticks=max(count,round(duration*20));loop=clip in (0,1)
        motions.append(dict(model=model.model_id,clip=clip,source_clip=source_clip,source_duration_seconds=duration,
                            duration_ticks=ticks,frames=count,directions=16,loop=loop))
        for number in range(count if not preview_only else 1):
            seconds=duration*number/(count if loop else max(count-1,1));pose=model.pose(source_clip,seconds)
            for direction in (range(16) if not preview_only else range(1)):
                rgba,depth,ax,ay=model.frame(pose,direction,ss)
                raw,sax,say=project_shadow(model,pose,direction,ss);shadow,sax,say=compact_shadow(raw,sax,say)
                path=folder/f'{clip}-{direction:02d}-{number:02d}.npz'
                np.savez_compressed(path,rgba=rgba,depth=depth,shadow=shadow)
                records.append(dict(model=model.model_id,clip=clip,direction=direction,frame=number,frames=count,
                    duration_ticks=ticks,source_duration_ticks=round(duration*20),loop=loop,source_clip=source_clip,
                    source_seconds=float(seconds),anchor_x=int(ax),anchor_y=int(ay),width=rgba.shape[1],height=rgba.shape[0],
                    npz=relative(path),sha256=sha(path),height_world=model.height,flags=4,
                    shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],shadow_anchor_x=sax,shadow_anchor_y=say,
                    shadow_pixels=int(np.count_nonzero(shadow)),shadow_alpha_max=int(shadow.max())))
                if direction==0 and (preview_only or number==count//2):
                    contacts.append((spec['name']+' '+source_clip,preview_frame(rgba,ax,ay,shadow,sax,say)))
        print(json.dumps(dict(model=spec['name'],clip=source_clip,frames=count,directions=16,triangles=model.original_triangles)),flush=True)
    receipt=dict(id=model.model_id,name=model.slug,unit=spec['unit'],source_model=identities[spec['name']],
                 source_model_scale=spec['scale'],gltf_metres_to_world=SOURCE_TO_WORLD,
                 source_origin_preserved=True,source_triangles=model.original_triangles,height_world=model.height,
                 bounds_world=model.bounds,source=[dict(gltf=relative(model.paths[0]),sha256=sha(model.paths[0]),buffers=model.body.buffer_sha256)],clips=motions)
    local=dict(complete=not preview_only,models=[receipt],frames=records,motions=motions,shadow=projection_metadata(ss))
    (folder/'manifest.json').write_text(json.dumps(local,indent=2)+'\n',encoding='utf8')
    for i,(name,image) in enumerate(contacts):image.save(folder/f'preview-{i:02d}.png')
    return receipt,records,motions,[(name,str(folder/f'preview-{i:02d}.png')) for i,(name,_) in enumerate(contacts)]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=OUT)
    parser.add_argument('--model',action='append',choices=[s['name'] for s in SPECS]);parser.add_argument('--frames',type=int,default=8)
    parser.add_argument('--ss',type=int,default=2);parser.add_argument('--workers',type=int,default=3);parser.add_argument('--preview-only',action='store_true')
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    archive=VpkArchive(STEAM/'pak01_dir.vpk');units_raw=archive.read('scripts/npc/npc_units.txt');units=units_raw.decode('utf-8-sig')
    selected=[s for s in SPECS if not args.model or s['name'] in args.model];identities={}
    for spec in selected:
        values=unit_block(units,spec['unit']);assert values['Model']+'_c'==spec['model'];assert float(values['ModelScale'])==spec['scale']
        identities[spec['name']]=archive.identity(spec['model'])
        slug=spec.get('export',spec['name']);gltf=SOURCE/slug/(slug+'.gltf')
        if not gltf.exists():
            subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name','roshan_'+slug,'--entry',spec['model'],'--output',str(gltf),'--skeleton'],check=True)
    items=[(spec,output,args.ss,args.frames,args.preview_only,identities) for spec in selected]
    if args.workers>1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(bake_model,items))
    else:results=list(map(bake_model,items))
    models=[m for m,_,_,_ in results];frames=[f for _,fs,_,_ in results for f in fs];motions=[m for _,_,ms,_ in results for m in ms]
    tool_names=['bake_roshan_ancients.py','bake_sprites.py','bake_hero_shadows.py','bake_jungle.py','gltf_assets.py','bake_sprite_raster.py','bake_sprite_raster.c']
    report=dict(complete=not args.preview_only,models=models,frames=frames,motions=motions,
        shadow=projection_metadata(args.ss),source_units=archive.identity('scripts/npc/npc_units.txt'),
        source_sha256={'tools/'+n:sha(ROOT/'tools'/n) for n in tool_names},
        projection=dict(zoom_q8=180,pitch_q8=181,directions=16),
        scope='Original installed source meshes/rigs/materials/modelscale, 5 actual Ancient groups and Roshan. Original pose soft shadows; no runtime geometry.')
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    contacts=[item for _,_,_,rows in results for item in rows];sheet=Image.new('RGB',(960,math.ceil(len(contacts)/4)*224),(17,29,30));draw=ImageDraw.Draw(sheet)
    for i,(label,path) in enumerate(contacts):
        x=i%4*240;y=i//4*224;sheet.paste(Image.open(path),(x,y+24));draw.text((x+4,y+4),label,fill=(230,210,160))
    sheet.save(output/'contact.png')
    print(json.dumps(dict(complete=report['complete'],models=len(models),frames=len(frames),manifest=relative(output/'manifest.json'))))


if __name__=='__main__':main()
