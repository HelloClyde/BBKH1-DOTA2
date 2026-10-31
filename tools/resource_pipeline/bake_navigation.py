"""Bake the installed Dota GNV grid and original static/dynamic obstructions.

GNV is the authoritative walk grid, not an image or hand-drawn forest map.
The 32-byte header and cell payload are checked exactly. Tree obstructions
come from matching original TRM records to visible default-layer entities.
Original world physics removes extra wall/cliff links between open cells.
Original NPC hulls block the twelve map-baked barracks. All twenty-two towers
and fourteen filler buildings use cell lists released on death.
No meshes, floats, trees or height arrays are needed by H1 at runtime.
"""
from pathlib import Path
import argparse,collections,csv,hashlib,heapq,json,math,os,re,struct,subprocess
from PIL import Image,ImageDraw
from gltf_assets import ROOT,np
from source2_vpk import VpkArchive

STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SIZE=256;CELL=16;BYTES=8192
SERVER_SHA256='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def relative(p):return Path(p).resolve().relative_to(ROOT).as_posix()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def run(command,env=None):
    p=subprocess.run(list(map(str,command)),capture_output=True,text=True,encoding='utf8',env=env)
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
    return p.stdout

def gnv(data):
    magic,step,hx,hy,w,h,ox,oy=struct.unpack_from('<Ifff4i',data)
    assert (magic,step,hx,hy)==(0xfadebead,64.,32.,32.)
    assert w==320 and h==328 and (ox,oy)==(-160,-168) and len(data)==32+w*h
    full=np.frombuffer(data,np.uint8,offset=32).reshape(h,w)
    # World cell center=(16*x+8,16*y+8); source=(4*(X-2048),4*(2048-Y)).
    # Floor(source/64)-offset is therefore (x+32,295-y), including Y reversal.
    flags=full[40:296,32:288][::-1].copy()
    assert flags.shape==(256,256)
    return flags,{'magic':hex(magic),'cell_source_units':step,'halfcell_source':[hx,hy],
                  'source_dimensions':[w,h],'source_offset_cells':[ox,oy],
                  'source_index_from_world_cell':['x+32','295-y'],'walk_bit':1,
                  'higher_flag_bits':'Retained as raw evidence; no unsupported semantic names assigned'}

def trees(raw,entities,all_initial_layers=False):
    magic,version,header,layers,count=struct.unpack_from('<5I',raw)
    assert (magic,version,header,layers)==(0x706d7274,1,64,3)
    cursor=header;names=[]
    for i in range(layers):end=raw.index(0,cursor);names.append(raw[cursor:end].decode());cursor=end+1
    assert names==['world_layer_radiant_base','world_layer_dire_base',''] and cursor==112
    assert len(raw)==cursor+count*12
    rows=[struct.unpack_from('<3i',raw,cursor+i*12) for i in range(count)]
    byxy=collections.defaultdict(list)
    for row in rows:byxy[row[:2]].append(row[2])
    out=[];default_count=0
    for e in entities:
        if e.get('classname')!='ent_dota_tree':continue
        sx,sy,sz=e['origin'];key=(round(sx),round(sy))
        layer={'world_layer_radiant_base':0,'world_layer_dire_base':1,'default':2}.get(e.get('_source_layer','default'),2)
        if not all_initial_layers and layer!=2:continue
        assert abs(sx-key[0])<.01 and abs(sy-key[1])<.01 and layer in byxy[key],e
        x=2048+sx/4;y=2048-sy/4
        if not (0<=x<4096 and 0<=y<4096):continue
        out.append({'hammer_id':e['hammerUniqueId'],'source_xyz':[sx,sy,sz],'world_xy':[x,y],
                    'trm_layer':layer,'model':e['model'],'tree_radius_world':16,'unit_clearance_world':6})
        if layer==2:default_count+=1
    assert len(out)>1800
    return out,{'header':{'version':version,'header_bytes':header,'layer_names':names,'records':count},'matched_visible_default_trees':default_count,
                'matched_visible_active_trees':len(out),'included_trm_layers':[0,1,2] if all_initial_layers else [2],
                'other_layers':'All initial active Radiant/Dire/default entity layers, with matching original TRM layer IDs' if all_initial_layers else 'Legacy default-only compatibility; initial active base trees require --all-initial-tree-layers and the combined entity input'}

