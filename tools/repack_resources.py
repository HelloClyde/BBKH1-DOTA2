"""Build/verify the bounded, shared-chunk H1R resource container.

Offline lossless conversion only: no Steam extraction, device or source edits.
SCENE preserves every original virtual byte/offset. SOUND stays one RAW section.
TREE is the independently verified sparse RAW v2 stream from pack_tree_sparse.py.
"""
from pathlib import Path
from array import array
import argparse,bisect,collections,hashlib,json,mmap,os,struct,sys,time,zlib

ROOT=Path(__file__).resolve().parents[1]
MAGIC=b'DOTA2H1R';VERSION=1;HEADER=128;SECTION=96;GUIDE=24
MAX_RAW=16384;SPAN_PAGE=1024;CHUNK_PAGE=512
NAMES=('SCENE.H1P','SOUND.H1A','VISION.H1V','TREE.H1T')
EXPECTED={'SCENE.H1P':'c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd',
          'SOUND.H1A':'a2be46eb35f2a86a483d154332aeb9cf44f3a24b6427e52e7a5619df01283d30',
          'VISION.H1V':'f23c061e9735e02bd098fe49adb723c56641c7ab0cc946e97b82c44ef9e7dc28'}
def require(value,message):
 if not value:raise ValueError(message)
def crc(b):return zlib.crc32(b)&0xffffffff
def identity(path):
 h=hashlib.sha256();c=0;n=0
 with Path(path).open('rb') as f:
  while b:=f.read(1024*1024):h.update(b);c=zlib.crc32(b,c);n+=len(b)
 return {'bytes':n,'sha256':h.hexdigest(),'crc32':c&0xffffffff}
