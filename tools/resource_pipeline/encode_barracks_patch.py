"""Verify/install a sparse same-source barracks ground/selection patch in C."""
from pathlib import Path
import hashlib,json,struct,sys
from PIL import Image,ImageDraw
from gltf_assets import ROOT,Gltf,np
from prepare_barracks_patch import OUT,Crop,Renderer

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def rgb565(rgb):
    rgb=rgb.astype(np.uint16);return ((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)
def rle(data):
    out=bytearray();i=0
    while i<len(data):
        end=i
        while end<len(data) and not data[end] and end-i<128:end+=1
        if end-i>=2:out.append(0x80|(end-i-1));i=end;continue
        end=i+1
        while end<len(data) and end-i<128:
            if data[end]==0 and end+1<len(data) and data[end+1]==0:break
            end+=1
        out.append(end-i-1);out+=data[i:end];i=end
    return bytes(out)
def unrle(data):
    out=bytearray();i=0
    while i<len(data):
        c=data[i];i+=1;n=(c&127)+1
        if c&128:out+=bytes(n)
        else:assert i+n<=len(data);out+=data[i:i+n];i+=n
    return bytes(out)
def runs(mask):
    for y,row in enumerate(mask):
        edges=np.diff(np.r_[False,row,False].astype(np.int8));starts=np.flatnonzero(edges==1);ends=np.flatnonzero(edges==-1)
        for x,end in zip(starts,ends):yield int(y),int(x),int(end-x)
def c_array(name,data,ctype='unsigned char',columns=20):
    out=[f'static const {ctype} {name}[{len(data)}]={{']
    for i in range(0,len(data),columns):out.append('    '+','.join(map(str,data[i:i+columns]))+',')
    return out+['};']

def main():
    layout=read(OUT/'layout.json');records=layout['records'];rects=layout['pairs']
    pack=ROOT/'build/release-v023/DOTA2/SCENE.H1P';assert sha(pack)=='c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd'
    base_rgb=np.asarray(Image.open(ROOT/'private/baked/v022/map-lit/map.png').convert('RGB'));base_z=np.load(ROOT/'private/baked/v022/map-lit/map-depth.npy')
    bytes_blob=bytearray();pairs=[];selection=[];selection_words=[];bar_floor=[];checks=0;total_changed=0;total_pixels=0
    before_files={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'private/baked/v022/map-lit/map.png',ROOT/'private/baked/v022/map-lit/map-depth.npy',pack)}
    # Confirm the claimed source panorama really is the installed tile pixels.
    with pack.open('rb') as f:
        h=struct.unpack('<16I',f.read(64));assert h[1]==12 and h[4:6]==(30,22);f.seek(h[8]);offsets=struct.unpack('<660I',f.read(2640))
        for rect in rects:
            for ty in range(rect[1]//128,(rect[3]-1)//128+1):
                for tx in range(rect[0]//128,(rect[2]-1)//128+1):
                    f.seek(offsets[ty*30+tx]);tile=np.frombuffer(f.read(65536),'<u2').reshape(2,128,128)
                    assert np.array_equal(tile[0],rgb565(base_rgb[ty*128:(ty+1)*128,tx*128:(tx+1)*128]))
                    assert np.array_equal(tile[1],base_z[ty*128:(ty+1)*128,tx*128:(tx+1)*128]);checks+=2
    all_masks=[]
    for pair,rect in enumerate(rects):
        left,top,right,bottom=rect;width=right-left;height=bottom-top
        values=[np.load(OUT/f'pair-{pair}-state-{state}.npz') for state in range(4)]
        colors=[rgb565(v['rgb']) for v in values];depths=[v['depth'] for v in values]
        original=colors[3];original_z=depths[3];source=rgb565(base_rgb[top:bottom,left:right]);source_z=base_z[top:bottom,left:right]
        changed=[(colors[s]!=original)|(depths[s]!=original_z) for s in range(3)];union=changed[0]|changed[1]|changed[2]
        assert np.all((original==source)[union]) and np.all((original_z==source_z)[union]),'Source mismatch in affected pixels'
        fullmask=np.zeros(base_z.shape,bool);fullmask[top:bottom,left:right]=union
        assert all(not np.any(fullmask&m) for m in all_masks),'Cross-pair changed pixels overlap'
        all_masks.append(fullmask);total_changed+=int(union.sum())
        group_masks={c:np.zeros(union.shape,bool) for c in range(1,8)};group_colors={};group_depths={};handled=[np.zeros(union.shape,bool) for _ in range(3)]
        for state in range(3):
            todo=changed[state]&~handled[state]
            bits=np.zeros(union.shape,np.uint8)
            for other in range(3):bits|=np.where(changed[other]&(colors[other]==colors[state])&(depths[other]==depths[state]),1<<other,0).astype(np.uint8)
            for cond in range(1,8):
                selected=todo&(bits==cond)
                if not selected.any():continue
                group_masks[cond]|=selected
                if cond not in group_colors:group_colors[cond]=np.zeros_like(original);group_depths[cond]=np.zeros_like(original_z)
                group_colors[cond][selected]=colors[state][selected];group_depths[cond][selected]=depths[state][selected]
                for other in range(3):
                    if cond&(1<<other):handled[other]|=selected
        assert all(np.array_equal(handled[s],changed[s]) for s in range(3))
        raw=bytearray();run_count=0;pixels=0
        for cond,mask in group_masks.items():
            for y,x,count in runs(mask):
                raw+=struct.pack('<3H',y*width+x,count,cond);previous=0
                raw+=group_colors[cond][y,x:x+count].astype('<u2').tobytes()
                for z in group_depths[cond][y,x:x+count]:
                    raw+=struct.pack('<H',(int(z)-previous)&65535);previous=int(z)
                run_count+=1;pixels+=count
        encoded=rle(raw);assert unrle(encoded)==raw
        offset=len(bytes_blob);bytes_blob+=encoded;pairs.append((left,top,width,height,offset,len(encoded),len(raw),run_count,pixels));total_pixels+=pixels
        # Independent replay all states against true SCENE input.
        for state in range(4):
            c=source.copy();z=source_z.copy();at=0
            for _ in range(run_count):
                start,count,condition=struct.unpack_from('<3H',raw,at);at+=6;previous=0
                run_colors=struct.unpack_from('<'+'H'*count,raw,at);at+=2*count
                for k in range(count):
                    delta=struct.unpack_from('<H',raw,at)[0];at+=2;previous=(previous+delta)&65535
                    if condition&(1<<state):yy,xx=divmod(start+k,width);c[yy,xx]=run_colors[k];z[yy,xx]=previous
            assert at==len(raw)
            expected_c=source.copy();expected_z=source_z.copy()
            if state!=3:expected_c[changed[state]]=colors[state][changed[state]];expected_z[changed[state]]=depths[state][changed[state]]
            assert np.array_equal(c,expected_c) and np.array_equal(z,expected_z);checks+=2
            np.savez_compressed(OUT/f'pair-{pair}-expected-{state}.npz',color=c,depth=z)
        # Source mesh coverage; no guessed rectangle or selectable shadow.
        for member in range(2):
            rec=records[pair*2+member];only=Crop(pair,rect);renderer=Renderer([only]);g=Gltf(Path(rec['gltf']));matrix=np.asarray(rec['matrix']).reshape(4,4).T
            points=np.concatenate([p['points'][np.unique(p['faces'])] for p in g.primitives()]);world=(points@matrix[:3,:3].T+matrix[:3,3])@np.array([[0,0,1],[-1,0,0],[0,1,0]],float).T*(4096/(16384*.0254))+[2048,2048,0]
            bar_floor.append(int(np.floor((world[:,1]-world[:,2]).min())))
            renderer.model(g,matrix)
            rgba,mesh_depth=only.raster.finish();mesh_z=np.rint(np.clip(mesh_depth,0,65535)).astype('<u2')
            for state in ((1<<member),3):
                mask=(rgba[:,:,3]>=128)&(depths[state]==mesh_z)
                assert mask.any(),(pair,member,state)
                # A source visible mask also matches installed pixels outside
                # the changed set; this keeps supplemental foreground props.
                if state==3:assert np.all((source_z==original_z)[mask])
                offset=len(selection_words);count=0
                for y,x,n in runs(mask):selection_words.extend((y*width+x,n));count+=1
                selection.append((offset,count));checks+=1
                Image.fromarray(mask.astype(np.uint8)*255).save(OUT/f'pair-{pair}-select-{member}-{state}.png')
        preview=Image.new('RGB',(width*4,height+18),(20,20,20));draw=ImageDraw.Draw(preview)
        for col,state in enumerate((3,1,2,0)):
            draw.text((col*width+3,2),'alive '+str(state),fill='white');preview.paste(Image.open(OUT/f'pair-{pair}-state-{state}.png'),(col*width,18))
        preview.save(OUT/f'pair-{pair}-contact.png')
    header=['#ifndef H1_DOTA_BARRACKS_PATCH_H','#define H1_DOTA_BARRACKS_PATCH_H',
        '/* Same-source HBK12 compiled sparse ground restoration. 4 lane-pair states; alive3 is a strict no-op. */',
        '#define DOTA_BARRACKS_PATCH_SOURCE_BYTES 406028212u',
        'typedef struct {short x,y,w,h;unsigned offset,bytes,raw_bytes,runs,pixels;} DotaBarracksPatch;',
        'typedef struct {unsigned offset,runs;} DotaBarracksSelection;',
        'static const DotaBarracksPatch dota_barracks_patches[6]={']
    header+=['    {'+','.join(map(str,p))+'},' for p in pairs];header+=['};','static const DotaBarracksSelection dota_barracks_selections[24]={']
    header+=['    {'+','.join(map(str,p))+'},' for p in selection];header+=['};']
    header+=c_array('dota_barracks_patch_bytes',bytes_blob);header+=c_array('dota_barracks_selection_runs',selection_words,'unsigned short',16)
    header+=c_array('dota_barracks_bar_floor',bar_floor,'short',12);header+=['#endif','']
    generated=ROOT/'private/generated/dota_barracks_patch.h';generated.write_text('\n'.join(header),encoding='utf8')
    data_bytes=len(bytes_blob)+len(selection_words)*2+6*28+24*8+24
    assert data_bytes<200000,('Patch exceeds conservative private RAM budget',data_bytes)
    report={'passed':True,'checks':checks,'pair_states':24,'changed_pixels_union':total_changed,'stored_pixel_records':total_pixels,'encoded_bytes':len(bytes_blob),'selection_bytes':len(selection_words)*2,'compiled_data_bytes':data_bytes,'baseline_changed_pixel_mismatch':0,'source_sha256':before_files,'generated':{'path':str(generated.relative_to(ROOT)),'sha256':sha(generated)},'pair_descriptors':pairs,'tool_sha256':{str(p.relative_to(ROOT)):sha(p) for p in (Path(__file__),ROOT/'tools/prepare_barracks_patch.py')},'scope':'Exact same-source local raster restoration and four-state replay; source-visible selection masks. Not device-tested.'}
    (OUT/'encoding-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(report),flush=True)
if __name__=='__main__':main()
