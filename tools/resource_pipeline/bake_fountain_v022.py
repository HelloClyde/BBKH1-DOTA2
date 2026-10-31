"""Bake original fountain meshes and independent authored particle tracks.

This is a fixed-camera offline interpretation, not the proprietary Source 2
global_lit_simple shader or particle VM. Source transforms, textures, sizes,
emission, gravity/drag, alpha, fade windows and UV rates remain explicit.
Random values repeat deterministically; independent tracks avoid quantizing
three unrelated material speeds to the old common 1.6-second loop.

Particle field/orientation semantics were checked against the MIT-licensed
ValveResourceFormat reference recorded in build/audit-v022/vrf-reference.
No implementation code is copied from that project.
"""
from pathlib import Path
import argparse,collections,hashlib,json,math,re,shutil,subprocess
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from bake_effects import linear,srgb,sample,ZOOM,PITCH
from bake_fountains import BASIS,WORLD_SCALE,ANCHORS,FountainCanvas
from source2_materials import OriginalMaterials
from source2_kv3_text import parse

OUT=ROOT/'private/baked/v022/fountain'
SOURCE=ROOT/'private/source2-v022/fountain'
GRAPHS=ROOT/'private/baked/fountain/source'
OLD=ROOT/'private/baked/fountain/manifest.json'
TREE_MAP=ROOT/'private/baked/v022/map'
TEXTURES={
 'orb':'materials/particle/orb_tintable/particle_circular_orb_tintable.vtex',
 'spray':'materials/particle/spray1/spray1.vtex',
 'wave':'materials/particle/particle_ring_wave_8.vtex',
 'rope':'materials/particle/beam_plasma_04.vtex',
 'radiance':'materials/particle/particle_flares/anam_white.vtex',
 'light':'materials/particle/particle_light_mask_circfade.vtex',
}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def rel(p):return Path(p).resolve().relative_to(ROOT).as_posix()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def write(p,x):Path(p).write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def graph(name):return parse((GRAPHS/('fountain_tintable_'+name+'.vpcf')).read_text(encoding='utf8'))
def find(g,section,cls):return next(x for x in g.get(section,[]) if x['_class']==cls)
def init(g,field,default):
    for op in g.get('m_Initializers',[]):
        if op['_class']=='C_INIT_InitFloat' and op.get('m_nOutputField',3)==field:return op.get('m_InputValue',{}).get('m_flLiteralValue',default)
    return default
def random_init(g,field,rng,default):
    for op in g.get('m_Initializers',[]):
        if op['_class']=='C_INIT_InitFloat' and op.get('m_nOutputField',3)==field:
            p=op.get('m_InputValue',{});lo=p.get('m_flRandomMin',p.get('m_flLiteralValue',default));hi=p.get('m_flRandomMax',lo)
            v=rng.uniform(min(lo,hi),max(lo,hi))
            if p.get('m_bHasRandomSignFlip',False) and rng.random()<.5:v=-v
            return v
    return default
def smooth(x):x=np.clip(x,0,1);return x*x*(3-2*x)
def qangle(q):
    p,y,r=np.radians(q);cp,sp,cy,sy,cr,sr=math.cos(p),math.sin(p),math.cos(y),math.sin(y),math.cos(r),math.sin(r)
    return np.array([[cy*cp,cy*sp*sr-sy*cr,cy*sp*cr+sy*sr],[sy*cp,sy*sp*sr+cy*cr,sy*sp*cr-cy*sr],[-sp,cp*sr,cp*cr]])
def toworld(p):return np.array([2048+p[0]/4,2048-p[1]/4,p[2]/4])
def vector_world(v):return np.asarray(v)*np.array([.25,-.25,.25])
def control_points(e):
    g=graph('ambient');rotation=qangle(e['angles']);orientation={0:rotation};positions={0:np.asarray(e['origin'],float)}
    for op in g['m_PreEmissionOperators']:
        if op['_class']=='C_OP_SetControlPointOrientation':
            orientation[op['m_nCP']]=orientation[op.get('m_nHeadLocation',0)]@qangle(op['m_vecRotation'])
        else:
            cp=op['m_nCP1'];head=op.get('m_nHeadLocation',0)
            positions[cp]=positions[head]+orientation.get(head,rotation)@np.asarray(op['m_vecCP1Pos'])
    return {k:toworld(v) for k,v in positions.items()},positions,orientation

