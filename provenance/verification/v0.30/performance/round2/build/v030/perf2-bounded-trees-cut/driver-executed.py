"""Opt-in physical H1 tree/menu acceptance: normal inputs, read-only RAM only.

Run only after the root operator verifies the deployed BDA/assets and idle
desktop. This driver does not deploy, upload, install, patch RAM or fast-forward.
The natural regrow wait takes at least180 unpaused game seconds, unless the
explicit --cut-only scope is selected.
"""
from pathlib import Path
import argparse,collections,hashlib,json,math,struct,sys,time,zlib
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
P=16384
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def bits(data,i):return(data[i>>3]>>(i&7))&1
def trunc(n,d):return abs(n)//d*(1 if n>=0 else -1)

class OriginalMap:
 def __init__(self,reference_assets):
  # PC-only selection/navigation oracle. Neither file is deployed in v0.30;
  # linked offline receipts prove equality with the unified logical streams.
  with (reference_assets/'SCENE.H1P').open('rb')as f:
   h=struct.unpack('<16I',f.read(64));self.n=h[12];f.seek(h[11]);self.ground=struct.unpack('<'+'h'*self.n**2,f.read(self.n**2*2))
  self.data=(reference_assets/'TREE.H1T').read_bytes();self.h=struct.unpack('<32I',self.data[:128]);assert self.h[11:14]==(2113,128,8580)
  self.xy=[struct.unpack_from('<HH',self.data,128+i*4)for i in range(2113)];self.box=[struct.unpack_from('<4H',self.data,8580+i*8)for i in range(2113)];self.tiles={}
  self.base=(ROOT/'provenance/tree-navigation/gnv-walk.bin').read_bytes();self.phys=(ROOT/'provenance/tree-navigation/physics-edges.bin').read_bytes();self.buildings=json.loads((ROOT/'provenance/tree-navigation/source-buildings.json').read_bytes())
 def height(self,x,y):
  x=max(0,min(4095,x));y=max(0,min(4095,y));gx=x*(self.n-1)*256//4095;gy=y*(self.n-1)*256//4095;ix,fx=divmod(gx,256);iy,fy=divmod(gy,256)
  if x==4095:ix,fx=self.n-2,256
  if y==4095:iy,fy=self.n-2,256
  a,b=self.ground[iy*self.n+ix:iy*self.n+ix+2];c,d=self.ground[(iy+1)*self.n+ix:(iy+1)*self.n+ix+2];h0=trunc(a*(256-fx)+b*fx,256);h1=trunc(c*(256-fx)+d*fx,256);return trunc(h0*(256-fy)+h1*fy,256)
 def screen(self,x,y,camera,z=None):
  cx,cy=camera;z=self.height(x,y)if z is None else z;return 240+x*180//256-cx*180//256,120+(y-z)*181*180//65536-(cy-self.height(cx,cy))*181*180//65536
 def tile(self,index):
  if index in self.tiles:return self.tiles[index]
  off,stored,count,crc,codec,_=struct.unpack_from('<6I',self.data,25484+index*24)
  if not stored:self.tiles[index]=None;return None
  src=self.data[off:off+stored]
  if codec:
   out=bytearray();i=0
   while i<len(src):
    token=src[i];i+=1;n=(token&127)+1
    if token&128:out.extend(bytes(n))
    else:out.extend(src[i:i+n]);i+=n
   assert len(out)==count;layers=struct.unpack_from('<I',out,4)[0]
   for start in [16,16+P*2,*[16+P*4+k*P*7+j*P*2 for k in range(layers)for j in range(3)]]:
    buf=bytearray(P*2);previous=0
    for j in range(P):
     zz=out[start+j]|out[start+P+j]<<8;previous=(previous+((zz>>1)^-(zz&1)))&65535;struct.pack_into('<H',buf,j*2,previous)
    out[start:start+P*2]=buf
   raw=bytes(out)
  else:raw=src
  assert len(raw)==count and zlib.crc32(raw)==crc;layers=struct.unpack_from('<I',raw,4)[0];assert struct.unpack_from('<4I',raw)==(0x31545048,layers,P,0)
  result=(struct.unpack_from('<'+str(P)+'H',raw,16+P*2),[(struct.unpack_from('<'+str(P)+'H',raw,16+P*4+k*P*7),struct.unpack_from('<'+str(P)+'H',raw,16+P*4+k*P*7+P*4),raw[16+P*4+k*P*7+P*6:16+P*4+(k+1)*P*7])for k in range(layers)]);self.tiles[index]=result;return result
 def hit(self,x,y,cut):
  if not(0<=x<3840 and 0<=y<2816):return-1
  tile=self.tile(y//128*30+x//128)
  if tile is None:return-1
  at=(y&127)*128+(x&127);base,layers=tile
  for owner,depth,alpha in layers:
   token=owner[at];ident=token&32767
   if token==65535 or not alpha[at]or depth[at]>base[at]:continue
   if token==32767:return-1
   if token&32768:
    if bits(cut,ident):return-1
    continue
   if not bits(cut,ident):return ident
  return-1
 def crown(self,ident,cut,camera):
  x0,y0,x1,y1=self.box[ident];cx=(x0+x1)//2;cy=(y0+y1)//2;ox,oy=self.screen(0,0,camera,0)
  points=sorted(((x,y)for y in range(y0,y1,2)for x in range(x0,x1,2)),key=lambda v:(v[0]-cx)**2+(v[1]-cy)**2)
  for x,y in points:
   sx,sy=x-512+ox,y-512+oy
   if 12<=sx<468 and 30<=sy<192 and self.hit(x,y,cut)==ident:return sx,sy
  return None
 def route(self,start,cut,excluded):
  blocked=bytearray(8192)
  for i,(x,y)in enumerate(self.xy):
   if bits(cut,i):continue
   for cy in range(max(0,y//16-2),min(256,y//16+2)):
    for cx in range(max(0,x//16-2),min(256,x//16+2)):
     if(cx*16+8-x)**2+(cy*16+8-y)**2<=22**2:j=cy*256+cx;blocked[j>>3]|=1<<(j&7)
  for row in self.buildings['static_barracks']+self.buildings['dynamic_towers']+self.buildings['dynamic_fillers']:
   x,y=row['world_xy'];r=row['collision_radius_world']
   for cy in range(max(0,int((y-r-16)//16)),min(256,int((y+r+16)//16)+1)):
    for cx in range(max(0,int((x-r-16)//16)),min(256,int((x+r+16)//16)+1)):
     if max(abs(cx*16+8-x)-8,0)**2+max(abs(cy*16+8-y)-8,0)**2<=r*r:j=cy*256+cx;blocked[j>>3]|=1<<(j&7)
  def open_(c):return 0<=c<65536 and bits(self.base,c)and not bits(blocked,c)
  sx,sy=start;begin=sy//16*256+sx//16;goals={}
  for i,(x,y)in enumerate(self.xy):
   if i in excluded or bits(cut,i)or(x-sx)**2+(y-sy)**2>400**2:continue
   for cy in range(max(0,y//16-2),min(256,y//16+3)):
    for cx in range(max(0,x//16-2),min(256,x//16+3)):
     c=cy*256+cx
     if open_(c)and(cx*16+8-x)**2+(cy*16+8-y)**2<=37**2:goals.setdefault(c,i)
  queue=collections.deque([begin]);parent={begin:None};end=None
  while queue:
   at=queue.popleft()
   if at in goals:end=at;break
   x,y=at%256,at//256
   for xx,yy,edge in [(x+1,y,at),(x-1,y,at-1),(x,y+1,65536+at),(x,y-1,65536+at-256)]:
    other=yy*256+xx
    if 0<=xx<256 and 0<=yy<256 and open_(other)and other not in parent and not bits(self.phys,edge):parent[other]=at;queue.append(other)
  assert end is not None,'No nearby source-clear tree approach; leave device untouched for operator review'
  path=[];at=end
  while at is not None:path.append((at%256*16+8,at//256*16+8));at=parent[at]
  return goals[end],list(reversed(path))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path);p.add_argument('--output',type=Path);p.add_argument('--debug-workspace',type=Path);p.add_argument('--reference-assets',type=Path,help='PC-only legacy SCENE/TREE crown and navigation reference');p.add_argument('--bundle',type=Path,default=ROOT/'assets/runtime/DOTA2.H1R');p.add_argument('--resource-verification',type=Path,default=ROOT/'build/v030/resource-runtime-verification-2/verification.json');p.add_argument('--tree-verification',type=Path,default=ROOT/'build/v030/tree-sparse/decoder-final/verification.json');p.add_argument('--deployment-verification',type=Path,help='Optional root-owned physical copy/readback receipt');p.add_argument('--offline-only',action='store_true',help='Validate identities and ELF layouts without opening USB');p.add_argument('--check-interface',action='store_true');p.add_argument('--cut-only',action='store_true',help='Test two normal-input cuts and cleanup; do not claim physical natural regrowth');a=p.parse_args()
 if a.check_interface:print(json.dumps(dict(prepared=True,USB_opened=False,game_state_ram_writes=0,natural_regrow_seconds=0 if a.cut_only else 180,resource_container='DOTA2.H1R',read_only_resource_stats=True,reference_assets_are_PC_only=True)));return
 assert a.build and a.output and a.reference_assets and(a.debug_workspace or a.offline_only);build=a.build.resolve();out=a.output.resolve();reference=a.reference_assets.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True);(out/'driver-executed.py').write_bytes(Path(__file__).read_bytes());original=OriginalMap(reference)
 meta=json.loads((build/'DOTA2.build.json').read_text());assert meta['bda_sha256']==sha(build/'DOTA2.bda')and meta['elf_sha256']==sha(build/'DOTA2.elf');layouts={};sizes={}
 abi=meta['binary']['abi'];assert isinstance(abi['DotaGame'],int)and abi['DotaGame']>0 and isinstance(abi['DotaView'],int)and abi['DotaView']>0 and abi['DotaUnit']==240
 bundle_sha=sha(a.bundle);reference_scene_sha=sha(reference/'SCENE.H1P');reference_tree_sha=sha(reference/'TREE.H1T');rv=json.loads(a.resource_verification.read_text());tv=json.loads(a.tree_verification.read_text());assert rv['passed']and rv['source_current']and tv['passed']and tv['source_current'];assert meta['runtime_assets']['DOTA2.H1R']['sha256']==bundle_sha==rv['full_pack_sha256'];assert rv['full_source_sha256'][0]==reference_scene_sha;assert tv['legacy_sha256']==reference_tree_sha and tv['sparse_sha256']==rv['full_source_sha256'][3]
 evidence=dict(resource_stream_equality=dict(path=str(a.resource_verification.resolve()),sha256=sha(a.resource_verification),SCENE_stream_sha256=rv['full_source_sha256'][0],TREE_stream_sha256=rv['full_source_sha256'][3]),tree_sparse_canonical_equality=dict(path=str(a.tree_verification.resolve()),sha256=sha(a.tree_verification),legacy_reference_sha256=tv['legacy_sha256'],sparse_transport_sha256=tv['sparse_sha256'],canonical_compared_bytes=tv['byte_exact_canonical_comparison_bytes']))
 if a.deployment_verification:
  dv=json.loads(a.deployment_verification.read_text());assert dv['passed'];files=dv['files'];assert any(f['sha256']==bundle_sha and Path(f['source']).name.upper()=='DOTA2.H1R'for f in files);assert any(f['sha256']==meta['bda_sha256'] and Path(f['source']).name.lower()=='dota2.bda'for f in files);evidence['physical_copy_readback']=dict(path=str(a.deployment_verification.resolve()),sha256=sha(a.deployment_verification),passed=True,files=files)
 wanted={'DotaGame','DotaUnit','DotaView','DotaTreesState','DotaItemsState','DotaItemEffect','ResourceStats','DotaCacheDebug'}
 with(build/'DOTA2.elf').open('rb')as f:
  elf=ELFFile(f);symbols={s.name:(int(s['st_value']),int(s['st_size']))for s in elf.get_section_by_name('.symtab').iter_symbols()}
  for cu in elf.get_dwarf_info().iter_CUs():
   for die in cu.iter_DIEs():
    n=die.attributes.get('DW_AT_name')
    if die.tag!='DW_TAG_typedef'or not n or n.value.decode()not in wanted:continue
    target=die.get_DIE_from_attribute('DW_AT_type')
    while target.tag=='DW_TAG_typedef':target=target.get_DIE_from_attribute('DW_AT_type')
    if'DW_AT_byte_size'not in target.attributes:continue
    name=n.value.decode();sizes[name]=target.attributes['DW_AT_byte_size'].value;layouts[name]={d.attributes['DW_AT_name'].value.decode():d.attributes['DW_AT_data_member_location'].value for d in target.iter_children()if d.tag=='DW_TAG_member'}
 assert wanted==set(layouts)and[sizes[n]for n in('DotaGame','DotaUnit','DotaView','DotaTreesState','ResourceStats','DotaCacheDebug')]==[abi['DotaGame'],240,abi['DotaView'],4500,84,64];assert symbols['dota_game'][1]==abi['DotaGame']and symbols['view'][1]==abi['DotaView'];assert symbols['resource_status'][1]==sizes['ResourceStats']and symbols['h1_dota_cache'][1]==sizes['DotaCacheDebug']
 report=dict(passed=False,physical_H1_tested=False,USB_opened=False,normal_inputs_only=True,game_state_ram_writes=0,bda_sha256=sha(build/'DOTA2.bda'),elf_sha256=sha(build/'DOTA2.elf'),driver_sha256=sha(__file__),deployed_resource=dict(filename='DOTA2.H1R',sha256=bundle_sha,bytes=a.bundle.stat().st_size,identity_basis='Frozen build metadata and root copy/readback receipt when provided; driver does not rewrite or rehash the device file'),PC_only_reference_assets=dict(directory=str(reference),SCENE_sha256=reference_scene_sha,TREE_legacy_sha256=reference_tree_sha,purpose='Offline crown hit-test and original navigation reference only; not deployed resource files'),linked_offline_evidence=evidence,layouts=layouts,sizes=sizes,checks=[],actions=[],screenshots=[],regrow_samples=[],profiles=[],resource_samples=[])
 def save():(out/'verification.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
 def check(name,ok,**e):report['checks'].append(dict(name=name,passed=bool(ok),**e));save();print(json.dumps(dict(check=name,passed=bool(ok))),flush=True);assert ok,name
 if a.offline_only:report.update(passed=True,offline_only=True);save();print(json.dumps(dict(passed=True,USB_opened=False,layouts_validated=True,resource_and_reference_identity_validated=True)));return
 sys.path.insert(0,str(a.debug_workspace.resolve()/'tools'));from h1_debug import Client
 with Client()as c:
  report['physical_H1_tested']=report['USB_opened']=True
  gp=symbols['dota_game'][0];vp=symbols['view'][0];tp=gp+layouts['DotaGame']['trees'];up=gp+layouts['DotaGame']['units'];itemp=gp+layouts['DotaGame']['items'];keys=('Z','X','C','V','B','N')
  def words(n,count):return list(struct.unpack('<'+'I'*count,c.read_memory(symbols[n][0],count*4)))
  def named_words(symbol,typename):
   data=c.read_memory(symbols[symbol][0],sizes[typename]);return{k:struct.unpack_from('<I',data,offset)[0]for k,offset in layouts[typename].items()}
  def resources():return named_words('resource_status','ResourceStats')
  def heap():return named_words('h1_dota_cache','DotaCacheDebug')
  def resource_sample(name,exiting=False):
   stats=resources();cache=heap();report['resource_samples'].append(dict(name=name,resource_stats=stats,cache_heap=cache));check(name+' resource ownership healthy',stats['last_error']==0 and(stats['mounted']==0 and stats['open_files']==0 and stats['heap_bytes']==0 if exiting else stats['mounted']==1 and stats['open_files']>0 and stats['heap_bytes']>0),resource_stats=stats,cache_heap=cache);return stats
  def integer(at):return struct.unpack('<i',c.read_memory(at,4))[0]
  def half(at):return struct.unpack('<H',c.read_memory(at,2))[0]
  def view(n):return integer(vp+layouts['DotaView'][n])
  def tick():return integer(gp+layouts['DotaGame']['tick'])
  def pos():return integer(up+layouts['DotaUnit']['x']),integer(up+layouts['DotaUnit']['y'])
  def camera():return view('camera_x'),view('camera_y')
  def cutbytes():return c.read_memory(tp+layouts['DotaTreesState']['cut'],268)
  def iscut(i):return bits(cutbytes(),i)
  def inv():return list(struct.unpack('<6i',c.read_memory(up+layouts['DotaUnit']['inventory'],24)))
  def charges(s):return half(itemp+layouts['DotaItemsState']['charges']+s*2)
  def wait(test,label,seconds=30):
   end=time.monotonic()+seconds
   while time.monotonic()<end:
    if test():return
    time.sleep(.08)
   raise AssertionError(label)
  def drawn():
   frame=words('h1_dota_debug',8)[1];wait(lambda:words('h1_dota_debug',8)[1]>=frame+2,'two complete frames')
  def key(n,draw=True):
   report['actions'].append(dict(key=n,receipt=c.press([n],ticks=20)));time.sleep(.18);c.release()
   if draw:drawn()
   save()
  def tap(x,y,draw=True):
   report['actions'].append(dict(tap=[x,y],receipt=c.tap(x,y,hold_seconds=.18)))
   if draw:drawn()
   save()
  def screen(n):wait(lambda:view('screen')==n,'screen'+str(n));drawn()
  def shot(n):
   path=out/(n+'.png');receipt=c.screenshot(path);report['screenshots'].append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),receipt=receipt));save()
  def profile(n,seconds=3):
   t0=time.monotonic();before=words('h1_dota_debug',8);t1=time.monotonic();start=(t0+t1)/2;audio=words('h1_dota_audio_debug',32);r_before=resources();heap_before=heap();time.sleep(seconds);t0=time.monotonic();after=words('h1_dota_debug',8);t1=time.monotonic();elapsed=(t0+t1)/2-start;now=words('h1_dota_audio_debug',32);tree=words('tree_stats',16);r_after=resources();heap_after=heap();report['profiles'].append(dict(name=n,fps=(after[1]-before[1])/elapsed,frames=after[1]-before[1],seconds=elapsed,pc_timing='Midpoints of completed-frame counter reads; earlier audio/resource/heap RPCs are inside the measured interval',tree=tree,audio_before=audio,audio_after=now,resource_stats_before=r_before,resource_stats_after=r_after,cache_heap_before=heap_before,cache_heap_after=heap_after));check(n+' tree/audio/resources healthy',tree[1]==1 and tree[2]==0 and now[1]==1 and now[2]==0 and now[6]==0 and now[5]>audio[5]and r_before['mounted']==r_after['mounted']==1 and r_before['last_error']==r_after['last_error']==0 and r_after['open_files']>0 and r_after['heap_bytes']>0)
  def approach(excluded):
   ident,path=original.route(pos(),cutbytes(),excluded);x,y=original.xy[ident];report['actions'].append(dict(tree_candidate=ident,world=[x,y],source_clear_path=path));save()
   for _ in range(50):
    px,py=pos()
    if(px-x)**2+(py-y)**2<=38**2:return ident
    cam=camera();best=None
    for wx,wy in path:
     sx,sy=original.screen(wx,wy,cam)
     if 22<=sx<458 and 40<=sy<190:best=(wx,wy,sx,sy)
    assert best,'No visible normal-input movement waypoint'
    wx,wy,sx,sy=best;tap(sx,sy);start=time.monotonic();old=pos()
    while time.monotonic()-start<3:
     nx,ny=pos()
     if(nx-wx)**2+(ny-wy)**2<=6**2 or(nx-x)**2+(ny-y)**2<=38**2:break
     time.sleep(.08)
    if pos()==old:raise AssertionError('Normal movement made no progress; no memory intervention attempted')
   raise AssertionError('Tree approach exceeded its normal-input budget')
  def use_tree(item,excluded):
   ident=approach(excluded);slots=inv();slot=slots.index(item);old=charges(slot);key(keys[slot]);check('Tree item enters point aiming',view('aim_item')==slot);point=original.crown(ident,cutbytes(),camera());assert point,'No frontmost original crown pixel visible from current camera';shot('before-tree-'+str(ident));before_tick=tick();tap(*point);wait(lambda:iscut(ident),'normal crown cut',5);after_tick=tick();deadline=half(tp+layouts['DotaTreesState']['regrow_at']+ident*2);remaining=(deadline-(after_tick&65535))&65535
   check('Original static tree cut by normal crown touch',not view('aim_item')>=0 and 3500<remaining<=3600,tree_id=ident,world=original.xy[ident],crown_screen=point,cut_tick_interval=[before_tick,after_tick],regrow_deadline_mod65536=deadline)
   if item==57:
    raw=c.read_memory(itemp+layouts['DotaItemsState']['effects'],128*sizes['DotaItemEffect']);regen=[]
    for i in range(128):
     off=i*sizes['DotaItemEffect'];tag=struct.unpack_from('<H',raw,off+layouts['DotaItemEffect']['tag'])[0];life=struct.unpack_from('<H',raw,off+layouts['DotaItemEffect']['ticks'])[0]
     if tag==57 and life:regen.append(life)
    check('Tango consumes exactly one charge and starts original16s buff',charges(slot)==old-1 and regen and 280<max(regen)<=320,charges_before=old,charges_after=charges(slot),remaining_regen_ticks=regen)
   else:check('Quelling Blade remains equipped and starts cooldown',inv()[slot]==17 and half(itemp+layouts['DotaItemsState']['cooldown']+slot*2)>0)
   shot('after-tree-'+str(ident));return ident,deadline
  def normal_exit(paused=False):
   if not paused:key('BACK');screen(2)
   tap(390,174,draw=False);wait(lambda:c.desktop_status()['menu_idle']==1,'normal exit to desktop',30)
   report['tree_after_exit']=words('tree_stats',16);report['audio_after_exit']=words('h1_dota_audio_debug',32);report['debug_after_exit']=c.status();resource_sample('normal-exit',exiting=True);report['cache_after_exit']=heap();check('Normal exit frees all unified resource and application heap',report['cache_after_exit']['released']==1 and report['cache_after_exit']['total_heap_bytes']==0);check('Normal exit frees all tree allocations and closes its file',report['tree_after_exit'][1:4]==[0,0,0]and report['tree_after_exit'][14]>0 and report['tree_after_exit'][15]==1);check('Normal exit releases audio and keeps debugger healthy',report['audio_after_exit'][1]==0 and report['audio_after_exit'][18]==0 and report['debug_after_exit']['state']==1 and report['debug_after_exit']['last_error']==0)
  try:
   check('Idle debug desktop before launch',c.status()['state']==1 and c.desktop_status()['menu_idle']==1);report['launch']=c.desktop_run(r'A:\应用\程序\DOTA2.bda');save();screen(0);resource_sample('startup-main-menu')
   sys.path.insert(0,str(ROOT/'sdk'));from h1_bda.resources import PAYLOAD_OFFSET
   payload=(build/'DOTA2.bda').read_bytes()[PAYLOAD_OFFSET:];identity={n:c.read_memory(symbols[n][0],32)==payload[symbols[n][0]-0x83c00020:symbols[n][0]-0x83c00020+32]for n in('tree_visual_open','resource_mount','resource_read_at','dota_trees_cut','dota_extra_tick','h1_dota_present')};check('Loaded new code matches frozen BDA samples',all(identity.values()),samples=identity);key('ENTER');screen(1);check('Fresh Axe game',integer(up+layouts['DotaUnit']['hero_id'])==0 and not any(cutbytes()));resource_sample('fresh-game')
   key('BACK');screen(2);frozen=c.read_memory(gp,sizes['DotaGame']);shot('pause-six-buttons')
   for k in('RIGHT','RIGHT','DOWN','LEFT','LEFT'):key(k)
   check('Six-button keyboard grid selects final bottom-left cell',view('pause_selected')==3);tap(90,174);screen(5);check('Help controls opened from pause',view('help_page')==0 and view('help_return_screen')==2);shot('help-controls');tap(230,46);check('Copyright/about tab active',view('help_page')==1);shot('help-about');tap(456,12);screen(2);tap(390,104);screen(6);key('BACK');screen(2)
   check('Pause/help/about/skill navigation leaves full Game unchanged',c.read_memory(gp,sizes['DotaGame'])==frozen);tap(240,174);screen(0);key('ENTER');screen(1);key('BACK');screen(2);tap(90,104);screen(1)
   key('M');screen(3);key('T');check('Axe recommended opening selects Tango57',view('shop_category')==8 and view('shop_item')==57);key('ENTER');tap(24,36)
   for _ in range(50):
    if view('shop_item')==17:break
    key('RIGHT')
   check('All catalogue selects Quelling Blade17',view('shop_category')==0 and view('shop_item')==17);key('ENTER');key('BACK');screen(1);wait(lambda:57 in inv()and 17 in inv(),'natural item delivery',30);check('Both purchased tree items delivered',57 in inv()and 17 in inv(),inventory=inv());key('K');tap(110,242)
   first,deadline=use_tree(57,set());second,deadline2=use_tree(17,{first});profile('trees-cut-live')
   report['physical_natural_regrow_tested']=not a.cut_only
   if a.cut_only:
    report['acceptance_scope']='Normal-input menu/help, purchasing/delivery, Tango and Quelling Blade cuts, live tree/audio/resource checks and normal cleanup; natural regrowth was not tested in this run.'
    key('BACK');screen(2)
    (out/'trees-cut-paused-game.bin').write_bytes(c.read_memory(gp,sizes['DotaGame']))
    (out/'trees-cut-paused-view.bin').write_bytes(c.read_memory(vp,sizes['DotaView']))
    normal_exit(paused=True);report['passed']=True;save();return
   start=time.monotonic();last_audio=time.monotonic();last_sample=None;first_regrown={}
   while iscut(first)or iscut(second):
    state=cutbytes();now=tick();sample=dict(game_tick=now,first_cut=bool(bits(state,first)),second_cut=bool(bits(state,second)),tree_stats=words('tree_stats',16));report['regrow_samples'].append(sample);last_sample=sample
    for ident in(first,second):
     if not bits(state,ident)and ident not in first_regrown:first_regrown[ident]=now
    report['first_regrown_ticks']=first_regrown;save()
    assert time.monotonic()-start<230,'Natural180-second regrowth timeout';assert sample['tree_stats'][1]==1 and sample['tree_stats'][2]==0
    if time.monotonic()-last_audio>15:profile('natural-regrow-wait',1);last_audio=time.monotonic()
    remaining=min((deadline-(now&65535))&65535,(deadline2-(now&65535))&65535);time.sleep(.05 if remaining<30 else .5)
   now=tick()
   for ident in(first,second):first_regrown.setdefault(ident,now)
   delta1=((first_regrown[first]&65535)-deadline)&65535;delta2=((first_regrown[second]&65535)-deadline2)&65535;report['first_regrown_ticks']=first_regrown;check('Both original trees naturally regrow after180 unpaused seconds',delta1<60 and delta2<60,tree_ids=[first,second],deadlines=[deadline,deadline2],first_observed_standing=first_regrown,observation_delay_ticks=[delta1,delta2],last_cut_sample=last_sample);shot('naturally-regrown-trees');profile('trees-regrown-live');normal_exit();report['passed']=True;save()
  except Exception as e:report['error']=str(e);save();raise
  finally:c.release();c.release_touch()
if __name__=='__main__':main()
