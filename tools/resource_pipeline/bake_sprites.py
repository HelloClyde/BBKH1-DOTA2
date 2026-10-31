"""Bake original Steam models/rigged equipment into fixed-view H1 sprites.

The PC evaluates original skeletons and rasterizes all original triangles. The
H1 receives RGBA/RGB565 pixels and relative depth, without meshes or bones.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
from PIL import Image,ImageDraw
from gltf_assets import Gltf,np,ROOT
from bake_sprite_raster import Raster
from skill_motion_catalog import SKILLS,EXTRA_PHASES,motion_specs,source_window

EXPORTS=ROOT/'private/source2/exports'
HEROES=['axe','lina','juggernaut','crystal_maiden']
CLIPS={
    'axe':['idle','run','attack_main','berserkers_call','death'],
    'lina':['idle','run','attack1','dragon_slave','death'],
    'juggernaut':['idle','run_run','attack','attack_spin','death'],
    'crystal_maiden':['cm_idle','cm_run','cm_attack','nova','die'],
}
HERO_SCALE=[114,86,80,83]
MODELS=[*HEROES,'radiant_tower','dire_tower','radiant_base','dire_base','radiant_creep','dire_creep']


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_normals(g,p,matrices):
    node=g.doc['nodes'][p['node_id']]
    primitive=g.doc['meshes'][node['mesh']]['primitives'][p['primitive_id']]
    at=primitive['attributes'];normals=g.accessor(at['NORMAL']).astype(np.float64)
    if 'skin' in node and 'JOINTS_0' in at and 'WEIGHTS_0' in at:
        skin=g.doc['skins'][node['skin']];joints=g.accessor(at['JOINTS_0']).astype(np.int32)
        weights=g.accessor(at['WEIGHTS_0']).astype(float);weights/=np.maximum(weights.sum(1,keepdims=True),1e-12)
        binds=g.accessor(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1) if 'inverseBindMatrices' in skin else np.repeat(np.eye(4)[None],len(skin['joints']),axis=0)
        transforms=np.array([np.linalg.inv((matrices[j]@b)[:3,:3]).T for j,b in zip(skin['joints'],binds)])
        out=np.zeros_like(normals)
        for slot in range(joints.shape[1]):out+=np.einsum('nij,nj->ni',transforms[joints[:,slot]],normals)*weights[:,slot,None]
    else:out=normals@np.linalg.inv(matrices[p['node_id']][:3,:3])
    out/=np.maximum(np.linalg.norm(out,axis=1)[:,None],1e-12)
    return out


class Model:
    def __init__(self,slug,model_id):
        self.slug=slug;self.model_id=model_id;self.textures={};self.receipts=[]
        if model_id<4:
            body=EXPORTS/('bake-source/'+slug if slug in ('axe','lina') else slug+'-allanim')/(slug+'.gltf')
            parts=json.loads((ROOT/'private/source2/reports/rigged-parts-manifest.json').read_text())['heroes'][slug]['parts']
            self.paths=[body,*map(Path,parts)];self.clips=CLIPS[slug];self.height=160;self.runtime_scale=HERO_SCALE[model_id]
        else:
            self.paths=[EXPORTS/'props'/slug/(slug+'.gltf')]
            self.clips=['idle','run'] if 'creep' in slug else [None]
            self.height=85 if 'creep' in slug else 230 if 'tower' in slug else 260
            self.runtime_scale=129 if 'creep' in slug else [124,118][model_id-4] if 'tower' in slug else [134,188][model_id-6]
        self.docs=[Gltf(p) for p in self.paths];self.body=self.docs[0]
        self.names={a.get('name'):i for i,a in enumerate(self.body.doc.get('animations',[]))}
        self.motion_specs=motion_specs(slug,self.clips)
        for name in (s['source_clip'] for s in self.motion_specs):
            if name is not None and name not in self.names:raise ValueError(f'{slug}: missing original clip {name}')
        # Use the same idle reference and canonical scaling as the live game.
        reference=self.pose(self.clips[0],0,raw=True)
        points=np.concatenate([p['points'] for p in reference]);lo=points.min(0);hi=points.max(0)
        self.floor=lo[1];self.scale=self.height/max(hi[1]-lo[1],1e-9)*self.runtime_scale/256
        root_joint=self.body.doc['skins'][0]['joints'][0] if self.body.doc.get('skins') else None
        self.root_name=self.body.doc['nodes'][root_joint]['name'] if root_joint is not None else None
        self.root0=self.body.globals(self.names.get(self.clips[0]),0)[root_joint][:3,3] if root_joint is not None else np.zeros(3)
        for path,g in zip(self.paths,self.docs):self.receipts.append({'gltf':str(path.relative_to(ROOT)),'sha256':digest(path),'buffers':g.buffer_sha256})
        self.original_triangles=sum(len(p['faces']) for p in reference)

    def pose(self,clip,seconds,raw=False):
        animation=self.names.get(clip);body_matrices=self.body.globals(animation,seconds)
        bones={n.get('name'):m for n,m in zip(self.body.doc['nodes'],body_matrices)}
        result=[]
        motion=np.zeros(3) if raw or not self.root_name else bones[self.root_name][:3,3]-self.root0
        for di,g in enumerate(self.docs):
            external=bones if di else None
            matrices=body_matrices if di==0 else g.globals(external_bones=external)
            for p in g.primitives(animation if di==0 else None,seconds,external_bones=external):
                if any(s in p['name'].lower() for s in ('stump','inspector','physics','collision','cavern')):continue
                p=dict(p,doc=di)
                if not raw:
                    xyz=p['points'];p['points']=np.column_stack((-xyz[:,0]+motion[0],xyz[:,2]-motion[2],xyz[:,1]-self.floor))*self.scale
                    n=source_normals(g,p,matrices);p['normals']=np.column_stack((-n[:,0],n[:,2],n[:,1]))
                result.append(p)
        return result

    def texture(self,di,mi):
        key=(di,mi)
        if key in self.textures:return self.textures[key]
        g=self.docs[di];material=g.material(mi);m=material['source'];vmat=m.get('extras',{}).get('vmat',{})
        if material.get('image'):
            image=np.asarray(Image.open(material['image']).convert('RGBA')).copy()
        else:raise ValueError(f'{self.slug}: missing original colour texture {material["name"]}')
        factor=np.asarray(material['factor'],dtype=float)
        image_stem=Path(material['image']).stem
        for key,value in vmat.get('TextureParams',{}).items():
            if key.startswith('g_tColor') and Path(value.replace('\\','/')).stem==image_stem:
                tint=vmat.get('VectorParams',{}).get('g_vColorTint'+key[len('g_tColor'):],[1,1,1]);factor[:3]*=tint[:3];break
        mode=m.get('alphaMode','OPAQUE')
        cutoff=float(m.get('alphaCutoff',.5)) if mode=='MASK' else 0
        if mode=='OPAQUE':image[:,:,3]=255
        self.textures[di,mi]=(image,cutoff,factor)
        return self.textures[di,mi]

    def frame(self,pose,direction,ss,include_shadow=False):
        angle=direction*math.tau/16;co=math.cos(angle);si=math.sin(angle);prepared=[];bounds=[]
        for p in pose:
            q=p['points'].copy();n=p['normals'].copy()
            q[:,0],q[:,1]=p['points'][:,0]*co-p['points'][:,1]*si,p['points'][:,0]*si+p['points'][:,1]*co
            n[:,0],n[:,1]=p['normals'][:,0]*co-p['normals'][:,1]*si,p['normals'][:,0]*si+p['normals'][:,1]*co
            projected=np.column_stack((q[:,0]*180/256,(q[:,1]-q[:,2])*181*180/65536,-(q[:,1]+q[:,2])))
            used=np.unique(p['faces']);bounds.append(projected[used,:2]);prepared.append((p,projected,n))
        bound=np.concatenate(bounds);left,top=np.floor(bound.min(0)).astype(int)-2;right,bottom=np.ceil(bound.max(0)).astype(int)+2
        raster=Raster(right-left+1,bottom-top+1,ss)
        for p,projected,n in prepared:
            projected[:,:2]-=[left,top];texture,cutoff,factor=self.texture(p['doc'],p['material'])
            raster.draw(projected,p['faces'],p['uv'],n,texture,cutoff=cutoff,factor=factor)
        rgba,depth=raster.finish();ys,xs=np.where(rgba[:,:,3]>0)
        if not len(xs):raise ValueError(f'{self.slug}: empty original pose')
        x0=max(0,int(xs.min())-1);x1=min(rgba.shape[1],int(xs.max())+2);y0=max(0,int(ys.min())-1);y1=min(rgba.shape[0],int(ys.max())+2)
        rgba=rgba[y0:y1,x0:x1];depth=depth[y0:y1,x0:x1];depth=np.where(np.isfinite(depth),depth,0)
        if max(rgba.shape[:2])>256:raise ValueError(f'{self.slug}: crop exceeds H1 frame limit {rgba.shape}')
        depth=np.rint(depth).clip(-32768,32767).astype('<i2');ax,ay=-left-x0,-top-y0
        if include_shadow:
            from bake_hero_shadows import project_shadow,compact_shadow
            shadow,sax,say=project_shadow(self,pose,direction,ss)
            shadow,sax,say=compact_shadow(shadow,sax,say)
            self.last_shadow_anchor=(sax,say)
            return rgba,depth,ax,ay,shadow
        return rgba,depth,ax,ay


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'private/baked/sprites')
    parser.add_argument('--model',action='append',choices=MODELS)
    parser.add_argument('--frames',type=int,default=8)
    parser.add_argument('--directions',type=int,default=16)
    parser.add_argument('--ss',type=int,default=2)
    parser.add_argument('--preview-only',action='store_true')
    parser.add_argument('--shadows-only',action='store_true',help='Augment every existing original hero body frame with its source-pose shadow')
    args=parser.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    if args.shadows_only:
        from bake_hero_shadows import augment_existing
        augment_existing(args.output,ROOT/'build/conversion/hero-shadow-before',args.ss)
        return
    records=[];sources=[];contact=[];motions=[]
    if args.model and not args.preview_only and (args.output/'manifest.json').exists():
        previous=json.loads((args.output/'manifest.json').read_text(encoding='utf8'))
        selected={MODELS.index(slug) for slug in args.model}
        records=[r for r in previous['frames'] if r['model'] not in selected]
        sources=[r for r in previous['models'] if r['id'] not in selected]
        motions=[r for r in previous.get('motions',[]) if r['model'] not in selected]
    for slug in args.model or MODELS:
        model=Model(slug,MODELS.index(slug));folder=args.output/slug;folder.mkdir(exist_ok=True)
        sources.append({'id':model.model_id,'name':slug,'source_triangles':model.original_triangles,'source':model.receipts,'canonical_scale':model.scale,'source_floor':float(model.floor)})
        for spec in model.motion_specs:
            clip_id=spec['clip_id'];clip=spec['source_clip']
            count=spec.get('frames',args.frames) if clip else 1
            source_duration=model.body.duration(model.names[clip]) if clip else .05
            start,end=source_window(spec,source_duration)
            duration=end-start;source_ticks=max(1,round(duration*20));loop=spec['mode']=='persistent_loop'
            # A stored frame has at least one game tick. Preserve legacy clip
            # metadata; explicitly record the minimum for new short starts.
            duration_ticks=max(count,source_ticks) if clip_id>=5 else source_ticks
            motion=dict(spec,model=model.model_id,hero=slug,source_start_seconds=start,
                        source_end_seconds=end,source_duration_seconds=source_duration,
                        sampled_duration_seconds=duration,source_duration_ticks=source_ticks,
                        duration_ticks=duration_ticks,frames=count,directions=16 if model.model_id<4 or model.model_id>=8 else 1)
            motions.append(motion)
            for frame in range(count if not args.preview_only else 1):
                seconds=start+duration*frame/(count if loop else max(1,count-1))
                pose=model.pose(clip,seconds)
                directions=range(args.directions) if model.model_id<4 or model.model_id>=8 else range(1)
                for direction in directions:
                    shadow=None
                    if model.model_id<4:rgba,depth,ax,ay,shadow=model.frame(pose,direction,args.ss,include_shadow=True)
                    else:rgba,depth,ax,ay=model.frame(pose,direction,args.ss)
                    name=f'{clip_id}-{direction:02d}-{frame:02d}';path=folder/(name+'.npz')
                    np.savez_compressed(path,rgba=rgba,depth=depth,**({'shadow':shadow} if shadow is not None else {}))
                    records.append({'model':model.model_id,'clip':clip_id,'direction':direction,'frame':frame,'frames':count,'duration_ticks':duration_ticks,'source_duration_ticks':source_ticks,'loop':loop,'source_clip':clip,'source_seconds':seconds,'skill':spec.get('skill'),'phase':spec['phase'],'anchor_x':int(ax),'anchor_y':int(ay),'width':rgba.shape[1],'height':rgba.shape[0],'npz':str(path.relative_to(ROOT)),'sha256':digest(path)})
                    if shadow is not None:
                        records[-1].update(flags=4,shadow_pixels=int(np.count_nonzero(shadow)),shadow_alpha_max=int(shadow.max()),body_box_in_frame=[0,0,rgba.shape[1],rgba.shape[0]],shadow_width=shadow.shape[1],shadow_height=shadow.shape[0],shadow_anchor_x=model.last_shadow_anchor[0],shadow_anchor_y=model.last_shadow_anchor[1])
                    if direction==0 and frame==0:
                        png=folder/(name+'.png');Image.fromarray(rgba).save(png);contact.append((slug+' '+str(clip),rgba))
            print(json.dumps({'model':slug,'clip':clip,'frames':count,'directions':len(directions),'triangles':model.original_triangles}),flush=True)
        manifest={'models':[sources[-1]],'frames':[r for r in records if r['model']==model.model_id],'motions':[m for m in motions if m['model']==model.model_id],'projection':{'zoom_q8':180,'vertical_q8':181,'directions':16,'anchor':'top-left=projected-feet-anchor; local bounds use floor/ceil','depth':'signed world units -(rotated localY+localZ)'},'rendering':'Full original source triangles and rigged equipment, original texture bilinear in linear light, smooth skinned normals, premultiplied 2x supersampling; no runtime triangles','source_sha256':{p.name:digest(p) for p in [Path(__file__),Path(__file__).with_name('skill_motion_catalog.py'),Path(__file__).with_name('bake_sprite_raster.py'),Path(__file__).with_name('bake_sprite_raster.c')]}}
        (folder/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    records.sort(key=lambda r:(r['model'],r['clip'],r['frame'],r['direction']))
    sources.sort(key=lambda r:r['id'])
    result={'models':sources,'frames':records,'motions':motions,'complete':not args.preview_only,'ss':args.ss}
    if any(r['model']<4 and r.get('flags',0)&4 for r in records):
        from bake_hero_shadows import projection_metadata
        result['shadow']=projection_metadata(args.ss)
    (args.output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    catalog={'heroes':SKILLS,'extra_phases':EXTRA_PHASES,'motions':motions,'complete':not args.preview_only,
             'source_sha256':{p.name:digest(p) for p in [Path(__file__),Path(__file__).with_name('skill_motion_catalog.py')]},
             'timing':'Original source seconds; each stored frame is at least1 tick at20Hz; short starts report both source and stored durations.'}
    (args.output/'skill-motion-catalog.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    if contact:
        out=Image.new('RGB',(5*180,math.ceil(len(contact)/5)*180),(34,42,41));draw=ImageDraw.Draw(out)
        for i,(name,rgba) in enumerate(contact):
            x=i%5*180;y=i//5*180;im=Image.fromarray(rgba);im=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
            out.paste(im,(x+(180-im.width)//2,y+24+(150-im.height)//2),im);draw.text((x+4,y+4),name,fill='white')
        out.save(args.output/'contact.png')
    print(json.dumps({'frames':len(records),'models':len(sources),'manifest':str(args.output/'manifest.json')}))


if __name__=='__main__':main()