def decoded_textures(materials):
    out={};receipts=[]
    for name,entry in TEXTURES.items():
        entry+='_c';folder=SOURCE/('particles-'+name);folder.mkdir(exist_ok=True,parents=True)
        raw=folder/Path(entry).name;raw.write_bytes(materials.vpk.read(entry));png=raw.with_suffix('.png')
        if not list(folder.glob('*.png')):
            subprocess.run([str(materials.tool),'-d','-i',str(raw),'-o',str(png)],check=True,capture_output=True)
        seq=collections.defaultdict(list)
        paths=list(folder.glob('*_seq*.png')) or [png]
        for path in paths:
            match=re.search(r'_seq(\d+)_(\d+)\.png$',path.name)
            seq[int(match[1]) if match else 0].append((int(match[2]) if match else 0,path))
        out[name]={};paths=[]
        for k,items in sorted(seq.items()):
            items.sort();assert [n for n,_ in items]==list(range(len(items))),(name,k)
            arrays=[]
            for _,path in items:
                rgba=np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)/255
                arrays.append(np.dstack((linear(rgba[:,:,:3]),rgba[:,:,3])));paths.append(dict(sequence=k,frame=len(arrays)-1,path=rel(path),sha256=sha(path)))
            out[name][k]=arrays
        assert out[name],name
        receipts.append(dict(name=name,source=materials.vpk.identity(entry),raw=rel(raw),raw_sha256=sha(raw),sequences={str(k):len(v) for k,v in out[name].items()},decoded=paths))
    return out,receipts

def texture_at(textures,name,age,rate,sequence=0,fps=False):
    seq=textures[name];key=sequence if sequence in seq else sorted(seq)[0];images=seq[key]
    frame=age*rate if fps else age*rate*len(images)
    lo=math.floor(frame);f=frame-lo
    return (images[lo%len(images)],images[(lo+1)%len(images)],f)

def sample_animated(texture,u,v):
    if isinstance(texture,tuple):return sample(texture[0],u,v)*(1-texture[2])+sample(texture[1],u,v)*texture[2]
    return sample(texture,u,v)

class AdditiveCanvas(FountainCanvas):
    """Linear additive energy + true per-pixel relative source depth."""
    def add(self,image,depth,tint,strength,rect=None):
        rect=rect or (slice(None),slice(None));alpha=np.clip(image[:,:,3]*strength,0,1)
        energy=image[:,:,:3]*np.asarray(tint)*np.asarray(np.maximum(strength,0))[...,None]*image[:,:,3:4]
        self.rgb[rect]+=energy;self.alpha[rect]=np.maximum(self.alpha[rect],alpha)
        self.depth[rect]=np.minimum(self.depth[rect],np.where(alpha>=1/255,depth,np.inf))
    def quad(self,texture,center,radius,angle,tint,strength,horizontal,flip=False):
        p=np.asarray(center);x,y,z=p;co,si=math.cos(angle),math.sin(angle)
        extent=radius*(abs(co)+abs(si));ex=extent*ZOOM;ey=extent*(PITCH if horizontal else ZOOM)
        cx,cy=x*ZOOM,(y-z)*PITCH
        x0=max(0,math.floor((cx-ex+self.ax)*self.ss));x1=min(self.width*self.ss,math.ceil((cx+ex+self.ax)*self.ss))
        y0=max(0,math.floor((cy-ey+self.ay)*self.ss));y1=min(self.height*self.ss,math.ceil((cy+ey+self.ay)*self.ss))
        if x0>=x1 or y0>=y1:return
        rect=(slice(y0,y1),slice(x0,x1));px,py=self.px[rect],self.py[rect]
        if horizontal:
            dx=px/ZOOM-x;dy=py/PITCH+z-y
            # WORLD_Z_ALIGNED canonical right=-SourceY/up=SourceX.
            u=.5+(dx*si+dy*co)/(2*radius);v=.5-(dx*co-dy*si)/(2*radius)
            depth=-(py/PITCH+2*z)
        else:
            dx=(px-x*ZOOM)/ZOOM;dy=(py-(y-z)*PITCH)/ZOOM
            u=.5+(dx*co+dy*si)/(2*radius);v=.5+(-dx*si+dy*co)/(2*radius)
            depth=-(y+z)+dy*ZOOM/PITCH
        if flip:u=1-u
        self.add(sample_animated(texture,u,v),depth,tint,strength,rect)
    def finish(self):
        s=self.ss;h,w=self.height,self.width;energy=self.rgb.reshape(h,s,w,s,3).mean((1,3))
        # Keep the authored linear alpha; applying an sRGB transfer function
        # to alpha itself made white fountain water opaque. RGB565 cannot
        # represent Source HDR, so resolve overlapping energy into straight
        # colour, then use the native additive alpha rule (no arbitrary gain).
        coverage=self.alpha.reshape(h,s,w,s).mean((1,3));straight=srgb(np.divide(energy,coverage[:,:,None],out=np.zeros_like(energy),where=coverage[:,:,None]>0))
        straight=np.clip(straight,0,1);alpha=np.rint(np.clip(coverage,0,1)*255).astype(np.uint8)
        rgba=np.dstack((np.rint(straight*255).astype(np.uint8),alpha))
        depth=self.depth.reshape(h,s,w,s).min((1,3));depth=np.rint(np.where(np.isfinite(depth)&(alpha>0),depth,0)).clip(-32768,32767).astype('<i2')
        ys,xs=np.where(alpha>0)
        if not len(xs):raise ValueError('empty fountain frame')
        x0,x1=max(0,int(xs.min())-1),min(w,int(xs.max())+2);y0,y1=max(0,int(ys.min())-1),min(h,int(ys.max())+2)
        return rgba[y0:y1,x0:x1],depth[y0:y1,x0:x1],self.ax-x0,self.ay-y0

