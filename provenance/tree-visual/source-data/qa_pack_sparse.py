from pathlib import Path
import hashlib,json,struct,sys,zlib
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT.parent/'.tools/dota3d-deps'))
import numpy as np
from PIL import Image,ImageDraw
P=ROOT/'build/v029/tree-bake/pack-sparse'
F=ROOT/'build/v029/tree-bake/frames'
O=ROOT.parent/'h1-dota2/private/baked/v022/map-lit'
N=16384
raw=(P/'TREE.H1T').read_bytes();h=list(struct.unpack_from('<32I',raw));saved=h[31];h[31]=0
assert zlib.crc32(struct.pack('<32I',*h))&0xffffffff==saved
assert h[:3]==[0x31525448,1,128] and h[3]==len(raw) and h[11:17]==[2113,128,8580,25484,41324,24]
assert zlib.crc32(raw[128:41324])&0xffffffff==h[19]
fm=json.loads((F/'manifest.json').read_text(encoding='utf8'))
assert raw[128:8580]==b''.join(struct.pack('<2H',(t['x_q8']+128)>>8,(t['y_q8']+128)>>8) for t in fm['ids'])
assert raw[8580:25484]==b''.join(struct.pack('<4h',*t['bbox']) for t in fm['ids'])
def rle(b):
    out=bytearray();i=0
    while i<len(b):
        c=b[i];i+=1;n=(c&127)+1
        if c&128:out+=bytes(n)
        else:out+=b[i:i+n];i+=n
    assert i==len(b);return bytes(out)
def inverse(b,L):
    o=bytearray(b)
    for off in [16,16+N*2]+[16+N*4+i*N*7+j*N*2 for i in range(L) for j in range(3)]:
        zz=np.frombuffer(b,'u1',N,off).astype('u2')|(np.frombuffer(b,'u1',N,off+N).astype('u2')<<8);delta=((zz>>1)^(0-(zz&1)))&65535;o[off:off+N*2]=(np.cumsum(delta,dtype='u8')&65535).astype('<u2').tobytes()
    return bytes(o)
