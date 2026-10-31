"""Bake original installed Pudge/Sniper/Sven/Tidehunter particle resources.

Original VPCF dependencies, material images and complete model triangles are
exported with CRC/SHA receipts. Source particle operators are inspected, not
executed: deterministic emission, CP placement and small-screen exposure are
explicit H1 adaptations. No generated replacement mesh or external image is
used. Runtime owns projectile positions, actual damage timing and visibility.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, math, re, subprocess, sys
from PIL import Image, ImageDraw
from gltf_assets import Gltf, np, ROOT
from source2_vpk import VpkArchive
from bake_skills import SkillCanvas
from bake_effects import linear, ZOOM, PITCH
from bake_sprites import source_normals
from bake_sprite_raster import Raster
from bake_hero_shadows import project_shadow, compact_shadow, projection_metadata

STEAM = Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
TOOL = ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
SOURCE = ROOT/'private/source2/exports/roster-fx'
OUT = ROOT/'private/baked/roster-fx'
SCALE = 100/2.54/4
SPECS = [
 (97,'pudge_meathook','pudge/pudge_meathook',100,'line'),
 (98,'pudge_rot','pudge/pudge_rot',95,'caster'),
 (99,'pudge_flesh_heap','pudge/pudge_fleshheap_block_activation',55,'caster'),
 (100,'pudge_dismember','pudge/pudge_dismember',55,'target'),
 (101,'sniper_shrapnel','sniper/sniper_shrapnel',100,'fixed_target'),
 (102,'sniper_headshot','sniper/sniper_headshot_slow',30,'target'),
 (103,'sniper_take_aim','sniper/sniper_take_aim_overhead',38,'caster'),
 (104,'sniper_assassinate','sniper/sniper_assassinate',100,'line'),
 (105,'sven_storm_bolt','sven/sven_spell_storm_bolt',78,'target'),
 (106,'sven_great_cleave','sven/sven_spell_great_cleave',90,'caster'),
 (107,'sven_warcry','sven/sven_spell_warcry',100,'caster'),
 (108,'sven_gods_strength','sven/sven_spell_gods_strength',65,'caster'),
 (109,'tidehunter_gush','tidehunter/tidehunter_gush',80,'line'),
 (110,'tidehunter_kraken_shell','tidehunter/tidehunter_shell',60,'caster'),
 (111,'tidehunter_anchor_smash','tidehunter/tidehunter_anchor',105,'caster'),
 (112,'tidehunter_ravage','tidehunter/tidehunter_spell_ravage',160,'caster'),
 (113,'sniper_concussive_grenade','sniper/sniper_shard_concussive_grenade_model',90,'target'),
 (114,'tidehunter_anchor','tidehunter/tidehunter_anchor_chain_proj',60,'fixed_target'),
 (115,'tidehunter_fish','tidehunter/tidehunter_leviathans_catch',32,'target'),
 (116,'sniper_base_attack','sniper/sniper_base_attack',32,'line'),
 (117,'pudge_hook_model','pudge/pudge_meathook_hook',35,'projectile'),
 (118,'sven_gauntlet_model','sven/sven_spell_storm_bolt_gauntlet',40,'projectile'),
 (119,'tidehunter_tentacle_model','tidehunter/tidehunter_ravage_tentacle_model',75,'fixed_target'),
 (120,'tidehunter_chain_link','tidehunter/tidehunter_anchor_chain_proj_model',15,'line')]
MESHES = {
 'hook':'models/heroes/pudge/righthook.vmdl_c',
 'gauntlet':'models/heroes/sven/gauntlet.vmdl_c',
 'tentacle':'models/heroes/tidehunter/tidehuntertentacle.vmdl_c',
 'grenade':'models/heroes/sniper/concussive_grenade.vmdl_c',
 'fish':'models/items/hex/fish_hex/tide_fish_hex.vmdl_c',
 'shell':'models/heroes/tidehunter/tidehunter_kraken_shell.vmdl_c',
 'kraken_shield':'models/heroes/tidehunter/tidehunter_kraken_shield.vmdl_c',
 'flesh_shield':'models/props_gameplay/status_shield.vmdl_c',
 'anchor':'particles/models/heroes/tidehunter/tidehunter_chain_model.vmdl_c',
 'chain_link':'models/heroes/tidehunter/tidehunter_chain_link.vmdl_c'}
EXTRA_GRAPHS = {104:['sniper/sniper_crosshair']}
# Every selected image is reachable from one of the exact original graphs.
TEXTURES = {
 'chain':'materials/particle/beam_chain.vtex_c',
 'beam':'materials/particle/particle_beam_generic.vtex_c',
 'irregular':'materials/particle/particle_glow_irregular1.vtex_c',
 'rot':'materials/particle/pool_01_color_tga_29a60c67.vtex_c',
 'smoke':'materials/particle/smoke1/smoke1.vtex_c',
 'smoke3':'materials/particle/smoke3/smoke3.vtex_c',
 'spray':'materials/particle/spray1/spray1.vtex_c',
 'blood':'materials/particle/droplets/droplets.vtex_c',
 'ring':'materials/particle/particle_ring_wavy6.vtex_c',
 'cloud':'materials/particle/projected_cloud_01_color_psd_64b7fd16.vtex_c',
 'sparks':'materials/particle/sparks/sparks.vtex_c',
 'glow':'materials/particle/particle_glow_05.vtex_c',
 'star':'materials/particle/star.vtex_c',
 'aim':'materials/particle/sniper/sniper_take_aim_icon.vtex_c',
 'fire':'materials/particle/fire_particle_10/fire_particle_10_low.vtex_c',
 'plasma':'materials/particle/beam_plasma_04.vtex_c',
 'electric':'materials/particle/electrical_arc_white/electrical_arc_white.vtex_c',
 'cleave':'materials/particle/beam_plasma_06.vtex_c',
 'wave':'materials/particle/particle_ring_wave_2.vtex_c',
 'water':'materials/particle/water_splash/water_splash.vtex_c',
 'shell':'materials/particle/tidehunter/tidehunter_kraken_shell.vtex_c',
 'ripple':'materials/particle/particle_ring_wave_10.vtex_c',
 'white':'materials/particle/particle_flares/aircraft_white_v3.vtex_c',
 'yellow':'materials/particle/particle_flares/aircraft_yellow.vtex_c',
 'crosshair':'materials/particle/bullseye/bullseye_anim.vtex_c',
 'flesh':'materials/models/props_gameplay/status_shield_fleshheap_color_psd_1268ab74.vtex_c'}

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def rel(path): return Path(path).resolve().relative_to(ROOT).as_posix()
def read(path): return json.loads(Path(path).read_text(encoding='utf8'))
def write(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def graph_path(root):
    hero,name=root.split('/')
    return f'particles/units/heroes/hero_{hero}/{name}.vpcf_c'

def graph_sources(v):
    rows=[]
    for mid,_,root,_,_ in SPECS:
        entry=graph_path(root);additional=[graph_path(s) for s in EXTRA_GRAPHS.get(mid,[])];pending=[entry,*additional];seen=set();textures=set();models=set()
        while pending:
            name=pending.pop()
            if name in seen: continue
            if name not in v.entries: raise ValueError('Missing original graph '+name)
            seen.add(name)
            for raw in re.findall(rb'(?:materials|particles|models)/[a-zA-Z0-9_./-]+?\.(?:vpcf|vmat|vtex|vmdl)(?:_c)?',v.read(name)):
                dep=raw.decode();dep=dep if dep.endswith('_c') else dep+'_c'
                if dep not in v.entries: continue
                if dep.endswith('.vtex_c'): textures.add(dep)
                elif dep.endswith('.vmdl_c'): models.add(dep)
                else: pending.append(dep)
        rows.append(dict(root=entry,additional_roots=additional,compiled_graphs=[v.identity(e) for e in sorted(seen)],referenced_textures=sorted(textures),referenced_models=sorted(models)))
    write(SOURCE/'particle-graphs.json',rows)
    return rows

def export_graph(v,entry):
    raw=SOURCE/'graphs'/entry;raw.parent.mkdir(parents=True,exist_ok=True)
    data=v.read(entry);raw.write_bytes(data);target=raw.with_suffix('.vpcf')
    identity=target.with_suffix('.identity.json')
    current=dict(source_sha256=sha(raw),tool_sha256=sha(TOOL))
    if not target.exists() or not identity.exists() or read(identity).get('source_sha256')!=current['source_sha256'] or read(identity).get('tool_sha256')!=current['tool_sha256'] or read(identity).get('decompiled_sha256')!=sha(target):
        p=subprocess.run([str(TOOL),'-d','-i',str(raw),'-o',str(target)],capture_output=True,text=True,encoding='utf8',errors='replace')
        raw.with_suffix('.log').write_text(p.stdout+p.stderr,encoding='utf8')
        if p.returncode or not target.exists(): raise RuntimeError('Cannot decompile '+entry)
        write(identity,dict(**current,decompiled_sha256=sha(target)))
    text=target.read_text(encoding='utf8')
    colours=[]
    for match in re.finditer(r'(\w*(?:Color|Tint)\w*)\s*=\s*\[([^]]+)\]',text):
        values=[float(n) for n in re.findall(r'-?\d+(?:\.\d+)?',match[2])]
        if len(values) in (3,4): colours.append(dict(field=match[1],value=values))
    radii=[dict(field=m[1],value=float(m[2])) for m in re.finditer(r'(\w*(?:Radius|Scale|Lifetime|Speed)\w*)\s*=\s*(-?\d+(?:\.\d+)?)',text)]
    return dict(source=v.identity(entry),decompiled=rel(target),decompiled_sha256=sha(target),colour_fields=colours,numeric_fields=radii)

def export_mesh(v,key,entry):
    target=SOURCE/'models'/key/(key+'.gltf')
    if not target.exists():
        subprocess.run([sys.executable,str(ROOT/'tools/source2_export.py'),'--name','roster_fx_'+key,'--entry',entry,'--output',str(target),'--skeleton'],check=True,capture_output=True)
    g=Gltf(target);parts=list(g.primitives(0,0) if g.doc.get('animations') else g.primitives())
    return dict(name=key,source=v.identity(entry),gltf=rel(target),gltf_sha256=sha(target),buffers=g.buffer_sha256,
                source_triangles=sum(len(p['faces']) for p in parts),source_animations=[dict(name=a['name'],duration=g.duration(i)) for i,a in enumerate(g.doc.get('animations',[]))],
                gltf_metres_to_world=SCALE,all_original_triangles=True)

def export_texture(v,key,entry):
    folder=SOURCE/'textures'/key;folder.mkdir(parents=True,exist_ok=True)
    raw=folder/(key+'.vtex_c');raw.write_bytes(v.read(entry));target=folder/(key+'.png')
    if not target.exists():
        p=subprocess.run([str(TOOL),'-d','-i',str(raw),'-o',str(target)],capture_output=True,text=True,encoding='utf8',errors='replace')
        (folder/'export.log').write_text(p.stdout+p.stderr,encoding='utf8')
        if p.returncode: raise RuntimeError('Cannot decode '+entry)
    paths=[target] if target.exists() and target.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n' else sorted(folder.glob(key+'_seq*.png'))
    if not paths: raise ValueError('No original image '+entry)
    return dict(name=key,source=v.identity(entry),raw_sha256=sha(raw),decoded=[dict(path=rel(p),sha256=sha(p),size=list(Image.open(p).size)) for p in paths])

def sources(workers=3):
    SOURCE.mkdir(parents=True,exist_ok=True);v=VpkArchive(STEAM/'pak01_dir.vpk');graphs=graph_sources(v)
    reachable={t for g in graphs for t in g['referenced_textures']}
    assert all(e in reachable for e in TEXTURES.values())
    entries=sorted({r['path'] for g in graphs for r in g['compiled_graphs'] if r['path'].endswith('.vpcf_c')})
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        config=list(pool.map(lambda e:export_graph(v,e),entries))
        meshes=list(pool.map(lambda kv:export_mesh(v,*kv),MESHES.items()))
        textures=list(pool.map(lambda kv:export_texture(v,*kv),TEXTURES.items()))
    units=v.read('scripts/npc/npc_units.txt');text=units.decode('utf-8-sig')
    start=text.index('"npc_dota_unit_tidehunter_anchor"');end=text.index('\n\t"',start+len('"npc_dota_unit_tidehunter_anchor"'))
    block=text[start:end];assert 'particles/models/heroes/tidehunter/tidehunter_chain_model.vmdl' in block
    (SOURCE/'tidehunter-anchor-unit.txt').write_text(block,encoding='utf8')
    receipt=dict(complete=True,graphs=graphs,particle_configs=config,meshes=meshes,textures=textures,tool_sha256=sha(TOOL),source_vpk_tree_sha256=v.identity(entries[0])['vpk_tree_sha256'],particle_engine_executed=False,
                 dead_in_water_unit=dict(source=v.identity('scripts/npc/npc_units.txt'),block=rel(SOURCE/'tidehunter-anchor-unit.txt'),block_sha256=sha(SOURCE/'tidehunter-anchor-unit.txt'),model='particles/models/heroes/tidehunter/tidehunter_chain_model.vmdl_c'))
    write(SOURCE/'source-manifest.json',receipt)
    print(json.dumps(dict(source_graphs=len(entries),meshes=len(meshes),textures=len(textures))),flush=True)
    return receipt

TEXTURE_CACHE = None
CONFIG_CACHE = None
MESH_CACHE = {}
def textures():
    global TEXTURE_CACHE
    if TEXTURE_CACHE is None:
        TEXTURE_CACHE={}
        for row in read(SOURCE/'source-manifest.json')['textures']:
            seq=[]
            for file in row['decoded'][:12]:
                rgba=np.asarray(Image.open(ROOT/file['path']).convert('RGBA'),dtype=np.float32)/255
                rgb=linear(rgba[:,:,:3]);peak=rgb.max(2)
                seq.append(np.dstack((rgb/np.maximum(peak[:,:,None],1e-8),rgba[:,:,3]*peak)).astype(np.float32))
            TEXTURE_CACHE[row['name']]=seq
    return TEXTURE_CACHE

def source_tint(graph,field,index=0):
    global CONFIG_CACHE
    if CONFIG_CACHE is None:
        CONFIG_CACHE={Path(r['source']['path']).stem:r for r in read(SOURCE/'source-manifest.json')['particle_configs']}
    rows=[r['value'] for r in CONFIG_CACHE[graph]['colour_fields'] if r['field']==field]
    if len(rows)<=index: raise ValueError('Missing original colour '+graph+':'+field)
    return tuple(float(v)/255 for v in rows[index][:3])

def source_number(graph,field):
    source_tint('pudge_dismember','m_ColorMax') # Initialize shared source cache.
    rows=[r['value'] for r in CONFIG_CACHE[graph]['numeric_fields'] if r['field']==field]
    if not rows:raise ValueError('Missing original value '+graph+':'+field)
    return rows[0]

class Mesh:
    def __init__(self,name):
        self.name=name;self.g=Gltf(SOURCE/'models'/name/(name+'.gltf'));self.textures={};self.poses={}
        self.ai=next((i for i,a in enumerate(self.g.doc.get('animations',[])) if a['name']=='spike_15frame'),0) if self.g.doc.get('animations') else None
    def pose(self,seconds):
        seconds=seconds if self.ai is not None and self.g.duration(self.ai)>0 else 0
        if seconds not in self.poses:
            matrices=self.g.globals(self.ai,seconds);parts=list(self.g.primitives(self.ai,seconds))
            for p in parts:p['normals']=source_normals(self.g,p,matrices)
            xyz=np.concatenate([p['points'] for p in parts]);self.poses[seconds]=(parts,xyz)
        return self.poses[seconds]
    def texture(self,mi):
        if mi not in self.textures:
            material=self.g.material(mi);m=material['source'];vmat=m.get('extras',{}).get('vmat',{})
            image=np.asarray(Image.open(material['image']).convert('RGBA')).copy();factor=np.asarray(material['factor'],float)
            for key,value in vmat.get('TextureParams',{}).items():
                if key.startswith('g_tColor') and Path(value.replace('\\','/')).stem==Path(material['image']).stem:
                    factor[:3]*=vmat.get('VectorParams',{}).get('g_vColorTint'+key[len('g_tColor'):],[1,1,1])[:3];break
            if self.name=='flesh_shield':
                image=np.asarray(Image.open(ROOT/read(SOURCE/'source-manifest.json')['textures'][-1]['decoded'][0]['path']).convert('RGBA')).copy()
            cutoff=float(m.get('alphaCutoff',.5)) if m.get('alphaMode')=='MASK' else 0
            if m.get('alphaMode','OPAQUE')=='OPAQUE':image[:,:,3]=255
            self.textures[mi]=(image,cutoff,factor)
        return self.textures[mi]

def add_mesh(c,name,seconds,direction,offset=(0,0,0),scale=1,strength=1,center=False,ground=False,tilt=0):
    if name not in MESH_CACHE:MESH_CACHE[name]=Mesh(name)
    model=MESH_CACHE[name];parts,xyz=model.pose(seconds);origin=(xyz.min(0)+xyz.max(0))*.5
    # Original fish animation starts with a deliberately zero-size birth pose.
    # H1 pickup presentation samples its visible phase and centers the CP at
    # the actual collectible; all animated bone/triangle deformation remains.
    raster=Raster(c.width,c.height,c.ss);angle=direction*math.tau/16;co=math.cos(angle);si=math.sin(angle)
    for p in parts:
        q=p['points'].copy();n=p['normals']
        if center:q-=origin
        elif ground:q[:,1]-=xyz[:,1].min()
        q=np.column_stack((-q[:,0],q[:,2],q[:,1]))*SCALE*scale;n=np.column_stack((-n[:,0],n[:,2],n[:,1]))
        if tilt:
            ct=math.cos(tilt);st=math.sin(tilt);y=q[:,1].copy();z=q[:,2].copy();q[:,1]=y*ct-z*st;q[:,2]=y*st+z*ct
            y=n[:,1].copy();z=n[:,2].copy();n[:,1]=y*ct-z*st;n[:,2]=y*st+z*ct
        x=q[:,0].copy();y=q[:,1].copy();q[:,0]=x*co-y*si;q[:,1]=x*si+y*co;q+=offset
        x=n[:,0].copy();y=n[:,1].copy();n[:,0]=x*co-y*si;n[:,1]=x*si+y*co
        projected=np.column_stack((c.ax+q[:,0]*ZOOM,c.ay+(q[:,1]-q[:,2])*PITCH,-q[:,1]-q[:,2]))
        image,cutoff,factor=model.texture(p['material']);raster.draw(projected,p['faces'],p['uv'],n,image,cutoff=cutoff,factor=factor)
    colour=raster.colour;alpha=colour[:,:,3]*strength
    # Opaque original material pixels occlude earlier particles. Subsequent
    # emission may be added above the mesh, matching the small H1 layer stack.
    c.rgb=c.rgb*(1-alpha[:,:,None])+colour[:,:,:3]*strength
    c.alpha=alpha+c.alpha*(1-alpha);c.depth=np.minimum(c.depth,np.where(alpha>.005,raster.depth,np.inf))

def splash(c,t,p,r,tint,strength=1):
    c.ring(t['ripple'],r,3.5,tint,strength*.7,p,arcs=6)
    for j in range(7):
        a=j*math.tau/7+p*.4;z=3+18*math.sin((p+j*.13)%1*math.pi)
        c.sprite(t['water'],math.cos(a)*r*.7,math.sin(a)*r*.7,z,9,13,tint,strength*.8,a)

def frame(mid,clip,number,direction,ss):
    p=number/(8 if clip==1 else 7);c=SkillCanvas(ss);angle=direction*math.tau/16
    seq=textures();t={k:s[number%len(s)] for k,s in seq.items()}
    life=.12+.88*math.sin(p*math.pi) if clip!=1 else .75+.25*math.cos(p*math.tau)
    white=(1,1,1);water=source_tint('tidehunter_gush_b','m_ColorMin');blood=source_tint('pudge_dismember','m_ColorMax')
    gold=source_tint('sniper_shard_concussive_grenade_ground_rings','m_ConstantColor')
    dx=-math.sin(angle);dy=math.cos(angle)
    if mid==97:
        if clip<2:
            # This is a normalized200-world rope segment. Runtime stretches
            # it between caster hand and actual117 projectile, never teleports.
            c.ray(t['chain'],-dx*100,-dy*100,10,dx*100,dy*100,10,4,source_tint('pudge_meathook_chain','m_ColorMax'),1.4)
            for j in range(4):
                d=-75+j*50;c.glow(t['irregular'],dx*d,dy*d,10,2,white,.2*life)
        else:
            for j in range(9):
                a=j*math.tau/9;r=5+28*p;c.sprite(t['blood'],math.cos(a)*r,math.sin(a)*r,7+17*math.sin(p*math.pi),6,10,blood,life,a)
    elif mid==98:
        rot=source_tint('pudge_rot_body','m_ColorMin')
        c.ground(t['rot'],90,p*.3,rot,.27*life)
        for j in range(11):
            a=j*math.tau/11+p*.6;r=22+50*((p+j*.31)%1);z=5+24*((p+j*.17)%1)
            c.sprite(t['smoke'],math.cos(a)*r,math.sin(a)*r,z,19,20,rot,.38*life,a)
            if j%3==0:c.glow(t['irregular'],math.cos(a)*r,math.sin(a)*r,z,1.7,rot,life*.8)
    elif mid==99:
        flesh=source_tint('pudge_fleshheap_block_activation','m_ColorMin')
        for j in range(3):
            a=j*math.tau/3+p*.6
            add_mesh(c,'flesh_shield',0,round(j*16/3)%16,offset=(math.cos(a)*25,math.sin(a)*25,30),scale=source_number('pudge_fleshheap_block_shield_model','m_flConstantRadius'),strength=.65*life,center=True)
        for j in range(5):
            a=j*math.tau/5+p;c.ray(t['plasma'],math.cos(a)*22,math.sin(a)*22,3,math.cos(a+.5)*17,math.sin(a+.5)*17,40,3,flesh,life*.75)
    elif mid==100:
        for j in range(11):
            a=j*math.tau/11-p;r=6+20*p;z=8+28*((p+j*.17)%1)
            c.sprite(t['blood'],math.cos(a)*r,math.sin(a)*r,z,6,10,blood,life*1.6,a)
        for side in (-1,1):c.ray(t['beam'],side*28,0,30,0,0,14,4,blood,life*.9)
    elif mid==101:
        c.ground(t['cloud'],100,p*.13,source_tint('sniper_shrapnel_aoe_proj','m_ColorMin'),.6*life)
        for j in range(12):
            a=j*2.399963;r=18+(j*31%78);x=math.cos(a)*r;y=math.sin(a)*r;fall=(p+j*.137)%1
            c.ray(t['sparks'],x-4,y-5,68*(1-fall)+8,x,y,8+48*(1-fall),1.6,gold,life*.9)
            if fall>.55:c.glow(t['yellow'],x,y,2,2.5,gold,life*2)
            if j%3==0:c.sprite(t['smoke3'],x,y,3+20*fall,16,14,white,.17*life,a)
    elif mid==102:
        yellow=source_tint('sniper_headshot_slow_a','m_ConstantColor')
        c.sprite(t['star'],0,0,25,10+6*p,10+6*p,yellow,life*1.3,p)
        c.ray(t['beam'],-14,0,25,14,0,25,2,yellow,life)
    elif mid==103:
        c.sprite(t['aim'],0,0,65,12,12,white,life*1.1)
        c.ring(t['wave'],20+5*p,2,gold,life*.4,p,arcs=6)
    elif mid==104:
        if clip==0:
            c.sprite(t['crosshair'],0,0,42,17,17,source_tint('sniper_assassinate_light','m_ConstantColor'),life*1.6)
        elif clip==1:
            # Thin white-hot bullet core with original warm flare halo.
            c.ray(t['plasma'],-dx*100,-dy*100,0,dx*100,dy*100,0,3.6,gold,1.5)
            c.ray(t['beam'],-dx*100,-dy*100,0,dx*100,dy*100,0,1.2,white,2.6)
        else:
            c.glow(t['glow'],0,0,18,12+12*p,gold,life*2)
            for j in range(8):
                a=j*math.tau/8;r=10+24*p;c.sprite(t['sparks'],math.cos(a)*r,math.sin(a)*r,12,9,3,white,life,a)
    elif mid==105:
        bolt=source_tint('sven_spell_storm_bolt','m_ConstantColor')
        c.glow(t['glow'],0,0,18,10+10*p,bolt,life*1.5)
        for j in range(6):
            a=j*math.tau/6+p;radius=(30+40*p) if clip==2 else 24
            c.ray(t['electric'],0,0,18,math.cos(a)*radius,math.sin(a)*radius,4+(j%3)*13,4,bolt,life)
        if clip==2:c.ring(t['wave'],20+55*p,3.5,bolt,life*.65,p)
    elif mid==106:
        cleave=source_tint('sven_spell_great_cleave','m_LightColor1')
        c.ribbon(t['cleave'],angle,100+70*p,35,18,cleave,life,p,bend=16)
        for j in range(5):
            a=angle+(j-2)*.18;ddx=-math.sin(a);ddy=math.cos(a)
            c.ray(t['cleave'],ddx*15,ddy*15,20,ddx*(65+25*p),ddy*(65+25*p),10,3,cleave,life*.7)
    elif mid in (107,108):
        tint=source_tint('sven_spell_warcry_wave','m_ColorMin') if mid==107 else source_tint('sven_spell_gods_strength_glow','m_ColorMin')
        if clip!=1:c.ring(t['wave'],15+(75 if mid==107 else 40)*p,3,tint,life*.8,p)
        for j in range(7):
            a=j*math.tau/7+p*.4;r=16+4*math.sin(p*math.tau+j);z=5+48*((p+j*.13)%1)
            c.sprite(t['cleave'],math.cos(a)*r,math.sin(a)*r,z,5,18,tint,life*.55,a)
        for x in (-5,5):c.glow(t['glow'],x,0,45,1.5,tint,life*1.6)
    elif mid==109:
        if clip==1:
            c.ribbon(t['water'],angle,80,24,0,water,life*1.3,p,bend=4)
            c.glow(t['glow'],0,0,0,6,water,life)
        else:splash(c,t,p,20+55*p,water,life)
    elif mid==110:
        if clip==1:add_mesh(c,'kraken_shield',0,0,scale=source_number('tidehunter_shell_shield_model','m_flConstantRadius'),strength=.45*life,center=True,offset=(0,0,29))
        else:add_mesh(c,'shell',0,0,scale=source_number('tidehunter_shell_model','m_flConstantRadius'),strength=.35*life,center=True,offset=(0,0,29))
        c.ground(t['shell'],44,p*.2,water,.55*life)
        splash(c,t,p,24+24*p,water,life*.4)
    elif mid==111:
        splash(c,t,p,25+78*p,source_tint('tidehunter_anchor_splash','m_ColorMin'),life*1.4)
        for j in range(8):
            a=j*math.tau/8+p*.6;c.ray(t['water'],math.cos(a)*20,math.sin(a)*20,12,math.cos(a)*(55+40*p),math.sin(a)*(55+40*p),2,6,water,life*.7)
    elif mid==112:
        # Only the low advancing water rim is baked here. Runtime places real
        # model119 along successive rings at725Source/s, not a ring-only ult.
        radius=160 if clip==1 else 15+145*p if clip==0 else 110+35*p
        c.ring(t['ripple'],radius,6,source_tint('tidehunter_ravage_water_b','m_ConstantColor'),life*.8,p,arcs=12)
        for j in range(10):
            a=j*math.tau/10;c.sprite(t['spray'],math.cos(a)*radius,math.sin(a)*radius,10,11,18,water,life*.6,a)
    elif mid==113:
        if clip<2:
            add_mesh(c,'grenade',0,direction,center=True,offset=(0,0,6),tilt=p*math.tau)
            c.glow(t['glow'],0,0,6,3,gold,life)
        else:
            c.ring(t['ring'],18+65*p,5,gold,life,p)
            c.glow(t['white'],0,0,6,20+20*p,gold,life*1.6)
            for j in range(10):
                a=j*math.tau/10;c.sprite(t['smoke'],math.cos(a)*45*p,math.sin(a)*45*p,12+15*p,16,20,gold,life*.5,a)
    elif mid==114:
        add_mesh(c,'anchor',0,0,strength=.3+.7*life if clip!=1 else 1)
        splash(c,t,p,15+25*p,water,life*.5)
    elif mid==115:
        add_mesh(c,'fish',.55+p*.45,direction,center=True,offset=(0,0,12+8*math.sin(p*math.pi)))
        splash(c,t,p,12+10*p,water,.3*life)
    elif mid==116:
        if clip<2:
            c.ray(t['beam'],-dx*26,-dy*26,0,dx*8,dy*8,0,1.25,white,2.8)
            c.ray(t['plasma'],-dx*26,-dy*26,0,dx*8,dy*8,0,3,gold,life*1.1)
            c.glow(t['glow'],dx*8,dy*8,0,2.3,white,life*2)
        else:
            c.glow(t['glow'],0,0,0,6+5*p,gold,life*1.8)
            for j in range(5):
                a=j*math.tau/5;c.sprite(t['sparks'],math.cos(a)*10*p,math.sin(a)*10*p,2,5,3,gold,life,a)
    elif mid==117:
        seconds=p*(4.5 if clip==1 else .7)
        add_mesh(c,'hook',0,direction,center=True,tilt=seconds*math.radians(80))
        if clip==2:c.sprite(t['blood'],0,0,0,15,15,blood,.7*life)
    elif mid==118:
        add_mesh(c,'gauntlet',0,direction,center=True,scale=1.2)
        bolt=source_tint('sven_spell_storm_bolt','m_ConstantColor')
        for j in range(4):
            a=j*math.tau/4+p;c.ray(t['electric'],math.cos(a)*13,math.sin(a)*13,0,math.cos(a+.5)*19,math.sin(a+.5)*19,6,2.5,bolt,life*.9)
    elif mid==119:
        # Preserve the original spike_15frame bone deformation and below-ground
        # start/end; source0.5s maps to ten H1ticks. Fixed yaw4 presents broadside.
        phase=min(.95,max(.05,p));add_mesh(c,'tentacle',phase*.5,4)
        splash(c,t,p,12+10*p,water,life*.8)
    elif mid==120:
        add_mesh(c,'chain_link',0,direction,center=True,scale=source_number('tidehunter_anchor_chain_proj_model','m_flConstantRadius'),offset=(0,0,2))
    else:raise ValueError(mid)
    return c.finish()

def directional(mid,clip):
    return mid in (97,106,116,117,118) and clip<2 or mid==104 and clip==1 or mid==109 and clip==1 or mid==113 and clip<2 or mid in (115,120)

def runtime_contract(mid):
    special={
      97:'Normalized200-world chain ribbon. Stretch/rotate between caster hand and live117 head. Actual hook head/pull belongs to game trajectory; no instant endpoint hook.',
      99:'Three original status_shield meshes with original fleshheap material image and Source2.5x radius,25-world orbit; source meat-coloured ropes/emission. H1 CP orbit is deterministic.',
      101:'Actual Shrapnel delayed rain-field: clip1 loops only after24tick Source delay; individual falling orange sparks and smoking impacts, not a persistent flash.',
      102:'Passive Headshot hit-only feedback; never start a cast animation.',
      104:'Clip0 original bullseye at locked target during40tick aim. Clip1 white-hot/orange normalized200-world shot, stretch live endpoints; clip2 hit.',
      105:'Original StormBolt graph is yellow lightning with a flying gauntlet. Use118 for live projectile and105clip2 for hit radius; do not invent a hammer mesh.',
      106:'Passive GreatCleave hit-only arc; rotate source-to-target. Never castable.',
      110:'Original KrakenShell/shell/shield meshes at Source0.8x/2.5x particle radii and115Source CP height, partial alpha; gameplay supplies true active/passive state.',
      112:'Water rim reference radius160world. Expand to actual max312.5world at181.25world/s. At each advancing ring distribute119 original animated tentacles at angular spacing about25world; stagger births with wave travel, each ten ticks. Do not use water rim alone.',
      114:'Current DeadInWater NPC original particles/models/heroes/tidehunter/tidehunter_chain_model mesh, not deprecated arm_of_the_deep. Clip1 ground unit persists while game kind7 anchor has HP, then clip2. Tether endpoints belong to gameplay.',
      115:'Current LeviathansCatch original fish_hex model and visible0.55..1.0s phase of fish_leviathans_catch skeleton. H1 recenters CP for collectible visibility; zero-size birth/despawn phases are excluded from the persistent pickup. No fake fish sprite.',
      116:'Original Sniper attack textures, narrow white1.25px core/orange3px halo. Runtime supplies projectile height and source-to-target16dir; no generic fireball.',
      117:'All278 original righthook triangles/materials, native1x scale, centered at live hook projectile. Direction0+worldY; rotations16. Original80degrees/sec spin; clip1 full turn90ticks. Source CP economy substitutions are not executed.',
      118:'All834 original Sven gauntlet triangles/materials, Source particle1.2x scale,16directions. This is the original projectile, not an invented hammer.',
      119:'All929 original tentacle triangles/materials, original spike_15frame skeleton0.5s. Fixed camera-facing yaw4; render many local ten-tick births along advancing Ravage ring. No model resizing from ult radius.',
      120:'Original tidehunter_chain_link model with original Source particle ConstantRadius10. Uniform Source scale preserves proportions: longest axis14.99world, about11.11world wide and4.79world thick. One static frame in16directions; runtime repeats every12world along true tether, not repeated114 anchors.'}
    return special.get(mid,'Original graph images with explicit deterministic offline emission; gameplay controls true start/loop/end timing and effect anchor.')

def bake_model(args):
    spec,ss=args;mid,name,root,radius,anchor=spec;folder=OUT/name;folder.mkdir(parents=True,exist_ok=True)
    records=[];clips=[];previews=[]
    for clip in range(1 if mid==120 else 3):
        count=1 if mid==120 else 8
        directions=16 if directional(mid,clip) else 1;ticks=10 if mid==119 else 90 if mid==117 and clip==1 else 16 if clip==1 else 14
        motion=dict(model=mid,clip=clip,frames=count,directions=directions,duration_ticks=ticks,loop=clip==1 and mid!=119,role=['cast/release','loop/travel','impact/end'][clip],source_clip=root,static_visual=mid==120 or mid==104 and clip==1)
        clips.append(motion);images=[]
        for number in range(count):
            for direction in range(directions):
                rgba,depth,ax,ay=frame(mid,clip,number,direction,ss);path=folder/f'{clip}-{direction:02d}-{number:02d}.npz'
                np.savez_compressed(path,rgba=rgba,depth=depth)
                # Original physical meshes use normal alpha/depth rather than
                # additive blending, so dark chain/metal/fish stay visible.
                flags=0 if mid in (113,114,115,117,118,119,120) and not(mid==113 and clip==2) else 2 if mid in (98,99,110) else 3
                records.append(dict(model=mid,clip=clip,direction=direction,frame=number,frames=count,duration_ticks=ticks,loop=motion['loop'],source_clip=root,
                    anchor_x=int(ax),anchor_y=int(ay),width=rgba.shape[1],height=rgba.shape[0],height_world=radius,npz=rel(path),sha256=sha(path),flags=flags))
                if direction==(4 if directions==16 else 0):
                    bg=Image.new('RGBA',(240,224),(30,43,39,255));bg.alpha_composite(Image.fromarray(rgba),(120-ax,132-ay));images.append(bg.convert('RGB'))
        images[0].save(folder/f'preview-{clip}.gif',save_all=True,append_images=images[1:],duration=max(25,round(ticks*50/8)),loop=0);images[min(4,len(images)-1)].save(folder/f'preview-{clip}.png');previews.append(images[min(4,len(images)-1)])
    model=dict(id=mid,name=name,reference_radius_world=radius,height_world=radius,reference_line_length_world=200 if mid in (97,104) else 0,anchor=anchor,
               clips=clips,particle_engine_executed=False,offline_emission=True,adaptation=runtime_contract(mid))
    print(json.dumps(dict(model=mid,name=name,frames=len(records))),flush=True)
    write(folder/'model-manifest.json',dict(model=model,frames=records,motions=clips,baker_sha256=sha(__file__)))
    return model,records,clips

def entity_shadows(frames,ss):
    class ShadowMesh:
        def __init__(self,model):self.model=model
        def texture(self,doc,material):return self.model.texture(material)
    for r in frames:
        if r['model'] not in (114,115):continue
        mid=r['model'];p=r['frame']/(8 if r['clip']==1 else 7);name='anchor' if mid==114 else 'fish'
        if name not in MESH_CACHE:MESH_CACHE[name]=Mesh(name)
        model=MESH_CACHE[name];parts,xyz=model.pose(.55+p*.45 if mid==115 else 0);origin=(xyz.min(0)+xyz.max(0))*.5;pose=[]
        for original in parts:
            q=original['points'].copy()
            if mid==115:q-=origin
            q=np.column_stack((-q[:,0],q[:,2],q[:,1]))*SCALE
            if mid==115:q[:,2]+=12+8*math.sin(p*math.pi)
            pose.append(dict(original,points=q,doc=0))
        shadow,sax,say=compact_shadow(*project_shadow(ShadowMesh(model),pose,r['direction'],ss))
        path=ROOT/r['npz']
        with np.load(path) as data:rgba=data['rgba'];depth=data['depth']
        before=hashlib.sha256(rgba.tobytes()+depth.tobytes()).hexdigest();np.savez_compressed(path,rgba=rgba,depth=depth,shadow=shadow)
        with np.load(path) as data:assert hashlib.sha256(data['rgba'].tobytes()+data['depth'].tobytes()).hexdigest()==before
        r.update(flags=r['flags']|4,shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],shadow_anchor_x=sax,shadow_anchor_y=say,shadow_pixels=int(np.count_nonzero(shadow)),shadow_alpha_max=int(shadow.max()),sha256=sha(path))

def previews(models,frames):
    sheet=Image.new('RGB',(960,math.ceil(len(models)/4)*246),(20,28,28));draw=ImageDraw.Draw(sheet)
    for i,m in enumerate(models):
        scale=256//m.get('raster_scale_q8',256)
        for clip in [x['clip'] for x in m['clips']]:
            motion=next(c for c in m['clips'] if c['clip']==clip);direction=4 if motion['directions']==16 else 0
            rows=sorted([r for r in frames if r['model']==m['id'] and r['clip']==clip and r['direction']==direction],key=lambda r:r['frame']);images=[]
            for r in rows:
                with np.load(ROOT/r['npz']) as data:
                    body=Image.fromarray(data['rgba']);bg=Image.new('RGBA',(240,224),(30,43,39,255))
                    if r['flags']&4:
                        sh=Image.new('RGBA',(r['shadow_width'],r['shadow_height']),(0,0,0,0));sh.putalpha(Image.fromarray(data['shadow']));bg.alpha_composite(sh,(120-r['shadow_anchor_x'],132-r['shadow_anchor_y']))
                if scale>1:body=body.resize((body.width*scale,body.height*scale),Image.Resampling.NEAREST)
                bg.alpha_composite(body,(120-r['anchor_x']*scale,132-r['anchor_y']*scale));images.append(bg.convert('RGB'))
            images[0].save(OUT/m['name']/f'preview-{clip}.gif',save_all=True,append_images=images[1:],duration=max(25,round(motion['duration_ticks']*50/8)),loop=0);images[min(4,len(images)-1)].save(OUT/m['name']/f'preview-{clip}.png')
            if clip==(1 if len(m['clips'])>1 else 0):
                x=i%4*240;y=i//4*246;sheet.paste(images[min(4,len(images)-1)],(x,y+22));draw.text((x+4,y+3),str(m['id'])+' '+m['name'],fill='white')
    sheet.save(OUT/'contact.png')

def bake(ss,workers,only=None):
    OUT.mkdir(parents=True,exist_ok=True);specs=[s for s in SPECS if only is None or s[0] in only]
    if workers>1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as pool:result=list(pool.map(bake_model,[(s,ss) for s in specs]))
    else:result=list(map(bake_model,[(s,ss) for s in specs]))
    models=[m for m,_,_ in result];frames=[r for _,rs,_ in result for r in rs];motions=[c for _,_,cs in result for c in cs]
    if only is not None and (OUT/'manifest.json').exists():
        old=read(OUT/'manifest.json');models=[m for m in old['models'] if m['id'] not in only]+models;frames=[r for r in old['frames'] if r['model'] not in only]+frames;motions=[r for r in old['motions'] if r['model'] not in only]+motions
    models.sort(key=lambda r:r['id']);frames.sort(key=lambda r:(r['model'],r['clip'],r['direction'],r['frame']));motions.sort(key=lambda r:(r['model'],r['clip']))
    from compact_skill_fx import half
    compaction=[]
    for model in models:
        rows=[r for r in frames if r['model']==model['id']];loop=[r for r in rows if r['clip']==1 and r['direction']==0]
        factor=model.get('raster_scale_q8',256);original=model.get('original_reference_radius_world',model['reference_radius_world'])
        if factor==256 and (max(r['width']*r['height']*5 for r in rows)>110*1024 or sum(r['width']*r['height']*5 for r in loop)>640*1024):
            for r in rows:
                file=ROOT/r['npz']
                with np.load(file) as data:rgba=data['rgba'];depth=data['depth']
                rgba,depth,ax,ay=half(rgba,depth,r['anchor_x'],r['anchor_y']);np.savez_compressed(file,rgba=rgba,depth=depth)
                r.update(width=rgba.shape[1],height=rgba.shape[0],anchor_x=ax,anchor_y=ay,height_world=original//2,sha256=sha(file),raster_scale_q8=128)
            factor=128
        model.update(original_reference_radius_world=original,reference_radius_world=original*factor//256,height_world=original*factor//256,raster_scale_q8=factor)
        compaction.append(dict(model=model['id'],raster_scale_q8=factor,maximum_frame_bytes=max(r['width']*r['height']*5 for r in rows),loop_direction0_bytes=sum(r['width']*r['height']*5 for r in loop)))
    entity_shadows(frames,ss)
    source=read(SOURCE/'source-manifest.json')
    manifest=dict(complete=len(models)==24,models=models,frames=frames,motions=motions,original_source=source,source_manifest=rel(SOURCE/'source-manifest.json'),source_manifest_sha256=sha(SOURCE/'source-manifest.json'),
                  projection=dict(zoom_q8=180,pitch_q8=181,directions=16,direction0='+worldY',depth='signed -(localY+localZ)',supersamples=ss),compaction=compaction,shadow=projection_metadata(ss),
                  source_sha256={rel(ROOT/'tools'/p):sha(ROOT/'tools'/p) for p in ['bake_roster_fx.py','bake_skills.py','bake_effects.py','bake_sprites.py','bake_sprite_raster.py','bake_sprite_raster.c','gltf_assets.py','compact_skill_fx.py','bake_hero_shadows.py']},
                  limitations='All model triangles/material images and named animations are original local Steam resources. Source operator graphs are inspected/decompiled and dependencies CRC-verified; H1 deterministic CP placement, emission count, fixed camera, exposure/alpha and finite eight-frame timing are explicit adaptations. No Source refraction, lighting simulation or economy substitutions execute at runtime.')
    write(OUT/'manifest.json',manifest)
    previews(models,frames);print(json.dumps(dict(complete=manifest['complete'],models=len(models),frames=len(frames))),flush=True)

def patch_ravage():
    """Correct only live112/1 rim payloads after integration-contract review.

    Runtime already supplies the advancing wave radius. The canonical loop
    must remain radius160 and animate water only, otherwise the two radius
    animations multiply and reset. All other2,600 frame files stay untouched.
    """
    from compact_skill_fx import half
    path=OUT/'manifest.json';manifest=read(path);before={r['npz']:sha(ROOT/r['npz']) for r in manifest['frames']};changed=[]
    rows=[r for r in manifest['frames'] if r['model']==112 and r['clip']==1]
    assert len(rows)==8 and all(r['direction']==0 for r in rows)
    for row in rows:
        rgba,depth,ax,ay=frame(112,1,row['frame'],0,2);rgba,depth,ax,ay=half(rgba,depth,ax,ay)
        file=ROOT/row['npz'];np.savez_compressed(file,rgba=rgba,depth=depth)
        row.update(width=rgba.shape[1],height=rgba.shape[0],anchor_x=ax,anchor_y=ay,height_world=80,raster_scale_q8=128,sha256=sha(file));changed.append(row['npz'])
    for file,digest in before.items():
        if file not in changed:assert sha(ROOT/file)==digest
    model=next(m for m in manifest['models'] if m['id']==112);model['adaptation']+=' Clip1 is fixed canonical160world with animated water; live radius is applied exactly once by runtime.'
    all_rows=[r for r in manifest['frames'] if r['model']==112];summary=next(c for c in manifest['compaction'] if c['model']==112)
    summary.update(maximum_frame_bytes=max(r['width']*r['height']*5 for r in all_rows),loop_direction0_bytes=sum(r['width']*r['height']*5 for r in rows))
    manifest['source_sha256']['tools/bake_roster_fx.py']=sha(__file__);write(path,manifest)
    # Preview changes are derived display files, never packed game payloads.
    previews(manifest['models'],manifest['frames'])
    proof=dict(passed=True,changed_frame_count=8,unchanged_frame_count=len(before)-8,changed_npz=changed,all_other_npz_sha256_preserved=True,canonical_loop_radius_world=160,baked_reference_radius_world=80,manifest_sha256=sha(path),baker_sha256=sha(__file__))
    write(ROOT/'build/tests/v022-roster-fx-ravage/verification.json',proof);print(json.dumps(proof),flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--part',choices=['sources','fx','all','patch-ravage'],default='all');parser.add_argument('--workers',type=int,default=3);parser.add_argument('--ss',type=int,default=2);parser.add_argument('--model',type=int,choices=range(97,121),action='append');args=parser.parse_args()
    if args.part in ('sources','all'): sources(args.workers)
    if args.part in ('fx','all'): bake(args.ss,args.workers,args.model)
    if args.part=='patch-ravage':patch_ravage()

if __name__=='__main__': main()