def physics(text):
    triangles=re.findall(r'\bm_Triangles\s*=\s*#\[(.*?)\]',text,re.S)
    vertices=re.findall(r'\bm_Vertices\s*=\s*#\[(.*?)\]',text,re.S)
    assert len(triangles)==len(vertices)==4
    assert '"npcclip"' in text and '"playerclip"' in text
    result=[];receipts=[]
    for i in range(4):
        f=np.frombuffer(bytes.fromhex(triangles[i]),'<u4').reshape(-1,3)
        p=np.frombuffer(bytes.fromhex(vertices[i]),'<f4').reshape(-1,3)
        assert f.max()<len(p) and np.isfinite(p).all()
        receipts.append({'collision_attribute_index':i,'vertices':len(p),'triangles':len(f),
                         'interaction':['default','npcclip/playerclip','water','window'][i],'included':i<2})
        if i<2:
            q=np.column_stack((2048+p[:,0]/4,2048-p[:,1]/4,p[:,2]/4))
            result.append(q[f].astype('<f4'))
    return np.concatenate(result),receipts

def npc_units(raw):
    """Read the original KeyValues pairs without relying on nearby comments."""
    source=re.sub(r'//[^\n]*','',raw.decode('utf8'))
    tokens=[a or b for a,b in re.findall(r'"((?:\\.|[^"\\])*)"|([{}])',source)]
    cursor=0
    def block():
        nonlocal cursor
        result={}
        while cursor<len(tokens):
            key=tokens[cursor];cursor+=1
            if key=='}':return result
            value=tokens[cursor];cursor+=1
            result[key]=block() if value=='{' else value
        return result
    return block()['DOTAUnits']

def native_hulls(server):
    """Read the audited original NPC parser, not a community radius table.

    This build is deliberately pinned. A different installed engine must be
    re-audited instead of silently assuming old executable offsets still fit.
    API registration and getters below establish both field meanings.
    """
    data=server.read_bytes();digest=hashlib.sha256(data).hexdigest()
    assert digest==SERVER_SHA256,'Installed Dota server.dll changed: re-audit the native hull source before baking'
    pe=struct.unpack_from('<I',data,0x3c)[0];assert data[pe:pe+4]==b'PE\0\0'
    count=struct.unpack_from('<H',data,pe+6)[0];optional=pe+24
    assert struct.unpack_from('<H',data,optional)[0]==0x20b
    image_base=struct.unpack_from('<Q',data,optional+24)[0]
    table=optional+struct.unpack_from('<H',data,pe+20)[0];sections=[]
    for i in range(count):
        row=table+i*40;size,va,rawsize,offset=struct.unpack_from('<4I',data,row+8)
        sections.append((va,max(size,rawsize),offset))
    def get(rva,size):
        va,length,offset=next(s for s in sections if s[0]<=rva<s[0]+s[1])
        return data[offset+rva-va:offset+rva-va+size]
    pieces=[]
    def exact(name,rva,expected):
        raw=bytes.fromhex(expected);assert get(rva,len(raw))==raw,name
        pieces.append({'name':name,'rva':hex(rva),'virtual_address':hex(image_base+rva),'bytes_hex':raw.hex()})
        return raw
    # The parser loads the exact enum name, then branches to the two fields.
    for name,rva,code in [
        ('BARRACKS enum compare',0x18ab47d,'488d15c4663002483bda7452488bcbe85f4f500185c07446'),
        ('TOWER enum compare',0x18ab401,'488d15e0663002483bda0f8422010000488bcbe8d74f500185c00f8412010000'),
        ('FILLER enum compare',0x18ab495,'488d15c4663002483bda7421488bcbe8474f500185c07415')]:
        raw=exact(name,rva,code);target=rva+7+struct.unpack_from('<i',raw,3)[0]
        enum=b'DOTA_HULL_SIZE_'+name.split()[0].encode()
        assert get(target,len(enum)+1)==enum+b'\0'
    barracks=exact('BARRACKS writes radius/padding',0x18ab4db,'c787a40c000000001043c787a80c000000008041')
    tower=exact('TOWER writes radius/padding',0x18ab533,'48c787a40c000000001043')
    filler=exact('FILLER writes radius/padding',0x18ab4c2,'c787a40c00000000c042c787a80c000000008041')
    for name,rva,code in [
        ('GetHullRadius registration',0x2516447,'488d052209c10148894310488d054709c101488903488d053d09c10148894308c6431801e84059edffc744242800000000488d05417f7afe4889442420'),
        ('GetCollisionPadding registration',0x25164e3,'488d05c608c10148894310488d05fb08c101488bcb488903488d05ee08c10148894308c6431801e8a158edffc744242800000000488d059277bdff4889442420'),
        ('GetPaddedCollisionRadius registration',0x2516584,'48897030488d057908c10148894310488d05ae08c101488903488d05a408c10148894308c6431801e8ff57edff488d05d8cd020048894338488d0dfdeb7afe48894b40'),
        ('GetHullRadius getter',0xcbe3c0,'f30f1081a40c0000c3'),
        ('GetCollisionPadding getter',0x20edcb0,'f30f1081a80c0000c3'),
        ('GetPaddedCollisionRadius getter',0xcc51c0,'f30f1081a80c0000f30f5881a40c0000c3')]:exact(name,rva,code)
    return {'server_dll':str(server),'server_sha256':digest,'image_base':hex(image_base),'code':pieces,
            'hulls':{'DOTA_HULL_SIZE_BARRACKS':{'radius_source':struct.unpack_from('<f',barracks,6)[0],'padding_source':struct.unpack_from('<f',barracks,16)[0]},
                     'DOTA_HULL_SIZE_TOWER':{'radius_source':struct.unpack_from('<f',tower,7)[0],'padding_source':0.},
                     'DOTA_HULL_SIZE_FILLER':{'radius_source':struct.unpack_from('<f',filler,6)[0],'padding_source':struct.unpack_from('<f',filler,16)[0]}},
            'semantics':'GetHullRadius reads0xca4; GetCollisionPadding reads0xca8; GetPaddedCollisionRadius returns their sum. Exact original API registrations/getters and enum parser branches are retained.',
            'fort_exclusion':'Original BUILDING parses radius0/padding16 and models have no PHYS. Native fort obstacle shape remains unproved; do not fabricate a rectangle or128 radius.'}