cache={};constant_pixels=0;maxL=0;chosen=None;best_overlap=0;constant_case=None
for tile in range(660):
    off,stored,decoded,crc,codec,cov=struct.unpack_from('<6I',raw,25484+tile*24)
    if not off:assert [stored,decoded,crc,codec,cov]==[0]*5;continue
    b=raw[off:off+stored];b=rle(b) if codec else b;magic,L,pixels,reserved=struct.unpack_from('<4I',b);assert [magic,pixels,reserved]==[0x31545048,N,0]
    if codec:b=inverse(b,L)
    assert len(b)==decoded==16+N*4+L*N*7 and zlib.crc32(b)&0xffffffff==crc
    base=np.frombuffer(b,'<u2',N,16).reshape(128,128);bd=np.frombuffer(b,'<u2',N,16+N*2).reshape(128,128);owners=[];cols=[];depth=[];alpha=[]
    for j in range(L):
        at=16+N*4+j*N*7;owners.append(np.frombuffer(b,'<u2',N,at));cols.append(np.frombuffer(b,'<u2',N,at+N*2));depth.append(np.frombuffer(b,'<u2',N,at+N*4));alpha.append(np.frombuffer(b,'u1',N,at+N*6))
    owners=np.stack(owners);cols=np.stack(cols);depth=np.stack(depth);alpha=np.stack(alpha)
    assert np.all((alpha==0)==(owners==65535));assert np.all((alpha==0)|(owners==32767)|((owners&32767)<2113));assert np.all(depth[:-1]<=depth[1:]);assert int((alpha>0).any(0).sum())==cov
    constant_pixels+=int(((owners==32767)&(alpha>0)).sum());cache[tile]=(base,bd,owners,cols,depth,alpha)
    alive=(owners<2113)&(alpha>=250);eligible=((owners<2113)|(owners==32767))&(alpha>0)
    first=np.argmax(eligible,axis=0);firstown=np.take_along_axis(owners,first[None,:],0)[0];firstalpha=np.take_along_axis(alpha,first[None,:],0)[0]
    for j in range(1,L):
        m=(firstown<2113)&(firstalpha>=250)&alive[j]&(np.arange(L)[:,None]<j).any(0)&(owners[j]!=firstown)
        # Require the second original visible source to be another tree.
        between=np.zeros(N,bool)
        for k in range(j):between|=(k>first)&eligible[k]
        m&=~between
        if m.any():
            indices=np.flatnonzero(m);pair=firstown[indices].astype('u4')*4096+owners[j,indices].astype('u4');keys,counts=np.unique(pair,return_counts=True);at=int(counts.argmax());score=int(counts[at])
            if score>best_overlap:
                pts=indices[pair==keys[at]];idx=int(pts[len(pts)//2]);chosen=(tile,idx,int(firstown[idx]),int(owners[j,idx]));best_overlap=score
    if constant_case is None:
        for j in range(1,L):
            m=(firstown<2113)&(firstalpha>=250)&(owners[j]==32767)&(alpha[j]>=200)
            between=np.zeros(N,bool)
            for k in range(j):between|=(k>first)&eligible[k]
            m&=~between
            if m.any():idx=int(np.flatnonzero(m)[0]);constant_case=(tile,idx,int(firstown[idx]));break
    maxL=max(maxL,L)
assert maxL==h[21] and chosen and constant_pixels>0
v=np.asarray(Image.open(O/'map.png').convert('RGB')).astype('u2');original=((v[:,:,0]>>3)<<11)|((v[:,:,1]>>2)<<5)|(v[:,:,2]>>3);original_depth=np.load(O/'map-depth.npy')
def compose(tile,dead):
    x,y=(tile%30)*128,(tile//30)*128;orig=original[y:y+128,x:x+128];od=original_depth[y:y+128,x:x+128]
    if tile not in cache:return orig.copy(),od.copy(),np.full((128,128),65535,'u2'),0
    base,bd,owners,cols,dep,a=cache[tile];deadtable=np.zeros(2113,bool);deadtable[list(dead)]=1
    has=(a>0);isconst=owners==32767;istree=(owners&32767)<2113;deadflag=np.zeros_like(has);deadflag[istree]=deadtable[(owners&32767)[istree]]
    enabled=has&(isconst|(istree&np.where((owners&32768)!=0,deadflag,~deadflag)))
    original_layers=has&(isconst|((owners<2113)));first=np.argmax(original_layers,axis=0);firstown=np.take_along_axis(owners,first[None,:],0)[0];firsthas=original_layers.any(0);cutfirst=np.zeros(N,bool);m=firsthas&(firstown<2113);cutfirst[m]=deadtable[firstown[m]]
    newstump=(enabled&((owners&32768)!=0)&(owners!=65535)&(dep<od.ravel()[None,:])).any(0);change=cutfirst|newstump
    rgb=base.ravel().astype('u4').copy();z=bd.ravel().copy();visible=np.full(N,65535,'u2')
    for j in range(len(owners)-1,-1,-1):
        valid=enabled[j];s=cols[j].astype('u4');aa=a[j].astype('u4');out=np.zeros(N,'u4')
        for shift,mask in [(11,31),(5,63),(0,31)]:out|=(((((s>>shift)&mask)*aa+((rgb>>shift)&mask)*(255-aa)+127)//255)<<shift)
        rgb[valid]=out[valid];z[valid]=dep[j][valid];visible[valid]=owners[j][valid]
    result=orig.ravel().copy();rd=od.ravel().copy();result[change]=rgb[change];rd[change]=z[change];assert np.array_equal(result[~change],orig.ravel()[~change])
    protected=~has.any(0);assert np.array_equal(result[protected],orig.ravel()[protected]) and np.array_equal(rd[protected],od.ravel()[protected])
    return result.reshape(128,128),rd.reshape(128,128),visible.reshape(128,128),int(protected.sum())
def rgb8(v):
    r=(v>>11)&31;g=(v>>5)&63;b=v&31;return np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype('u1')
tile,idx,A,B=chosen;tests=[];states=[set(),{A},{A,B},set()];panels=[];protected=0
for state in states:
    color,depth,owner,p=compose(tile,state);panels.append(color);protected+=p
    if not state:assert np.array_equal(color,original[(tile//30)*128:(tile//30+1)*128,(tile%30)*128:(tile%30+1)*128])
    if len(state)==1:assert owner.ravel()[idx]==B
    if len(state)==2:assert owner.ravel()[idx] not in (A,B)
    tests.append({'dead_ids':sorted(state),'sample_visible_token':int(owner.ravel()[idx]),'rgb_sha256':hashlib.sha256(color.tobytes()).hexdigest(),'depth_sha256':hashlib.sha256(depth.tobytes()).hexdigest()})
assert np.array_equal(panels[0],panels[-1]);canvas=Image.new('RGB',(128*4,148),(24,30,25));draw=ImageDraw.Draw(canvas)
for i,(c,label) in enumerate(zip(panels,['Original','Cut A','Cut A+B','Regrown'])):canvas.paste(Image.fromarray(rgb8(c)),(i*128,20));draw.text((i*128+3,3),label,fill='white')
canvas.save(P/'overlap-cut-regrow-preview.png')
constant_check=None
if constant_case:
    tile2,index,C=constant_case;color,depth,owner,p=compose(tile2,{C});assert owner.ravel()[index]==32767;constant_check={'tile':tile2,'pixel':index,'cut_id':C,'visible_token':32767}
report={'passed':True,'actual_pack_readback':True,'physical_h1_tested':False,'sha256':hashlib.sha256(raw).hexdigest(),'qa_source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'all_header_tables_crc_verified':True,'all_nonempty_tiles_inverse_crc_verified':len(cache),'max_layers':maxL,'constant_layer_pixels':constant_pixels,'overlap_case':{'tile':tile,'pixel':idx,'opaque_overlap_pixels':best_overlap,'tree_a':A,'tree_b':B,'hammer_a':fm['ids'][A]['hammer_id'],'hammer_b':fm['ids'][B]['hammer_id'],'states':tests},'constant_after_foreground_tree_cut':constant_check,'source_static_pixels_preserved_in_four_states':protected,'regrow_identical_to_original':True,'empty_original_stump_ids':fm['original_stump_mesh_absent_ids'],'preview_sha256':hashlib.sha256((P/'overlap-cut-regrow-preview.png').read_bytes()).hexdigest(),'scope':'Independent Python RLE/inverse/CRC parser and host composition per runtime contract. Source-static foreground and unmodified original closest-alive pixels retain original bytes. This is not the H1 provider or physical runtime validation.'}
(P/'qa-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(report))
