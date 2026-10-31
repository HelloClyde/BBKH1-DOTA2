"""Offline animated river colours and geometric visibility for H1.

The original Steam flow normal and foam textures drive a two-phase scrolling
loop. H1 receives colour indices, two palettes and a coverage/shore byte per
visible river pixel. There is no normal/particle shader or triangle work on H1.
"""
from pathlib import Path
import argparse,hashlib,json,math
from PIL import Image
from gltf_assets import Gltf,np,ROOT
from bake_sprite_raster import Raster

OUT=ROOT/'private/baked/water'
MAP=ROOT/'build/conversion/baked-map'
WATER_NAMES={'water_generic_000','water_generic_000_slope'}
BASIS=np.array([[0,0,1],[-1,0,0],[0,1,0]],float)
SCALE=4096/(16384*.0254)
SIZE=128;FRAMES=32;FRAME_TICKS=2

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(path):return json.loads(Path(path).read_text(encoding='utf8'))
def rgb565(rgb):
    a=np.asarray(rgb,dtype=np.uint16)
    return (((a[...,0]>>3)<<11)|((a[...,1]>>2)<<5)|(a[...,2]>>3)).astype('<u2')
def rgb8(v):
    v=np.asarray(v,dtype=np.uint32)
    return np.stack(((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31),axis=-1).astype(np.uint8)
def mix565(fg,bg,alpha):
    fg=np.asarray(fg,dtype=np.uint32);bg=np.asarray(bg,dtype=np.uint32);alpha=np.asarray(alpha,dtype=np.uint32);inv=255-alpha
    r=(((fg>>11)&31)*alpha+((bg>>11)&31)*inv+127)//255
    g=(((fg>>5)&63)*alpha+((bg>>5)&63)*inv+127)//255
    b=((fg&31)*alpha+(bg&31)*inv+127)//255
    return ((r<<11)|(g<<5)|b).astype('<u2')

def periodic(texture):
    """A light periodic lowpass avoids a hard edge in the authored texture crop."""
    h,w=texture.shape[:2];fy=np.fft.fftfreq(h)[:,None];fx=np.fft.fftfreq(w)[None,:]
    filt=np.exp(-2*math.pi**2*.72**2*(fx*fx+fy*fy))
    if texture.ndim==3:filt=filt[:,:,None]
    return np.fft.ifft2(np.fft.fft2(texture,axes=(0,1))*filt,axes=(0,1)).real

def sample(texture,x,y):
    h,w=texture.shape[:2];ix=np.floor(x).astype(int);iy=np.floor(y).astype(int);fx=x-ix;fy=y-iy
    a=texture[iy%h,ix%w];b=texture[iy%h,(ix+1)%w];c=texture[(iy+1)%h,ix%w];d=texture[(iy+1)%h,(ix+1)%w]
    if texture.ndim==3:fx=fx[...,None];fy=fy[...,None]
    return (a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy

def flow(texture,x,y,phase,dx,dy):
    """Two half-cycle phases hide resets while keeping continuous drift."""
    other=(phase+.5)%1;weight=abs(2*phase-1)
    return sample(texture,x+dx*phase,y+dy*phase)*(1-weight)+sample(texture,x+dx*other,y+dy*other)*weight

def textures():
    folder=ROOT/'private/source2/exports/map-water/maps/dota/worldnodes'
    normal=folder/'water_river_normal_sharp_temp_normal_tga_4ab60bd5.png'
    foam=folder/'water_foam_02_color_psd_589c8514.png'
    raw=np.asarray(Image.open(normal).convert('RGB').resize((SIZE,SIZE),Image.Resampling.LANCZOS),float)/255
    n=periodic(raw[:,:,:2]*2-1)
    rawfoam=np.asarray(Image.open(foam).convert('RGB').resize((SIZE,SIZE),Image.Resampling.LANCZOS),float)/255
    f=periodic(rawfoam.mean(2))
    yy,xx=np.mgrid[:SIZE,:SIZE];frames=[]
    for frame in range(FRAMES):
        # The two flow phases exchange roles after half a phase cycle. Sample
        # that complete visual cycle over all 32 frames instead of twice.
        phase=frame/(FRAMES*2)
        a=flow(n,xx,yy,phase,25,-10)
        b=flow(n,xx*2+37,yy*2+19,(phase+.23)%1,-16,13)
        nx=a[:,:,0]*.76+b[:,:,1]*.24;ny=a[:,:,1]*.76-b[:,:,0]*.24
        broad=np.clip(.50+nx*1.5+ny*.55,0,1)
        # Local normal-facing reflections move with the original ripple field.
        spec=np.exp(-((nx-.075)**2+(ny+.025)**2)/.008)
        shade=np.clip(.10+broad*.57+spec*.33,0,1)
        foamflow=flow(f,xx,yy,(phase+.11)%1,31,-12)
        foamcrest=np.clip((foamflow-.38)/.51,0,1)
        # A wave's local crest gates foam, avoiding a permanently white shore.
        foamcrest*=np.clip(.25+spec*.50+broad*.50,0,1)
        hi=np.rint(shade*15).astype(np.uint8);lo=np.rint(foamcrest*15).astype(np.uint8)
        frames.append((hi<<4)|lo)
    frames=np.asarray(frames,dtype=np.uint8)
    palettes=np.empty((2,256),dtype='<u2')
    for shading in range(16):
        t=shading/15
        water=np.array([25,48,46])*(1-t)+np.array([105,142,133])*t
        for amount in range(16):
            f=(amount/15)**3
            deep=water*(1-f*.10)+np.array([145,177,166])*f*.10
            shore=water*(1-f*.91)+np.array([202,223,213])*f*.91
            palettes[0,shading*16+amount]=rgb565(np.rint(deep))
            palettes[1,shading*16+amount]=rgb565(np.rint(shore))
    # Exact phase wrap and spatial periods are analytic, not an end-frame blend.
    return frames,palettes,[normal,foam]

def geometry_mask(info):
    width,height=info['width'],info['height'];origin=np.array(info['origin_pixels'])
    depth=np.load(MAP/'map-depth.npy');manifest_path=ROOT/'private/source2/reports/water-manifest.json'
    manifest=load(manifest_path);raster=Raster(width,height,ss=2);sources=[];triangle_count=0;parameters=None
    for entry in manifest['assets']:
        path=Path(entry['gltf']);g=Gltf(path);included=False
        for p in g.primitives():
            m=g.material(p['material'])
            if m.get('name') not in WATER_NAMES:continue
            v=m.get('source',{}).get('extras',{}).get('vmat',{});parameters=parameters or v
            xyz=p['points']@BASIS.T*SCALE+[2048,2048,0]
            projected=np.column_stack((xyz[:,0]*180/256-origin[0],(xyz[:,1]-xyz[:,2])*180*181/65536-origin[1],32768-xyz[:,1]-xyz[:,2]))
            normal=np.zeros_like(xyz);normal[:,2]=1
            raster.draw(projected,p['faces'],p['uv'],normal,np.array([[[255,255,255,255]]],np.uint8),unlit=True)
            triangle_count+=len(p['faces']);included=True
        if included:sources.append({'gltf':str(path.relative_to(ROOT)),'sha256':sha(path),'buffers':g.buffer_sha256})
    rgba,z=raster.finish();del raster
    raw_cover=np.rint(rgba[:,:,3].astype(float)*3/255).astype(np.uint8)
    wet=raw_cover>0;visible=wet&np.isfinite(z)&(np.abs(z.astype(float)-depth.astype(float))<=3)
    cover=np.where(visible,raw_cover,0).astype(np.uint8)
    # Distance is computed before foreground occlusion: trees and bridges must
    # not acquire foam halos merely because they hide water beneath them.
    inner=wet.copy();shore=np.zeros_like(raw_cover);shore_width=13
    for step in range(shore_width):
        eroded=inner&np.roll(inner,1,0)&np.roll(inner,-1,0)&np.roll(inner,1,1)&np.roll(inner,-1,1)
        eroded[0]=False;eroded[-1]=False;eroded[:,0]=False;eroded[:,-1]=False
        edge=inner&~eroded;shore[edge]=round(63*(1-step/shore_width));inner=eroded
    shore[~visible]=0
    mask=(shore<<2)|cover
    assert len(sources)==85 and triangle_count==3905
    assert not np.any(mask[~visible]) and int((cover>0).sum())>500000
    return mask,{'source_manifest_sha256':sha(manifest_path),'source_resources':sources,'source_resource_count':len(sources),'source_triangles':triangle_count,'visible_water_pixels':int((cover>0).sum()),'shore_pixels':int((shore>0).sum()),'foreground_occluded_water_pixels':int((wet&~visible).sum()),'shore_width_pixels':shore_width,'geometry_ss':2,'depth_tolerance_world':3,'source_water_vmat':parameters}

def preview(frames,palettes,mask,info):
    image=rgb565(np.asarray(Image.open(MAP/'map.png').convert('RGB')));grid=np.load(MAP/'ground-grid.npy')
    output={};coverage=mask&3;shore=mask>>2
    for name,x,y in [('mid-river',2048,2048),('radiant-bank',1850,2200)]:
        # Preview uses a nearest-grid focus; the runtime uses bilinear height.
        z=int(grid[round(y*128/4095),round(x*128/4095)])
        left=x*180//256-240-info['origin_pixels'][0]
        top=(y-z)*180*181//65536-96-info['origin_pixels'][1]
        bg=image[top:top+192,left:left+480];co=coverage[top:top+192,left:left+480];sh=shore[top:top+192,left:left+480]
        yy,xx=np.mgrid[top:top+192,left:left+480];tx=(xx+info['origin_pixels'][0])&127;ty=(yy+info['origin_pixels'][1])&127
        pictures=[];changes=[];previous=None
        for frame in frames:
            index=frame[ty,tx];fg=mix565(palettes[1,index],palettes[0,index],(sh.astype(np.uint32)*255+31)//63)
            out=mix565(fg,bg,co.astype(np.uint32)*85)
            if previous is not None:changes.append(int(np.count_nonzero(out!=previous)))
            pictures.append(Image.fromarray(rgb8(out)));previous=out
        pictures[0].save(OUT/(name+'.gif'),save_all=True,append_images=pictures[1:],duration=100,loop=0,optimize=False)
        pictures[0].save(OUT/(name+'-frame0.png'));pictures[8].save(OUT/(name+'-frame8.png'))
        output[name]={'camera':[x,y,z],'changed_pixels_between_adjacent_frames_min':min(changes),'changed_pixels_between_adjacent_frames_max':max(changes),'water_pixels':int((co>0).sum()),'gif_sha256':sha(OUT/(name+'.gif'))}
    return output

def main():
    global MAP,OUT
    parser=argparse.ArgumentParser();parser.add_argument('--reuse-mask',action='store_true')
    parser.add_argument('--map',type=Path,default=MAP);parser.add_argument('--output',type=Path,default=OUT)
    args=parser.parse_args();MAP=args.map.resolve();OUT=args.output.resolve();OUT.mkdir(parents=True,exist_ok=True)
    info=load(MAP/'manifest.json');assert sha(MAP/'map.png')==info['map_sha256'] and sha(MAP/'map-depth.npy')==info['depth_sha256']
    frames,palettes,source_textures=textures();np.save(OUT/'frames.npy',frames);np.save(OUT/'palettes.npy',palettes)
    if args.reuse_mask:
        old=load(OUT/'manifest.json');mask=np.load(OUT/'mask.npy');geometry=old['geometry']
        assert old['static_map_depth_sha256']==info['depth_sha256']
    else:
        mask,geometry=geometry_mask(info);np.save(OUT/'mask.npy',mask)
    samples=preview(frames,palettes,mask,info)
    assert len({frame.tobytes() for frame in frames})==FRAMES and all(np.any(frames[i]!=frames[(i+1)%FRAMES]) for i in range(FRAMES))
    report={'complete':True,'frame_count':FRAMES,'frame_ticks':FRAME_TICKS,'tile_size':SIZE,'files':{name:sha(OUT/name) for name in ('frames.npy','palettes.npy','mask.npy')},'source_textures':[{'path':str(p.relative_to(ROOT)),'sha256':sha(p)} for p in source_textures],'static_map_sha256':info['map_sha256'],'static_map_depth_sha256':info['depth_sha256'],'mask_encoding':'HIGH6=shore strength0..63; LOW2=coverage0..3 (alpha=coverage*85)','frame_encoding':'high4bits=normal-driven reflection; low4bits=advected original foam peak; palette0deep,1shore','animation':'Source normal/foam textures, two half-cycle scrolling phases and counter-flow normals, periodic 3.2second loop','runtime':'Palette lookup and fixed alpha only, no normal/particle shader or triangles','geometry':geometry,'previews':samples,'generator_sha256':sha(__file__),'scope':'Simplified offline interpretation of original source normal and foam; not full Source2 water refraction/reflection simulation.'}
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:report[k] for k in ('complete','frame_count','frame_ticks','files','previews')}))

if __name__=='__main__':main()