def buildings(vpk,entities,base_walk,server,raw):
    script='scripts/npc/npc_units.txt';script_raw=vpk.read(script);(raw/'npc_units.txt').write_bytes(script_raw)
    units=npc_units(script_raw);source=native_hulls(server);identities=[vpk.identity(script)];models={}
    yy,xx=np.mgrid[:SIZE,:SIZE];wx=xx*CELL+8;wy=yy*CELL+8
    def footprint(x,y,radius):
        # Closed cell-square / swept circular hull intersection, not just
        # cell centres: no legal point can hide inside a boundary cell.
        dx=np.maximum(np.abs(wx-x)-CELL/2,0);dy=np.maximum(np.abs(wy-y)-CELL/2,0)
        return dx*dx+dy*dy<=radius*radius
    def record(e,kind):
        unit=units[e['MapUnitName']];hull=source['hulls'][unit['BoundsHullName']]
        assert e['scales']==[1.,1.,1.] and 'ModelScale' not in unit
        model=re.fullmatch(r'resource_name:"([^"]+)"',e['model']).group(1)
        if model not in models:
            path=model+'_c';data=vpk.read(path);(raw/Path(path).name).write_bytes(data);size,version,_,offset,count=struct.unpack_from('<IHHII',data)
            assert size==len(data) and version==12
            tags=[struct.unpack_from('<4s',data,8+offset+i*12)[0].decode('ascii') for i in range(count)]
            assert 'PHYS' not in tags,'Re-audit building with native physics hull'
            identity=vpk.identity(path);identities.append(identity);models[model]={'identity':identity,'compiled_blocks':tags,'physics':'No PHYS block; navigation uses original NPC BoundsHullName'}
        x=2048+e['origin'][0]/4;y=2048-e['origin'][1]/4
        if kind!='static-barracks':x=math.floor(x+.5);y=math.floor(y+.5) # Same source anchors as game.c.
        padding=hull['padding_source']
        radius=(hull['radius_source']+padding+24)/4
        return {'kind':kind,'hammer_id':e['hammerUniqueId'],'targetname':e['targetname'],'npc_unit':e['MapUnitName'],'model':model,
                'source_xyz':e['origin'],'angles':e['angles'],'scales':e['scales'],'world_xy':[x,y],
                'bounds_hull_name':unit['BoundsHullName'],'radius_source':hull['radius_source'],'padding_source':padding,
                'hero_clearance_source':24,'collision_radius_world':radius,'rounding':'unrounded original entity origin' if kind=='static-barracks' else 'nearest world unit, half upward'}
    barracks=[record(e,'static-barracks') for e in entities if e.get('classname')=='npc_dota_barracks'];assert len(barracks)==12
    blocked=np.zeros_like(base_walk)
    for row in barracks:
        mask=footprint(*row['world_xy'],row['collision_radius_world']);blocked|=mask
        row['blocked_cells']=int(mask.sum());row['newly_blocked_cells']=int((mask&base_walk).sum())
    walk=base_walk&~blocked;towers=[];cells=[];offsets=[0]
    for team,faction in enumerate(('goodguys','badguys')):
        for lane,lane_name in enumerate(('top','mid','bot')):
            for tier in range(2):
                name=f'npc_dota_{faction}_tower{tier+1}_{lane_name}'
                e,= [e for e in entities if e.get('classname')=='npc_dota_tower' and e.get('MapUnitName')==name]
                row=record(e,'dynamic-tower');row.update(team=team,lane=lane,tier=tier,mask_bit=len(towers))
                mask=footprint(*row['world_xy'],row['collision_radius_world'])&walk
                indices=np.flatnonzero(mask).astype(int).tolist();row['base_walkable_cells']=indices
                cells.extend(indices);offsets.append(len(cells));towers.append(row)
    high=[]
    for team,faction in enumerate(('goodguys','badguys')):
        for lane,lane_name in enumerate(('top','mid','bot')):
            e,=[e for e in entities if e.get('MapUnitName')==f'npc_dota_{faction}_tower3_{lane_name}']
            high.append((e,team,lane,2))
        high.extend((e,team,1,3) for e in entities if e.get('MapUnitName')==f'npc_dota_{faction}_tower4')
    for e,team,lane,tier in high:
        row=record(e,'dynamic-tower');row.update(team=team,lane=lane,tier=tier,mask_bit=len(towers))
        indices=np.flatnonzero(footprint(*row['world_xy'],row['collision_radius_world'])&walk).astype(int).tolist()
        row['base_walkable_cells']=indices;cells.extend(indices);offsets.append(len(cells));towers.append(row)
    fillers=[]
    for e in entities:
        if e.get('classname')!='npc_dota_building':continue
        row=record(e,'dynamic-filler');row.update(team=0 if e['MapUnitName']=='npc_dota_goodguys_fillers' else 1,mask_bit=len(fillers))
        indices=np.flatnonzero(footprint(*row['world_xy'],row['collision_radius_world'])&walk).astype(int).tolist()
        row['base_walkable_cells']=indices;cells.extend(indices);offsets.append(len(cells));fillers.append(row)
    assert len(towers)==22 and len(fillers)==14
    excluded=[{'classname':e['classname'],'targetname':e.get('targetname'),'npc_unit':e.get('MapUnitName'),'source_xyz':e['origin'],
               'reason':'Not a simulated tower; never bake a permanent obstacle for an unimplemented destructible building' if e['classname']=='npc_dota_tower' else source['fort_exclusion']}
              for e in entities if e.get('classname') in ('npc_dota_fort','npc_dota_tower') and e.get('MapUnitName') not in {t['npc_unit'] for t in towers}]
    info={'complete':True,'rasterization':'Closed16x16 world cell-square intersects source circular hull plus original24-Source hero clearance; no guessed image rectangles.',
          'static_barracks':barracks,'dynamic_towers':towers,'dynamic_fillers':fillers,'excluded_dynamic_native_unknown':excluded,'models':models,
          'static_cells_removed':int((blocked&base_walk).sum()),'dynamic_tower_cell_indices':len(cells),'dynamic_tower_list_bytes':(len(cells)+len(offsets))*2,
          'runtime':'Only the existing walk plane is mutable. Dynamic lists contain exclusively static-base walkable cells. Restore all listed cells, then block living towers when mask changes; no second bitmap.',
          'source_hulls':source}
    return walk,blocked,info,identities,cells,offsets