def write_json(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n',encoding='utf8',newline='\n')
def align(n,to=4):return (n+to-1)&~(to-1)
def u16_values(b):
 a=array('H');a.frombytes(b)
 if sys.byteorder!='little':a.byteswap()
 return a
def transform16(b):
 require(len(b)%2==0,'Odd delta16 plane');a=u16_values(b);n=len(a);out=bytearray(len(b));previous=0
 for i,value in enumerate(a):
  delta=(value-previous)&65535;z=((delta<<1)^(-(delta>>15)))&65535
  out[i]=z&255;out[n+i]=z>>8;previous=value
 return bytes(out)
def inverse16(b):
 require(len(b)%2==0,'Odd inverse16 plane');n=len(b)//2;out=array('H');previous=0
 for i in range(n):
  z=b[i]|b[n+i]<<8;value=(previous+((z>>1)^(-(z&1))))&65535;out.append(value);previous=value
 if sys.byteorder!='little':out.byteswap()
 return out.tobytes()
def deflate(b):
 z=zlib.compressobj(6,zlib.DEFLATED,-15);return z.compress(b)+z.flush()
def inflate(b,n):
 z=zlib.decompressobj(-15);out=z.decompress(b,n+1)
 require(len(out)==n and z.eof and not z.unconsumed_tail and not z.unused_data,'Invalid bounded DEFLATE block')
 return out
def ranges_with_gaps(regions,size):
 cursor=0
 for off,n,typ,label in sorted(regions):
  require(n>0 and off>=cursor and off+n<=size,'Overlapping/out-of-range semantic span: '+label)
  if off>cursor:yield cursor,off-cursor,False,'opaque-tables-padding'
  yield off,n,typ,label;cursor=off+n
 if cursor<size:yield cursor,size-cursor,False,'opaque-tail'

def scene_regions(b):
 require(len(b)>=64,'Truncated SCENE');h=struct.unpack_from('<16I',b)
 require(h[:2]==(0x314b4248,12) and h[2]==len(b),'SCENE identity/header differs')
 require(h[3:6]==(128,30,22) and h[10]==44278 and h[12:15]==(129,180,181),'SCENE layout differs')
 regions=[];tiles=struct.unpack_from('<660I',b,h[8]);records=list(struct.iter_unpack('<IIHHhh8H',b[h[9]:h[9]+h[10]*32]))
 require(len(records)==h[10],'Truncated SCENE frame directory');groups={};previous=(-1,-1,-1,-1);shadow_frames=0
 for tile,off in enumerate(tiles):
  if off:
   regions.extend([(off,32768,True,'map/rgb'),(off+32768,32768,True,'map/depth')])
 for i,r in enumerate(records):
  off,n,w,hh,ax,ay,model,clip,direction,frame,ticks,flags,height,res=r;pixels=w*hh
  require(0<w<=256 and 0<hh<=256 and off+n<=len(b) and n>=pixels*5,'Invalid SCENE frame dimensions/range')
  require(model<192 and clip<11 and direction<16 and 1<=ticks<=200 and not res and not flags&~7,'Invalid SCENE frame metadata')
  key=(model,clip,direction);require((*key,frame)>previous,'Unsorted SCENE frame identity');previous=(*key,frame)
  require(frame==groups.get(key,0),'Non-contiguous SCENE group frames');groups[key]=frame+1
  regions.extend([(off,pixels*2,True,'sprite/rgb'),(off+pixels*2,pixels,False,'sprite/alpha'),(off+pixels*3,pixels*2,True,'sprite/depth')])
  if flags&4:
   sw,sh=struct.unpack_from('<2H',b,off+pixels*5);require(0<sw<=256 and 0<sh<=256 and n==pixels*5+8+sw*sh,'Invalid SCENE shadow')
   regions.extend([(off+pixels*5,8,False,'sprite/shadow-header'),(off+pixels*5+8,sw*sh,False,'sprite/shadow-alpha')]);shadow_frames+=1
  else:require(n==pixels*5,'Unexpected SCENE frame tail')
 wh=struct.unpack_from('<8I',b,h[15]);require(wh[0]==0x31525457 and wh[1]==32 and wh[3]==128 and wh[7]==16384,'Invalid water extension')
 masks=struct.unpack_from('<660I',b,wh[5])
 for tile,off in enumerate(masks):
  if off:require(tiles[tile]!=0,'Water mask without map');regions.append((off,16384,False,'water/mask'))
 regions.extend([(wh[6],1024,True,'water/palette'),(wh[4],wh[1]*wh[7],False,'water/animation')])
 iar=struct.unpack_from('<8I',b,h[15]+32);require(iar[:3]==(0x31524149,1,32),'Invalid item extension')
 require(iar[3]<iar[5]<iar[6]<len(b) and iar[3]+iar[4]==iar[5],'Invalid item footer ranges')
 regions.extend([(iar[3],iar[4],False,'item/catalog'),(iar[5],iar[6]-iar[5],False,'item/hero-ui'),(iar[6],len(b)-iar[6],False,'item/font')])
 return list(ranges_with_gaps(regions,len(b))),{'frames_verified':len(records),'groups_verified':len(groups),'shadow_frames_verified':shadow_frames,'map_tiles':sum(bool(x) for x in tiles),'water_masks':sum(bool(x) for x in masks),'all_virtual_bytes_preserved':True}

def tree_regions(b,path,chunking='tile'):
 s=json.loads(Path(path).read_text(encoding='utf8'));require(s['file']=='TREE.H1T' and hashlib.sha256(b).hexdigest()==s['sha256'],'TREE span manifest identity differs')
 h=list(struct.unpack_from('<32I',b));saved=h[31];h[31]=0
 require(h[:3]==[0x31525448,2,128] and h[3]==len(b) and h[4:9]==[3840,2816,128,30,22],'Not a sparse RAW TREE v2 stream')
 require(h[11:17]==[2113,128,8580,25484,41324,24] and h[30]==0 and crc(struct.pack('<32I',*h))==saved,'Invalid TREE v2 header')
 require(crc(b[128:41324])==h[19] and b[88:120]==bytes.fromhex(EXPECTED['SCENE.H1P']),'TREE tables/source identity differs')
 count=0;frames=0;cursor=41324;tile_regions=[(0,41324,False,'tree/header-and-tables')]
 for tile in range(660):
  off,n,decoded,c,code,coverage=struct.unpack_from('<6I',b,25484+24*tile)
  if not off:require([n,decoded,c,code,coverage]==[0]*5,'Invalid empty TREE tile');continue
  require(code==2 and off==cursor and n>=65544 and off+n<=len(b),'Invalid TREE tile range/codec')
  number,layers=struct.unpack_from('<2I',b,off);require(0<number<=2113*2+1 and 0<layers<=h[21] and decoded==65552+layers*114688 and coverage<=16384,'Invalid TREE tile counts')
  at=off+65544;tokens=set()
  for _ in range(number):
   token,x,y,w,hh,r0,r1,r2=struct.unpack_from('<8H',b,at);at+=16
   require(token==32767 or (token&32767)<2113,'Invalid TREE owner token')
   require(token not in tokens and not (r0|r1|r2) and 0<w<=128 and 0<hh<=128 and x+w<=128 and y+hh<=128,'Invalid TREE frame bounds/identity')
   tokens.add(token);at+=w*hh*5;require(at<=off+n,'Truncated TREE frame')
  require(at==off+n,'Unexpected TREE tile tail');cursor=off+n
  tile_regions.append((off,n,False,'tree/tile-contiguous'))
  count+=1;frames+=number
 require(cursor==len(b),'Unclaimed TREE tile data')
 regions=[];end=0
 for p in s['spans']:
  require(p['offset']==end and p['length']>0 and p['type'] in ('raw','u16'),'Invalid TREE semantic coverage')
  require(p['type']!='u16' or p['length']%2==0,'Odd TREE plane');regions.append((end,p['length'],p['type']=='u16','tree/'+p['name']));end+=p['length']
 require(end==len(b),'TREE semantics omit bytes')
 return tile_regions if chunking=='tile' else regions,{'sparse_tiles_verified':count,'sparse_frames_verified':frames,'semantic_spans_verified':len(regions),'chunking':chunking,'canonical_tile_crc_contract':'Sparse producer independently reconstructs/compares all 364 original canonical tiles; outer container preserves this v2 stream byte-for-byte.'}

def build(args):
 out=args.output.resolve();require(not out.exists(),'Output exists; preserve it and choose a fresh directory');out.mkdir(parents=True)
 paths=[args.scene,args.sound,args.vision,args.tree];sources=[identity(p) for p in paths]
 for name,source in zip(NAMES,sources):
  if name in EXPECTED:require(source['sha256']==EXPECTED[name],'Unrecognized legacy source '+name)
 payload_path=out/'chunk-data.tmp';payload=payload_path.open('wb');pool=[];lookup={};spans={};stats=collections.defaultdict(collections.Counter);structural={};started=time.monotonic()
 def add_chunk(b,u16,label):
  digest=hashlib.sha256(b).digest();stats[label]['raw_referenced_bytes']+=len(b);stats[label]['references']+=1
  if digest in lookup:stats[label]['dedup_raw_bytes']+=len(b);return lookup[digest]
  encoded=deflate(b);codec=1
  if u16:
   alternative=deflate(transform16(b))
   if len(alternative)<len(encoded):encoded=alternative;codec=2
  if len(encoded)>=len(b):encoded=b;codec=0
  require(0<len(encoded)<=len(b)<=MAX_RAW,'Invalid bounded chunk');i=len(pool);lookup[digest]=i
  pos=payload.tell();payload.write(encoded);padding=align(payload.tell())-payload.tell();payload.write(bytes(padding))
  pool.append([pos,len(encoded),len(b),crc(b),codec]);stats[label]['stored_bytes']+=len(encoded);stats[label]['unique_chunks']+=1
  return i
 for kind in (0,2,3):
  path=paths[kind]
  with path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as b:
   if kind==0:regions,proof=scene_regions(b)
   elif kind==3:regions,proof=tree_regions(b,args.tree_spans,args.tree_chunking)
   else:regions=[(0,len(b),False,'vision')];proof={'virtual_bytes':len(b)}
   target=[]
   for off,n,u16,label in regions:
    for at in range(off,off+n,MAX_RAW):
     block=b[at:min(at+MAX_RAW,off+n)];target.append([at,len(block),add_chunk(block,u16,label)])
   cursor=0
   for off,n,chunk in target:require(off==cursor,'Logical stream gap/overlap');cursor+=n
   require(cursor==len(b),'Incomplete logical stream');spans[kind]=target;structural[NAMES[kind]]=proof
  print(json.dumps({'stream':NAMES[kind],'spans':len(target),'unique_chunks':len(pool),'stored_chunk_bytes':payload.tell(),'elapsed_seconds':round(time.monotonic()-started,2)}),flush=True)
 payload.close()
 # All guides/pages are bounded RAW records. The directory is never mandatory
 # resident state; its small guide and a few data pages may be cached.
 sections=[];span_pages={};chunk_pages=[pool[i:i+CHUNK_PAGE] for i in range(0,len(pool),CHUNK_PAGE)]
 cursor=HEADER+4*SECTION
 for kind in (0,2,3):
  pages=[spans[kind][i:i+SPAN_PAGE] for i in range(0,len(spans[kind]),SPAN_PAGE)];span_pages[kind]=pages
  sections.append({'kind':kind,'guide_offset':cursor,'pages':pages});cursor+=len(pages)*GUIDE
 chunk_guide_at=cursor;cursor+=len(chunk_pages)*GUIDE
 span_guides={};span_bytes={}
 for section in sections:
  guide=[];kind=section['kind'];pages=section['pages']
  for records in pages:
   b=b''.join(struct.pack('<3I',*p) for p in records);guide.append([records[0][0],records[-1][0]+records[-1][1],cursor,len(b),len(records),crc(b)]);span_bytes[cursor]=b;cursor+=len(b)
  span_guides[kind]=b''.join(struct.pack('<6I',*p) for p in guide)
 chunk_pages_at=cursor;cursor+=len(pool)*20;sound_at=align(cursor,4096);chunk_data_at=align(sound_at+sources[1]['bytes']);file_bytes=chunk_data_at+payload_path.stat().st_size
 require(file_bytes<0x80000000,'Container exceeds H1 signed offset window')
 chunk_guides=[];chunk_page_bytes={};first=0;cursor=chunk_pages_at
 for page in chunk_pages:
  b=b''.join(struct.pack('<5I',chunk_data_at+p[0],*p[1:]) for p in page)
  chunk_guides.append([first,len(page),cursor,len(b),crc(b),0]);chunk_page_bytes[cursor]=b;cursor+=len(b);first+=len(page)
 chunk_guide=b''.join(struct.pack('<6I',*p) for p in chunk_guides)
 section_rows=[]
 for kind in range(4):
  row=[0]*24;row[0]=kind;row[1]=int(kind==1);row[2]=sources[kind]['bytes'];row[3]=sources[kind]['crc32'];row[4:12]=struct.unpack('<8I',bytes.fromhex(sources[kind]['sha256']))
  if kind==1:row[16]=sound_at;row[17]=sources[kind]['bytes']
  else:
   section=next(s for s in sections if s['kind']==kind);row[12]=len(spans[kind]);row[13]=len(section['pages']);row[14]=section['guide_offset'];row[15]=crc(span_guides[kind])
  section_rows.append(row)
 section_table=b''.join(struct.pack('<24I',*r) for r in section_rows)
 header=bytearray(128);header[:8]=MAGIC
 words=[VERSION,HEADER,file_bytes,4,HEADER,SECTION,len(pool),len(chunk_pages),chunk_guide_at,GUIDE,20,CHUNK_PAGE,MAX_RAW,0,sound_at,crc(section_table),crc(chunk_guide)]
 struct.pack_into('<17I',header,8,*words);struct.pack_into('<I',header,124,crc(header))
 target=out/'DOTA2.H1R'
 with target.open('wb') as f:
  f.write(header);f.write(section_table)
  for section in sections:require(f.tell()==section['guide_offset'],'Span guide layout');f.write(span_guides[section['kind']])
  require(f.tell()==chunk_guide_at,'Chunk guide layout');f.write(chunk_guide)
  for off,b in sorted(span_bytes.items()):require(f.tell()==off,'Span page layout');f.write(b)
  for off,b in sorted(chunk_page_bytes.items()):require(f.tell()==off,'Chunk page layout');f.write(b)
  f.write(bytes(sound_at-f.tell()));require(f.tell()==sound_at,'RAW SOUND alignment')
  with paths[1].open('rb') as src:
   while b:=src.read(1024*1024):f.write(b)
  f.write(bytes(chunk_data_at-f.tell()))
  with payload_path.open('rb') as src:
   while b:=src.read(1024*1024):f.write(b)
  require(f.tell()==file_bytes,'Final pack size')
 result=verify(target,sources)
 require(sources==[identity(p) for p in paths],'Source changed during conversion')
 report={'complete':True,'format':'DOTA2H1R','format_version':1,'bytes':file_bytes,'sha256':identity(target)['sha256'],'output':str(target),'source_sha256':identity(__file__)['sha256'],'input_files':{name:{'path':str(path.resolve()),**s} for name,path,s in zip(NAMES,paths,sources)},'tree_semantic_spans':{'path':str(args.tree_spans.resolve()),**identity(args.tree_spans)},'structural_validation':structural,'chunks':len(pool),'chunk_codecs':dict(collections.Counter(p[4] for p in pool)),'max_chunk_raw':max(p[2] for p in pool),'max_chunk_stored':max(p[1] for p in pool),'sound_raw_contiguous':True,'sound_physical_offset':sound_at,'directory_bytes':sound_at-HEADER,'chunk_data_bytes':payload_path.stat().st_size,'category_stats':dict(stats),'verification':result,'elapsed_seconds':round(time.monotonic()-started,3),'physical_h1_tested':False,'runtime_integration_tested':False}
 write_json(out/'manifest.json',report);print(json.dumps({k:report[k] for k in ('complete','bytes','sha256','chunks','elapsed_seconds','max_chunk_raw','max_chunk_stored')}),flush=True)
 return report

class Reader:
 def __init__(self,path):
  self.file=Path(path).open('rb');self.size=os.fstat(self.file.fileno()).st_size;h=self.read(0,128);require(h[:8]==MAGIC,'Bad H1R magic')
  w=list(struct.unpack_from('<30I',h,8));self.words=w
  require(w[:6]==[1,128,self.size,4,128,96] and w[9:14]==[24,20,512,16384,0],'Unsupported H1R header')
  require(not any(w[17:29]),'Nonzero H1R reserved fields');zero=bytearray(h);struct.pack_into('<I',zero,124,0);require(crc(zero)==w[29],'H1R header CRC')
  table=self.read(128,384);require(crc(table)==w[15],'H1R section table CRC');self.sections=[struct.unpack_from('<24I',table,i*96) for i in range(4)]
  chunk_guide=self.read(w[8],w[7]*24);require(crc(chunk_guide)==w[16],'H1R chunk guide CRC');self.chunk_guide=list(struct.iter_unpack('<6I',chunk_guide));self.span_guide={};self.chunk_pages={};self.decoded={}
  cursor=0
  for first,count,off,n,c,res in self.chunk_guide:
   require(first==cursor and count==min(512,w[6]-cursor) and count>0 and n==count*20 and res==0,'Invalid chunk page guide');self.read(off,n);cursor+=count
  require(cursor==w[6],'Chunk guide count differs')
  for kind,s in enumerate(self.sections):
   require(s[0]==kind and s[2]>0 and not any(s[18:]),'Invalid section metadata')
   if kind==1:
    require(s[1]==1 and s[16]%4096==0 and s[17]==s[2] and not any(s[12:16]),'SOUND must be RAW contiguous');self.read(s[16],s[17])
   else:
    require(s[1]==0 and s[12]>0 and s[13]>0 and not any(s[16:18]),'Invalid chunked section')
    guide=self.read(s[14],s[13]*24);require(crc(guide)==s[15],'Span guide CRC');pages=list(struct.iter_unpack('<6I',guide));end=0;records=0
    for begin,stop,off,n,count,c in pages:
     require(begin==end and begin<stop<=s[2] and 0<count<=1024 and n==count*12,'Invalid span page guide');self.read(off,n);end=stop;records+=count
    require(end==s[2] and records==s[12],'Incomplete span coverage');self.span_guide[kind]=pages
 def read(self,off,n):
  require(0<=off<=self.size and 0<=n<=self.size-off,'Out-of-file resource range');self.file.seek(off);b=self.file.read(n);require(len(b)==n,'Short resource read');return b
 def chunk(self,index):
  require(index<self.words[6],'Invalid chunk ID');page=index//512
  if page not in self.chunk_pages:
   first,count,off,n,c,res=self.chunk_guide[page];b=self.read(off,n);require(crc(b)==c,'Chunk page CRC');self.chunk_pages={page:list(struct.iter_unpack('<5I',b))}
  record=self.chunk_pages[page][index%512];off,stored,raw,c,codec=record
  require(off%4==0 and 0<stored<=raw<=MAX_RAW and codec in (0,1,2) and (codec==0 and stored==raw or codec!=0 and stored<raw) and (codec!=2 or raw%2==0),'Invalid bounded chunk metadata')
  key=(off,stored,raw,c,codec)
  if key in self.decoded:return self.decoded[key]
  b=self.read(off,stored);b=inflate(b,raw) if codec else b;b=inverse16(b) if codec==2 else b;require(crc(b)==c,'Canonical chunk CRC')
  # Host verification does not require a file-sized or directory-sized cache.
  if len(self.decoded)>=4096:self.decoded.clear()
  self.decoded[key]=b;return b
 def stream(self,kind):
  s=self.sections[kind]
  if kind==1:
   for off in range(0,s[2],MAX_RAW):yield self.read(s[16]+off,min(MAX_RAW,s[2]-off))
  else:
   cursor=0
   for begin,stop,off,n,count,c in self.span_guide[kind]:
    b=self.read(off,n);require(crc(b)==c,'Span page CRC')
    for at,raw,index in struct.iter_unpack('<3I',b):
     require(at==cursor and 0<raw<=MAX_RAW,'Span hole/invalid length');block=self.chunk(index);require(len(block)==raw,'Span/chunk size mismatch');yield block;cursor+=raw
    require(cursor==stop,'Span guide range mismatch')
   require(cursor==s[2],'Stream length mismatch')
 def close(self):self.file.close()

def verify(path,expected=None):
 reader=Reader(path);results={}
 try:
  for kind,name in enumerate(NAMES):
   h=hashlib.sha256();c=0;n=0
   for b in reader.stream(kind):h.update(b);c=zlib.crc32(b,c);n+=len(b)
   s=reader.sections[kind];require(n==s[2] and c&0xffffffff==s[3] and h.digest()==struct.pack('<8I',*s[4:12]),'Logical source identity differs: '+name)
   result={'bytes':n,'sha256':h.hexdigest(),'crc32':c&0xffffffff,'byte_exact':True}
   if expected:require(result['sha256']==expected[kind]['sha256'] and n==expected[kind]['bytes'],'Reconstruction differs from input')
   results[name]=result
 finally:reader.close()
 return {'passed':True,'all_four_streams_byte_exact':True,'streams':results,'sound_not_recompressed':True,'no_MAP12_stream':True,'physical_h1_tested':False}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--legacy-dir',type=Path,default=ROOT/'assets/runtime');p.add_argument('--scene',type=Path);p.add_argument('--sound',type=Path);p.add_argument('--vision',type=Path);p.add_argument('--tree',type=Path);p.add_argument('--tree-spans',type=Path);p.add_argument('--tree-chunking',choices=('tile','semantic'),default='tile');p.add_argument('--output',type=Path,default=ROOT/'build/v030/repack');p.add_argument('--verify-only',type=Path);p.add_argument('--verification-output',type=Path)
 args=p.parse_args()
 if args.verify_only:
  result=verify(args.verify_only);result.update(pack=identity(args.verify_only),source_sha256=identity(__file__)['sha256'])
  if args.verification_output:write_json(args.verification_output,result)
  print(json.dumps(result,indent=2));return
 for attr,name in zip(('scene','sound','vision'),NAMES[:3]):setattr(args,attr,getattr(args,attr) or args.legacy_dir/name)
 require(args.tree is not None and args.tree_spans is not None,'Supply the verified sparse TREE v2 --tree and --tree-spans')
 build(args)
if __name__=='__main__':main()
