"""Bounded execution of the complete candidate BDA's MIPS resource/tree APIs.

The H1 filesystem/heap are modeled by CoreMachine. The actual compiled MIPS
DEFLATE, delta inverse, resource cursors, sparse tree decoder, crown picker and
cut compositor execute unchanged. No application entry, device or FPS claim.
"""
from pathlib import Path
import argparse,array,hashlib,json,struct,sys,time,traceback,zlib
ROOT=Path(__file__).resolve().parents[1]
RESOURCE_FIELDS='mounted version file_bytes open_files heap_bytes last_error mount_attempts open_attempts read_requests bytes_returned physical_reads physical_bytes seeks index_hits index_misses chunk_hits chunk_misses decoded_chunks decoded_bytes service_calls errors'.split()
TREE_FIELDS='open_attempts ready error heap_bytes metadata_bytes cache_bytes scratch_bytes read_requests read_bytes tile_hits tile_misses decoded_tiles patched_pixels depth_queries closes released'.split()

def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def uints(data,at,n):return struct.unpack_from('<'+'I'*n,data,at)
def source_bytes(path,offset,count):
 with path.open('rb') as f:f.seek(offset);data=f.read(count)
 assert len(data)==count
 return data

def index(bundle):
 data=bundle.read_bytes();h=uints(data,0,32);assert data[:8]==b'DOTA2H1R' and h[2]==1 and h[4]==len(data)
 check=bytearray(data[:128]);check[124:128]=bytes(4);assert zlib.crc32(check)&0xffffffff==h[31]
 chunks=[]
 for page in range(h[9]):
  first,count,at,size,crc,res=uints(data,h[10]+page*24,6);assert first==len(chunks) and size==count*20 and not res;assert zlib.crc32(data[at:at+size])&0xffffffff==crc
  chunks.extend(uints(data,at+i*20,5) for i in range(count))
 sections=[];refs={}
 for sid in range(4):
  at=128+sid*96;rid,flags,size,crc=uints(data,at,4);count,pages,guides,gcrc=uints(data,at+48,4);raw_at,raw_n=uints(data,at+64,2);assert rid==sid
  spans=[]
  for page in range(pages):
   begin,end,off,n,records,rc=uints(data,guides+page*24,6);assert zlib.crc32(data[off:off+n])&0xffffffff==rc
   for i in range(records):
    start,length,cid=uints(data,off+i*12,3);spans.append((start,length,cid));refs.setdefault(cid,(sid,start,length))
  assert len(spans)==count
  sections.append(dict(id=sid,flags=flags,bytes=size,sha256=data[at+16:at+48].hex(),raw_offset=raw_at,raw_bytes=raw_n,spans=spans))
 return chunks,sections,refs