def canvas(ss):return AdditiveCanvas(224,200,112,155,ss)
def fade(g,age,life):
    f=1.
    for op in g.get('m_Operators',[]):
        if op['_class']=='C_OP_FadeInSimple':f=min(f,float(smooth(age/(op.get('m_flFadeInTime',.25)*life))))
        if op['_class']=='C_OP_FadeOutSimple':f=min(f,float(1-smooth((age/life-(1-op.get('m_flFadeOutTime',.25)))/op.get('m_flFadeOutTime',.25))))
    return f
def movement(pos,velocity,gravity,drag,age):
    # Source's position/previous-position integrator, at the H1's fixed20Hz.
    # A final partial step handles 30/s and8/s birth times without rate drift.
    p=np.array(pos,float);prev=p-np.array(velocity,float)/20;prior=.05;remain=age
    while remain>1e-9:
        dt=min(.05,remain);d=dt/prior*(1-drag)**(dt*30);new=p+(p-prev)*d+np.asarray(gravity)*dt*dt
        prev,p=p,new;prior=dt;remain-=dt
    return p

def orb(c,t,textures,points,tint,bot_angle):
    for i,name in enumerate(('bot','mid','top')):
        g=graph(name);cp=find(g,'m_Initializers','C_INIT_CreateWithinSphere')['m_nControlPointNumber'];r=init(g,3,0)/4
        rng=np.random.default_rng(20221001+i);angle=math.radians(random_init(g,4,rng,0))
        if name=='bot':angle+=bot_angle
        renderer=g['m_Renderers'][0];texture=texture_at(textures,'orb',t,renderer['m_flAnimationRate'])
        c.quad(texture,points[cp],r,angle,tint,init(g,7,1)*renderer['m_flOverbrightFactor'],True)

def waves(c,t,textures,points,tint):
    g=graph('waves');em=find(g,'m_Emitters','C_OP_ContinuousEmitter');rate=em['m_flEmitRate']['m_flLiteralValue'];period=2.;counts=[]
    for stream,name in enumerate(('bot','mid','top')):
        parent=graph(name);cp=find(parent,'m_Initializers','C_INIT_CreateWithinSphere')['m_nControlPointNumber'];alive=0
        for birth in range(math.floor((t-6)*rate),math.floor(t*rate)+1):
            age=t-birth/rate;rng=np.random.default_rng(22000+stream*100+birth%int(rate*period));life=random_init(g,1,rng,1)
            if not 0<=age<life:continue
            alive+=1;radius=init(parent,3,0)/4;rotation=math.radians(random_init(g,4,rng,0));alpha=random_init(g,7,rng,1)
            z=rng.uniform(-10,-2)/4;move=find(g,'m_Operators','C_OP_BasicMovement');pos=movement(points[cp]+[0,0,z],[0,0,0],vector_world(move['m_Gravity']),move.get('m_fDrag',0),age)
            rop=find(g,'m_Operators','C_OP_InterpolateRadius');radius*=rop['m_flStartScale']+(rop.get('m_flEndScale',1)-rop['m_flStartScale'])*age/life
            spin=find(g,'m_Operators','C_OP_RampScalarSpline');rotation+=rng.uniform(spin['m_RateMin'],spin['m_RateMax'])*age
            renderer=g['m_Renderers'][0];texture=texture_at(textures,'wave',age,renderer['m_flAnimationRate'],7)
            c.quad(texture,pos,radius,rotation,tint,alpha*fade(g,age,life),True,rng.random()<.5)
        assert alive<=g['m_nMaxParticles'];counts.append(alive)
    return counts

