"""Original per-instance natural tree/stump frames and lossless TREE.H1T pack.

Optional Windows offline asset workflow; not invoked by ordinary BDA builds.
Historical exports/Steam are read-only. Output and material caches are local.
"""
from pathlib import Path
import argparse, ctypes, gc, hashlib, json, os, re, shutil, struct, subprocess, sys, time, zlib

ROOT=Path(__file__).resolve().parents[1]
N=128*128
SCENE_SHA='c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd'

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def write(p,v):Path(p).write_text(json.dumps(v,indent=2,ensure_ascii=False)+'\n',encoding='utf8',newline='\n')

def context(source,out):
    sys.dont_write_bytecode=True
    def guard(event,a):
        if event=='open' and isinstance(a[0],(str,bytes,os.PathLike)):
            mode,flags=a[1],a[2]
            writing=(isinstance(mode,str) and any(c in mode for c in 'wax+')) or (isinstance(flags,int) and flags&(os.O_WRONLY|os.O_RDWR|os.O_CREAT|os.O_TRUNC|os.O_APPEND))
            if writing and Path(os.fsdecode(a[0])).resolve().is_relative_to(source):raise PermissionError('Read-only historical source: '+str(a[0]))
    sys.addaudithook(guard);sys.path.insert(0,str(source/'tools'))
    import bake_map as bm
    import bake_sprite_raster as raster
    import source2_materials as sm
    from gltf_assets import Gltf,np
    from PIL import Image
    sm.ROOT=ROOT;raster.ROOT=ROOT
    def library():
        c=source/'tools/bake_sprite_raster.c';folder=out/'helpers';folder.mkdir(parents=True,exist_ok=True);p=folder/('raster-'+sha(c)[:12]+'.dll')
        if not p.exists():
            cc=Path('C:/msys64/ucrt64/bin/gcc.exe');env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
            subprocess.run([str(cc),'-std=c99','-O3','-shared','-Wall','-Wextra',str(c),'-o',str(p),'-lm'],check=True,env=env,capture_output=True)
        dll=ctypes.CDLL(str(p));v=ctypes.c_void_p;dll.bake_clear.argtypes=[ctypes.c_int,ctypes.c_int,v,v]
        dll.bake_draw.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_int,v,v,v,v,ctypes.c_int,v,ctypes.c_int,ctypes.c_int,ctypes.c_float,v,ctypes.c_int,ctypes.c_int]
        return dll
    raster.library=library
    return bm,raster,sm,Gltf,np,Image

