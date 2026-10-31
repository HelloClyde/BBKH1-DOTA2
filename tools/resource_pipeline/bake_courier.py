"""Bake both original default flying couriers for Turbo's automatic delivery.

The extra128 Source-unit lift is an explicit H1 visual choice, not a recovered
native fly-height value. It is baked into both bodies and shadows. H1 places
the finished sprites at the ground anchor without adding a second lift.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,importlib.util,json,math,re,subprocess,sys
from PIL import Image,ImageDraw
from gltf_assets import Gltf,np,ROOT
from source2_vpk import VpkArchive
from bake_sprites import Model
from bake_jungle import original_normals
from bake_roshan_ancients import preview_frame,unit_block
from bake_hero_shadows import project_shadow,compact_shadow,projection_metadata

STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SOURCE=ROOT/'private/source2/exports/courier'
OUT=ROOT/'private/baked/courier'
GLTF_TO_WORLD=100/2.54/4
SPECS=[dict(id=75,name='courier_radiant',team=0,asset='radiant',model='donkey_wings',report='courier_default_flying'),
       dict(id=76,name='courier_dire',team=1,asset='dire',model='donkey_dire_wings',report='courier_default_dire_flying')]
CLIPS={0:'donkey_wings_idle',1:'donkey_wings_run'}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def relative(p):return Path(p).resolve().relative_to(ROOT).as_posix()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))

def default_item(text):
    match=re.search(r'"595"\s*\{',text);assert match
    end=match.end();depth=1
    while depth:
        depth+=int(text[end]=='{')-int(text[end]=='}');end+=1
    spec=importlib.util.spec_from_file_location('courier_source_kv',ROOT/'private/passives/extract_sources.py')
    kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv)
    value=kv.parse(text[match.start():end])['595']
    assert value['name']=='Default Courier' and value['baseitem']=='1'
    return value

class CourierModel(Model):
    def __init__(self,spec,lift_source=128):
        self.spec=spec;self.slug=spec['name'];self.model_id=spec['id'];self.textures={}
        self.paths=[SOURCE/spec['model']/(spec['model']+'.gltf')];self.docs=[Gltf(self.paths[0])];self.body=self.docs[0]
        self.names={a['name']:i for i,a in enumerate(self.body.doc['animations'])};self.clips=dict(CLIPS)
        self.scale=GLTF_TO_WORLD;self.floor=0;self.flight_height_source=lift_source;self.flight_height_world=lift_source/4
        root=self.body.doc['skins'][0]['joints'][0];self.root_name=self.body.doc['nodes'][root]['name']
        self.root0=self.body.globals(self.names[CLIPS[0]],0)[root][:3,3]
        self.original_triangles=sum(len(p['faces']) for p in self.body.primitives(self.names[CLIPS[0]],0))
        self.material_triangles=[dict(material=self.body.doc['materials'][p['material']]['name'],triangles=len(p['faces'])) for p in self.body.primitives(self.names[CLIPS[0]],0)]
        self.source_bone_count=len(self.body.doc['skins'][0]['joints'])
        points=np.concatenate([p['points'] for p in self.pose(CLIPS[0],0)])
        self.bounds=[points.min(0).tolist(),points.max(0).tolist()];self.height=int(math.ceil(points[:,2].max()))

    def pose(self,clip,seconds):
        animation=self.names[clip];matrices=self.body.globals(animation,seconds)
        motion=matrices[self.body.doc['skins'][0]['joints'][0]][:3,3]-self.root0
        parts=[]
        for original in self.body.primitives(animation,seconds):
            if any(x in original['name'].lower() for x in ('physics','collision','inspector')):continue
            p=dict(original,doc=0);q=p['points'];normals=original_normals(self.body,p,matrices)
            p['points']=np.column_stack((-q[:,0]+motion[0],q[:,2]-motion[2],q[:,1]))*self.scale
            p['points'][:,2]+=self.flight_height_world
            p['normals']=np.column_stack((-normals[:,0],normals[:,2],normals[:,1]));parts.append(p)
        return parts

def bake_one(args):
    spec,lift,ss,frames,identities=args;model=CourierModel(spec,lift);folder=OUT/spec['name'];folder.mkdir(parents=True,exist_ok=True)
    records=[];motions=[];previews=[];all_low=[];all_high=[]
    for clip,source in CLIPS.items():
        duration=model.body.duration(model.names[source]);ticks=round(duration*20)
        motions.append(dict(model=model.model_id,clip=clip,source_clip=source,source_duration_seconds=duration,duration_ticks=ticks,frames=frames,directions=16,loop=True))
        for number in range(frames):
            seconds=duration*number/frames;pose=model.pose(source,seconds)
            p=np.concatenate([p['points'] for p in pose]);all_low.append(p.min(0));all_high.append(p.max(0))
            for direction in range(16):
                rgba,depth,ax,ay=model.frame(pose,direction,ss)
                shadow,sax,say=project_shadow(model,pose,direction,ss);shadow,sax,say=compact_shadow(shadow,sax,say)
                path=folder/f'{clip}-{direction:02d}-{number:02d}.npz';np.savez_compressed(path,rgba=rgba,depth=depth,shadow=shadow)
                records.append(dict(model=model.model_id,clip=clip,direction=direction,frame=number,frames=frames,
                    duration_ticks=ticks,source_duration_ticks=ticks,source_clip=source,source_seconds=seconds,loop=True,
                    anchor_x=int(ax),anchor_y=int(ay),width=rgba.shape[1],height=rgba.shape[0],height_world=model.height,
                    npz=relative(path),sha256=sha(path),flags=4,shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],
                    shadow_anchor_x=sax,shadow_anchor_y=say,shadow_pixels=int(np.count_nonzero(shadow)),shadow_alpha_max=int(shadow.max())))
                if direction==0:
                    previews.append((clip,number,preview_frame(rgba,ax,ay,shadow,sax,say)))
        print(json.dumps(dict(courier=model.slug,source_clip=source,frames=frames,directions=16,triangles=model.original_triangles)),flush=True)
    low=np.min(all_low,axis=0);high=np.max(all_high,axis=0)
    model.height=int(math.ceil(high[2]))
    for r in records:r['height_world']=model.height
    receipt=dict(id=model.model_id,name=model.slug,team=spec['team'],source_asset=spec['asset'],source_model=identities[spec['name']],
        source_default_item=595,source_model_scale=1,gltf_metres_to_world=GLTF_TO_WORLD,
        source_origin_preserved=True,root_horizontal_motion_removed=True,source_bone_count=model.source_bone_count,
        source_triangles=model.original_triangles,source_material_triangles=model.material_triangles,
        source=[dict(gltf=relative(model.paths[0]),sha256=sha(model.paths[0]),buffers=model.body.buffer_sha256)],
        export_provenance=relative(ROOT/'private/source2/reports'/(spec['report']+'-provenance.json')),
        export_provenance_sha256=sha(ROOT/'private/source2/reports'/(spec['report']+'-provenance.json')),
        height_world=model.height,bounds_world=[low.tolist(),high.tolist()],flight_height_source=lift,
        flight_height_world=lift/4,source_pose_vertical_range_before_lift_world=[float(low[2]-lift/4),float(high[2]-lift/4)],
        clips=motions)
    for clip in CLIPS:
        images=[im for c,f,im in previews if c==clip];images[0].save(folder/f'preview-{clip}.gif',save_all=True,append_images=images[1:],duration=250,loop=0)
        images[0].save(folder/f'preview-{clip}.png')
    return receipt,records,motions

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--flight-height-source',type=int,default=128);p.add_argument('--ss',type=int,default=2);p.add_argument('--workers',type=int,default=2);a=p.parse_args()
    assert 0<=a.flight_height_source<=1024;OUT.mkdir(parents=True,exist_ok=True);SOURCE.mkdir(parents=True,exist_ok=True)
    v=VpkArchive(STEAM/'pak01_dir.vpk');raw=v.read('scripts/items/items_game.txt');(SOURCE/'items_game.txt').write_bytes(raw)
    item=default_item(raw.decode('utf8'));(SOURCE/'default-courier-item.json').write_text(json.dumps(item,indent=2)+'\n',encoding='utf8')
    unit_raw=v.read('scripts/npc/npc_units.txt');units=unit_block(unit_raw.decode('utf-8-sig'),'npc_dota_courier')
    flying=unit_block(unit_raw.decode('utf-8-sig'),'npc_dota_flying_courier');assert float(units['ModelScale'])==float(flying['ModelScale'])==1
    (SOURCE/'courier-unit-definitions.json').write_text(json.dumps(dict(courier=units,deprecated_flying=flying),indent=2)+'\n',encoding='utf8')
    identities={}
    for spec in SPECS:
        entry='models/props_gameplay/'+spec['model']+'.vmdl_c'
        matches=[x for x in item['visuals'].values() if isinstance(x,dict) and x.get('type')=='courier_flying' and x.get('asset')==spec['asset']]
        assert len(matches)==1 and matches[0]['modifier']+'_c'==entry
        identities[spec['name']]=v.identity(entry);path=SOURCE/spec['model']/(spec['model']+'.gltf')
        if not path.exists():subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name',spec['report'],'--entry',entry,'--output',str(path),'--skeleton'],check=True)
    args=[(s,a.flight_height_source,a.ss,8,identities) for s in SPECS]
    if a.workers>1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=a.workers) as pool:results=list(pool.map(bake_one,args))
    else:results=list(map(bake_one,args))
    tools=['bake_courier.py','bake_sprites.py','bake_jungle.py','bake_roshan_ancients.py','bake_hero_shadows.py','gltf_assets.py','bake_sprite_raster.py','bake_sprite_raster.c']
    report=dict(complete=True,models=[m for m,_,_ in results],frames=[r for _,rows,_ in results for r in rows],motions=[m for _,_,ms in results for m in ms],
        source_items=v.identity('scripts/items/items_game.txt'),source_units=v.identity('scripts/npc/npc_units.txt'),
        source_default_item=595,source_default_item_file=relative(SOURCE/'default-courier-item.json'),
        source_sha256={'tools/'+name:sha(ROOT/'tools'/name) for name in tools},
        shadow=projection_metadata(a.ss),projection=dict(zoom_q8=180,pitch_q8=181,directions=16),
        flight=dict(extra_height_source=a.flight_height_source,extra_height_world=a.flight_height_source/4,
            basis='Explicit H1 visual adaptation; native engine fly-height override not recovered.',
            vertical_source_bone_motion_preserved=True,body_and_shadow_include_extra_lift=True,runtime_ground_anchor=True,runtime_additional_lift=0),
        scope='Both original default flying couriers, selected by original item595 faction mappings; complete separate original rigs/materials and idle/fly loops. Explicit H1 extra visual lift is baked once into body and shadow, no runtime bones or meshes.')
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    sheet=Image.new('RGB',(960,224),(17,29,30));draw=ImageDraw.Draw(sheet)
    for i,(spec,clip) in enumerate((s,c) for s in SPECS for c in CLIPS):
        sheet.paste(Image.open(OUT/spec['name']/f'preview-{clip}.png'),(i*240,24));draw.text((i*240+4,4),spec['name']+' '+CLIPS[clip],fill=(230,210,160))
    sheet.save(OUT/'contact.png');print(json.dumps(dict(complete=True,frames=len(report['frames']),models=len(report['models']),flight_height_source=a.flight_height_source)))
if __name__=='__main__':main()