def crown_fixture(raw_tree,tree_id=966):
 """Select an opaque, originally visible source-tree sample, not an outcome."""
 with raw_tree.open('rb') as f:
  f.seek(8580+tree_id*8);bbox=struct.unpack('<4h',f.read(8));candidates=[]
  for ty in range(bbox[1]//128,(bbox[3]-1)//128+1):
   for tx in range(bbox[0]//128,(bbox[2]-1)//128+1):
    tile=ty*30+tx;f.seek(25484+tile*24);off,n,raw,crc,codec,cov=struct.unpack('<6I',f.read(24));assert codec==2;f.seek(off);b=f.read(n);frames,layers=uints(b,0,2);base=array.array('H');base.frombytes(b[8+32768:8+65536]);front=[65536]*16384;owners=[65535]*16384;colors=[0]*16384;alpha=[0]*16384;pos=8+65536
    for _ in range(frames):
     token,x,y,w,h,r0,r1,r2=struct.unpack_from('<8H',b,pos);pos+=16;count=w*h;c=array.array('H');c.frombytes(b[pos:pos+count*2]);z=array.array('H');z.frombytes(b[pos+count*2:pos+count*4]);a=b[pos+count*4:pos+count*5];pos+=count*5
     if token&32768:continue
     for py in range(h):
      for px in range(w):
       q=py*w+px;p=(y+py)*128+x+px
       if a[q] and z[q]<=base[p] and z[q]<front[p]:front[p]=z[q];owners[p]=token;colors[p]=c[q];alpha[p]=a[q]
    assert pos==len(b)
    for p,owner in enumerate(owners):
     if owner==tree_id and alpha[p]==255:
      mx,my=tx*128+p%128,ty*128+p//128;distance=abs(mx*2-bbox[0]-bbox[2])+abs(my*2-bbox[1]-bbox[3]);candidates.append((distance,mx,my,front[p],colors[p],tile,layers))
 assert candidates
 _,x,y,z,c,t,l=min(candidates)
 return dict(tree_id=tree_id,global_x=x-512,global_y=y-512,original_depth=z,source_color=c,tile=t,layers=l,bbox=bbox)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--bundle',type=Path,default=ROOT/'assets/runtime/DOTA2.H1R');p.add_argument('--legacy',type=Path,default=ROOT/'build/v030/legacy');p.add_argument('--tree-stream',type=Path,default=ROOT/'build/v030/tree-sparse/TREE.H1T');p.add_argument('--output',type=Path,required=True);p.add_argument('--reserve-read-cache',action='store_true',help='Exercise optional MIPS32 heap index/decoded cache, including late tree reservation');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 build=a.build.resolve();bda=build/'DOTA2.bda';elf=build/'DOTA2.elf';metadata=build/'DOTA2.build.json';meta=json.loads(metadata.read_text());assert not meta.get('diagnostic_only') and sha(bda)==meta['bda_sha256'] and sha(elf)==meta['elf_sha256'];production=meta['source_sha256'];compiled={n:h for n,h in production.items() if Path(n).suffix.lower() in ['.c','.h','.s','.ld']};assert all(sha(ROOT/n)==h for n,h in compiled.items())
 abi=meta['binary']['abi'];assert isinstance(abi['DotaGame'],int) and abi['DotaGame']>0 and abi['DotaUnit']==240
 noncompiled_changes=[n for n,h in production.items() if n not in compiled and (not (ROOT/n).is_file() or sha(ROOT/n)!=h)];test_inputs={p.relative_to(ROOT).as_posix():sha(p) for p in [Path(__file__).resolve(),ROOT/'tests/mips_core_model.py']}
 sources=[a.legacy/'SCENE.H1P',a.legacy/'SOUND.H1A',a.legacy/'VISION.H1V',a.tree_stream];chunks,sections,refs=index(a.bundle)
 for sid,path in enumerate(sources):assert path.stat().st_size==sections[sid]['bytes'] and sha(path)==sections[sid]['sha256']
 fixture=crown_fixture(a.tree_stream);bundle_sha=sha(a.bundle);legacy_tree_sha=sha(a.legacy/'TREE.H1T')
 tree_directory=source_bytes(a.tree_stream,25484,660*24);maximum_tile=max(range(660),key=lambda i:uints(tree_directory,i*24,6)[1]);maximum_entry=uints(tree_directory,maximum_tile*24,6);maximum_tree=dict(tile=maximum_tile,stored_bytes=maximum_entry[1],canonical_bytes=maximum_entry[2],layers=(maximum_entry[2]-16-65536)//114688,global_x=(maximum_tile%30)*128+64-512,global_y=(maximum_tile//30)*128+64-512)
 deps=ROOT.parent/'.tools/dota3d-deps'
 if deps.is_dir():sys.path.insert(0,str(deps))
 sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
 from elftools.elf.elffile import ELFFile
 assets={'DOTA2.H1R':a.bundle,'TREE.H1T':a.legacy/'TREE.H1T'};checks=0;cases=[];calls=[];machines=[];started=time.monotonic()
 def check(ok,label):
  nonlocal checks;checks+=1
  if not ok:raise AssertionError(label)
 def machine():
  m=CoreMachine(bda,elf,assets);machines.append(m)
  with elf.open('rb') as stream:
   for segment in ELFFile(stream).iter_segments():
    if segment['p_type']=='PT_LOAD' and segment['p_filesz']:check(m.read(int(segment['p_vaddr']),int(segment['p_filesz']))==segment.data(),'Exact loaded complete-BDA payload equals ELF segment')
  return m
 def call(m,name,*args):
  start=time.monotonic();result=m.call(name,*args);calls.append(dict(api=name,seconds=round(time.monotonic()-start,6)));return result
 def stats(m,name,fields):
  # The final BDA link discards the test-only tree getter. Observe its retained
  # ELF data symbol read-only; no code, state, or result is substituted.
  if name in m.symbols:address=call(m,name)&0xffffffff
  else:address=m.symbols['tree_stats'];check(m.symbol_sizes['tree_stats']==len(fields)*4,'actual retained TreeVisualStats ABI')
  return dict(zip(fields,uints(m.read(address,len(fields)*4),0,len(fields))))
 def pointer(m,name,*args):
  value=call(m,name,*args)&0xffffffff;check(value!=0,name+' returned handle');return value
 def expect_read(m,f,sid,offset,count,label,pread=True):
  result=call(m,'resource_read_at',f,offset,0x81501000,count) if pread else call(m,'resource_read',f,0x81501000,count);check(result==(1 if pread else count),label+' length');check(m.read(0x81501000,count)==source_bytes(sources[sid],offset,count),label+' bytes')
 try:
  m=machine();m.write(0x81500000,b'B:\\DOTA2\\DOTA2.H1R\0');check(call(m,'resource_mount',0x81500000)==1,'Mount final H1R')
  if a.reserve_read_cache:
   before=call(m,'resource_heap_bytes');allocations=len(m.allocations);added=call(m,'resource_reserve_read_cache');check(added==168336,'MIPS32 8-index/4-decoded cache byte budget');check(call(m,'resource_heap_bytes')==before+added and len(m.allocations)==allocations+1,'single optional heap block includes MIPS32 pointer descriptors');check(call(m,'resource_reserve_read_cache')==added and len(m.allocations)==allocations+1,'repeat reserve retains allocation');cases.append(dict(case='actual-mips32-read-cache-reserve',passed=True,added_heap_bytes=added))
  handles={sid:pointer(m,'resource_open',sid) for sid in range(4)}
  for sid,f in handles.items():check(call(m,'resource_size',f)==sections[sid]['bytes'],'Original logical stream size')
  selected=[]
  for codec in [1,2]:
   cid=max((i for i,c in enumerate(chunks) if c[4]==codec and i in refs),key=lambda i:chunks[i][1]);sid,offset,n=refs[cid];expect_read(m,handles[sid],sid,offset,n,'codec'+str(codec));selected.append(dict(case='codec'+str(codec),chunk=cid,section=sid,offset=offset,raw_bytes=n,stored_bytes=chunks[cid][1],codec=codec))
  cid=max((i for i in refs),key=lambda i:(chunks[i][2],chunks[i][1]));sid,offset,n=refs[cid];check(n==16384,'Maximum decoded block is 16KiB');expect_read(m,handles[sid],sid,offset,n,'maximum block');selected.append(dict(case='maximum-block',chunk=cid,section=sid,offset=offset,raw_bytes=n,stored_bytes=chunks[cid][1],codec=chunks[cid][4]))
  # Cross the 1024-record span-index page boundary as well as a decoded span.
  span=sections[0]['spans'][1023];cross=span[0]+span[1]-7;expect_read(m,handles[0],0,cross,97,'cross-span-and-index-page');selected.append(dict(case='cross-span-index-page',section=0,offset=cross,raw_bytes=97))
  cases.append(dict(case='representative-compressed-reads',passed=True,reads=selected));print('CASE compressed reads',flush=True)
  f=handles[0];check(call(m,'resource_seek',f,1234,0)==1234,'scene seek SET');expect_read(m,f,0,1234,23,'scene sequential',False);check(call(m,'resource_seek',f,0,1)==1257,'scene tell after read');expect_read(m,f,0,cross,97,'pread distant');check(call(m,'resource_seek',f,0,1)==1257,'pread preserves scene cursor');expect_read(m,f,0,1257,31,'scene resumes cursor',False);check(call(m,'resource_seek',f,-8,1)==1280,'negative relative seek');size=sections[0]['bytes'];check(call(m,'resource_seek',f,-19,2)==size-19,'scene seek END');check(call(m,'resource_read',f,0x81501000,64)==19,'EOF clipped read');check(m.read(0x81501000,19)==source_bytes(sources[0],size-19,19),'EOF bytes');check(call(m,'resource_read',f,0x81501000,1)==0,'EOF empty read');cases.append(dict(case='seek-pread-eof',passed=True))
  audio1=handles[1];audio2=pointer(m,'resource_open',1);check(call(m,'resource_seek',audio1,17,0)==17,'sound A seek');expect_read(m,audio1,1,17,37,'sound A first',False);check(call(m,'resource_seek',audio2,501,0)==501,'sound B seek');expect_read(m,audio2,1,501,53,'sound B first',False);expect_read(m,audio1,1,700,47,'sound A pread');check(call(m,'resource_seek',audio1,0,1)==54,'sound A pread cursor');expect_read(m,handles[2],2,0,64,'interleaved visual read');expect_read(m,audio1,1,54,29,'sound A resumes',False);expect_read(m,audio2,1,554,31,'sound B independent resumes',False);check(call(m,'resource_seek',audio1,0,1)==83 and call(m,'resource_seek',audio2,0,1)==585,'independent final sound cursors');check(call(m,'resource_close',audio2)==0,'sound B close');cases.append(dict(case='sound-two-independent-cursors',passed=True,positions=[83,585]));print('CASE cursors',flush=True)
  resource_before=stats(m,'resource_stats',RESOURCE_FIELDS);check(not resource_before['last_error'] and not resource_before['errors'],'no resource error');check(resource_before['decoded_chunks']>0,'MIPS decoder actually executed')
  for f in handles.values():check(call(m,'resource_close',f)==0,'close logical stream')
  check(call(m,'resource_unmount')==1 and not call(m,'resource_heap_bytes') and not m.allocations and not m.handles,'resource final heap/handles released');cases.append(dict(case='resource-close-unmount',passed=True,stats=resource_before,public_calls=m.calls,modeled_physical_bytes=m.bytes_read));m.dispose()
  # Both tree branches execute this exact candidate ELF, with separate zeroed
  # initial BSS. The test never writes Game state or renderer outcome values.
  tree_results={}
  for mode in ['unified','legacy']:
   m=machine();gp=m.symbols['dota_game'];check(m.symbol_sizes['dota_game']==abi['DotaGame'],'actual Game ABI matches frozen build metadata');treepath=0
   if mode=='legacy':m.write(0x81500000,b'B:\\DOTA2\\TREE.H1T\0');treepath=0x81500000
   check(call(m,'tree_visual_open',treepath)==1,'tree open '+mode);check(call(m,'dota_trees_ready')==1,'source geometry attached')
   if a.reserve_read_cache and mode=='unified':check(call(m,'resource_reserve_read_cache')==168336,'late MIPS32 reserve migrates already validated tree index/cache entries')
   gx,gy=fixture['global_x'],fixture['global_y'];original=fixture['original_depth'];m.write(0x81501000,struct.pack('<HH',0x1234,original));before=m.bytes_read
   call(m,'tree_visual_apply',0x81501000,0x81501002,gp,gx,gy,1,1);check(m.read(0x81501000,4)==struct.pack('<HH',0x1234,original) and m.bytes_read==before,'zero cut unchanged and no tree IO')
   maximum_pick=call(m,'tree_visual_pick',gp,maximum_tree['global_x'],maximum_tree['global_y']);check(not call(m,'tree_visual_error'),'maximum sparse/canonical tile decoded and CRC checked')
   picked=call(m,'tree_visual_pick',gp,gx,gy);check(picked==966,'actual original crown owner');check(call(m,'dota_trees_cut',gp,966)==1 and call(m,'dota_trees_is_cut',gp,966)==1,'actual source tree cut');after_pick=call(m,'tree_visual_pick',gp,gx,gy);check(after_pick!=966,'cut crown no longer hits removed tree');depth=call(m,'tree_visual_depth_at',gp,gx,gy,original);check(depth!=original,'cut changes actual depth');before=m.bytes_read;cached=call(m,'tree_visual_depth_at',gp,gx,gy,original);check(cached==depth and m.bytes_read==before,'warm patch depth no reread');m.write(0x81501000,struct.pack('<HH',0x1234,original));call(m,'tree_visual_apply',0x81501000,0x81501002,gp,gx,gy,1,1);color,applied_depth=struct.unpack('<HH',m.read(0x81501000,4));check(applied_depth==depth,'patch and depth query agree');ts=stats(m,'tree_visual_stats',TREE_FIELDS);check(not ts['error'],'no sparse/legacy decoder error');tree_results[mode]=dict(picked=picked,maximum_tile_pick=maximum_pick,after_cut_pick=after_pick,color=color,depth=depth,stats=ts,public_calls=m.calls,modeled_physical_bytes=m.bytes_read);call(m,'tree_visual_close');check(not call(m,'dota_trees_ready') and not call(m,'tree_visual_dynamic_bytes'),'tree closes borrowed geometry and heap');check(call(m,'resource_unmount')==1 and not m.allocations and not m.handles,'all tree/resource allocations released');print('CASE tree '+mode,flush=True);m.dispose()
  for key in ['picked','maximum_tile_pick','after_cut_pick','color','depth']:check(tree_results['unified'][key]==tree_results['legacy'][key],'actual MIPS legacy/unified tree '+key)
  check(tree_results['unified']['stats']['scratch_bytes']==1064976,'v2 bounded 80KiB frame temporary');check(tree_results['legacy']['stats']['scratch_bytes']==1193899,'v1 only actual encoded temporary');cases.append(dict(case='actual-tree-v2-versus-v1',passed=True,fixture=fixture,maximum_tile=maximum_tree,runs=tree_results))
  check(all(sha(ROOT/n)==h for n,h in compiled.items()),'actual compiled sources current');check(sha(bda)==meta['bda_sha256'] and sha(elf)==meta['elf_sha256'] and sha(a.bundle)==bundle_sha,'candidate BDA ELF and final bundle unchanged');check(all(sha(ROOT/n)==h for n,h in test_inputs.items()),'test sources current')
  report=dict(passed=True,actual_complete_bda=True,actual_mips32=True,physical_h1_tested=False,USB_opened=False,entry_executed=False,game_state_writes_by_test=0,reserved_read_cache=a.reserve_read_cache,scope=__doc__,checks=checks,cases=cases,bda_sha256=sha(bda),elf_sha256=sha(elf),metadata_sha256=sha(metadata),build_abi=abi,bundle_sha256=bundle_sha,legacy_tree_sha256=legacy_tree_sha,tree_stream_sha256=sha(a.tree_stream),compiled_source_current=True,compiled_source_sha256=compiled,test_source_sha256=test_inputs,noncompiled_build_inputs_changed=noncompiled_changes,all_build_inputs_current=not noncompiled_changes,source_current=True,API_calls=calls,elapsed_seconds=time.monotonic()-started)
  (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,checks=checks,cases=len(cases),actual_mips32=True,elapsed_seconds=report['elapsed_seconds'],compiled_source_current=True)))
 except Exception as e:
  (out/'failure.json').write_text(json.dumps(dict(passed=False,error=str(e),cases=cases,API_calls=calls,bda_sha256=sha(bda),elf_sha256=sha(elf),bundle_sha256=bundle_sha,USB_opened=False),indent=2)+'\n');(out/'failure.txt').write_text(traceback.format_exc());raise
 finally:
  for m in machines:m.dispose()
if __name__=='__main__':main()
