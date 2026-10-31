"""Replace selected baked payloads without re-reading unrelated source NPZs.

Keep every unselected payload byte and directory row fixed. Move the resource
footer after the replacement payloads, so the font remains the last resource.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,struct
from pack_baked import ROOT,np,rgb565,read,sha
def main():
 p=argparse.ArgumentParser();p.add_argument('--pack',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--model',type=int,required=True);p.add_argument('--clip',type=int,required=True);a=p.parse_args();path=a.pack.resolve();manifest=a.manifest.resolve();m=read(manifest);assert m['complete']
 records=[r for r in m['frames'] if (r['model'],r['clip'])==(a.model,a.clip)];assert records
 baseline=path.with_name(path.stem+'-before-refresh'+path.suffix);assert not baseline.exists();shutil.copy2(path,baseline);before=sha(path)
 fmt='<IIHHhh8H';modified=[]
 with path.open('r+b') as f:
  h=list(struct.unpack('<16I',f.read(64)));assert h[1]==12 and h[2]==path.stat().st_size
  f.seek(h[9]);rows=list(struct.iter_unpack(fmt,f.read(h[10]*32)));row_map={tuple(r[6:10]):i for i,r in enumerate(rows)}
  f.seek(h[15]+32);iar=list(struct.unpack('<8I',f.read(32)));assert iar[:3]==[0x31524149,1,32]
  start=iar[3];f.seek(start);footer=f.read();assert len(footer)==h[2]-start
  f.seek(iar[6]);font=struct.unpack('<8I',f.read(32));assert iar[6]+font[2]==h[2]
  f.seek(start)
  for r in records:
   src=ROOT/r['npz'];assert sha(src)==r['sha256'];key=tuple(r[k] for k in ('model','clip','direction','frame'));i=row_map[key];old=rows[i]
   with np.load(src) as d:rgba=d['rgba'];depth=d['depth'].astype('<i2');assert not(r['flags']&4)
   hh,ww=rgba.shape[:2];payload=rgb565(rgba[:,:,:3]).tobytes()+rgba[:,:,3].tobytes()+depth.tobytes();offset=f.tell();f.write(payload);f.write(bytes((-f.tell())&3))
   count=r['frames'];n=r['frame'];ticks=r['duration_ticks'];duration=max(1,(n+1)*ticks//count-n*ticks//count)
   row=(offset,len(payload),ww,hh,r['anchor_x'],r['anchor_y'],*key,duration,r['flags'],r['height_world'],0)
   assert row[6:12]==old[6:12];rows[i]=row;modified.append(dict(index=i,key=key,old_offset=old[0],old_size=old[1],new_offset=offset,new_size=len(payload),source_sha256=r['sha256']))
  new_start=f.tell();f.write(footer);h[2]=f.tell();f.truncate();shift=new_start-start
  for i in (3,5,6):iar[i]+=shift
  f.seek(0);f.write(struct.pack('<16I',*h));f.seek(h[15]+32);f.write(struct.pack('<8I',*iar));f.seek(h[9]);f.write(b''.join(struct.pack(fmt,*r) for r in rows))
 # Independent read-back: every unselected payload is byte-identical.
 selected={r['index'] for r in modified};unchanged=0
 with baseline.open('rb') as old,path.open('rb') as new:
  for i,r in enumerate(rows):
   if i in selected:continue
   old.seek(r[0]);new.seek(r[0]);assert old.read(r[1])==new.read(r[1]);unchanged+=1
 report=read(path.with_suffix('.json'));report.update(sha256=sha(path),bytes=h[2],sprite_payload_bytes=sum(r[1] for r in rows))
 for name,i in [('catalog.hit',3),('heroes.uir',5),('catalog.fnt',6)]:report['item_assets']['offsets'][name]=iar[i]
 report['source_manifests'][str(manifest.relative_to(ROOT))]=sha(manifest);report['source_sha256'][Path(__file__).name]=sha(Path(__file__))
 proof=dict(passed=True,before_sha256=before,after_sha256=report['sha256'],baseline=str(baseline.relative_to(ROOT)),modified_frames=modified,unchanged_payloads_checked=unchanged,footer_preserved=True)
 report['frame_refresh']=proof;path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');path.with_name(path.stem+'-refresh-verification.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(passed=True,changed=len(modified),unchanged=unchanged,bytes=h[2],sha256=report['sha256'])))
if __name__=='__main__':main()