def frame_stage(source,out):
    if out.exists():raise ValueError('Frame output exists; preserve it and choose a fresh path')
    out.mkdir(parents=True);bm,raster,sm,Gltf,np,Image=context(source,out)
    doc=read(ROOT/'provenance/vision/trees.json');ids=doc['trees'];trees=read(source/'private/source2-v022/active-entities/trees.json')
    assert len(ids)==2113 and len(trees)==2475
    byid={e['hammerUniqueId']:e for e in trees};assert len(byid)==2475
    owners={t['hammer_id']:i for i,t in enumerate(ids)};assert len(owners)==2113
    for t in ids:
        e=byid[t['hammer_id']];assert t['x_q8']%256==t['y_q8']%256==0
        assert [t['x_q8']/256,t['y_q8']/256]==e['world_xyz'][:2]
    cat_path=source/'private/source2/reports/map-completion-manifest.json';cat=read(cat_path)
    matrices=np.asarray([e['matrix_column_major'] for e in trees]);audit=[]
    for asset in cat['assets']:
        if 'tree' not in asset['name']:continue
        mats=np.asarray([p['matrix_column_major'] for p in asset['placements']]);exact=0;near=[]
        for start in range(0,len(mats),128):
            part=mats[start:start+128];ds=((part[:,None,12:15]-matrices[None,:,12:15])**2).sum(2);ix=ds.argmin(1)
            exact+=int((np.max(np.abs(part-matrices[ix]),axis=1)<.0001).sum());near+=np.sqrt(ds[np.arange(len(ix)),ix]).tolist()
        cuttable=sum(p.get('entity',{}).get('hammerUniqueId') in owners for p in asset['placements'])
        audit.append(dict(asset=asset['name'],kind=asset['kind'],placements=len(mats),full_matrix_matches=exact,cuttable_placements=cuttable,constant_placements=len(mats)-cuttable,nearest_translation_metres=[min(near),float(np.median(near)),max(near)],classification='constant worldnode aggregate; no natural-tree matrix identity' if asset['kind']=='aggregate' else 'original cine ent_dota_tree; four exact interactable IDs, four constants'))
    assert all(a['full_matrix_matches']==0 for a in audit if a['kind']=='aggregate')
    write(out/'association-audit.json',dict(passed=True,source_catalog_sha256=sha(cat_path),source_tree_sha256=sha(source/'private/source2-v022/active-entities/trees.json'),records=audit,policy='No nearest-neighbour ownership guess. Independent aggregates and trees outside exact 2113 Hammer IDs remain always alive.'))
    materials=sm.OriginalMaterials(out/'materials');oldcache=source/'private/source2-v022/tree-materials'
    oldresource=materials.resource;oldimage=materials.image
    def seed(name):
        name=name if name.endswith('_c') else name+'_c';folder=name.replace('/','__').replace('.','_');a=oldcache/folder;b=materials.output/folder
        if a.is_dir() and not b.exists():shutil.copytree(a,b)
    def resource(name):seed(name);return oldresource(name)
    def image(name):seed(name);return oldimage(name)
    materials.resource=resource;materials.image=image
    class Renderer(bm.MapBake):
        def __init__(self):
            self.images={};self.sources={};self.triangles=0;self.instances=0;self.replacements={};self.skin_images={};self.offset=(0,0)
        def record(self,g):
            key=str(g.path.relative_to(source))
            if key not in self.sources:self.sources[key]={'sha256':sha(g.path),'buffers':g.buffer_sha256}
        def material(self,g,p):
            original=g.material(p['material']);name=original.get('source',{}).get('extras',{}).get('vmat',{}).get('Name');replacement=self.replacements.get(name)
            if replacement and replacement!=name:
                if replacement not in self.skin_images:self.skin_images[replacement]=materials.material(replacement)
                tex,cut,factor=self.skin_images[replacement];return tex,cut,factor.copy()
            return super().material(g,p)
        def draw(self,xyz,faces,uv,normals,texture,collect_height=False,**kw):
            q=np.column_stack((xyz[:,0]*180/256+512,(xyz[:,1]-xyz[:,2])*180*181/65536+512,32768-xyz[:,1]-xyz[:,2]));q[:,:2]-=self.offset
            self.raster.draw(q,faces,uv,normals,texture,**kw);self.triangles+=len(faces)
    renderer=Renderer();gltfs={};parts_cache={};groups={};records=[]
    def model(e):
        entry=re.search(r'"([^"]+)"',e['model'])[1];stem=Path(entry).stem
        if stem not in gltfs:
            path=source/f'private/source2/exports/bake-trees/{stem}/{stem}.gltf'
            if not path.is_file():path=source/next(a['gltf'] for a in cat['assets'] if a['name']==stem)
            assert path.is_file(),path
            gltfs[stem]=Gltf(path);parts_cache[stem]=list(gltfs[stem].primitives())
            material_groups=materials.resource(entry).get('m_materialGroups',[])
            if material_groups:groups[stem]=materials.groups(entry)
            else:
                assert stem=='tree_cine_02_low'
                groups[stem]={str(t['skin']):{} for t in trees if entry in t['model']}
        return entry,stem,gltfs[stem],parts_cache[stem]
    def projected_bounds(parts,matrix):
        points=np.concatenate([p['points'][np.unique(p['faces'])] for p in parts]);xyz=(points@matrix[:3,:3].T+matrix[:3,3])@bm.BASIS.T*bm.SCALE+[2048,2048,0]
        q=np.column_stack((xyz[:,0]*180/256+512,(xyz[:,1]-xyz[:,2])*180*181/65536+512));lo=np.floor(q.min(0)).astype(int)-2;hi=np.ceil(q.max(0)).astype(int)+3
        return [int(lo[0]),int(lo[1]),int(hi[0]),int(hi[1])]
    banned=('inspector','physics','collision','cavern')
    # Preflight every actual source model/skin before rendering any instance.
    for entity in trees:
        entry,stem,g,parts=model(entity);assert str(entity['skin']) in groups[stem]
        assert any('stump' in p['name'].lower() for p in parts) or stem=='tree_cine_02_low'
    start=time.monotonic()
    for i,t in enumerate(ids):
        e=byid[t['hammer_id']];entry,stem,g,parts=model(e);matrix=np.asarray(e['matrix_column_major']).reshape(4,4).T;renderer.replacements=groups[stem][str(e['skin'])];both=[]
        for dead in (False,True):
            selected=[p for p in parts if not any(v in p['name'].lower() for v in banned) and ('stump' in p['name'].lower())==dead]
            if not selected:
                assert dead and stem=='tree_cine_02_low',(stem,dead)
                x,y=both[0][:2];name=f'{i:04}-stump.npz';np.savez_compressed(out/name,rgba=np.zeros((1,1,4),'u1'),depth=np.full((1,1),65535,dtype='<u2'));records.append(dict(id=i,hammer_id=t['hammer_id'],token=0x8000|i,dead=True,x=x,y=y,width=1,height=1,path=name,sha256=sha(out/name),original_stump_mesh_absent=True));continue
            x,y,xx,yy=projected_bounds(selected,matrix);renderer.offset=(x,y);renderer.raster=raster.Raster(xx-x,yy-y,ss=2);renderer.model(g,matrix,parts=selected,tint=np.asarray(e['rendercolor'])/255.)
            rgba,depth=renderer.raster.finish();mask=rgba[:,:,3]>0;assert mask.any();ys,xs=np.where(mask);left,right,top,bottom=int(xs.min()),int(xs.max())+1,int(ys.min()),int(ys.max())+1
            rgba=rgba[top:bottom,left:right].copy();depth=np.where(rgba[:,:,3]>0,np.clip(np.rint(depth[top:bottom,left:right]),0,65534),65535).astype('<u2');x+=left;y+=top
            name=f'{i:04}-'+('stump' if dead else 'alive')+'.npz';np.savez_compressed(out/name,rgba=rgba,depth=depth)
            rec=dict(id=i,hammer_id=t['hammer_id'],token=(0x8000 if dead else 0)|i,dead=dead,x=x,y=y,width=rgba.shape[1],height=rgba.shape[0],path=name,sha256=sha(out/name));records.append(rec);both.append([x,y,x+rgba.shape[1],y+rgba.shape[0]])
        t['bbox']=[min(b[0] for b in both),min(b[1] for b in both),max(b[2] for b in both),max(b[3] for b in both)]
        if i%100==0:print(json.dumps(dict(tree_frames=i+1,total=2113,seconds=time.monotonic()-start)),flush=True)
    # Always-alive outside-world natural trees and independently placed leaf
    # aggregates. Resolve their mutual occlusion once, without any cut owner.
    renderer.offset=(0,0);renderer.raster=raster.Raster(3840,2816,ss=2)
    constant_trees=[e for e in trees if e['hammerUniqueId'] not in owners and 'tree_cine' not in e['model']]
    for e in constant_trees:
        entry,stem,g,parts=model(e);renderer.replacements=groups[stem][str(e['skin'])];selected=[p for p in parts if not any(v in p['name'].lower() for v in (*banned,'stump'))]
        renderer.model(g,np.asarray(e['matrix_column_major']).reshape(4,4).T,parts=selected,tint=np.asarray(e['rendercolor'])/255.)
    renderer.replacements={};scene=Gltf(source/'private/source2/exports/map-geometry/dota.gltf');const_placements=0
    for asset in cat['assets']:
        if 'tree' not in asset['name']:continue
        g=Gltf(source/asset['gltf']);parts=list(g.primitives())
        for placement in asset['placements']:
            entity=placement.get('entity',{})
            if entity and entity['hammerUniqueId'] in owners:continue
            selected=None
            if asset['kind']=='aggregate':
                node=placement['node'];p=scene.doc['meshes'][scene.doc['nodes'][node]['mesh']]['primitives'][0];faces=scene.accessor(p['indices']).reshape(-1,3).astype(np.int32);selected=[dict(parts[placement['draw']],faces=faces)]
            renderer.model(g,np.asarray(placement['matrix_column_major']).reshape(4,4).T,parts=selected,visible=lambda p:not any(v in p['name'].lower() for v in (*banned,'stump')),tint=placement['tint']);const_placements+=1
    rgba,depth=renderer.raster.finish();depth=np.where(rgba[:,:,3]>0,np.clip(np.rint(depth),0,65534),65535).astype('<u2');np.savez_compressed(out/'constant.npz',rgba=rgba,depth=depth)
    Image.fromarray(rgba).resize((960,704)).save(out/'constant-preview.png');write(out/'materials.json',dict(receipts=materials.receipts,helper_sha256=sha(source/'tools/source2_materials.py')))
    result=dict(complete=True,ss=2,ids=ids,frames=records,constant=dict(path='constant.npz',sha256=sha(out/'constant.npz'),natural_tree_count=len(trees)-len(ids),aggregate_and_cine_placements=const_placements),source_models=renderer.sources,source_trees_sha256=sha(source/'private/source2-v022/active-entities/trees.json'),id_table_sha256=sha(ROOT/'provenance/vision/trees.json'),association_audit_sha256=sha(out/'association-audit.json'),original_stump_mesh_absent_ids=[r['id'] for r in records if r.get('original_stump_mesh_absent')],generator_sha256=sha(__file__),elapsed_seconds=time.monotonic()-start)
    write(out/'manifest.json',result);print(json.dumps(dict(frames_complete=True,seconds=result['elapsed_seconds'])),flush=True)