def spray_and_radiance(c,t,textures,points,orientations):
    counts={}
    for name in ('splashes','radiance'):
        g=graph(name);renderer=g['m_Renderers'][0];rate=find(g,'m_Emitters','C_OP_ContinuousEmitter')['m_flEmitRate']['m_flLiteralValue'];alive=0
        for birth in range(math.floor((t-.5)*rate),math.floor(t*rate)+1):
            rng=np.random.default_rng(33000+(name=='radiance')*1000+birth%int(rate*2));age=t-birth/rate;life=random_init(g,1,rng,1)
            if not 0<=age<life:continue
            if alive>=g['m_nMaxParticles']:continue
            alive+=1;radius=random_init(g,3,rng,1)/4;rotation=math.radians(random_init(g,4,rng,0));alpha=min(1.,max(0.,random_init(g,7,rng,1)))
            initpos=find(g,'m_Initializers','C_INIT_CreateWithinSphere');cp=initpos['m_nControlPointNumber'];pos=points[cp].copy();velocity=np.zeros(3)
            if name=='splashes':
                direction=rng.normal(size=3);direction*=initpos['m_vecDistanceBias'];direction/=np.linalg.norm(direction)
                pos+=vector_world(direction*rng.uniform(0,initpos['m_fRadiusMax']));velocity=vector_world(direction*rng.uniform(initpos['m_fSpeedMin'],initpos['m_fSpeedMax']))
                # CP3 orientation defaults to the fountain entity's orientation;
                # only CP6's explicit orientation is pitched by90degrees.
                local=np.array([0,0,rng.uniform(45,55)]);velocity+=vector_world(orientations[0]@local)
                colors=find(g,'m_Initializers','C_INIT_RandomColor');a=np.array(colors['m_ColorMin'][:3])/255;b=np.array(colors['m_ColorMax'][:3])/255;color=a+(b-a)*rng.random()
                spin=find(g,'m_Operators','C_OP_RampScalarSpline');rotation+=rng.uniform(spin['m_RateMin'],spin['m_RateMax'])*age
            else:color=np.asarray(g['m_ConstantColor'][:3])/255
            move=find(g,'m_Operators','C_OP_BasicMovement');pos=movement(pos,velocity,vector_world(move.get('m_Gravity',[0,0,0])),move.get('m_fDrag',0),age)
            rop=find(g,'m_Operators','C_OP_InterpolateRadius');fraction=age/life
            if rop.get('m_flBias',.5)!=.5:
                b=rop['m_flBias'];fraction=fraction/(((1/b)-2)*(1-fraction)+1)
            radius*=rop.get('m_flStartScale',1)+(rop.get('m_flEndScale',1)-rop.get('m_flStartScale',1))*fraction
            cf=find(g,'m_Operators','C_OP_ColorInterpolate');begin=cf.get('m_flFadeStartTime',0);end=cf.get('m_flFadeEndTime',1);f=float(np.clip((age/life-begin)/(end-begin),0,1));color=color*(1-f)+np.array(cf['m_ColorFade'][:3])/255*f
            seqmax=find(g,'m_Initializers','C_INIT_RandomSequence')['m_nSequenceMax'];sequence=int(rng.integers(seqmax+1));key='spray' if name=='splashes' else 'radiance'
            texture=texture_at(textures,key,age,renderer.get('m_flAnimationRate',1),sequence,renderer.get('m_bAnimateInFPS',False))
            c.quad(texture,pos,radius,rotation,color,alpha*fade(g,age,life)*renderer.get('m_flOverbrightFactor',1),False)
        counts[name]=alive
    return counts

