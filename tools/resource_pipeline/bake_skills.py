"""Bake all sixteen skill-specific FX from original installed Steam assets.

This is an offline interpretation of each skill: original textures and the
ward/ice meshes are rendered on the PC. Particle timing, paths and emission
are deliberately reconstructed for the existing fixed-step H1 rules.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,math,re,subprocess
from PIL import Image,ImageDraw
from gltf_assets import Gltf,np,ROOT
from source2_vpk import VpkArchive
from bake_effects import Canvas,sample,linear,srgb,STEAM,ZOOM,PITCH
from bake_sprite_raster import Raster

OUT=ROOT/'private/baked/skills'
SOURCE=ROOT/'private/source2/exports/skill-fx'
TEXTURES={
    'glow':'materials/particle/softglow.vtex_c',
    'spin':'materials/particle/juggernaut/juggernaut_blade_fury.vtex_c',
    'red':'materials/particle/particle_flares/aircraft_red.vtex_c',
    'green':'materials/particle/particle_flares/aircraft_green.vtex_c',
    'blue':'materials/particle/particle_flares/aircraft_blue2.vtex_c',
    'fire':'materials/particle/fire_particle_10/fire_particle_10_low.vtex_c',
    'flame':'materials/particle/fire_particle_8/fire_particle_8_low.vtex_c',
    'firebeam':'materials/particle/beam_fire_01.vtex_c',
    'arc':'materials/particle/electrical_arc2/electrical_arc_2_05.vtex_c',
    'ribbon':'materials/particle/ribbon_beam1.vtex_c',
    'plasma':'materials/particle/beam_plasma_06.vtex_c',
    'crystal':'materials/particle/crystal/crystal.vtex_c',
    'frost':'materials/particle/frost_add_projected_color_psd_e16f0483.vtex_c',
    'fleks':'materials/particle/impact/fleks3.vtex_c',
    'ring':'materials/particle/particle_ring_wavy6.vtex_c',
    'scorch':'materials/particle/lina/lina_scorch_mark_color.vtex_c',
}
NAMES=[('axe_call','caster',160,0,False,True),('axe_hunger','target',45,0,False,True),
       ('axe_helix','caster',160,0,False,False),('axe_cull','target',90,0,True,False),
       ('lina_dragon','line',100,200,True,False),('lina_array','target',160,0,False,False),
       ('lina_soul','caster',60,0,False,True),('lina_laguna','line',25,200,True,False),
       ('jugg_fury','caster',150,0,False,True),('jugg_ward','fixed_target',90,0,False,True),
       ('jugg_crit','caster',55,0,False,True),('jugg_omni','line',45,200,True,False),
       ('cm_nova','target',160,0,False,False),('cm_frostbite','target',60,0,False,True),
       ('cm_aura','caster',90,0,False,True),('cm_field','caster',160,0,False,True)]
ROOT_GRAPHS=[
    'axe/axe_beserkers_call','axe/axe_battle_hunger','axe/axe_counterhelix','axe/axe_culling_blade',
    'lina/lina_spell_dragon_slave','lina/lina_spell_light_strike_array','lina/lina_fiery_soul','lina/lina_spell_laguna_blade',
    'juggernaut/juggernaut_blade_fury','juggernaut/juggernaut_healing_ward','juggernaut/jugg_crit_blur','juggernaut/juggernaut_omni_slash',
    'crystalmaiden/maiden_crystal_nova','crystalmaiden/maiden_frostbite','crystalmaiden/maiden_ambient_hand','crystalmaiden/maiden_freezing_field_caster']
BASE_TICKS=[14,16,12,14,18,14,16,12,16,20,16,12,16,16,20,20]
IMPACTS={3,4,7,11}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def source_graphs(vpk):
    output=[]
    for spec in ROOT_GRAPHS:
        hero,name=spec.split('/');root=f'particles/units/heroes/hero_{hero}/{name}.vpcf_c'
        pending=[root];seen=set();images=set();records=[]
        while pending:
            entry=pending.pop()
            if entry in seen:continue
            if entry not in vpk.entries:raise ValueError('Missing original skill graph '+entry)
            seen.add(entry);raw=vpk.read(entry);records.append(vpk.identity(entry))
            for match in re.findall(rb'(?:materials|particles|models)/[a-zA-Z0-9_./-]+',raw):
                ref=match.decode();ref=ref if ref.endswith('_c') else ref+'_c'
                if ref not in vpk.entries:continue
                if ref.endswith('.vtex_c'):images.add(ref)
                elif ref.endswith(('.vpcf_c','.vmat_c')):pending.append(ref)
        output.append({'root':root,'compiled_graphs':records,'referenced_textures':sorted(images)})
    return output

def sources(archive,tool):
    vpk=VpkArchive(archive);SOURCE.mkdir(parents=True,exist_ok=True)
    graphs=source_graphs(vpk)
    def export(item):
        key,entry=item;folder=SOURCE/key;folder.mkdir(exist_ok=True)
        raw=folder/(key+'.vtex_c');compiled=vpk.read(entry)
        if not raw.exists():raw.write_bytes(compiled)
        elif raw.read_bytes()!=compiled:raise ValueError('Source texture changed '+key)
        target=folder/(key+'.png');log=folder/'export.log'
        if not log.exists():
            process=subprocess.run([str(tool),'-d','-i',str(raw),'-o',str(target)],capture_output=True,text=True)
            log.write_text(process.stdout+process.stderr,encoding='utf8')
            if process.returncode:raise RuntimeError(process.stdout+process.stderr)
        decoded=[]
        if target.exists() and target.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n':decoded=[target]
        else:decoded=sorted(folder.glob(key+'_seq*.png'))
        if not decoded:raise ValueError('No decoded PNG sequence for '+entry)
        # All original sequence PNG identities are retained even when only the
        # first twelve images are needed by the small-screen animation.
        sequence=[]
        for path in decoded[:12]:
            rgba=np.asarray(Image.open(path).convert('RGBA'),dtype=np.float32)/255
            colour=linear(rgba[:,:,:3]);peak=colour.max(2)
            opacity=rgba[:,:,3]*peak
            colour/=np.maximum(peak[:,:,None],1e-8)
            sequence.append(np.dstack((colour,opacity)).astype(np.float32))
        receipt={'name':key,'source':vpk.identity(entry),'raw_sha256':sha(raw),'tool_sha256':sha(tool),
                 'decoded':[{'path':str(p.relative_to(ROOT)),'sha256':sha(p),'size':list(Image.open(p).size)} for p in decoded]}
        return key,sequence,receipt
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:items=list(pool.map(export,TEXTURES.items()))
    return {k:im for k,im,_ in items},[r for _,_,r in items],graphs


class SkillCanvas(Canvas):
    def __init__(self,ss):super().__init__(ss)
    def sprite(self,texture,cx,cy,z,rx,ry,tint,strength,angle=0):
        x=self.px-cx*ZOOM;y=self.py-(cy-z)*PITCH;co=math.cos(angle);si=math.sin(angle)
        a=x*co+y*si;b=-x*si+y*co
        self.add(sample(texture,.5+a/(rx*2),.5+b/(ry*2)),-(cy+z)+y/PITCH,tint,strength)
    def ring(self,texture,radius,width,tint,strength,phase=0,arcs=1):
        radius=max(1,radius);distance=np.sqrt(self.x*self.x+self.y*self.y)
        angle=np.arctan2(self.y,self.x);band=np.exp(-((distance-radius)/max(width,1))**2)
        band*=.60+.40*np.cos(angle*arcs-phase*math.tau)**2
        image=sample(texture,.5+self.x/(radius*2.5),.5+self.y/(radius*2.5))
        # Particle geometry supplies the annulus. The original luminous image
        # modulates it, but its black outer margin must not erase the ring.
        image[:,:,:3]=.45+.55*image[:,:,:3]
        image[:,:,3]=band*(.52+.48*image[:,:,3])
        self.add(image,-self.y-2,tint,strength)
    def ribbon(self,texture,angle,length,width,z,tint,strength,phase=0,bend=0):
        dx=-math.sin(angle);dy=math.cos(angle);wx=self.x;wy=self.y+z
        along=wx*dx+wy*dy;across=wx*dy-wy*dx
        across-=np.sin(along*.06+phase*math.tau)*bend
        image=sample(texture,.5+along/length,.5+across/width)
        self.add(image,-self.y-2*z,tint,strength)
    def ray(self,texture,x0,y0,z0,x1,y1,z1,width,tint,strength):
        # A short billboard ribbon between projected world endpoints. Its
        # depth interpolates the same endpoints, rather than always overlaying.
        ax=x0*ZOOM;ay=(y0-z0)*PITCH;bx=x1*ZOOM;by=(y1-z1)*PITCH
        dx=bx-ax;dy=by-ay;length=math.hypot(dx,dy)
        if length<.1:return
        along=((self.px-ax)*dx+(self.py-ay)*dy)/(length*length)
        across=((self.px-ax)*dy-(self.py-ay)*dx)/length
        image=sample(texture,along,.5+across/max(width,1))
        beam=np.exp(-(across/max(width*.35,.5))**2)*((along>=0)&(along<=1))
        image[:,:,:3]=.35+.65*image[:,:,:3]
        image[:,:,3]=beam*(.55+.45*image[:,:,3])
        d=-(y0+z0)*(1-along)-(y1+z1)*along
        self.add(image,d,tint,strength)


MODEL_CACHE={}
def render_model(name,height,ss):
    key=(name,height,ss)
    if key in MODEL_CACHE:return MODEL_CACHE[key]
    path=ROOT/'private/source2/exports/skill-models'/name/(name+'.gltf');g=Gltf(path);parts=list(g.primitives());points=np.concatenate([p['points'] for p in parts])
    low=points.min(0);high=points.max(0);scale=height/(high[1]-low[1]);raster=Raster(240,224,ss)
    for part in parts:
        xyz=part['points'];q=np.column_stack((-xyz[:,0],xyz[:,2],xyz[:,1]-low[1]))*scale
        projected=np.column_stack((120+q[:,0]*ZOOM,132+(q[:,1]-q[:,2])*PITCH,-q[:,1]-q[:,2]))
        n=g.doc['nodes'][part['node_id']];at=g.doc['meshes'][n['mesh']]['primitives'][part['primitive_id']]['attributes']
        normal=g.accessor(at['NORMAL']).astype(float)@np.linalg.inv(g.globals()[part['node_id']][:3,:3]);normal=np.column_stack((-normal[:,0],normal[:,2],normal[:,1]))
        m=g.material(part['material']);texture=np.array(Image.open(m['image']).convert('RGBA'))
        source=m['source'];cutoff=source.get('alphaCutoff',.5) if source.get('alphaMode')=='MASK' else .01
        if source.get('alphaMode','OPAQUE')=='OPAQUE':texture[:,:,3]=255
        raster.draw(projected,part['faces'],part['uv'],normal,texture,cutoff=cutoff,factor=m['factor'])
    result=(raster.colour.copy(),raster.depth.copy(),{'gltf':str(path.relative_to(ROOT)),'sha256':sha(path),'buffers':g.buffer_sha256,'triangles':sum(len(p['faces']) for p in parts)})
    MODEL_CACHE[key]=result;return result

def add_model(canvas,name,height,fade):
    colour,depth,_=render_model(name,height,canvas.ss)
    canvas.rgb+=colour[:,:,:3]*fade;canvas.alpha=np.maximum(canvas.alpha,colour[:,:,3]*fade)
    canvas.depth=np.minimum(canvas.depth,np.where(colour[:,:,3]*fade>.005,depth,np.inf))

def frame(skill,clip,number,count,direction,tex,ss):
    p=number/(count if clip==1 else max(1,count-1));c=SkillCanvas(ss);angle=direction*math.tau/16
    white=(.9,1.,1.);red=(1.,.035,.015);orange=(1.,.20,.025);gold=(1.,.65,.14);green=(.12,1.,.32);ice=(.20,.66,1.)
    glow=tex['glow'][0];spin=tex['spin'];fire=tex['fire'][number%len(tex['fire'])];flame=tex['flame'][number%len(tex['flame'])];crystal=tex['crystal'][number%len(tex['crystal'])]
    life=max(.06,math.sin(p*math.pi)) if clip!=1 else .74+.26*math.sin(p*math.tau)
    impact=clip==2
    if skill==0: # Berserker's Call: red outward call, upright ember sparks.
        r=90+55*p if clip==0 else 145+8*math.sin(p*math.tau)
        c.ring(tex['ring'][0],r,9,red,2.8*life,p,5)
        c.ring(glow,r-17,5,gold,1.1*life,p,3)
        for j in range(12):
            a=j*math.tau/12+.4*p;rr=r*.7;z=10+65*((p+j*.13)%1)
            c.glow(tex['red'][0],math.cos(a)*rr,math.sin(a)*rr,z,3.3,red,.7*life)
    elif skill==1: # Battle Hunger: distinctive red marker above target.
        z=82+6*math.sin(p*math.tau);c.sprite(tex['red'][0],0,0,z,9,13,red,1.7*life)
        c.ring(glow,36+5*math.sin(p*math.tau),4,red,.9*life,p)
        for side in (-1,1):
            c.ray(tex['ribbon'][0],side*13,0,z-7,0,0,z+15,3,gold,.9*life)
        for j in range(5):c.glow(glow,math.sin(j*2.2)*17,math.cos(j*2.2)*17,18+60*((p+j*.17)%1),2.6,red,.7)
    elif skill==2: # Counter Helix: broad red/steel spiral and sharp blade arcs.
        c.ground(spin[number%len(spin)],155,p*math.tau,red,2.3*life)
        for j in range(3):
            a=p*math.tau+j*math.tau/3;c.ribbon(tex['plasma'][0],a,245,8,26,white,life,p)
        c.ring(glow,142,5,orange,1.2*life,p,3)
    elif skill==3: # Culling Blade: descending directional blade, red impact.
        if not impact:
            z=145*(1-p)+15;dx=-math.sin(angle);dy=math.cos(angle)
            c.ray(tex['plasma'][0],-dx*48,-dy*48,z+30,dx*38,dy*38,z,11,white,1.4*life)
            c.ray(tex['ribbon'][0],-dx*38,-dy*38,z+20,dx*35,dy*35,z-8,20,red,1.2*life)
        else:
            c.ring(tex['ring'][0],30+65*p,8,red,2*(1-p)+.1,p,6)
            for j in range(10):
                a=j*math.tau/10+.2;c.glow(tex['red'][0],math.cos(a)*70*p,math.sin(a)*70*p,8+60*(1-p),3,red,1-p+.05)
    elif skill==4: # Dragon Slave: travelling flame front along caster/target.
        dx=-math.sin(angle);dy=math.cos(angle)
        if not impact:
            travel=-90+180*p
            for j in range(7):
                across=(j-3)*18;cx=dx*travel+dy*across;cy=dy*travel-dx*across
                c.sprite(fire,cx,cy,18+9*math.sin(j),18,27,orange,1.6*life,-angle*.12)
            c.ribbon(tex['firebeam'][0],angle,195,42,8,orange,.8*life,p,4)
        else:
            c.ring(tex['ring'][0],30+65*p,9,orange,1.5*(1-p)+.05)
            for j in range(8):c.glow(glow,math.sin(j*2)*50*p,math.cos(j*2)*50*p,6+60*p,3,gold,1-p+.03)
    elif skill==5: # Light Strike Array: immediate flash then erupting fire.
        c.ground(tex['scorch'][0],150,.12,orange,.6+.5*life)
        c.ring(tex['ring'][0],135,9,orange,1.8*life,p,6)
        height=16+160*math.sin(p*math.pi)
        c.sprite(fire,0,0,height*.4,33,14+height*.45,orange,2.6*life)
        for j in range(9):
            a=j*math.tau/9;c.sprite(flame,math.cos(a)*80,math.sin(a)*80,10+height*.22,13,12+height*.22,gold,life)
    elif skill==6: # Fiery Soul: self flames around hands and a warm foot rune.
        c.ring(tex['ring'][0],47,4,orange,1.4*life,p,4)
        for side in (-1,1):c.sprite(flame,side*30,0,48+4*math.sin(p*math.tau),10,18,orange,1.4*life)
        for j in range(7):
            a=j*math.tau/7+p*math.tau;c.glow(glow,math.cos(a)*32,math.sin(a)*32,10+70*((p+j*.11)%1),2.3,gold,.75)
    elif skill==7: # Laguna Blade: electrical red-white bolt, independent impact.
        if not impact:
            c.ribbon(tex['arc'][number%len(tex['arc'])],angle,198,15,38,red,2.1*life,p,4)
            c.ribbon(tex['firebeam'][0],angle,198,3.8,38,white,1.7*life,p,1)
        else:
            c.glow(tex['red'][0],0,0,40,30*(1-.7*p),red,2.7*(1-p)+.1)
            for j in range(10):
                a=j*math.tau/10;c.ray(tex['arc'][0],0,0,40,math.cos(a)*65*p,math.sin(a)*65*p,30+25*math.sin(a),3,white,1-p+.05)
    elif skill==8: # Blade Fury: persistent orange/white sword cyclone.
        c.ground(spin[number%len(spin)],150,p*math.tau*2,orange,3*life)
        for j in range(4):c.ribbon(tex['plasma'][0],p*math.tau+j*math.pi/2,230,6,25,white,life)
        c.ring(glow,140,4,gold,life,p,4)
    elif skill==9: # Healing Ward: original model, green pulses and rising motes.
        add_model(c,'healing_ward',72,min(1,.22+1.4*p) if clip==0 else 1)
        c.ring(tex['spin'][4%len(spin)],55+32*p,7,green,1.6*life,p,5)
        c.glow(tex['green'][0],0,0,75,11,green,.6+.4*life)
        for j in range(8):
            a=j*math.tau/8;c.glow(glow,math.cos(a)*45,math.sin(a)*45,8+80*((p+j*.13)%1),2.8,green,.7)
    elif skill==10: # Blade Dance: local sword glint and two sharp crossed trails.
        c.ribbon(tex['plasma'][0],.65+math.sin(p*math.tau)*.12,95,6,46,gold,1.4*life)
        c.ribbon(tex['ribbon'][0],-.65,80,3,44,white,1.1*life)
        c.ring(glow,42,3,gold,.8*life,p,3)
        for j in range(4):c.glow(glow,-24+j*16,0,28+j*9,2.5,gold,life)
    elif skill==11: # Omnislash: distinct blue-white crossed sword streaks.
        if not impact:
            c.ribbon(tex['ribbon'][0],angle+.18*math.sin(p*math.tau),198,9,45,white,1.8*life,p)
            c.ribbon(tex['plasma'][0],angle-.12,170,3.5,42,(.15,.55,1.),1.4*life,p)
        else:
            for j in range(3):c.ribbon(tex['ribbon'][0],j*math.tau/3+p*.3,100*(.5+p),4,40,white,1.8*(1-p)+.1)
    elif skill==12: # Crystal Nova: expanding frost ring with ice-shard burst.
        c.ground(tex['frost'][0],150*(.4+.6*p),p*.15,ice,1.5*life)
        c.ring(tex['ring'][0],30+125*p,7,ice,2.4*(1-.7*p),p,7)
        for j in range(14):
            a=j*math.tau/14;r=20+125*p
            c.sprite(crystal,math.cos(a)*r,math.sin(a)*r,12+50*math.sin(p*math.pi),3.6,7.5,white,1.4*life,a)
    elif skill==13: # Frostbite: original ice model enclosing the target.
        add_model(c,'frost_cage',88,min(1,.25+p*2.5))
        c.ground(tex['frost'][0],58,0,ice,1.15*life)
        for j in range(7):
            a=j*math.tau/7;c.sprite(crystal,math.cos(a)*35,math.sin(a)*35,30+42*((p+j*.19)%1),3.8,8,ice,.6*life,a)
    elif skill==14: # Arcane Aura: blue runic orbit and outward mana motes.
        c.ring(tex['ring'][0],65+10*math.sin(p*math.tau),4,ice,1.6*life,p,6)
        c.ring(glow,40,3,(.45,.23,1.),.8*life,p,3)
        for j in range(10):
            a=j*math.tau/10-p*math.tau;c.glow(tex['blue'][0],math.cos(a)*50,math.sin(a)*50,12+40*math.sin(a)**2,2.9,ice,.7+.3*life)
    elif skill==15: # Freezing Field: individual falling ice pillars and bursts.
        c.ground(tex['frost'][0],158,p*.1,ice,.9*life)
        c.ring(tex['ring'][0],150,4,ice,.8*life,p,8)
        for j in range(12):
            a=j*2.399963;r=28+112*math.sqrt((j+.5)/12);q=(p+j*.173)%1
            x=math.cos(a)*r;y=math.sin(a)*r
            c.sprite(crystal,x,y,110*(1-q)+5,4.6,13,white,.55+.45*life,a)
            if q>.62:c.glow(tex['blue'][0],x,y,5,7+(q-.62)*30,ice,(1-q)*2.3)
        c.glow(tex['blue'][0],0,0,12,18,ice,.3*life)
    return c.finish()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=OUT)
    parser.add_argument('--archive',type=Path,default=STEAM/'pak01_dir.vpk');parser.add_argument('--tool',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--ss',type=int,default=2);parser.add_argument('--frames',type=int,default=8);parser.add_argument('--preview-only',action='store_true');args=parser.parse_args()
    if not 4<=args.frames<=12 or not 1<=args.ss<=3:parser.error('Use4..12 frames,ss1..3')
    args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    textures,texture_receipts,graphs=sources(args.archive,args.tool);records=[];catalog=[];contact=[]
    for skill,(name,anchor,radius,length,directional,persistent) in enumerate(NAMES):
        model=16+skill;folder=args.output/name;folder.mkdir(exist_ok=True);clips=[0]+([1] if persistent else [])+([2] if skill in IMPACTS else [])
        catalog.append({'model':model,'hero':skill//4,'skill':skill%4,'name':name,'anchor':anchor,'reference_radius_world':radius,'reference_length_world':length,'directions':16 if directional else 1,'flags':2 if skill in (9,13) else 3,
                        'clips':[{'id':clip,'role':['start','loop','impact'][clip],'loop':clip==1,'duration_ticks':BASE_TICKS[skill] if clip==0 else 16 if clip==1 else 10} for clip in clips],
                        'original_particle_root':graphs[skill]['root'],'adaptation':'Dedicated small-screen composition; original particle images, explicit skill-specific emission and motion; not Source2 operators'})
        previews=[]
        for clip in clips:
            ticks=BASE_TICKS[skill] if clip==0 else 16 if clip==1 else 10
            for direction in range(16 if directional and clip!=2 else 1):
                for number in range(args.frames):
                    if args.preview_only and direction:continue
                    rgba,depth,ax,ay=frame(skill,clip,number,args.frames,direction,textures,args.ss)
                    path=folder/f'{clip}-{direction:02d}-{number:02d}.npz';np.savez_compressed(path,rgba=rgba,depth=depth)
                    records.append({'model':model,'clip':clip,'direction':direction,'frame':number,'frames':args.frames,'duration_ticks':ticks,'loop':clip==1,'source_clip':name+':'+['start','loop','impact'][clip],
                                    'source_seconds':ticks/20*number/(args.frames if clip==1 else args.frames-1),'anchor_x':ax,'anchor_y':ay,'width':rgba.shape[1],'height':rgba.shape[0],'height_world':radius,'flags':2 if skill in (9,13) else 3,'npz':str(path.relative_to(ROOT)),'sha256':sha(path)})
                    if direction==0 and clip==0:previews.append((rgba,ax,ay))
        pictures=[]
        for rgba,ax,ay in previews:
            im=Image.new('RGB',(240,224),(24,34,33));src=Image.fromarray(rgba);im.paste(src,(120-ax,132-ay),src);pictures.append(im)
        pictures[0].save(folder/'preview.gif',save_all=True,append_images=pictures[1:],duration=100,loop=0,optimize=False)
        pictures[len(pictures)//2].save(folder/'preview.png');contact.append((name,pictures[len(pictures)//2]))
        print(json.dumps({'skill':name,'clips':clips,'directions':16 if directional else 1,'frames_so_far':len(records)}),flush=True)
    model_receipts=[]
    for name,height in [('healing_ward',72),('frost_cage',88)]:model_receipts.append(render_model(name,height,args.ss)[2])
    manifest={'complete':not args.preview_only,'skills':catalog,'models':[{'id':r['model'],'name':r['name']} for r in catalog],'frames':records,'source_textures':texture_receipts,'source_particle_graphs':graphs,'source_models':model_receipts,
              'projection':{'zoom_q8':180,'vertical_q8':181,'directions':16,'direction0':'+worldY','reference_line_length_world':200,'depth':'signed -(localY+localZ)'},
              'source_sha256':{str(Path(__file__).relative_to(ROOT)):sha(__file__),'tools/bake_effects.py':sha(ROOT/'tools/bake_effects.py'),'tools/bake_sprite_raster.py':sha(ROOT/'tools/bake_sprite_raster.py'),'tools/bake_sprite_raster.c':sha(ROOT/'tools/bake_sprite_raster.c')},
              'limitations':'Particle graph references and imagery are original. Operators are reconstructed for the current offline H1 rules; original Source2 renderer, audio, refraction and dynamic light simulation are not executed.'}
    (args.output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    sheet=Image.new('RGB',(960,4*256),(20,28,28));draw=ImageDraw.Draw(sheet)
    for i,(name,image) in enumerate(contact):x=i%4*240;y=i//4*256;sheet.paste(image,(x,y+24));draw.text((x+5,y+4),name,fill='white')
    sheet.save(args.output/'contact.png')
    if not args.preview_only:
        from compact_skill_fx import compact
        compact(args.output/'manifest.json')
    print(json.dumps({'skills':len(catalog),'frames':len(records),'manifest':str(args.output/'manifest.json')}))

if __name__=='__main__':main()
