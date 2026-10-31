"""Bake simplified animated FX from installed Steam particle textures.

The exported particle images are original Valve data. Motion, tint, emission
and compositing here are a deterministic offline approximation, not execution
of Source 2's particle operators. H1 blits the completed RGBA/depth frames.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import subprocess
from PIL import Image,ImageDraw
from gltf_assets import np,ROOT
from source2_vpk import VpkArchive

STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
RESOURCES={
    'spin':'materials/particle/juggernaut/juggernaut_blade_fury.vtex_c',
    'scorch':'materials/particle/lina/lina_scorch_mark_color.vtex_c',
    'glow':'materials/particle/softglow.vtex_c',
}
KINDS=[('attack',35,6,3),('burst',160,14,3),('line',100,18,3),
       ('heal',90,20,3),('death',60,16,2),('gold',50,16,3)]
ZOOM=180/256;PITCH=181*180/65536


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def linear(rgb):return np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
def srgb(rgb):return np.where(rgb<=.0031308,rgb*12.92,1.055*np.maximum(rgb,0)**(1/2.4)-.055)


def export_sources(archive,tool):
    vpk=VpkArchive(archive);folder=ROOT/'private/source2/exports/baked-fx'
    folder.mkdir(parents=True,exist_ok=True);images={};receipts=[]
    for slug,entry in RESOURCES.items():
        raw=ROOT/'private/source2/raw/baked-fx'/(slug+'.vtex_c')
        raw.parent.mkdir(parents=True,exist_ok=True);raw.write_bytes(vpk.read(entry))
        output=folder/(slug+'.png');command=[str(tool),'-d','-i',str(raw),'-o',str(output)]
        process=subprocess.run(command,capture_output=True,text=True)
        (folder/(slug+'-export.log')).write_text(process.stdout+process.stderr,encoding='utf8')
        if process.returncode:raise RuntimeError('VRF texture export failed: '+slug)
        # VRF reconstructs this original sheet as nine independent sequence
        # images plus an MKS description. The .png-named main output is VTEX
        # source text, so deliberately read the actual sequence PNGs instead.
        paths=sorted(folder.glob(slug+'_seq*.png')) if slug=='spin' else [output]
        if not paths:raise ValueError('Missing decoded original texture '+slug)
        decoded=[]
        for path in paths:
            rgba=np.asarray(Image.open(path).convert('RGBA'),dtype=np.float32)/255
            rgb=linear(rgba[:,:,:3]);strength=rgb.max(2)
            # Express luminous black-background textures as straight RGB and
            # alpha, so black borders do not create opaque rectangular sprites.
            alpha=rgba[:,:,3] if slug=='scorch' else rgba[:,:,3]*strength
            normalized=rgb if slug=='scorch' else rgb/np.maximum(strength[:,:,None],1e-8)
            decoded.append(np.dstack((normalized,alpha)).astype(np.float32))
        images[slug]=decoded
        receipts.append({'name':slug,'source':vpk.identity(entry),'raw':str(raw.relative_to(ROOT)),
                         'tool_sha256':digest(tool),'command':command,
                         'decoded':[{'path':str(p.relative_to(ROOT)),'sha256':digest(p),
                                     'size':list(Image.open(p).size)} for p in paths]})
    return images,receipts


def sample(texture,u,v):
    h,w=texture.shape[:2];inside=(u>=0)&(u<=1)&(v>=0)&(v<=1)
    x=np.clip(u*(w-1),0,w-1);y=np.clip(v*(h-1),0,h-1)
    x0=np.floor(x).astype(int);y0=np.floor(y).astype(int)
    x1=np.minimum(x0+1,w-1);y1=np.minimum(y0+1,h-1);fx=(x-x0)[...,None];fy=(y-y0)[...,None]
    result=(texture[y0,x0]*(1-fx)+texture[y0,x1]*fx)*(1-fy)+(texture[y1,x0]*(1-fx)+texture[y1,x1]*fx)*fy
    result[:,:,3]*=inside
    return result


class Canvas:
    def __init__(self,ss):
        self.ss=ss;self.width=240;self.height=224;self.ax=120;self.ay=132
        y,x=np.mgrid[:self.height*ss,:self.width*ss]
        self.px=(x+.5)/ss-self.ax;self.py=(y+.5)/ss-self.ay
        self.x=self.px/ZOOM;self.y=self.py/PITCH
        self.rgb=np.zeros((*x.shape,3),dtype=np.float32);self.alpha=np.zeros(x.shape,dtype=np.float32)
        self.depth=np.full(x.shape,np.inf,dtype=np.float32)

    def add(self,image,depth,tint,strength,rect=None):
        rect=rect or (slice(None),slice(None));alpha=np.clip(image[:,:,3]*strength,0,1)
        self.rgb[rect]+=image[:,:,:3]*np.asarray(tint,dtype=np.float32)*alpha[:,:,None]
        self.alpha[rect]=np.maximum(self.alpha[rect],alpha)
        self.depth[rect]=np.minimum(self.depth[rect],np.where(alpha>.005,depth,np.inf))

    def ground(self,texture,radius,rotation,tint,strength):
        co=math.cos(rotation);si=math.sin(rotation)
        u=.5+(self.x*co+self.y*si)/(2*radius);v=.5+(-self.x*si+self.y*co)/(2*radius)
        self.add(sample(texture,u,v),-self.y,tint,strength)

    def glow(self,texture,x,y,z,radius_px,tint,strength):
        cx=x*ZOOM;cy=(y-z)*PITCH;r=max(1.,radius_px)
        left=max(0,math.floor((cx-r+self.ax)*self.ss));right=min(self.width*self.ss,math.ceil((cx+r+self.ax)*self.ss))
        top=max(0,math.floor((cy-r+self.ay)*self.ss));bottom=min(self.height*self.ss,math.ceil((cy+r+self.ay)*self.ss))
        if right<=left or bottom<=top:return
        rect=(slice(top,bottom),slice(left,right));dx=self.px[rect]-cx;dy=self.py[rect]-cy
        image=sample(texture,.5+dx/(2*r),.5+dy/(2*r))
        self.add(image,-(y+z)+dy/PITCH,tint,strength,rect)

    def beam(self,texture,phase,direction,tint):
        angle=direction*math.tau/16;dx=-math.sin(angle);dy=math.cos(angle)
        along=self.x*dx+self.y*dy;across=self.x*dy-self.y*dx
        across-=np.sin(along*.075+phase*math.tau)*2.2
        image=sample(texture,.5+across/24,.5+along/200)
        self.add(image,-self.y-3,tint,1.5*(1-.55*phase))
        for j in range(3):
            distance=-90+180*((phase+j*.23)%1)
            self.glow(texture,dx*distance,dy*distance,5,5.5,tint,.6*(1-phase)+.08)

    def finish(self):
        ss=self.ss;h=self.height;w=self.width
        alpha=self.alpha.reshape(h,ss,w,ss).mean((1,3))
        rgb=self.rgb.reshape(h,ss,w,ss,3).mean((1,3))/np.maximum(alpha[:,:,None],1e-8)
        depth=self.depth.reshape(h,ss,w,ss).min((1,3))
        rgba=np.dstack((np.rint(np.clip(srgb(rgb),0,1)*255).astype(np.uint8),np.rint(alpha*255).astype(np.uint8)))
        depth=np.rint(np.where(np.isfinite(depth)&(alpha>0),depth,0)).clip(-32768,32767).astype('<i2')
        ys,xs=np.where(rgba[:,:,3]>0)
        if not len(xs):raise ValueError('Empty baked FX frame')
        x0=max(0,int(xs.min())-1);x1=min(w,int(xs.max())+2);y0=max(0,int(ys.min())-1);y1=min(h,int(ys.max())+2)
        return rgba[y0:y1,x0:x1],depth[y0:y1,x0:x1],self.ax-x0,self.ay-y0


def effect_frame(kind,frame,count,direction,textures,ss):
    p=frame/max(1,count-1);canvas=Canvas(ss);glow=textures['glow'][0]
    spin=textures['spin'];scorch=textures['scorch'][0]
    warm=(1.,.24,.055);green=(.17,1.,.33);purple=(.42,.24,.8)
    if kind=='attack':
        canvas.ground(spin[8],35*(.65+.45*p),p*2.6,warm,2.5*(1-p)+.08)
        canvas.glow(glow,0,0,8,10*(1-.5*p),(1.,.68,.24),1-p+.03)
        for j in range(7):
            a=j*math.tau/7+.2;r=8+28*p
            canvas.glow(glow,math.cos(a)*r,math.sin(a)*r,8+25*p,2.2,warm,.8*(1-p)+.02)
    elif kind=='burst':
        canvas.ground(spin[frame%len(spin)],160*(.3+.7*p),p*2,warm,2.4*(1-.75*p))
        canvas.glow(glow,0,0,5,28*(1-.45*p),(1.,.5,.12),.8*(1-p)+.02)
        for j in range(18):
            a=j*math.tau/18+.23;r=32+100*p;z=12+70*math.sin(p*math.pi)
            canvas.glow(glow,math.cos(a)*r,math.sin(a)*r,z,2.7,(1.,.55,.16),.8*(1-p)+.03)
    elif kind=='line':canvas.beam(glow,p,direction,(1.,.42,.08))
    elif kind=='heal':
        canvas.ground(spin[4],90*(.78+.2*math.sin(p*math.pi)),p*.8,green,1.6*(1-.6*p))
        for j in range(14):
            a=j*math.tau/14+p*.65;r=26+32*((j%3)/2);z=8+85*((p+j*.11)%1)
            canvas.glow(glow,math.cos(a)*r,math.sin(a)*r,z,3.7,green,.65*(1-.6*p))
    elif kind=='death':
        canvas.ground(scorch,60*(.7+.3*p),.2,(.8,.3,.2),.85*(1-.8*p))
        for j in range(13):
            a=j*math.tau/13+.9*p;r=15+30*p;z=8+105*p+10*(j%3)
            canvas.glow(glow,math.cos(a)*r,math.sin(a)*r,z,3.7,purple,.7*(1-p)+.02)
    elif kind=='gold':
        for j in range(13):
            a=j*math.tau/13;r=12+28*math.sin(p*math.pi);z=12+105*p+8*(j%3)
            canvas.glow(glow,math.cos(a)*r,math.sin(a)*r,z,3.2,(1.,.63,.07),.9*(1-p)+.02)
    else:raise ValueError(kind)
    return canvas.finish()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'private/baked/effects')
    parser.add_argument('--archive',type=Path,default=STEAM/'pak01_dir.vpk')
    parser.add_argument('--tool',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--frames',type=int,default=8)
    parser.add_argument('--ss',type=int,default=2)
    args=parser.parse_args()
    if not 2<=args.frames<=16 or not 1<=args.ss<=4:parser.error('Use 2..16 frames and 1..4 supersampling')
    args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    textures,sources=export_sources(args.archive,args.tool);records=[];models=[];preview={}
    for index,(kind,radius,duration,flags) in enumerate(KINDS):
        folder=args.output/kind;folder.mkdir(exist_ok=True);directions=range(16) if kind=='line' else range(1)
        model={'id':10+index,'name':kind,'reference_radius_world':radius,'directions':len(directions),
               'method':'Deterministic offline composition of original Steam particle images; simplified emission/motion/tint, not original Source 2 particle simulation'}
        models.append(model)
        for direction in directions:
            for frame in range(args.frames):
                rgba,depth,ax,ay=effect_frame(kind,frame,args.frames,direction,textures,args.ss)
                path=folder/f'0-{direction:02d}-{frame:02d}.npz';np.savez_compressed(path,rgba=rgba,depth=depth)
                records.append({'model':10+index,'clip':0,'direction':direction,'frame':frame,'frames':args.frames,
                                'duration_ticks':duration,'loop':False,'source_clip':'offline_'+kind,'source_seconds':duration/20*frame/(args.frames-1),
                                'anchor_x':ax,'anchor_y':ay,'width':rgba.shape[1],'height':rgba.shape[0],
                                'height_world':radius,'flags':flags,'npz':str(path.relative_to(ROOT)),'sha256':digest(path)})
                if direction==0:
                    png=path.with_suffix('.png');Image.fromarray(rgba).save(png);preview[kind,frame]=rgba
        (folder/'manifest.json').write_text(json.dumps({'models':[model],'frames':[r for r in records if r['model']==10+index]},indent=2)+'\n')
        print(json.dumps({'effect':kind,'frames':args.frames,'directions':len(directions),'radius':radius}),flush=True)
    manifest={'models':models,'frames':records,'complete':True,'ss':args.ss,'source_textures':sources,
              'projection':{'zoom_q8':180,'vertical_q8':181,'directions':16,'line_direction0':'+worldY','line_reference_length_world':200,'depth':'signed -(localY+localZ)'},
              'source_sha256':{Path(__file__).name:digest(__file__)},
              'limitations':'Generic effect kind animation; does not reproduce every hero-specific Source 2 particle graph, original operator timing, lighting, refraction or audio.'}
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    contact=Image.new('RGB',(args.frames*240,len(KINDS)*240),(30,38,38));draw=ImageDraw.Draw(contact)
    for row,(kind,*_) in enumerate(KINDS):
        for frame in range(args.frames):
            im=Image.fromarray(preview[kind,frame]);x=frame*240;y=row*240
            contact.paste(im,(x+(240-im.width)//2,y+24+(210-im.height)//2),im)
            draw.text((x+4,y+4),f'{kind} {frame}',fill='white')
    contact.save(args.output/'contact.png')
    print(json.dumps({'effects':len(models),'total_frames':len(records),'manifest':str(args.output/'manifest.json')}))


if __name__=='__main__':main()