def catmull(p0,p1,p2,p3,t):return .5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t)
def rope(c,rotation_phase,uv_phase,textures,points,orientation):
    g=graph('circular');renderer=g['m_Renderers'][0];tint=np.asarray(g['m_ConstantColor'][:3])/255;count=int(find(g,'m_Emitters','C_OP_InstantaneousEmitter')['m_nParticlesToEmit']['m_flLiteralValue']);nodes=[]
    radius=g['m_flConstantRadius']*15*renderer['m_flRadiusScale']/4
    for i in range(count):
        angle=(i+1)*math.tau/16-rotation_phase*math.tau
        # RingWave defaults XY around CP3; CP3's orientation remains that of
        # CP0. The parent explicit90degree orientation applies to positions.
        nodes.append(points[3]+vector_world(orientation@np.array([math.cos(angle)*50,math.sin(angle)*50,5])))
    nodes=np.array(nodes);texture=textures['rope'][0][0];arc=uv_phase*renderer['m_flTextureVWorldSize'];along=renderer['m_flTextureVOffset']
    for segment in range(count-1):
        p0,p1,p2,p3=nodes[max(0,segment-1)],nodes[segment],nodes[segment+1],nodes[min(count-1,segment+2)]
        length=np.linalg.norm(p2-p1)*4
        for sub in range(32):
            a=sub/32;b=(sub+1)/32;pa=catmull(p0,p1,p2,p3,a);pb=catmull(p0,p1,p2,p3,b)
            ax,ay=pa[0]*ZOOM,(pa[1]-pa[2])*PITCH;bx,by=pb[0]*ZOOM,(pb[1]-pb[2])*PITCH
            vx,vy=bx-ax,by-ay;ll=vx*vx+vy*vy
            if ll<1e-9:continue
            extent=radius*ZOOM+1
            x0=max(0,int(math.floor((min(ax,bx)-extent+c.ax)*c.ss)));x1=min(c.width*c.ss,int(math.ceil((max(ax,bx)+extent+c.ax)*c.ss)))
            y0=max(0,int(math.floor((min(ay,by)-extent+c.ay)*c.ss)));y1=min(c.height*c.ss,int(math.ceil((max(ay,by)+extent+c.ay)*c.ss)))
            if x0>=x1 or y0>=y1:continue
            rect=(slice(y0,y1),slice(x0,x1));y,x=c.px[rect],c.py[rect]
            tt=np.clip(((y-ax)*vx+(x-ay)*vy)/ll,0,1);dx=y-ax-tt*vx;dy=x-ay-tt*vy;perp=(-dx*vy+dy*vx)/math.sqrt(ll)
            inside=((y-ax)*vx+(x-ay)*vy>=0)&((y-ax)*vx+(x-ay)*vy<=ll)
            # U is mirrored by original finalTextureScaleU=-1. The rope
            # follows a smooth Catmull-Rom path at original tessellation5.
            u=.5-perp/(2*radius*ZOOM);v=(arc+length*(a+(b-a)*tt))/renderer['m_flTextureVWorldSize']+along
            image=sample(texture,np.mod(u,1),np.mod(v,1));image[:,:,3]*=inside&(np.abs(perp)<=radius*ZOOM)
            f=(segment+(a+(b-a)*tt))/7;alpha=np.minimum((segment+(a+(b-a)*tt))/4,1)*np.clip((7-(segment+(a+(b-a)*tt)))/4,0,1)*(1-f)
            color=tint*(1-.5*np.minimum((segment+(a+(b-a)*tt))/4,1))[:,:,None]
            depth=-(pa[1]+pa[2])*(1-tt)-(pb[1]+pb[2])*tt
            c.add(image,depth,color,alpha*renderer['m_flOverbrightFactor'],rect)
        arc+=length