def source_passages(base_walk,raw_walk,base_right,base_down,building_info):
    """Recover only source-clear narrow passages lost to square quantisation.

    Search within three source-grid cells of a newly isolated cell, in both
    directions perpendicular to its nearest two original barracks. Minimise
    newly reopened cells, then path length. Every node and cardinal edge must
    be outside the original swept circles and legal in original GNV/TRM/PHYS.
    """
    def labels(walk):
        result=np.full((SIZE,SIZE),-1,np.int32);sizes=[]
        for sy,sx in zip(*np.where(walk)):
            if result[sy,sx]>=0:continue
            group=len(sizes);q=collections.deque([(int(sx),int(sy))]);result[sy,sx]=group;count=0
            while q:
                x,y=q.popleft();count+=1
                for xx,yy,closed in [(x+1,y,base_right[y,x]),(x-1,y,base_right[y,x-1] if x else True),(x,y+1,base_down[y,x]),(x,y-1,base_down[y-1,x] if y else True)]:
                    if not closed and 0<=xx<SIZE and 0<=yy<SIZE and walk[yy,xx] and result[yy,xx]<0:result[yy,xx]=group;q.append((xx,yy))
            sizes.append(count)
        return result,sizes
    before,before_sizes=labels(base_walk);after,after_sizes=labels(raw_walk)
    original_main=int(np.argmax(before_sizes));new_main=int(np.argmax(after_sizes))
    orphan=raw_walk&(before==original_main)&(after!=new_main)
    carve=np.zeros_like(raw_walk);circle_records=[];paths=[]
    require_groups=sorted(set(after[orphan].astype(int)))
    # Current source map has exactly one one-cell quantisation orphan. Any
    # future map change requires a fresh source audit rather than broad carve.
    assert len(require_groups)==1 and int(orphan.sum())==1
    for group in require_groups:
        sy,sx=map(int,np.argwhere(after==group)[0]);start=sy*SIZE+sx;ox=sx*CELL+8;oy=sy*CELL+8
        pair=sorted(building_info['static_barracks'],key=lambda b:(b['world_xy'][0]-ox)**2+(b['world_xy'][1]-oy)**2)[:2]
        a,b=pair;dx=b['world_xy'][0]-a['world_xy'][0];dy=b['world_xy'][1]-a['world_xy'][1];distance=math.hypot(dx,dy)
        assert a['collision_radius_world']+b['collision_radius_world']<distance<2*46+4*CELL
        normal=(-dy/distance,dx/distance)
        circles=[]
        for row in pair:
            x,y=row['world_xy'];radius=row['collision_radius_world']
            # Original centres rounded automatically to1/32 world. +3/32
            # radius conservatively covers centre and projection truncation.
            circle={'hammer_id':row['hammer_id'],'world_xy':row['world_xy'],'radius_world':radius,
                    'centre_q5':[math.floor(x*32+.5),math.floor(y*32+.5)],'radius_q5':math.ceil(radius*32)+3}
            circles.append(circle);circle_records.append(circle)
        def trunc(n,d):return (abs(n)//d)*(1 if n>=0 else -1)
        def safe_segment(x,y,xx,yy):
            ax=x*32;ay=y*32;vx=(xx-x)*32;vy=(yy-y)*32;length=vx*vx+vy*vy
            for c in circles:
                cx,cy=c['centre_q5'];projection=(cx-ax)*vx+(cy-ay)*vy
                if not length or projection<=0:px,py=ax,ay
                elif projection>=length:px,py=xx*32,yy*32
                else:px,py=ax+trunc(vx*projection,length),ay+trunc(vy*projection,length)
                if (px-cx)**2+(py-cy)**2<=c['radius_q5']**2:return False
            return True
        for sign in (-1,1):
            queue=[(0,0,start)];best={start:(0,0)};parent={};end=None
            while queue:
                count,steps,cell=heapq.heappop(queue)
                if best[cell]!=(count,steps):continue
                x=cell%SIZE;y=cell//SIZE;px=x*CELL+8;py=y*CELL+8
                if after[y,x]==new_main and sign*((px-ox)*normal[0]+(py-oy)*normal[1])>=CELL:
                    end=cell;break
                for xx,yy,closed in [(x+1,y,base_right[y,x]),(x-1,y,base_right[y,x-1] if x else True),(x,y+1,base_down[y,x]),(x,y-1,base_down[y-1,x] if y else True)]:
                    if closed or not(0<=xx<SIZE and 0<=yy<SIZE and base_walk[yy,xx]):continue
                    qx=xx*CELL+8;qy=yy*CELL+8
                    if (qx-ox)**2+(qy-oy)**2>(3*CELL)**2 or not safe_segment(px,py,qx,qy):continue
                    candidate=(count+int(not raw_walk[yy,xx]),steps+1);other=yy*SIZE+xx
                    if candidate<best.get(other,(1<<30,1<<30)):best[other]=candidate;parent[other]=cell;heapq.heappush(queue,(*candidate,other))
            assert end is not None,'No strictly source-clear narrow building passage'
            path=[];cell=end
            while True:
                path.append(cell)
                if not raw_walk[cell//SIZE,cell%SIZE]:carve[cell//SIZE,cell%SIZE]=True
                if cell==start:break
                cell=parent[cell]
            path.reverse();paths.append({'direction':sign,'cells':path,'world_centres':[[c%SIZE*CELL+8,c//SIZE*CELL+8] for c in path],
                                         'new_cells':best[end][0],'source_gnv_tree_physics_edges_open':True,'source_swept_circles_clear':True})
    indices=np.flatnonzero(carve).astype(int).tolist();assert len(indices)==2
    walk=raw_walk|carve
    right=base_right.copy();down=base_down.copy()
    right[:,:-1]|=~(walk[:,:-1]&walk[:,1:]);down[:-1]|=~(walk[:-1]&walk[1:])
    # All new open graph edges are tested using the same conservatively
    # rounded original circles. No general no-corner-cut rule is relaxed.
    for cell in indices:
        x=cell%SIZE;y=cell//SIZE
        for xx,yy in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
            if not(0<=xx<SIZE and 0<=yy<SIZE and walk[yy,xx]):continue
            if not safe_segment(x*CELL+8,y*CELL+8,xx*CELL+8,yy*CELL+8):
                if y==yy:right[y,min(x,xx)]=True
                else:down[min(y,yy),x]=True
    info={'raw_conservative_barracks_cells':building_info['static_cells_removed'],'carved_cell_count':len(indices),'cells':indices,
          'source_circles':circle_records,'paths':paths,'point_and_segment_runtime_check':'Only local carved cells/segments use exact integer nearest-point tests against these two original circles. Other cells retain conservative rasterisation.',
          'fixed_point':'Q5 centres generated from exact original world origin by half-up rounding; radius ceil(source_radius*32)+3. Maximum centre error sqrt(2)/64 and projection truncation sqrt(2)/32 are below3/32-world safety margin.',
          'normal_corner_rule':'Unchanged: both orthogonal routes must be open for a diagonal. No custom diagonal shortcut.'}
    return walk,right,down,carve,info

def header(path,walk,right,down,tower_cells,tower_offsets,passages):
    text=['/* Generated from original Dota GNV/TRM/PHYS/NPC hulls;1=walkable, edge1=blocked. */',
          '#ifndef H1_DOTA_NAV_DATA_H','#define H1_DOTA_NAV_DATA_H',
          '#define DOTA_NAV_SIZE 256','#define DOTA_NAV_CELL 16','#define DOTA_NAV_BITMAP_BYTES 8192']
    hashes={}
    for name,array in [('walk',walk),('right',right),('down',down)]:
        b=np.packbits(array.astype(np.uint8).ravel(),bitorder='little').tobytes();assert len(b)==BYTES
        hashes[name]=hashlib.sha256(b).hexdigest();qualifier='' if name=='walk' else 'const '
        text.append(f'static {qualifier}unsigned char dota_nav_{name}_bits[8192] = {{')
        text.extend(','.join(str(v) for v in b[i:i+32])+',' for i in range(0,len(b),32));text.append('};')
    text.extend(['#define DOTA_NAV_TOWER_COUNT 22','#define DOTA_NAV_FILLER_COUNT 14','#define DOTA_NAV_STRUCTURE_COUNT 36',f'#define DOTA_NAV_TOWER_CELL_COUNT {len(tower_cells)}'])
    for name,values in [('tower_offsets',tower_offsets),('tower_cells',tower_cells)]:
        text.append(f'static const unsigned short dota_nav_{name}[{len(values)}] = {{')
        text.extend(','.join(str(v) for v in values[i:i+16])+',' for i in range(0,len(values),16));text.append('};')
    values=passages['cells'];circles=passages['source_circles']
    text.extend([f'#define DOTA_NAV_PASSAGE_CELLS {len(values)}',f'#define DOTA_NAV_PASSAGE_CIRCLES {len(circles)}',
                 f'#define DOTA_NAV_PASSAGE_MIN_X {min(v%SIZE for v in values)*CELL}',f'#define DOTA_NAV_PASSAGE_MAX_X {(max(v%SIZE for v in values)+1)*CELL}',
                 f'#define DOTA_NAV_PASSAGE_MIN_Y {min(v//SIZE for v in values)*CELL}',f'#define DOTA_NAV_PASSAGE_MAX_Y {(max(v//SIZE for v in values)+1)*CELL}',
                 f'static const unsigned short dota_nav_passage_cells[{len(values)}] = {{'+','.join(map(str,values))+'};',
                 f'static const int dota_nav_passage_circles[{len(circles)}][3] = {{'])
    text.extend('{'+','.join(map(str,(*c['centre_q5'],c['radius_q5'])))+'},' for c in circles);text.append('};')
    text.append('#endif');path.write_text('\n'.join(text)+'\n',encoding='ascii');return hashes

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'private/navigation')
    p.add_argument('--header',type=Path,default=ROOT/'private/generated/dota_nav.h');p.add_argument('--cc',type=Path,default=Path('C:/msys64/ucrt64/bin/gcc.exe'))
    p.add_argument('--entities',type=Path,default=ROOT/'private/source2/reports/entity-default-parsed.json')
    p.add_argument('--all-initial-tree-layers',action='store_true')
    p.add_argument('--vrf',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    p.add_argument('--server',type=Path,default=STEAM/'bin/win64/server.dll');a=p.parse_args()
    out=a.output;out.mkdir(parents=True,exist_ok=True);raw=out/'raw';raw.mkdir(exist_ok=True);binpath=out/'bin';binpath.mkdir(exist_ok=True)
    vpk=VpkArchive(STEAM/'maps/dota.vpk');identities=[]
    for name in ('maps/dota.gnv','maps/dota.trm','maps/dota/world_physics.vmdl_c'):
        data=vpk.read(name);(raw/Path(name).name).write_bytes(data);identities.append(vpk.identity(name))
    flags,grid_info=gnv((raw/'dota.gnv').read_bytes());walk=(flags&1)!=0;original_walk=walk.copy();np.save(out/'source-flags.npy',flags)
    entities_path=a.entities;entities=read(entities_path)
    tree_records,tree_info=trees((raw/'dota.trm').read_bytes(),entities,a.all_initial_tree_layers)
    yy,xx=np.mgrid[:SIZE,:SIZE];wx=xx*CELL+8;wy=yy*CELL+8;tree_blocked=np.zeros_like(walk)
    for tree in tree_records:
        x,y=tree['world_xy'];tree_blocked|=(wx-x)**2+(wy-y)**2<=22**2
    walk&=~tree_blocked;np.save(out/'tree-blocked.npy',tree_blocked);source_walk=walk.copy()
    tree_cells_removed=int(np.count_nonzero(original_walk&~walk))
    walk,building_blocked,building_info,building_ids,tower_cells,tower_offsets=buildings(VpkArchive(STEAM/'pak01_dir.vpk'),entities,walk,a.server,raw)
    identities.extend(building_ids);np.save(out/'building-blocked.npy',building_blocked)
    env=dict(os.environ);env['PATH']=str(a.cc.parent)+os.pathsep+env.get('PATH','')
    height_input=ROOT/'build/conversion/baked-map/height-triangles.f32';source_tri=np.fromfile(height_input,'<f4').reshape(-1,3,3)
    source_tri[:,:,:2]=(source_tri[:,:,:2]-8)*(4095/(16*255));source_tri.astype('<f4').tofile(out/'height-triangles.f32')
    run([a.cc,'-std=c99','-O2','-ffp-contract=off',ROOT/'tools/height_grid.c','-lm','-o',binpath/'height_grid.exe'],env)
    height_log=run([binpath/'height_grid.exe',out/'height-triangles.f32',out/'height.f32',256],env);(out/'height-log.txt').write_text(height_log,encoding='utf8')
    height=np.fromfile(out/'height.f32','<f4').reshape(SIZE,SIZE);unknown=height< -1e20
    assert not np.any(unknown&walk),'GNV walkable cell lacks source ground triangle'
    np.save(out/'height.npy',height)
    text=run([a.vrf,'-i',raw/'world_physics.vmdl_c','-b','PHYS']);(raw/'world-physics-PHYS.txt').write_text(text,encoding='utf8')
    tri,phys_info=physics(text);tri.tofile(out/'physics-triangles.f32');source_walk.astype(np.uint8).tofile(out/'source-walk.u8')
    run([a.cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',ROOT/'tools/nav_collision.c','-lm','-o',binpath/'nav_collision.exe'],env)
    phys_log=run([binpath/'nav_collision.exe',out/'physics-triangles.f32',out/'height.f32',out/'source-walk.u8',out/'edges.u8',out/'physics-edges.csv'],env)
    edge_info=json.loads(phys_log);edge_data=np.fromfile(out/'edges.u8',np.uint8).reshape(2,SIZE,SIZE);right,down=edge_data.astype(bool)
    walk,right,down,carve,passage_info=source_passages(source_walk,walk,right,down,building_info)
    building_info['source_passages']=passage_info;building_info['final_static_cells_removed']=int(np.count_nonzero(source_walk&~walk))
    # Rebuild lists from the corrected static base, never from raw GNV or the
    # old conservative plane. Thus death restores exactly original legal cells.
    tower_cells=[];tower_offsets=[0]
    for row in building_info['dynamic_towers']+building_info['dynamic_fillers']:
        x,y=row['world_xy'];radius=row['collision_radius_world'];dx=np.maximum(abs(wx-x)-8,0);dy=np.maximum(abs(wy-y)-8,0)
        cells=np.flatnonzero((dx*dx+dy*dy<=radius**2)&walk).astype(int).tolist();row['base_walkable_cells']=cells
        tower_cells.extend(cells);tower_offsets.append(len(tower_cells))
    np.save(out/'building-passages.npy',carve);np.save(out/'walk.npy',walk);walk.astype(np.uint8).tofile(out/'walk.u8')
    (out/'buildings.json').write_text(json.dumps(building_info,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    np.stack((right,down)).astype(np.uint8).tofile(out/'edges.u8')
    np.save(out/'right.npy',right);np.save(out/'down.npy',down);hashes=header(a.header,walk,right,down,tower_cells,tower_offsets,passage_info)
    evidence=[]
    with (out/'physics-edges.csv').open(encoding='ascii') as stream:
        for row in csv.DictReader(stream):
            index=int(row['triangle']);cx=int(row['cell_x']);cy=int(row['cell_y']);is_down=int(row['down'])
            ax=cx*16+8;ay=cy*16+8;bx=ax+(0 if is_down else 16);by=ay+(16 if is_down else 0)
            evidence.append({'triangle':index,'triangle_world_xyz':tri[index].astype(float).tolist(),
                             'cell':[cx,cy],'direction':'down' if is_down else 'right',
                             'world_endpoints':[[ax,ay,float(row['ground_a'])],[bx,by,float(row['ground_b'])]],
                             'body_ray_height':float(row['body_z']),'transverse_offset_world':int(row['transverse_step'])*6,
                             'intersection_world_xyz':[float(row[k]) for k in ('hit_x','hit_y','hit_z')],
                             'original_gnv_flags':[int(flags[cy,cx]),int(flags[cy+is_down,cx+1-is_down])]})
    assert len(evidence)==edge_info['physics_right_edges_added']+edge_info['physics_down_edges_added']
    (out/'physics-edges.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf8')
    (out/'trees.json').write_text(json.dumps(tree_records,indent=2)+'\n',encoding='utf8')
    image=np.zeros((SIZE,SIZE,3),np.uint8);image[original_walk]=(95,130,95);image[~original_walk]=(95,42,40);image[tree_blocked&original_walk]=(31,70,35);image[walk]=(115,166,109);image[building_blocked&original_walk]=(173,73,146)
    im=Image.fromarray(image).resize((1024,1024),Image.Resampling.NEAREST);draw=ImageDraw.Draw(im)
    for y,x in zip(*np.where(walk&np.roll(walk,-1,axis=1)&right)):
        if x<SIZE-1:draw.line(((x+1)*4,(y+.25)*4,(x+1)*4,(y+.75)*4),fill=(255,193,70))
    for y,x in zip(*np.where(walk&np.roll(walk,-1,axis=0)&down)):
        if y<SIZE-1:draw.line(((x+.25)*4,(y+1)*4,(x+.75)*4,(y+1)*4),fill=(255,193,70))
    im.save(out/'navigation.png')
    files=[ROOT/'tools/bake_navigation.py',ROOT/'tools/nav_collision.c',ROOT/'tools/height_grid.c',ROOT/'tools/source2_vpk.py',entities_path,height_input]
    receipt={'complete':True,'grid':grid_info,'world_size':4096,'cell_world_units':16,'dimensions':[256,256],
             'runtime_bytes':24576,'dynamic_tower_list_bytes':building_info['dynamic_tower_list_bytes'],'bitmap_bit_order':'little,linear index y*256+x','walkable_cells':int(walk.sum()),'original_walkable_cells':int(original_walk.sum()),
             'tree_cells_removed':tree_cells_removed,'raw_static_building_cells_removed':building_info['static_cells_removed'],'static_building_cells_removed':building_info['final_static_cells_removed'],'source_passage_carves':passage_info['carved_cell_count'],'source_passage_data_bytes':28,'buildings':'buildings.json', 'trees':tree_info,'physics_meshes':phys_info,'physics_edges':edge_info,
             'original_resources':identities,'header':relative(a.header),'header_sha256':sha(a.header),'bitmap_sha256':hashes,
             'source_sha256':{relative(f):sha(f) for f in files},'tools':{'vrf_sha256':sha(a.vrf),'gcc_sha256':sha(a.cc)},
             'files':{relative(out/name):sha(out/name) for name in ('source-flags.npy','tree-blocked.npy','building-blocked.npy','building-passages.npy','buildings.json','walk.npy','height.npy','right.npy','down.npy','trees.json','physics-edges.json')},
             'clearance':{'body_radius_world':6,'tree_static_radius_world':16,'physical_ray_heights_world':[8,20,32],'stepheight_world':32,'stepheight_source':'default worldspawn _dotatilegrid_stepheight128'},
             'limitations':'Fixed original trees, twelve original barracks and static world collision; all22 towers and14 fillers release original legal cells on death. Native fort obstacle shape remains unproved and is not guessed. No tree cutting/regrowth, moving-unit collision or exact Source2 capsule solver.'}
    (out/'manifest.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({k:receipt[k] for k in ('complete','walkable_cells','tree_cells_removed','physics_edges','header_sha256')}))
if __name__=='__main__':main()
