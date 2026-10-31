"""Original Steam Slark/Necrophos bodies, default wearables, skill FX and UI.

The PC evaluates original Source skeleton clips and all original mesh triangles.
Particle emission is an explicit offline H1 interpretation, recorded per skill;
the Source particle textures, skull and animated Reaper's Scythe remain original.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,importlib.util,json,math,re,subprocess,sys
from PIL import Image,ImageDraw
from gltf_assets import Gltf,np,ROOT
from source2_vpk import VpkArchive
from bake_sprites import Model,source_normals
from bake_hero_shadows import project_shadow,compact_shadow,projection_metadata
from bake_roshan_ancients import preview_frame
from bake_skills import SkillCanvas
from bake_effects import linear,ZOOM,PITCH
from bake_sprite_raster import Raster

SOURCE=ROOT/'private/source2/exports/extra-heroes'
OUT=ROOT/'private/baked/extra-heroes'
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
TOOL=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
GLTF_TO_WORLD=100/2.54/4
HEROES=[dict(id=77,name='slark',scale=.9,clips=['idle','run','attack','dark_pact','death','dark_pact','pounce','attack_shiv','shadow_dance']),
        dict(id=78,name='necrolyte',scale=.79,clips=['idle','run','attack','cast_wave','death','cast_wave','cast_ghost_shroud','idle','cast_ult'])]
ABILITIES=['slark_dark_pact','slark_pounce','slark_saltwater_shiv','slark_shadow_dance',
           'necrolyte_death_pulse','necrolyte_ghost_shroud','necrolyte_heartstopper_aura','necrolyte_reapers_scythe','necrolyte_base_attack']
ROOTS=['slark/slark_dark_pact_pulses','slark/slark_pounce_leash','slark/slark_dredged_shiv_impact','slark/slark_shadow_dance',
       'necrolyte/necrolyte_pulse_enemy','necrolyte/necrolyte_spirit','necrolyte/necrolyte_heartstop_target_null','necrolyte/necrolyte_scythe_model','necrolyte/necrolyte_base_attack']
TEXTURES={'blue':'materials/particle/beam_blue_energy_01.vtex_c','wave':'materials/particle/particle_ring_wave_10.vtex_c',
          'mist':'materials/particle/smoke/steam/steam.vtex_c','claw':'materials/particle/beam_claw_rip.vtex_c',
          'caustic':'materials/particle/water/caustic/caustic.vtex_c','smoke':'materials/particle/pyroclastic/pyroclastic.vtex_c',
          'glow':'materials/particle/softglow.vtex_c','spirit':'materials/particle/projected_spirit_psd_b02c45b.vtex_c',
          'chain':'materials/particle/beam_chain.vtex_c','swirl':'materials/particle/wisp/particle_swirl.vtex_c',
          'attack_glow':'materials/particle/particle_glow_05.vtex_c','attack_wisp':'materials/particle/smoke/maya_wispy/wispy_small.vtex_c',
          'attack_green':'materials/particle/particle_flares/aircraft_green.vtex_c'}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def relative(p):return Path(p).resolve().relative_to(ROOT).as_posix()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def write(p,v):Path(p).write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def kvparse(text):
    spec=importlib.util.spec_from_file_location('extra_hero_source_kv',ROOT/'private/passives/extract_sources.py')
    kv=importlib.util.module_from_spec(spec);spec.loader.exec_module(kv);return kv.parse(text)

def source_definitions(v):
    SOURCE.mkdir(parents=True,exist_ok=True);itemsraw=v.read('scripts/items/items_game.txt');(SOURCE/'items_game.txt').write_bytes(itemsraw)
    items=kvparse(itemsraw.decode('utf-8-sig'))['items_game']['items'];defaults={}
    for spec in HEROES:
        name=spec['name'];entry=f'scripts/npc/heroes/npc_dota_hero_{name}.txt';raw=v.read(entry);(SOURCE/(name+'-unit.txt')).write_bytes(raw)
        unit=kvparse(raw.decode('utf-8-sig'))['DOTAHeroes']['npc_dota_hero_'+name]
        assert float(unit['ModelScale'])==spec['scale']
        parts=[dict(id=k,item=i) for k,i in items.items() if isinstance(i,dict) and i.get('prefab')=='default_item' and 'npc_dota_hero_'+name in i.get('used_by_heroes',{}) and i.get('model_player')]
        assert len(parts)==5
        defaults[name]=dict(unit=unit,defaults=parts,source_unit=v.identity(entry))
        for part,model in [('body',unit['Model']),*[(Path(i['item']['model_player']).stem,i['item']['model_player']) for i in parts]]:
            path=SOURCE/name/(part+'.gltf')
            if not path.exists():subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name',f'extra_{name}_{part}','--entry',model+'_c','--output',str(path),'--skeleton'],check=True)
    for name in ('necrolyte_reapers_scythe','necrolyte_skull'):
        path=SOURCE/'fx'/name/(name+'.gltf')
        if not path.exists():subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name','extra_fx_'+name,'--entry','models/heroes/necrolyte/'+name+'.vmdl_c','--output',str(path),'--skeleton'],check=True)
    write(SOURCE/'hero-defaults.json',defaults);return defaults

class ExtraModel(Model):
    def __init__(self,spec):
        self.spec=spec;self.slug=spec['name'];self.model_id=spec['id'];self.textures={};self.floor=0
        defs=read(SOURCE/'hero-defaults.json')[self.slug];self.defaults=defs['defaults'];self.source_unit=defs['source_unit']
        self.paths=[SOURCE/self.slug/'body.gltf',*[SOURCE/self.slug/(Path(i['item']['model_player']).stem+'.gltf') for i in self.defaults]]
        self.docs=[Gltf(p) for p in self.paths];self.body=self.docs[0]
        self.names={a['name']:i for i,a in enumerate(self.body.doc['animations'])};self.scale=GLTF_TO_WORLD*spec['scale']
        if 'idle_clip' in spec:self.names['idle']=self.names[spec['idle_clip']]
        self.root=self.body.doc['skins'][0]['joints'][0];self.root0=self.body.globals(self.names['idle'],0)[self.root][:3,3]
        self.height=1;self.receipts=[];self.binding=[]
        body_names={n.get('name','').casefold() for n in self.body.doc['nodes']}
        for path,g in zip(self.paths,self.docs):
            self.receipts.append(dict(gltf=relative(path),sha256=sha(path),buffers=g.buffer_sha256))
            used=set()
            for node in g.doc['nodes']:
                if 'skin' not in node or 'mesh' not in node:continue
                skin=g.doc['skins'][node['skin']]
                for p in g.doc['meshes'][node['mesh']]['primitives']:
                    at=p['attributes']
                    if 'JOINTS_0' in at:
                        joints=g.accessor(at['JOINTS_0']);weights=g.accessor(at['WEIGHTS_0'])
                        used.update(g.doc['nodes'][skin['joints'][int(j)]]['name'] for j in joints[weights>1e-6])
            missing=sorted(n for n in used if n.casefold() not in body_names)
            if path!=self.paths[0] and missing:raise ValueError(f'{self.slug} unbound original wearable bones {missing}')
            self.binding.append(dict(gltf=relative(path),weighted_bones=len(used),missing_body_bones=missing))
        pose=self.pose('idle',0);self.original_triangles=sum(len(p['faces']) for p in pose)
        points=np.concatenate([p['points'] for p in pose]);self.height=int(math.ceil(points[:,2].max()))

    def pose(self,clip,seconds):
        ai=self.names[clip];matrices=self.body.globals(ai,seconds)
        bones={n.get('name'):m for n,m in zip(self.body.doc['nodes'],matrices)};motion=matrices[self.root][:3,3]-self.root0;out=[]
        for di,g in enumerate(self.docs):
            external=bones if di else None;gm=matrices if di==0 else g.globals(external_bones=external)
            for original in g.primitives(ai if di==0 else None,seconds,external_bones=external):
                if any(n in original['name'].lower() for n in ('physics','collision','inspector')):continue
                p=dict(original,doc=di);q=p['points'];n=source_normals(g,p,gm)
                p['points']=np.column_stack((-q[:,0]+motion[0],q[:,2]-motion[2],q[:,1]))*self.scale
                p['normals']=np.column_stack((-n[:,0],n[:,2],n[:,1]));out.append(p)
        return out

def bake_body(args):
    spec,ss=args;model=ExtraModel(spec);folder=OUT/spec['name'];folder.mkdir(parents=True,exist_ok=True)
    records=[];motions=[];low=[];high=[];contact=[]
    for clip,source in enumerate(spec['clips']):
        duration=model.body.duration(model.names[source]);ticks=max(8,round(duration*20));loop=clip in (0,1) or clip in spec.get('loop_clips',[]) or (spec['name']=='necrolyte' and clip==7)
        motion=dict(model=model.model_id,clip=clip,source_clip=source,duration_ticks=ticks,source_duration_ticks=round(duration*20),source_duration_seconds=duration,frames=8,directions=16,loop=loop)
        if spec['name']=='necrolyte' and clip==7:motion['passive']=True;motion['adaptation']='No native passive cast: idle pose is only an event/preview fallback; E is not castable.'
        motions.append(motion);previews=[]
        for number in range(8):
            seconds=duration*number/(8 if loop else 7);pose=model.pose(source,seconds)
            points=np.concatenate([p['points'] for p in pose]);low.append(points.min(0));high.append(points.max(0))
            for direction in range(16):
                rgba,depth,ax,ay=model.frame(pose,direction,ss);shadow,sax,say=compact_shadow(*project_shadow(model,pose,direction,ss))
                path=folder/f'{clip}-{direction:02d}-{number:02d}.npz';np.savez_compressed(path,rgba=rgba,depth=depth,shadow=shadow)
                records.append(dict(model=model.model_id,clip=clip,direction=direction,frame=number,frames=8,duration_ticks=ticks,source_duration_ticks=round(duration*20),source_clip=source,source_seconds=seconds,loop=loop,
                    anchor_x=int(ax),anchor_y=int(ay),width=rgba.shape[1],height=rgba.shape[0],height_world=model.height,npz=relative(path),sha256=sha(path),flags=4,
                    shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],shadow_anchor_x=sax,shadow_anchor_y=say,shadow_pixels=int(np.count_nonzero(shadow)),shadow_alpha_max=int(shadow.max())))
                if direction==0:previews.append(preview_frame(rgba,ax,ay,shadow,sax,say,scale=3))
        previews[0].save(folder/f'preview-{clip}.gif',save_all=True,append_images=previews[1:],duration=100,loop=0);previews[0].save(folder/f'preview-{clip}.png');contact.append(previews[3])
        print(json.dumps(dict(hero=model.slug,clip=clip,source_clip=source,frames=128,triangles=model.original_triangles)),flush=True)
    lo=np.min(low,0);hi=np.max(high,0);height=int(math.ceil(hi[2]))
    for row in records:row['height_world']=height
    receipt=dict(id=model.model_id,name=model.slug,source_unit=model.source_unit,source_model_scale=spec['scale'],gltf_metres_to_world=GLTF_TO_WORLD,
        source_origin_preserved=True,root_horizontal_motion_removed=True,vertical_bone_motion_preserved=True,source_floor=0,
        source_triangles=model.original_triangles,source_bone_count=len(model.body.doc['skins'][0]['joints']),source=model.receipts,
        default_wearables=[dict(item_id=p['id'],name=p['item']['name'],source_model=p['item']['model_player']) for p in model.defaults],wearable_binding=model.binding,
        bounds_world=[lo.tolist(),hi.tolist()],height_world=height,clips=motions)
    return receipt,records,motions

def graph_sources(v):
    out=[]
    for root in ROOTS:
        h,n=root.split('/');entry=f'particles/units/heroes/hero_{h}/{n}.vpcf_c';pending=[entry];seen=set();textures=set();models=set()
        while pending:
            e=pending.pop()
            if e in seen:continue
            if e not in v.entries:raise ValueError('Missing original graph '+e)
            seen.add(e)
            for m in re.findall(rb'(?:materials|particles|models)/[a-zA-Z0-9_./-]+?\.(?:vpcf|vmat|vtex|vmdl)(?:_c)?',v.read(e)):
                ref=m.decode();ref=ref if ref.endswith('_c') else ref+'_c'
                if ref not in v.entries:continue
                if ref.endswith('.vtex_c'):textures.add(ref)
                elif ref.endswith('.vmdl_c'):models.add(ref)
                else:pending.append(ref)
        out.append(dict(root=entry,compiled_graphs=[v.identity(e) for e in sorted(seen)],referenced_textures=sorted(textures),referenced_models=sorted(models)))
    write(SOURCE/'particle-graphs.json',out);return out

def decode_texture(v,key,entry):
    folder=SOURCE/'textures'/key;folder.mkdir(parents=True,exist_ok=True);raw=folder/(key+'.vtex_c');raw.write_bytes(v.read(entry));target=folder/(key+'.png')
    if not target.exists():
        p=subprocess.run([str(TOOL),'-d','-i',str(raw),'-o',str(target)],capture_output=True,text=True,encoding='utf8');(folder/'export.log').write_text(p.stdout+p.stderr,encoding='utf8')
        if p.returncode:raise RuntimeError('Texture export failed '+entry)
    paths=[target] if target.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n' else sorted(folder.glob(key+'_seq*.png'))
    if not paths:raise ValueError('No PNG decoded '+entry)
    return paths,dict(name=key,source=v.identity(entry),raw_sha256=sha(raw),decoded=[dict(path=relative(p),sha256=sha(p),size=list(Image.open(p).size)) for p in paths])

def scythe_particle_config(v):
    entry='particles/units/heroes/hero_necrolyte/necrolyte_scythe_model.vpcf_c';folder=SOURCE/'fx';folder.mkdir(parents=True,exist_ok=True)
    raw=folder/'necrolyte_scythe_model.vpcf_c';raw.write_bytes(v.read(entry));dump=folder/'necrolyte_scythe_model-DATA.txt'
    if not dump.exists():
        p=subprocess.run([str(TOOL),'-i',str(raw),'-b','DATA'],capture_output=True,text=True,encoding='utf8',errors='replace')
        if p.returncode:raise RuntimeError('Cannot inspect original scythe particle DATA')
        dump.write_text(p.stdout,encoding='utf8')
    text=dump.read_text(encoding='utf8');radius=float(re.search(r'm_flConstantRadius\s*=\s*([0-9.]+)',text).group(1));assert radius==3
    return dict(source=v.identity(entry),data_dump=relative(dump),data_dump_sha256=sha(dump),source_constant_radius=radius,applied_particle_model_scale=radius,
        source_radius_interpolation=dict(end_scale=3,end_normalized_time=.4,executed=False),source_animation_remap_seconds=.63,
        h1_horizontal_motion='Center the planar scythe at the victim for fixed camera visibility; original skeleton/vertical sweep retained.',h1_orientation='Runtime may select direction4 to present the planar blade to the fixed camera.')

def ui(v):
    folder=OUT/'ui';folder.mkdir(parents=True,exist_ok=True);rows=[]
    entries={h['name']:f"panorama/images/heroes/icons/npc_dota_hero_{h['name']}_png.vtex_c" for h in HEROES}
    entries.update({name:f'panorama/images/spellicons/{name}_png.vtex_c' for name in ABILITIES[:8]})
    for name,entry in entries.items():
        paths,source=decode_texture(v,'ui_'+name,entry);image=Image.open(paths[0]).convert('RGBA').resize((24,24),Image.Resampling.LANCZOS);path=folder/(name+'.png');image.save(path)
        rows.append(dict(name=name,path=relative(path),sha256=sha(path),width=24,height=24,source=source))
    write(OUT/'ui-manifest.json',dict(complete=True,icons=rows));return rows

FX_MESHES={}
def add_fx_mesh(c,name,seconds,direction,offset=(0,0,0),strength=1,particle_scale=1,recenter=False):
    """Evaluate the original FX skeleton, retain its native Source dimensions."""
    if name not in FX_MESHES:
        path=SOURCE/'fx'/name/(name+'.gltf');FX_MESHES[name]=Gltf(path)
    g=FX_MESHES[name];ai=0
    if name=='necrolyte_skull':ai=next(i for i,a in enumerate(g.doc['animations']) if a['name']=='anim')
    matrices=g.globals(ai,seconds);raster=Raster(c.width,c.height,c.ss)
    angle=direction*math.tau/16;co=math.cos(angle);si=math.sin(angle)
    parts=list(g.primitives(ai,seconds));points=np.concatenate([p['points'] for p in parts]);center=(points.min(0)+points.max(0))*.5
    for p in parts:
        xyz=p['points'].copy()
        if recenter:xyz[:,0]-=center[0];xyz[:,2]-=center[2]
        q=np.column_stack((-xyz[:,0],xyz[:,2],xyz[:,1]))*GLTF_TO_WORLD*particle_scale
        n=source_normals(g,p,matrices);n=np.column_stack((-n[:,0],n[:,2],n[:,1]))
        x=q[:,0].copy();y=q[:,1].copy();q[:,0]=x*co-y*si;q[:,1]=x*si+y*co;q+=offset
        x=n[:,0].copy();y=n[:,1].copy();n[:,0]=x*co-y*si;n[:,1]=x*si+y*co
        projected=np.column_stack((c.ax+q[:,0]*ZOOM,c.ay+(q[:,1]-q[:,2])*PITCH,-q[:,1]-q[:,2]))
        material=g.material(p['material']);m=material['source'];texture=np.asarray(Image.open(material['image']).convert('RGBA')).copy()
        if m.get('alphaMode','OPAQUE')=='OPAQUE':texture[:,:,3]=255
        cutoff=float(m.get('alphaCutoff',.5)) if m.get('alphaMode')=='MASK' else 0
        raster.draw(projected,p['faces'],p['uv'],n,texture,cutoff=cutoff,factor=material['factor'])
    colour=raster.colour;alpha=colour[:,:,3]*strength
    c.rgb+=colour[:,:,:3]*strength;c.alpha=np.maximum(c.alpha,alpha);c.depth=np.minimum(c.depth,np.where(alpha>.005,raster.depth,np.inf))

def fx_frame(skill,clip,number,direction,textures,ss):
    p=number/(8 if clip==1 else 7);c=SkillCanvas(ss);angle=direction*math.tau/16
    blue=(.08,.40,1.);cyan=(.08,.80,1.);green=(.20,1.,.12);sick=(.48,1.,.03);white=(.8,1.,.65)
    tex={k:seq[number%len(seq)] for k,seq in textures.items()};glow=tex['glow'];life=.08+.92*math.sin(p*math.pi) if clip!=1 else .66+.20*math.cos(p*math.tau)
    if skill==0:
        # Dark Pact: visible contraction followed by separate delayed water pulses.
        radius=(100-65*p) if clip==0 else (28+72*p) if clip==2 else 78+14*math.sin(p*math.tau)
        c.ground(tex['wave'],radius,p*.8,cyan,life*1.4)
        for j in range(9):
            a=j*math.tau/9+p*.7;r=radius*(.55+.25*(j%2));z=6+18*((p+j*.17)%1)
            c.sprite(tex['mist'],math.cos(a)*r,math.sin(a)*r,z,12,17,blue,life*.45,angle=a)
            c.ray(tex['blue'],math.cos(a)*r,math.sin(a)*r,z,0,0,16,3,cyan,life*.45)
    elif skill==1:
        if clip==0:
            # Sprite follows the actual moving/jumping hero; no baked teleport.
            for j in range(5):c.ribbon(tex['blue'],angle,45+j*9,4+j*2,5+j*2,cyan,life*(1-j*.12),p,bend=2)
            c.ground(tex['wave'],20+36*p,p,blue,life)
            dx=-math.sin(angle);dy=math.cos(angle)
            for j in range(8):
                distance=8+j*7;side=math.sin(j*2.7+p*math.tau)*5;z=3+12*math.sin((p+j*.12)%1*math.pi)
                x=-dx*distance+dy*side;y=-dy*distance-dx*side
                c.glow(glow,x,y,z,2.2+(j%3)*.6,cyan,life*.80)
                c.sprite(tex['mist'],x,y,z,5,9,cyan,life*.25,angle)
        elif clip==1:
            # Baked leash perimeter and rope, anchored at the victim by gameplay.
            c.ring(tex['wave'],95,3,cyan,.85,p,arcs=6)
            for j in range(6):
                a=j*math.tau/6+p*.3;c.ray(tex['blue'],0,0,21,math.cos(a)*92,math.sin(a)*92,2,2,blue,.55)
        else:
            c.ground(tex['wave'],20+80*p,p,cyan,life)
            for j in range(12):
                a=j*math.tau/12;r=10+58*p;c.sprite(tex['caustic'],math.cos(a)*r,math.sin(a)*r,24*math.sin(p*math.pi),10,15,cyan,life*.65,a)
    elif skill==2:
        # Original Saltwater Shiv ripped slash and water-caustic, distinct from innate.
        for j in range(3):
            c.ribbon(tex['claw'],angle+(j-1)*.18,32+40*p,9,18+j*5,cyan,life*1.3,p,bend=8)
        c.sprite(tex['caustic'],0,0,18,25+12*p,22,blue,life*.65,angle)
    elif skill==3:
        # Shadow Dance has low opaque-looking dark body haze, blue eyes/wisps.
        for j in range(9):
            a=j*math.tau/9+p*.9;r=18+8*math.cos(j+p*math.tau);z=10+18*((p+j*.21)%1)
            strength=(.8 if clip==1 else life)*.70
            c.sprite(tex['smoke'],math.cos(a)*r,math.sin(a)*r,z,20,24,(.035,.08,.14),strength,a)
        for x in (-4,4):c.glow(glow,x,-2,29,2.6,cyan,.9*life)
        c.ground(tex['wave'],45+8*math.sin(p*math.tau),p,blue,life*.4)
    elif skill==4:
        if clip==1:
            add_fx_mesh(c,'necrolyte_skull',p*2.966,direction,offset=(0,0,18))
            c.ribbon(tex['blue'],angle,30,9,18,green,.6,p,bend=4)
            c.glow(glow,0,0,18,9,green,.7)
        elif clip==0:
            # Source Death Pulse uses skull projectiles, not only a ground ring.
            for j in range(6):
                a=j*math.tau/6;radius=12+95*p
                add_fx_mesh(c,'necrolyte_skull',p*2.966,round(j*16/6)%16,offset=(math.cos(a)*radius,math.sin(a)*radius,12),strength=.25+.75*life)
                c.ray(tex['blue'],math.cos(a)*max(0,radius-25),math.sin(a)*max(0,radius-25),12,math.cos(a)*radius,math.sin(a)*radius,12,5,green,.7*life)
            c.ground(tex['wave'],20+100*p,p,green,life*.8)
        else:
            c.glow(glow,0,0,18,14,green,life)
            for j in range(8):
                a=j*math.tau/8;c.sprite(tex['spirit'],math.cos(a)*24,math.sin(a)*24,8+26*p,10,20,green,life*.6,a)
    elif skill==5:
        c.ground(tex['spirit'],82+8*math.sin(p*math.tau),p*.25,green,life*.65)
        for j in range(8):
            a=j*math.tau/8+p*.5;z=6+42*((p+j*.13)%1)
            c.sprite(tex['mist'],math.cos(a)*26,math.sin(a)*26,z,18,28,green,life*.35,a)
            c.sprite(tex['spirit'],math.cos(a)*34,math.sin(a)*34,z,9,17,white,life*.45,a)
    elif skill==6:
        # Heartstopper is passive: subtle continuous disease wisps; never a cast burst.
        for j in range(8):
            a=j*math.tau/8+p*.7;r=42+80*((p+j*.21)%1)
            c.sprite(tex['mist'],math.cos(a)*r,math.sin(a)*r,4+9*(j%3),17,10,sick,.18 if clip==1 else .23*life,a)
        c.ground(tex['spirit'],150,p*.3,sick,.13*life)
    elif skill==7:
        if clip==0:
            # The original animated giant mesh swings into the victim's anchor.
            add_fx_mesh(c,'necrolyte_reapers_scythe',p*.6333333254,direction,strength=.35+.65*life,particle_scale=3,recenter=True)
            for side in (-1,1):c.ray(tex['chain'],side*18,0,0,side*12,0,63,5,green,.8*life)
            c.glow(glow,0,0,18,9,green,life*.7)
        elif clip==1:
            for a in (0,math.pi/2,math.pi,math.pi*1.5):
                c.ray(tex['chain'],math.cos(a)*22,math.sin(a)*22,1,math.cos(a+.4)*12,math.sin(a+.4)*12,55,4,green,.75)
            c.ground(tex['swirl'],38,p,green,.7)
        else:
            c.ray(tex['chain'],0,0,75,0,0,1,18,white,life*1.6)
            c.ground(tex['swirl'],22+65*p,p,green,life)
            for j in range(10):
                a=j*math.tau/10;r=15+50*p;c.glow(glow,math.cos(a)*r,math.sin(a)*r,8+25*math.sin(p*math.pi),3,green,life)
    elif skill==8:
        # Original normal attack is a vapor/energy ball, distinct from Q skulls.
        if clip in (0,1):
            # Native flare maps contain a very small hot core. Exposure is
            # raised offline so it survives the H1's few-pixel projectile.
            c.glow(tex['attack_glow'],0,0,0,5.5,green,8.)
            c.sprite(tex['attack_green'],0,0,0,6,6,(1,1,1),4.,p*math.tau)
            c.sprite(tex['attack_wisp'],0,0,0,8,9,green,.6,p*math.tau)
            dx=-math.sin(angle);dy=math.cos(angle)
            for j in range(6):
                distance=4+j*4;side=math.sin(p*math.tau+j)*2.5
                c.glow(tex['attack_glow'],-dx*distance+dy*side,-dy*distance-dx*side,0,2.2,green,4.-j*.50)
                c.sprite(tex['attack_wisp'],-dx*distance,-dy*distance,0,4+j,5,green,.25-j*.025,p+j)
        else:
            c.glow(tex['attack_glow'],0,0,0,5+9*p,green,life*6.)
            for j in range(7):
                a=j*math.tau/7;r=3+18*p;c.sprite(tex['attack_wisp'],math.cos(a)*r,math.sin(a)*r,4*p,6,8,green,life*.40,a)
    return c.finish()

def bake_fx(v,ss,only=None):
    graphs=graph_sources(v);particle_config=scythe_particle_config(v);textures={};sources=[]
    for key,entry in TEXTURES.items():
        paths,receipt=decode_texture(v,key,entry);seq=[]
        for path in paths[:12]:
            im=np.asarray(Image.open(path).convert('RGBA'),dtype=np.float32)/255;rgb=linear(im[:,:,:3]);peak=rgb.max(2);seq.append(np.dstack((rgb/np.maximum(peak[:,:,None],1e-8),im[:,:,3]*peak)).astype(np.float32))
        textures[key]=seq;sources.append(receipt)
    models=[];frames=[];motions=[];reference=[100,100,45,55,120,100,175,100,28]
    descriptions=[
        'Original blue-energy/water textures: contraction is clip0; gameplay delay triggers clip1 radial purging pulses, clip2 final splashes.',
        'Actual original Pounce skeletal animation stays on hero; clip0 short water trail follows moving ground anchor; clip1 draws victim leash perimeter/ropes; clip2 landing water.',
        'Original Saltwater Shiv attack_shiv body pose plus claw-rip and water-caustic textures; no Essence Shift icon or cast substitution.',
        'Original Shadow Dance body clip and particle smoke, blue water wisps/eyes. Visibility rules and body fade are applied by gameplay, cloud is separate.',
        'Original animated Necro skull mesh flies radially in clip0; clip1 is an original skull projectile for gameplay target travel; clip2 healing/soul impact.',
        'Original projected-spirit and steam texture emissions rise around the hero during Ghost Shroud; gameplay provides ethereal status.',
        'Heartstopper Aura has no original cast or visible target particle (null Source graph). Low continuous disease wisps adapt original Necro spirit/steam textures; gameplay remains passive.',
        'Original giant necrolyte_reapers_scythe mesh, original bone animation and all238 triangles. Source particle m_flConstantRadius=3 is applied as a3x particle model scale. H1 recenters horizontal particle trajectory at victim (retains vertical sweep); holds3x instead of executing Source radius interpolation/CP transforms. Clip0 execution swing, clip1 chains, clip2 impact.',
        'Original Necrophos base_attack graph aircraft_green, particle_glow_05 and maya_wispy textures reconstructed as green vapor orb and trailing green souls. Offline flare exposure is raised8x for the few-pixel H1 orb. Ground-relative Z is0 because gameplay supplies live arc height. This normal attack has no skull mesh and is distinct from Death Pulse.'
    ]
    for skill,name in enumerate(ABILITIES):
        if only is not None and skill not in only:continue
        mid=79+skill;folder=OUT/name;folder.mkdir(parents=True,exist_ok=True);clips=[];previews=[]
        for clip in range(3):
            directional=(skill in (1,2,7) and clip==0) or (skill==4 and clip==1) or (skill==8 and clip in (0,1))
            directions=16 if directional else 1;ticks=16 if clip==1 else (13 if skill==7 and clip==0 else 14)
            motion=dict(model=mid,clip=clip,source_clip=ROOTS[skill],frames=8,directions=directions,duration_ticks=ticks,loop=clip==1);clips.append(motion);motions.append(motion)
            images=[]
            for number in range(8):
                for direction in range(directions):
                    rgba,depth,ax,ay=fx_frame(skill,clip,number,direction,textures,ss);path=folder/f'{clip}-{direction:02d}-{number:02d}.npz';np.savez_compressed(path,rgba=rgba,depth=depth)
                    frames.append(dict(model=mid,clip=clip,direction=direction,frame=number,frames=8,duration_ticks=ticks,loop=clip==1,anchor_x=int(ax),anchor_y=int(ay),width=rgba.shape[1],height=rgba.shape[0],height_world=reference[skill],npz=relative(path),sha256=sha(path),flags=3))
                    if direction==4 or (direction==0 and not directional):
                        bg=Image.new('RGBA',(240,224),(30,43,39,255));bg.alpha_composite(Image.fromarray(rgba),(120-ax,132-ay));images.append(bg.convert('RGB'))
            images[0].save(folder/f'preview-{clip}.gif',save_all=True,append_images=images[1:],duration=100,loop=0);images[3].save(folder/f'preview-{clip}.png');previews.append(images[3])
        models.append(dict(id=mid,name=name,height_world=reference[skill],source_graph=graphs[skill],clips=clips,adaptation=descriptions[skill],particle_engine_executed=False,offline_emission=True))
        print(json.dumps(dict(fx=name,model=mid,frames=sum(m['frames']*m['directions'] for m in clips))),flush=True)
    # Explicit original FX mesh provenance is separate from hero default sickle.
    mesh_receipts=[]
    for name in ('necrolyte_reapers_scythe','necrolyte_skull'):
        path=SOURCE/'fx'/name/(name+'.gltf');g=Gltf(path)
        mesh_receipts.append(dict(name=name,gltf=relative(path),sha256=sha(path),buffers=g.buffer_sha256,source_triangles=sum(len(p['faces']) for p in g.primitives(0,0)),source_animations=[dict(name=a['name'],duration=g.duration(i)) for i,a in enumerate(g.doc['animations'])],gltf_metres_to_world=GLTF_TO_WORLD))
    if only is not None:
        previous=read(OUT/'fx-manifest.json');mids={79+skill for skill in only}
        models=[m for m in previous['models'] if m['id'] not in mids]+models;frames=[f for f in previous['frames'] if f['model'] not in mids]+frames;motions=[m for m in previous['motions'] if m['model'] not in mids]+motions
        models.sort(key=lambda m:m['id']);frames.sort(key=lambda f:(f['model'],f['clip'],f['direction'],f['frame']));motions.sort(key=lambda m:(m['model'],m['clip']))
    write(OUT/'fx-manifest.json',dict(complete=True,models=models,frames=frames,motions=motions,particle_sources=sources,particle_graphs=graphs,original_fx_meshes=mesh_receipts,scythe_particle_config=particle_config))

def merge_manifest(ss):
    body=read(OUT/'bodies-manifest.json') if (OUT/'bodies-manifest.json').exists() else {};fx=read(OUT/'fx-manifest.json') if (OUT/'fx-manifest.json').exists() else {};uirows=read(OUT/'ui-manifest.json') if (OUT/'ui-manifest.json').exists() else {}
    report=dict(complete=bool(body.get('complete') and len(body.get('models',[]))==2 and fx.get('complete') and len(fx.get('models',[]))==9 and uirows.get('complete')),models=body.get('models',[])+fx.get('models',[]),frames=body.get('frames',[])+fx.get('frames',[]),motions=body.get('motions',[])+fx.get('motions',[]),icons=uirows.get('icons',[]),
        shadow=projection_metadata(ss),projection=dict(zoom_q8=180,pitch_q8=181,directions=16),particle_sources=fx.get('particle_sources',[]),particle_graphs=fx.get('particle_graphs',[]),original_fx_meshes=fx.get('original_fx_meshes',[]),scythe_particle_config=fx.get('scythe_particle_config'),
        source_sha256={'tools/'+n:sha(ROOT/'tools'/n) for n in ['bake_extra_heroes.py','verify_extra_hero_assets.py','bake_skills.py','bake_effects.py','bake_sprites.py','bake_hero_shadows.py','gltf_assets.py','bake_sprite_raster.py','bake_sprite_raster.c']},
        scope='Two original complete Steam hero rigs with five default model wearables each; original Source scale/origin, 16 directions and original bone animation; separate compact directional shadows. Original particle texture/mesh resources interpreted offline for fixed-step H1 FX.')
    write(OUT/'manifest.json',report);print(json.dumps(dict(complete=report['complete'],frames=len(report['frames']),models=len(report['models']),icons=len(report['icons']))))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--part',choices=['all','sources','bodies','fx','ui'],default='all');p.add_argument('--workers',type=int,default=2);p.add_argument('--ss',type=int,default=2);p.add_argument('--fx-skill',type=int,choices=range(9),action='append');a=p.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);v=VpkArchive(STEAM/'pak01_dir.vpk')
    if a.part in ('all','sources'):source_definitions(v)
    if a.part in ('all','bodies'):
        args=[(s,a.ss) for s in HEROES]
        if a.workers>1:
            with concurrent.futures.ProcessPoolExecutor(max_workers=a.workers) as pool:results=list(pool.map(bake_body,args))
        else:results=list(map(bake_body,args))
        write(OUT/'bodies-manifest.json',dict(complete=True,models=[m for m,_,_ in results],frames=[r for _,rows,_ in results for r in rows],motions=[m for _,_,ms in results for m in ms]))
    if a.part in ('all','ui'):ui(v)
    if a.part in ('all','fx'):bake_fx(v,a.ss,a.fx_skill)
    merge_manifest(a.ss)

if __name__=='__main__':main()