def mesh_frame(receipt,phase,ss,materials,anchor):
    g=Gltf(ROOT/receipt['gltf']);matrix=np.asarray(receipt['matrix_gltf']);primitives=[];allpoints=[]
    for p in g.primitives():
        q=(p['points']@matrix[:3,:3].T+matrix[:3,3])@BASIS.T*WORLD_SCALE+[2048,2048,0]-anchor
        node=g.doc['nodes'][p['node_id']];at=g.doc['meshes'][node['mesh']]['primitives'][p['primitive_id']]['attributes']
        original_normal=g.accessor(at['NORMAL']).astype(float)
        normal=original_normal@np.linalg.inv(matrix[:3,:3]@g.globals()[p['node_id']][:3,:3])@BASIS.T;normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-12)
        material=g.material(p['material']);texture=np.asarray(Image.open(material['image']).convert('RGBA'));original=material['source']['extras']['vmat']
        materials.resource(original['Name'])
        for path in original.get('TextureParams',{}).values():materials.image(path)
        primitives.append((p,q,normal,texture,original));allpoints.append(q)
    pts=np.concatenate(allpoints);xy=np.column_stack((pts[:,0]*ZOOM,(pts[:,1]-pts[:,2])*PITCH));lo=np.floor(xy.min(0)).astype(int)-2;hi=np.ceil(xy.max(0)).astype(int)+2
    w,h=hi-lo+1;assert max(w,h)<=256
    c=AdditiveCanvas(int(w),int(h),-int(lo[0]),-int(lo[1]),ss)
    # Rasterize additive mesh triangles without the old nearest-fragment
    # overwrite. Every intersecting translucent face contributes light.
    for p,q,normals,texture,original in primitives:
        projected=np.column_stack((q[:,0]*ZOOM,(q[:,1]-q[:,2])*PITCH));uv=p['uv'].copy()
        tex=np.dstack((linear(texture[:,:,:3].astype(float)/255),np.ones(texture.shape[:2])))
        flow=materials.image(original['TextureParams']['g_tScrollSpeed']).astype(float)/255
        angle_power=original['FloatParams'].get('g_flAlphaAnglePower',1);view=np.array([0.,-1.,-1.]);view/=np.linalg.norm(view)
        for face in p['faces']:
            a,b,d=projected[face];det=np.cross(b-a,d-a)
            if abs(det)<1e-10:continue
            x0=max(0,int(math.floor((min(a[0],b[0],d[0])+c.ax)*ss)));x1=min(w*ss,int(math.ceil((max(a[0],b[0],d[0])+c.ax)*ss)))
            y0=max(0,int(math.floor((min(a[1],b[1],d[1])+c.ay)*ss)));y1=min(h*ss,int(math.ceil((max(a[1],b[1],d[1])+c.ay)*ss)))
            if x0>=x1 or y0>=y1:continue
            rect=(slice(y0,y1),slice(x0,x1));px=c.px[rect];py=c.py[rect]
            wb=((px-a[0])*(d[1]-a[1])-(py-a[1])*(d[0]-a[0]))/det;wc=((b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]))/det;wa=1-wb-wc
            inside=(wa>=0)&(wb>=0)&(wc>=0);uu=wa*uv[face[0],0]+wb*uv[face[1],0]+wc*uv[face[2],0];vv=wa*uv[face[0],1]+wb*uv[face[1],1]+wc*uv[face[2],1]
            control=sample(flow,np.mod(uu,1),np.mod(vv,1))
            # Verified original DXBC: t1.g is UV speed; t1.a is opacity.
            # t0.a is NOT used for fountain opacity. Two staggered advected
            # phases preserve local G*direction*speed velocity while making
            # a compact seamless loop instead of a485-second frame bank.
            p0=phase;p1=(phase+.5)%1;weight=1-abs(2*phase-1)
            image=sample(tex,np.mod(uu-control[:,:,1]*p0,1),np.mod(vv,1))*weight+sample(tex,np.mod(uu-control[:,:,1]*p1,1),np.mod(vv,1))*(1-weight)
            image[:,:,3]=control[:,:,3]*inside
            normal=wa[:,:,None]*normals[face[0]]+wb[:,:,None]*normals[face[1]]+wc[:,:,None]*normals[face[2]];normal/=np.maximum(np.linalg.norm(normal,axis=2)[:,:,None],1e-9)
            facing=np.abs(normal@view)**angle_power
            depth=-(wa*(q[face[0],1]+q[face[0],2])+wb*(q[face[1],1]+q[face[1],2])+wc*(q[face[2],1]+q[face[2],2]))
            c.add(image,depth,(1,1,1),facing,rect)
    return c.finish()

def save_frame(records,track,direction,number,count,result,seconds):
    rgba,depth,ax,ay=result;folder=OUT/track['name'];folder.mkdir(parents=True,exist_ok=True);path=folder/f"{track['clip']}-{direction:02d}-{number:02d}.npz"
    np.savez_compressed(path,rgba=rgba,depth=depth)
    if direction==0:Image.fromarray(rgba).save(path.with_suffix('.png'))
    records.append(dict(model=track['model'],clip=track['clip'],direction=direction,frame=number,frames=count,duration_ticks=count,loop=True,anchor_x=ax,anchor_y=ay,width=rgba.shape[1],height=rgba.shape[0],npz=rel(path),sha256=sha(path),source_clip=track['name'],source_seconds=seconds,height_world=1,flags=3))

