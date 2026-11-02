"""Losslessly convert TREE v1 sorted planes to v2 tile-local implicit-owner frames.

The output is an uncompressed logical stream for the H1R outer container. Every
tile retains the original canonical CRC; reconstruction is verified here before
publication. No source meshes or released asset files are modified.
"""
from pathlib import Path
import argparse, ctypes, hashlib, json, os, shutil, struct, subprocess, time, zlib
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
N=16384
DATA=41324

def sha(data):return hashlib.sha256(data).hexdigest()
def unpack_planes(data,layers):
    basec=np.frombuffer(data,'<u2',N,16).reshape(128,128)
    basez=np.frombuffer(data,'<u2',N,16+N*2).reshape(128,128)
    result=[]
    for k in range(layers):
        start=16+N*4+k*N*7
        result.append(tuple(np.frombuffer(data,t,N,start+o).reshape(128,128) for t,o in [('<u2',0),('<u2',N*2),('<u2',N*4),('u1',N*6)]))
    return basec,basez,result

def convert(source,out,cc):
    started=time.monotonic();data=source.read_bytes();header=list(struct.unpack('<32I',data[:128]))
    assert header[0]==0x31525448 and header[1]==1 and header[3]==len(data) and header[15]==DATA
    check=bytearray(data[:128]);check[124:128]=bytes(4);assert zlib.crc32(check)&0xffffffff==header[31]
    assert zlib.crc32(data[128:DATA])&0xffffffff==header[19]
    out.mkdir(parents=True,exist_ok=False)
    c=out/'codec.c';c.write_text('#include "cache_rle.h"\nunsigned decode(const unsigned char*s,unsigned n,unsigned char*d,unsigned e){return cache_rle_decode(s,n,d,e);}\n')
    library=out/('codec.dll' if os.name=='nt' else 'codec.so');env=dict(os.environ);env['PATH']=str(Path(cc).parent)+os.pathsep+env.get('PATH','')
    subprocess.run([str(cc),'-O2','-shared',*([] if os.name=='nt' else ['-fPIC']),'-I',str(ROOT/'platform'),str(c),'-o',str(library)],env=env,check=True,capture_output=True)
    dll=ctypes.CDLL(str(library));dll.decode.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
    directory=[];spans=[dict(offset=0,length=DATA,type='raw',name='header-and-tables')];rows=[];maxstored=0;verified=0
    target=out/'TREE.H1T'
    with target.open('w+b') as stream:
        stream.write(bytes(DATA))
        for tile in range(660):
            at,n,raw,crc,codec,cov=struct.unpack_from('<6I',data,25484+tile*24)
            if not n:directory.append([0]*6);continue
            assert at>=DATA and at+n<=len(data) and raw>=16+N*4 and (raw-16-N*4)%(N*7)==0
            layers=(raw-16-N*4)//(N*7);canonical=data[at:at+n]
            if codec:
                assert codec==1;buf=ctypes.create_string_buffer(raw);src=ctypes.create_string_buffer(canonical);assert dll.decode(src,n,buf,raw)==raw
                transformed=buf.raw;body=bytearray(transformed)
                for off in [16,16+N*2]+[16+N*4+k*N*7+j*N*2 for k in range(layers) for j in range(3)]:
                    v=np.frombuffer(transformed,'u1',N*2,off);zz=v[:N].astype('u2')|(v[N:].astype('u2')<<8);delta=((zz>>1)^(0-(zz&1)))&65535;body[off:off+2*N]=(np.cumsum(delta,dtype='u8')&65535).astype('<u2').tobytes()
                canonical=bytes(body)
            assert len(canonical)==raw and zlib.crc32(canonical)&0xffffffff==crc
            bc,bz,ll=unpack_planes(canonical,layers)
            tokens=sorted({int(t) for own,c,z,a in ll for t in np.unique(own[a>0])},key=lambda t:0x7fffffff if t==32767 else (t&32767)*2+bool(t&32768))
            start=stream.tell();blob=bytearray(struct.pack('<2I',len(tokens),layers));parts=[]
            def append(payload,kind,name):
                spans.append(dict(offset=start+len(blob),length=len(payload),type=kind,tile=tile,name=name));blob.extend(payload)
            spans.append(dict(offset=start,length=8,type='raw',tile=tile,name='tile-header'))
            append(bc.tobytes(),'u16','base-color');append(bz.tobytes(),'u16','base-depth')
            for token in tokens:
                c=np.zeros((128,128),'<u2');z=np.full((128,128),65535,'<u2');a=np.zeros((128,128),'u1')
                for own,co,zo,ao in ll:
                    mask=(own==token)&(ao>0);c[mask]=co[mask];z[mask]=zo[mask];a[mask]=ao[mask]
                yy,xx=np.where(a>0);x,y,x1,y1=int(xx.min()),int(yy.min()),int(xx.max())+1,int(yy.max())+1;w,h=x1-x,y1-y
                append(struct.pack('<8H',token,x,y,w,h,0,0,0),'raw','frame-header')
                # Invalid pixels are canonical zeros; depth is ignored for alpha0.
                cz=np.where(a[y:y1,x:x1]>0,z[y:y1,x:x1],0).astype('<u2')
                append(c[y:y1,x:x1].tobytes(),'u16','frame-color');append(cz.tobytes(),'u16','frame-depth');append(a[y:y1,x:x1].tobytes(),'raw','frame-alpha');parts.append((token,c,z,a))
            zs=np.stack([r[2].ravel() for r in parts]);aa=np.stack([r[3].ravel() for r in parts]);ccs=np.stack([r[1].ravel() for r in parts]);owners=np.broadcast_to(np.array(tokens,'<u2')[:,None],zs.shape);order=np.argsort(zs,axis=0,kind='stable');rebuilt=bytearray(struct.pack('<4I',0x31545048,layers,N,0)+bc.tobytes()+bz.tobytes())
            for k in range(layers):
                ix=order[k:k+1];a=np.take_along_axis(aa,ix,0)[0];valid=a>0;own=np.where(valid,np.take_along_axis(owners,ix,0)[0],65535).astype('<u2');c=np.where(valid,np.take_along_axis(ccs,ix,0)[0],0).astype('<u2');z=np.where(valid,np.take_along_axis(zs,ix,0)[0],65535).astype('<u2');rebuilt+=own.tobytes()+c.tobytes()+z.tobytes()+a.tobytes()
            assert bytes(rebuilt)==canonical,(tile,'canonical mismatch');verified+=1
            stream.write(blob);directory.append([start,len(blob),raw,crc,2,cov]);maxstored=max(maxstored,len(blob));rows.append(dict(tile=tile,frames=len(tokens),stored_bytes=len(blob),canonical_bytes=raw,crc32=f'{crc:08x}'))
        header[1]=2;header[3]=stream.tell();header[18]=maxstored;tables=data[128:25484]+b''.join(struct.pack('<6I',*entry) for entry in directory);header[19]=zlib.crc32(tables)&0xffffffff;header[31]=0;header[31]=zlib.crc32(struct.pack('<32I',*header))&0xffffffff;stream.seek(0);stream.write(struct.pack('<32I',*header)+tables)
    end=0
    for span in spans:assert span['offset']==end;end+=span['length']
    assert end==target.stat().st_size
    report=dict(complete=True,format_version=2,input_sha256=sha(data),output_sha256=sha(target.read_bytes()),bytes=target.stat().st_size,canonical_tiles_byte_identical=verified,max_sparse_bytes=maxstored,max_canonical_bytes=header[17],max_frames=max(r['frames'] for r in rows),semantic_spans=len(spans),generator_sha256=sha(Path(__file__).read_bytes()),elapsed_seconds=time.monotonic()-started,tiles=rows)
    (out/'semantic-spans.json').write_text(json.dumps(dict(file='TREE.H1T',sha256=report['output_sha256'],spans=spans),indent=2)+'\n',encoding='utf8');(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps({k:v for k,v in report.items() if k!='tiles'}))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default=shutil.which('gcc') or 'C:/msys64/ucrt64/bin/gcc.exe');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();convert(a.input.resolve(),out,a.cc)
if __name__=='__main__':main()