def pack_stage(source,frames,out,background,tolerance,sparse_base=True):
    if out.exists():raise ValueError('Pack output exists; choose a fresh path')
    out.mkdir(parents=True);bm,raster,sm,Gltf,np,Image=context(source,out);fm=read(frames/'manifest.json');assert fm['complete'] and len(fm['ids'])==2113
    for rec in fm['frames']:assert sha(frames/rec['path'])==rec['sha256']
    old=source/'private/baked/v022/map-lit';orig=np.asarray(Image.open(old/'map.png').convert('RGB'));od=np.load(old/'map-depth.npy');bg=np.asarray(Image.open(background/'map.png').convert('RGB'));sd=np.load(background/'map-depth.npy');assert od.shape==sd.shape==(2816,3840)
    const=np.load(frames/'constant.npz');cr=const['rgba'];cd=const['depth'];near=np.full(od.shape,65535,dtype='<u2');top=np.full(od.shape,65535,dtype='<u2');loaded=[]
    def overlap(rec):
        x,y=rec['x'],rec['y'];xx,yy=x+rec['width'],y+rec['height'];a,b=max(0,x),max(0,y);c,d=min(3840,xx),min(2816,yy)
        return (a,b,c,d,slice(b-y,d-y),slice(a-x,c-x)) if a<c and b<d else None
    for rec in fm['frames']:
        arr=np.load(frames/rec['path']);rgba,depth=arr['rgba'],arr['depth'];loaded.append((rec,rgba,depth))
        if rec['dead']:continue
        q=overlap(rec)
        if q:
            a,b,c,d,ys,xs=q;z=depth[ys,xs];m=(rgba[ys,xs,3]>0)&(z<near[b:d,a:c]);near[b:d,a:c][m]=z[m];top[b:d,a:c][m]=rec['id']
    natural=(near<65535)&(np.abs(near.astype('i4')-od.astype('i4'))<=tolerance)&(near.astype('i4')+1<sd.astype('i4'))&((cr[:,:,3]==0)|(near.astype('i4')+1<cd.astype('i4')))
    def rgb565(rgb):
        v=rgb.astype(np.uint16);return ((v[:,:,0]>>3)<<11)|((v[:,:,1]>>2)<<5)|(v[:,:,2]>>3)
    base_rgb=np.where(natural,rgb565(bg),rgb565(orig)).astype('<u2');base_depth=np.where(natural,sd,od).astype('<u2');Image.fromarray(natural.astype('u1')*255).save(out/'original-top-natural.png')
    # Index frame crops by tile; every potential live/dead source is retained.
    bins=[[] for _ in range(660)]
    for item in loaded:
        rec=item[0];q=overlap(rec)
        if q:
            a,b,c,d,_,_=q
            for ty in range(b//128,(d-1)//128+1):
                for tx in range(a//128,(c-1)//128+1):bins[ty*30+tx].append(item)
    csource=out/'codec.c';csource.write_text('#include "cache_rle.h"\nunsigned tree_size(const unsigned char*s,unsigned n){return cache_rle_encoded_size(s,n);}\nunsigned tree_encode(const unsigned char*s,unsigned n,unsigned char*d,unsigned c){return cache_rle_encode(s,n,d,c);}\nunsigned tree_decode(const unsigned char*s,unsigned n,unsigned char*d,unsigned e){return cache_rle_decode(s,n,d,e);}\n',encoding='utf8')
    cc=Path('C:/msys64/ucrt64/bin/gcc.exe');dllpath=out/'codec.dll';env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','');subprocess.run([str(cc),'-O3','-shared','-I',str(ROOT/'platform'),str(csource),'-o',str(dllpath)],check=True,env=env,capture_output=True)
    dll=ctypes.CDLL(str(dllpath));ptr=ctypes.c_void_p
    dll.tree_size.argtypes=[ptr,ctypes.c_uint];dll.tree_encode.argtypes=dll.tree_decode.argtypes=[ptr,ctypes.c_uint,ptr,ctypes.c_uint]
    def transform(data,layers,inverse=False):
        result=bytearray(data);offsets=[16,16+N*2]+[16+N*4+i*N*7+j*N*2 for i in range(layers) for j in range(3)]
        for off in offsets:
            if inverse:
                lo=np.frombuffer(data,dtype='u1',count=N,offset=off).astype('u2');hi=np.frombuffer(data,dtype='u1',count=N,offset=off+N).astype('u2');zz=lo|(hi<<8);delta=((zz>>1)^(0-(zz&1)))&65535;v=(np.cumsum(delta,dtype='u8')&65535).astype('<u2');result[off:off+2*N]=v.tobytes()
            else:
                v=np.frombuffer(data,dtype='<u2',count=N,offset=off).astype('u4');delta=(v-np.r_[0,v[:-1]])&65535;zz=((delta<<1)^(0-(delta>>15)))&65535;result[off:off+N]=(zz&255).astype('u1').tobytes();result[off+N:off+2*N]=(zz>>8).astype('u1').tobytes()
        return bytes(result)
    target=out/'TREE.H1T';stream=target.open('w+b');stream.write(bytes(41324));directory=[];maxlayers=maxdecoded=maxstored=0;coverage=0;previews={};start=time.monotonic()
    for tile,items in enumerate(bins):
        tx,ty=tile%30,tile//30;x,y=tx*128,ty*128;rows=[]
        for rec,rgba,depth in items:
            a,b=max(x,rec['x']),max(y,rec['y']);c,d=min(x+128,rec['x']+rec['width']),min(y+128,rec['y']+rec['height']);ys,xs=slice(b-rec['y'],d-rec['y']),slice(a-rec['x'],c-rec['x']);dst=(slice(b-y,d-y),slice(a-x,c-x));alpha=np.zeros((128,128),'u1');z=np.full((128,128),65535,dtype='<u2');colour=np.zeros((128,128),dtype='<u2');z[dst]=depth[ys,xs];alpha[dst]=rgba[ys,xs,3];colour[dst]=rgb565(rgba[ys,xs,:3]);valid=(alpha>0)&(z.astype('i4')+1<base_depth[y:y+128,x:x+128].astype('i4'))&(z.astype('i4')+1<sd[y:y+128,x:x+128].astype('i4'));alpha[~valid]=0;z[~valid]=65535
            if valid.any():rows.append((rec['token'],colour,z,alpha))
        if not rows:directory.append([0]*6);continue
        ca=cr[y:y+128,x:x+128,3].copy();cz=cd[y:y+128,x:x+128].copy();valid=(ca>0)&(cz.astype('i4')+1<base_depth[y:y+128,x:x+128].astype('i4'))&(cz.astype('i4')+1<sd[y:y+128,x:x+128].astype('i4'));ca[~valid]=0;cz[~valid]=65535
        if valid.any():rows.append((0x7fff,rgb565(cr[y:y+128,x:x+128,:3]),cz,ca))
        zs=np.stack([r[2].ravel() for r in rows]);aa=np.stack([r[3].ravel() for r in rows]);order=np.argsort(zs,axis=0,kind='stable');counts=(aa>0).sum(0);layers=int(counts.max());cov=int((counts>0).sum());maxlayers=max(maxlayers,layers);coverage+=cov
        owners=np.broadcast_to(np.array([r[0] for r in rows],dtype='<u2')[:,None],zs.shape);colours=np.stack([r[1].ravel() for r in rows]);tile_rgb=base_rgb[y:y+128,x:x+128].copy();tile_depth=base_depth[y:y+128,x:x+128].copy()
        if sparse_base:
            # No layer can ever pick/patch these pixels, including any future
            # combination of cuts. Keep every layer and every active base byte.
            unused=(counts==0).reshape(128,128);tile_rgb[unused]=0;tile_depth[unused]=0
        body=bytearray(struct.pack('<4I',0x31545048,layers,N,0)+tile_rgb.tobytes()+tile_depth.tobytes())
        for i in range(layers):
            ix=order[i:i+1];a=np.take_along_axis(aa,ix,0)[0];z=np.take_along_axis(zs,ix,0)[0];own=np.take_along_axis(owners,ix,0)[0].copy();colour=np.take_along_axis(colours,ix,0)[0].copy();own[a==0]=65535;colour[a==0]=0;body+=own.astype('<u2').tobytes()+colour.astype('<u2').tobytes()+z.astype('<u2').tobytes()+a.tobytes()
        canonical=bytes(body);encoded_input=transform(canonical,layers);src=ctypes.create_string_buffer(encoded_input);n=dll.tree_size(src,len(encoded_input));buf=ctypes.create_string_buffer(n);assert dll.tree_encode(src,len(encoded_input),buf,n)==n;compressed=buf.raw
        if len(compressed)<len(canonical):stored,codec=compressed,1;decoded=ctypes.create_string_buffer(len(canonical));assert dll.tree_decode(buf,n,decoded,len(canonical))==len(canonical);assert transform(decoded.raw,layers,True)==canonical
        else:stored,codec=canonical,0
        offset=stream.tell();stream.write(stored);stream.write(bytes((-len(stored))&3));directory.append([offset,len(stored),len(canonical),zlib.crc32(canonical)&0xffffffff,codec,cov]);maxdecoded=max(maxdecoded,len(canonical));maxstored=max(maxstored,len(stored))
        if tile%60==0:print(json.dumps(dict(tile=tile,max_layers=maxlayers,bytes=stream.tell(),seconds=time.monotonic()-start)),flush=True)
    filebytes=stream.tell();xy=b''.join(struct.pack('<2H',(t['x_q8']+128)>>8,(t['y_q8']+128)>>8) for t in fm['ids']);boxes=b''.join(struct.pack('<4h',*t['bbox']) for t in fm['ids']);tables=xy+boxes+b''.join(struct.pack('<6I',*r) for r in directory);assert len(tables)==41324-128
    header=[0x31525448,1,128,filebytes,3840,2816,128,30,22,(-512)&0xffffffff,(-512)&0xffffffff,2113,128,8580,25484,41324,24,maxdecoded,maxstored,zlib.crc32(tables)&0xffffffff,1,maxlayers,*struct.unpack('<8I',bytes.fromhex(SCENE_SHA)),0,0];assert len(header)==32;header[31]=zlib.crc32(struct.pack('<32I',*header))&0xffffffff;stream.seek(0);stream.write(struct.pack('<32I',*header)+tables);stream.close()
    report=dict(complete=True,tree_count=2113,tile_count=660,nonempty_tiles=sum(bool(d[0]) for d in directory),max_layers=maxlayers,max_decoded_bytes=maxdecoded,max_stored_bytes=maxstored,file_bytes=filebytes,sha256=sha(target),scene_sha256=SCENE_SHA,header_words=header,frame_manifest_sha256=sha(frames/'manifest.json'),background_manifest_sha256=sha(background/'manifest.json'),original_map_sha256=sha(old/'map.png'),original_depth_sha256=sha(old/'map-depth.npy'),top_natural_pixels=int(natural.sum()),coverage_pixels=coverage,depth_match_tolerance=tolerance,sparse_base=sparse_base,sparse_base_rule='Only pixels where every canonical layer alpha is zero have both base planes zeroed. All layer bytes and active-pixel base values retain their original bytes.' if sparse_base else 'Original full base planes retained.',bbox_semantics='half-open map-local minx,miny,maxx,maxy, union alive+stump',codec='Per-u16-plane delta/zigzag/lo-hi transpose plus actual cache_rle.h; header/alpha unchanged. Codec0 is canonical raw.',every_compressed_tile_roundtrip_verified=True,generator_sha256=sha(__file__),elapsed_seconds=time.monotonic()-start)
    write(out/'manifest.json',report);print(json.dumps(dict(pack_complete=True,bytes=filebytes,max_layers=maxlayers,max_decoded=maxdecoded,max_stored=maxstored,sha256=report['sha256'])),flush=True)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',type=Path,default=ROOT.parent/'h1-dota2');p.add_argument('--frames',type=Path,default=ROOT/'build/v029/tree-bake/frames');p.add_argument('--output',type=Path,default=ROOT/'build/v029/tree-bake/pack');p.add_argument('--background',type=Path,default=ROOT/'build/v029/tree-bake/no-trees');p.add_argument('--stage',choices=['all','frames','pack'],default='all');p.add_argument('--depth-tolerance',type=int,default=1);p.add_argument('--full-base',action='store_true',help='Retain unused base pixels for reproducing the original large pack');a=p.parse_args();source=a.source_root.resolve();frames=a.frames.resolve();out=a.output.resolve()
    for x in [frames,out]:assert x.is_relative_to(ROOT/'build') and not x.is_relative_to(source)
    if a.stage in ('all','frames'):frame_stage(source,frames)
    if a.stage in ('all','pack'):pack_stage(source,frames,out,a.background.resolve(),a.depth_tolerance,not a.full_base)

if __name__=='__main__':main()