def light_map(materials,textures,entity,points):
    out=ROOT/'private/baked/v022/map-lit';out.mkdir(exist_ok=True,parents=True);before={n:sha(TREE_MAP/n) for n in ('map.png','map-depth.npy','ground-grid.npy','manifest.json')}
    m=read(TREE_MAP/'manifest.json');rgb=np.asarray(Image.open(TREE_MAP/'map.png').convert('RGB'));depth=np.load(TREE_MAP/'map-depth.npy');h,w=depth.shape
    yy,xx=np.mgrid[:h,:w];ox,oy=m['origin_pixels'];wx=(xx+ox+.5)/ZOOM;diff=(yy+oy+.5)/PITCH;sm=32768-depth.astype(float);wy=(sm+diff)/2;wz=(sm-diff)/2
    center=points[3];dx,dy,dz=wx-center[0],wy-center[1],wz-center[2];distance=np.sqrt(dx*dx+dy*dy+dz*dz)
    g=graph('light');renderer=g['m_Renderers'][0];radius=init(g,3,0)*renderer['m_flRadiusScale']/4
    # Source range, alpha, RGB tint, falloff start and original cookie texture;
    # H1 uses a fixed-camera Lambert/inverse-square diffuse approximation.
    rr=distance/radius;fall=1-smooth((rr-renderer['m_flStartFalloff'])/(1-renderer['m_flStartFalloff']));cookie=sample(textures['light'][0][0],.5+dx/(2*radius),.5+dy/(2*radius))[:,:,0]
    # Recover visible-surface normals from the already source-mesh baked
    # depth, reject discontinuities rather than leaking light over edges.
    dyx=np.gradient(wy,axis=1);dzx=np.gradient(wz,axis=1);dyy=np.gradient(wy,axis=0);dzy=np.gradient(wz,axis=0)
    normal=np.stack((dyx*dzy-dzx*dyy,-dzy/ZOOM,dyy/ZOOM),axis=2);normal/=np.maximum(np.linalg.norm(normal,axis=2)[:,:,None],1e-8)
    toward=np.stack((-dx,-dy,-dz),axis=2)/np.maximum(distance[:,:,None],1e-8);lambert=np.maximum(0,(normal*toward).sum(2))
    rng=np.random.default_rng(22004);alpha=random_init(g,7,rng,1);power=alpha*renderer['m_flAlphaScale'];gain=(radius*.5/np.maximum(distance,1e-6))**2*power*fall*cookie*lambert
    gain[rr>=1]=0;tint=np.asarray(entity['particle_tint_color'])/255;energy=linear(rgb/255.)*(1+gain[:,:,None]*tint)
    output=np.rint(np.clip(srgb(energy),0,1)*255).astype(np.uint8);Image.fromarray(output).save(out/'map.png')
    for n in ('map-depth.npy','ground-grid.npy'):shutil.copy2(TREE_MAP/n,out/n)
    m.update(map_sha256=sha(out/'map.png'),depth_sha256=sha(out/'map-depth.npy'),ground_grid_sha256=sha(out/'ground-grid.npy'))
    m['fountain_deferred_light']=dict(source_graph=rel(GRAPHS/'fountain_tintable_light.vpcf'),source_graph_sha256=sha(GRAPHS/'fountain_tintable_light.vpcf'),source_radius=350,source_radius_scale=2,world_center=center.tolist(),world_radius=radius,source_tint=entity['particle_tint_color'],source_alpha=alpha,source_alpha_scale=5,source_start_falloff=.8,source_light_distance=renderer.get('m_flLightDistance',0),source_spot_fov=renderer['m_flSpotFoV'],changed_pixels=int(np.any(output!=rgb,axis=2).sum()),depth_unchanged=sha(out/'map-depth.npy')==before['map-depth.npy'],baseline=rel(TREE_MAP),baseline_sha256=before,cookie=TEXTURES['light'],double_light_audit='Original map bake fixed daylight/contact occlusion only; no RenderDeferredLight, particle cookie or dynamic fountain light is included. Original fountain bake did not load this cookie. Artist albedo is not treated as an already evaluated dynamic light.',adaptation='Static seeded alpha, original point-light radius/tint/falloff/cookie. H1 fixed-camera diffuse normal estimate/inverse-square response; not the Source2 deferred photometric shader. Spotlight FOV unused because original LightDistance=0 (point light).',generator_sha256=sha(__file__))
    write(out/'manifest.json',m);assert before=={n:sha(TREE_MAP/n) for n in before}
    return dict(output=rel(out),manifest_sha256=sha(out/'manifest.json'),map_sha256=m['map_sha256'],depth_sha256=m['depth_sha256'],changed_pixels=m['fountain_deferred_light']['changed_pixels'])

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--ss',type=int,default=2);ap.add_argument('--only',choices=['mesh','particles','light']);a=ap.parse_args();assert 1<=a.ss<=4
    OUT.mkdir(parents=True,exist_ok=True);materials=OriginalMaterials(SOURCE);textures,texture_receipts=decoded_textures(materials)
    entities=read(ROOT/'private/source2-v022/active-entities/entities.json');entity=next(e for e in entities if e.get('classname')=='ent_dota_fountain' and str(e['teamnumber'])=='2')
    points,source_points,orientations=control_points(entity);anchor=np.array(ANCHORS[0]);relative_points={k:v-anchor for k,v in points.items()};old=read(OLD);records=[];tracks=[];counts=[]
    if a.only in (None,'mesh'):
        for name,model,clip,num,den in [('radiant',6,5,21,40),('waterfall',6,6,9,4),('dire',7,5,15,8)]:
            receipt=next(r for r in old['source']['meshes'] if r['name']==name);track=dict(name=name+'-uv',model=model,clip=clip,directions=1,frames=16,phase_rate_numerator=num,phase_rate_denominator=den,phase_sign=-1,source=receipt,animation='Independent16 flow phases. Original scroll texture G multiplies local UV velocity; A is opacity. Dual staggered advection phases crossfade smoothly to close the short H1 loop. Scalar direction*speed rate remains exact, multiplied by original localG.');tracks.append(track)
            for number in range(16):save_frame(records,track,0,number,16,mesh_frame(receipt,number/16,a.ss,materials,np.array(ANCHORS[model-6])),number/16/(num/den))
            print(json.dumps(dict(track=track['name'],frames=16)),flush=True)
    if a.only in (None,'particles'):
        track=dict(name='circular-rope',model=6,clip=7,directions=16,frames=16,rotation_rate_numerator=1,rotation_rate_denominator=12,uv_rate_numerator=19,uv_rate_denominator=200,source_scroll_rate=-86.36364,source_texture_world_size=909.090942,adaptation='16 rotation poses selected independently from16 original texture scroll phases; original8-node half-ring and5-level Catmull-Rom tessellation, original count gradients. Source randomness fixed.');tracks.append(track)
        for direction in range(16):
            for number in range(16):
                c=canvas(a.ss);rope(c,direction/16,-number/16,textures,relative_points,orientations[0]);save_frame(records,track,direction,number,16,c.finish(),number/16/(19/200))
        print(json.dumps(dict(track=track['name'],frames=256)),flush=True)
        for name,clip,n,seconds in [('orb-horizontal',8,16,5),('waves',9,40,2),('spray-radiance',10,40,2)]:
            dirs=16 if clip==8 else 1
            track=dict(name=name,model=6,clip=clip,directions=dirs,frames=n,duration_seconds=seconds,phase_ticks=seconds*20,bot_rotation_radians_per_second=.15 if clip==8 else 0,adaptation='Fixed seeded random stream repeats every2seconds for continuous particles; original emission/lifetime/size/fades remain. Orb texture .2cycles/s uses5seconds. Bot rotation .15rad/s has an independent16-direction phase.');tracks.append(track)
            for direction in range(dirs):
                for number in range(n):
                    t=number*seconds/n;c=canvas(a.ss)
                    if clip==8:orb(c,t,textures,relative_points,np.asarray(entity['particle_tint_color'])/255,direction*math.tau/16)
                    elif clip==9:counts.append(dict(track=name,t=t,counts=waves(c,t,textures,relative_points,np.asarray(entity['particle_tint_color'])/255)))
                    else:counts.append(dict(track=name,t=t,counts=spray_and_radiance(c,t,textures,relative_points,orientations)))
                    save_frame(records,track,direction,number,n,c.finish(),t)
            print(json.dumps(dict(track=name,frames=n*dirs)),flush=True)
    light=None
    if a.only in (None,'light'):light=light_map(materials,textures,entity,points)
    materials.save()
    name='manifest.json' if a.only is None else 'manifest-'+a.only+'.json'
    manifest=dict(complete=True,minimum_pack_version=12,frames=records,tracks=tracks,anchors=ANCHORS,source_entity=entity,control_points_source={str(k):v.tolist() for k,v in source_points.items()},control_points_world={str(k):v.tolist() for k,v in points.items()},source_particle_graphs={p.name:sha(p) for p in GRAPHS.glob('fountain*.vpcf')},source_textures=texture_receipts,source_materials=rel(SOURCE/'manifest.json'),original_shader=rel(ROOT/'private/source2-v022/fountain-shaders/fountain-shader.json'),static_light=light,particle_counts=counts,uncompressed_bytes=sum(r['width']*r['height']*5 for r in records),source_sha256={rel(p):sha(p) for p in (Path(__file__),ROOT/'tools/source2_materials.py',ROOT/'tools/source2_kv3_text.py',OLD,ROOT/'private/source2-v022/fountain-shaders/fountain-shader.json')},limitations=['Original DXBC verifies scroll.G=local velocity and scroll.A=opacity, color.A ignored. Original normal/view angle alpha is used. H1 dual staggered advection phases close a compact loop; native continuous flow has no short common period.','Full Source2 dynamic specular/fog/exposure are not reproduced. Original g_tSpecular resource is retained. Translucent mesh uses its original unlit colour with authored alpha, not arbitrary brightness/opacity factors.','Deterministic repeating random draws and spatial/temporal sprite sampling adapt the continuous Source particle graph to H1. No particle count or source lifetime is reduced.','The waves texture has no spritesheet. Authored sequence7 resolves to its sole original image, without inventing8 textures.','Native additive RGB565/sRGB saturates; original straight alpha is retained, not gamma-encoded. HDR particle overlap resolves into bounded straight RGB.'])
    write(OUT/name,manifest);print(json.dumps(dict(complete=True,frames=len(records),uncompressed_bytes=manifest['uncompressed_bytes'],output=rel(OUT),static_light=light)),flush=True)
if __name__=='__main__':main()
