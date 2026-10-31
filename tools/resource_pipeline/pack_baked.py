"""Pack original fixed-view assets and external item/UI/font tables into HBK12."""
from pathlib import Path
import argparse,hashlib,json,re,struct
from PIL import Image
from gltf_assets import np,ROOT

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf8'))
def rgb565(rgb):
    a=rgb.astype(np.uint16)
    return (((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)).astype('<u2')

def refresh_external_assets(path,folder):
    """Replace only the resource footer; all graphic offsets/bytes stay fixed."""
    import shutil
    folder=folder.resolve()
    assets=read(folder/'manifest.json');assert assets['complete'];offsets={}
    payloads={name:(folder/name).read_bytes() for name in ('catalog.hit','heroes.uir','catalog.fnt')}
    for name,data in payloads.items():assert hashlib.sha256(data).hexdigest()==assets['files'][name]['sha256']
    with path.open('r+b') as f:
        header=list(struct.unpack('<16I',f.read(64)));assert header[0]==0x314b4248 and header[1] in (11,12) and path.stat().st_size==header[2]
        f.seek(header[15]+32);iar=list(struct.unpack('<8I',f.read(32)));assert iar[:3]==[0x31524149,1,32]
        f.seek(iar[6]);font=struct.unpack('<8I',f.read(32));assert font[0]==0x31544e46 and iar[6]+font[2]==header[2]
        assert header[15]+64<iar[3]<iar[5]<iar[6]
        same=True
        for name,offset in zip(payloads,(iar[3],iar[5],iar[6])):
            offsets[name]=offset;f.seek(offset)
            if f.read(len(payloads[name]))!=payloads[name]:same=False
        if not same:
            # Random writes can rehydrate a WOF/LZX-compressed file. Reserve
            # its entire logical size before touching the known-good footer.
            required=header[2]+sum(map(len,payloads.values()))+1048576
            if shutil.disk_usage(path.parent).free<required:raise OSError('Not enough free space to safely refresh a compressed pack: need '+str(required)+' bytes')
            f.seek(iar[3])
            for name,data in payloads.items():
                f.write(bytes((-f.tell())&3));offsets[name]=f.tell();f.write(data)
            header[2]=f.tell();f.truncate();iar[3]=offsets['catalog.hit'];iar[4]=len(payloads['catalog.hit']);iar[5]=offsets['heroes.uir'];iar[6]=offsets['catalog.fnt']
            f.seek(0);f.write(struct.pack('<16I',*header));f.seek(header[15]+32);f.write(struct.pack('<8I',*iar))
        f.seek(header[9]);directory=list(struct.iter_unpack('<IIHHhh8H',f.read(header[10]*32)))
    report_path=path.with_suffix('.json');baseline=ROOT/'dist/baked/DOTA2/SCENE.json'
    report=read(report_path) if report_path.exists() and report_path.stat().st_size else read(baseline) if baseline.exists() else {}
    report.update(format='HBK'+str(header[1]),version=header[1],pack=str(path.relative_to(ROOT)),bytes=header[2],sha256=sha(path),sprite_frames=header[10],sprite_groups=len({r[6:9] for r in directory}),sprite_payload_bytes=sum(r[1] for r in directory),shadow_frames=sum(bool(r[11]&4) for r in directory))
    if header[1]==11:report.update(extra_hero_frames=3240,item_entity_frames=1808,item_entity_models=5,dedicated_skills=24,ranged_attack_styles=8,hero_shadow_frames=7680)
    report['item_assets']=dict(items=assets['items'],glyphs=assets['glyphs'],offsets=offsets,files=assets['files']);report.setdefault('source_manifests',{})[str((folder/'manifest.json').relative_to(ROOT))]=sha(folder/'manifest.json')
    for manifest in ('extra-heroes','item-entities'):report['source_manifests'][str(Path('private/baked')/manifest/'manifest.json')]=sha(ROOT/'private/baked'/manifest/'manifest.json')
    report.setdefault('source_sha256',{})[Path(__file__).name]=sha(__file__);report['external_assets_refreshed_without_graphic_changes']=True
    temporary=report_path.with_suffix('.json.tmp');temporary.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');temporary.replace(report_path)
    print(json.dumps(dict(complete=True,pack_sha256=report['sha256'],bytes=header[2],glyphs=assets['glyphs'])))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--map',type=Path,default=ROOT/'private/baked/v022/map-lit')
    parser.add_argument('--sprites',type=Path,default=ROOT/'private/baked/sprites/manifest.json')
    parser.add_argument('--effects',type=Path,default=ROOT/'private/baked/effects/manifest.json')
    parser.add_argument('--skills',type=Path,default=ROOT/'private/baked/skills/manifest.json')
    parser.add_argument('--projectiles',type=Path,default=ROOT/'private/baked/projectiles/manifest.json')
    parser.add_argument('--components',type=Path,default=ROOT/'private/baked/skill-components/manifest.json')
    parser.add_argument('--jungle',type=Path,default=ROOT/'private/baked/jungle/manifest.json')
    parser.add_argument('--fountains',type=Path,default=ROOT/'private/baked/v022/fountain/manifest.json')
    parser.add_argument('--roshan',type=Path,default=ROOT/'private/baked/roshan/manifest.json')
    parser.add_argument('--courier',type=Path,default=ROOT/'private/baked/courier/manifest.json')
    parser.add_argument('--map-completion',type=Path,default=ROOT/'private/baked/map-completion/manifest.json')
    parser.add_argument('--extra-heroes',type=Path,default=ROOT/'private/baked/extra-heroes/manifest.json')
    parser.add_argument('--item-assets',type=Path,default=ROOT/'private/items/assets-v022')
    parser.add_argument('--roster-bodies',type=Path,default=ROOT/'private/baked/roster-heroes/body-manifest.json')
    parser.add_argument('--roster-effects',type=Path,default=ROOT/'private/baked/roster-fx/manifest.json')
    parser.add_argument('--complete-jungle',type=Path,default=ROOT/'private/baked/jungle-complete/manifest.json')
    parser.add_argument('--item-entities',type=Path,default=ROOT/'private/baked/item-entities/manifest.json')
    parser.add_argument('--water',type=Path,default=ROOT/'private/baked/v022/water-lit')
    parser.add_argument('--static-water',action='store_true',help='Legacy HBK1 without the water animation extension')
    parser.add_argument('--assets-only',action='store_true',help='Refresh an existing HBK11 external item/UI/font footer without touching any graphic bytes')
    parser.add_argument('--output',type=Path,default=ROOT/'dist/baked/DOTA2/SCENE.H1P')
    args=parser.parse_args()
    args.output=args.output.resolve()
    if args.assets_only:return refresh_external_assets(args.output,args.item_assets)
    if args.static_water:parser.error('HBK12 requires animated water; original HBK1 remains available in historical builds')
    info=read(args.map/'manifest.json');sprite=read(args.sprites);effect=read(args.effects);skills=read(args.skills);projectiles=read(args.projectiles);components=read(args.components)
    jungle=read(args.jungle);fountains=read(args.fountains);roshan=read(args.roshan);completion=read(args.map_completion);courier=read(args.courier)
    extras=read(args.extra_heroes);item_assets=read(args.item_assets/'manifest.json');entities=read(args.item_entities)
    roster=read(args.roster_bodies);roster_fx=read(args.roster_effects);complete_jungle=read(args.complete_jungle)
    if not all(m.get('complete') for m in (sprite,effect,skills,projectiles,components,jungle,fountains,roshan,completion,courier,extras,item_assets,entities,roster,roster_fx,complete_jungle)):raise ValueError('Incomplete asset manifest')
    for name in ('catalog.hit','heroes.uir','catalog.fnt'):
        if sha(args.item_assets/name)!=item_assets['files'][name]['sha256']:raise ValueError('External item/UI/font asset changed '+name)
    rgb=np.asarray(Image.open(args.map/'map.png').convert('RGB'));depth=np.load(args.map/'map-depth.npy')
    assert sha(args.map/'map.png')==info['map_sha256'] and sha(args.map/'map-depth.npy')==info['depth_sha256']
    height,width=rgb.shape[:2];assert (width,height)==(info['width'],info['height']) and width%128==height%128==0
    assert depth.shape==(height,width) and depth.dtype==np.dtype('<u2')
    cols,rows=width//128,height//128
    replaced=set(components['replace_models']);assert replaced=={17,20,23,25}
    base=sprite['frames']+effect['frames']+skills['frames']+projectiles['frames']
    records=sorted([r for r in base if r['model'] not in replaced]+components['frames']+jungle['frames']+fountains['frames']+roshan['frames']+completion['frames']+courier['frames']+extras['frames']+entities['frames']+roster['frames']+roster_fx['frames']+complete_jungle['frames'],key=lambda r:(r['model'],r['clip'],r['direction'],r['frame']))
    assert {r['model'] for r in components['frames']}==replaced|{44}
    assert {r['model'] for r in jungle['frames']}=={45,46,47}
    assert {(r['model'],r['clip']) for r in fountains['frames']}=={(6,c) for c in range(5,11)}|{(7,5)}
    assert len(roster['frames'])==4864 and {r['model'] for r in roster['frames']}==set(range(93,97))
    assert len(complete_jungle['frames'])==15360 and {r['model'] for r in complete_jungle['frames']}==set(range(128,158))
    assert {r['model'] for r in completion['frames']}==set(range(59,75))
    assert len(courier['frames'])==512 and {r['model'] for r in courier['frames']}=={75,76}
    assert {(r['model'],r['clip'],r['direction']) for r in courier['frames']}=={(m,c,d) for m in (75,76) for c in (0,1) for d in range(16)}
    assert len(extras['frames'])==3240 and {r['model'] for r in extras['frames']}==set(range(77,88))
    assert len(entities['frames'])==1808 and {r['model'] for r in entities['frames']}==set(range(88,93))
    assert 0<len(records)<=65535
    # Full source ground and exact static fragments; no tree/landmark heights.
    grid_path=args.map/'ground-grid.npy';assert sha(grid_path)==info['ground_grid_sha256']
    grid=np.load(grid_path);assert grid.shape==(129,129) and grid.dtype==np.dtype('<i2')
    tile_index=64;frame_index=tile_index+cols*rows*4;grid_offset=frame_index+len(records)*32
    payload_start=(grid_offset+grid.nbytes+3)&~3
    water=None;water_frames=None;water_palette=None;water_mask=None;water_offsets=[]
    if not args.static_water:
        water=read(args.water/'manifest.json')
        assert water['complete'] and water['tile_size']==128 and 1<=water['frame_count']<=32 and 1<=water['frame_ticks']<=20
        for name in ('frames.npy','palettes.npy','mask.npy'):assert sha(args.water/name)==water['files'][name],name
        water_frames=np.load(args.water/'frames.npy');water_palette=np.load(args.water/'palettes.npy');water_mask=np.load(args.water/'mask.npy')
        assert water_frames.dtype==np.uint8 and water_frames.shape==(water['frame_count'],128,128)
        assert water_palette.dtype==np.dtype('<u2') and water_palette.shape==(2,256)
        assert water_mask.dtype==np.uint8 and water_mask.shape==(height,width)
        extension_offset=payload_start;item_extension_offset=extension_offset+32;mask_index_offset=item_extension_offset+32
        palette_offset=mask_index_offset+cols*rows*4;water_frames_offset=palette_offset+1024
        payload_start=water_frames_offset+water_frames.nbytes
    args.output.parent.mkdir(parents=True,exist_ok=True);offsets=[];frames=[];groups={};frame_bytes=0;tile_bytes=0
    heights=[round(160*s/256) for s in (114,86,80,83)]+[round(230*s/256) for s in (124,118)]+[round(260*s/256) for s in (134,188)]+[round(85*129/256)]*2
    with args.output.open('wb') as f:
        f.write(bytes(payload_start))
        for cy in range(rows):
            for cx in range(cols):
                z=depth[cy*128:(cy+1)*128,cx*128:(cx+1)*128]
                if np.all(z==65535):offsets.append(0);continue
                colour=rgb565(rgb[cy*128:(cy+1)*128,cx*128:(cx+1)*128]);offsets.append(f.tell())
                f.write(colour.tobytes());f.write(z.astype('<u2').tobytes());tile_bytes+=65536
        for record in records:
            path=ROOT/record['npz'];assert sha(path)==record['sha256'],str(path)
            with np.load(path) as data:
                rgba=data['rgba'];z=data['depth'].astype('<i2')
                shadow=data['shadow'].copy() if record.get('flags',0)&4 else None
            h,w=rgba.shape[:2];assert 0<h<=256 and 0<w<=256 and rgba.dtype==np.uint8 and z.shape==(h,w)
            key=tuple(record[k] for k in ('model','clip','direction'));number=groups.get(key,0)
            assert record['frame']==number and 0<=key[0]<192 and 0<=key[1]<11 and 0<=key[2]<16
            groups[key]=number+1
            count=record['frames'];ticks=record['duration_ticks']
            duration=max(1,((number+1)*ticks)//count-(number*ticks)//count)
            assert duration<=200
            if key[0]<4 or 48<=key[0]<=58 or 75<=key[0]<=78 or 88<=key[0]<=96 or 128<=key[0]<=157:assert shadow is not None and shadow.dtype==np.uint8
            tail=b''
            if shadow is not None:
                assert key[0]<4 or 48<=key[0]<=78 or 88<=key[0]<=96 or key[0] in (114,115) or 128<=key[0]<=157
                sh,sw=shadow.shape;assert 0<sh<=256 and 0<sw<=256
                assert (sw,sh)==(record['shadow_width'],record['shadow_height'])
                tail=struct.pack('<HHhh',sw,sh,record['shadow_anchor_x'],record['shadow_anchor_y'])+shadow.tobytes()
            offset=f.tell();payload=rgb565(rgba[:,:,:3]).tobytes()+rgba[:,:,3].tobytes()+z.tobytes()+tail
            f.write(payload);f.write(bytes((-f.tell())&3));frame_bytes+=len(payload)
            height_world=record.get('height_world',heights[key[0]] if key[0]<10 else 100)
            frames.append(struct.pack('<IIHHhhHHHHHHHH',offset,len(payload),w,h,record['anchor_x'],record['anchor_y'],*key,number,duration,record.get('flags',0),height_world,0))
        if water is not None:
            for cy in range(rows):
                for cx in range(cols):
                    mask=water_mask[cy*128:(cy+1)*128,cx*128:(cx+1)*128]
                    if not np.any(mask&3):water_offsets.append(0);continue
                    water_offsets.append(f.tell());f.write(mask.tobytes())
        assert len(groups)<=8192
        item_offsets={}
        for name in ('catalog.hit','heroes.uir','catalog.fnt'):
            f.write(bytes((-f.tell())&3));item_offsets[name]=f.tell();f.write((args.item_assets/name).read_bytes())
        size=f.tell();assert size<0x80000000
        f.seek(0);f.write(struct.pack('<6I2i8I',0x314b4248,12,size,128,cols,rows,*info['origin_pixels'],tile_index,frame_index,len(frames),grid_offset,129,180,181,extension_offset if water is not None else 0))
        f.seek(tile_index);f.write(struct.pack('<'+'I'*len(offsets),*offsets));f.seek(frame_index);f.write(b''.join(frames));f.seek(grid_offset);f.write(grid.tobytes())
        if water is not None:
            f.seek(extension_offset);f.write(struct.pack('<8I',0x31525457,water['frame_count'],water['frame_ticks'],128,water_frames_offset,mask_index_offset,palette_offset,16384))
            f.seek(mask_index_offset);f.write(struct.pack('<'+'I'*len(water_offsets),*water_offsets))
            f.seek(palette_offset);f.write(water_palette.tobytes());f.seek(water_frames_offset);f.write(water_frames.tobytes())
            f.seek(item_extension_offset);f.write(struct.pack('<8I',0x31524149,1,32,item_offsets['catalog.hit'],item_assets['files']['catalog.hit']['bytes'],item_offsets['heroes.uir'],item_offsets['catalog.fnt'],0))
    report={'format':'HBK11' if water is not None else 'HBK1','version':11 if water is not None else 1,'pack':str(args.output.relative_to(ROOT)),'sha256':sha(args.output),'bytes':size,'map_tiles':sum(bool(x) for x in offsets),'map_tile_bytes':tile_bytes,'sprite_frames':len(frames),'sprite_groups':len(groups),'sprite_payload_bytes':frame_bytes,'source_manifests':{str(p.relative_to(ROOT)):sha(p) for p in (args.map/'manifest.json',args.sprites,args.effects,args.skills,args.projectiles,args.components,args.jungle,args.fountains,args.roshan,args.map_completion,args.courier,args.extra_heroes,args.item_entities,args.item_assets/'manifest.json',ROOT/'private/baked/sprites/skill-motion-catalog.json')},'ground_grid_sha256':sha(grid_path),'projection':{'zoom':180,'pitch':181,'origin_pixels':info['origin_pixels']},'source_sha256':{Path(__file__).name:sha(__file__)},'dedicated_skills':24,'ranged_attack_styles':8,'additional_npc_models':4,'projectile_frames':len(projectiles['frames'])+264,'replaced_skill_models':sorted(replaced),'skill_component_frames':len(components['frames']),'summon_body_model':44,'shop_models':[45,46],'tormentor_model':47,'jungle_frames':len(jungle['frames']),'shadow_frames':sum(bool(r.get('flags',0)&4) for r in records),'fountain_frames':len(fountains['frames']),'wild_frames':len(roshan['frames']),'wild_models':11,'map_completion_frames':len(completion['frames']),'filler_models':14,'lotus_models':2,'courier_models':2,'courier_frames':len(courier['frames']),'hero_shadow_frames':5376+2304,'wild_shadow_frames':len(roshan['frames']),'extra_hero_frames':len(extras['frames']),'item_entity_frames':len(entities['frames']),'item_entity_models':5,'item_assets':dict(items=item_assets['items'],glyphs=item_assets['glyphs'],offsets=item_offsets,files=item_assets['files']),'pack_contents':'Original body/skill/component/projectile/NPC RGB565/alpha/depth pixels; offline pose shadows, original animated fountains and water, complete external HIT1 item table, original UI and bounded glyphs; no mesh triangles or bones'}
    report.update(format='HBK12',version=12,heroes=10,unique_lineup=True,ordinary_neutral_camps=26,ordinary_neutral_species=30,roster_body_frames=len(roster['frames']),roster_effect_frames=len(roster_fx['frames']),complete_jungle_frames=len(complete_jungle['frames']),hero_shadow_frames=sum(bool(r.get('flags',0)&4) for r in records if r['model']<4 or r['model'] in (77,78,93,94,95,96)),dedicated_skills=40,ranged_attack_styles=9)
    for manifest in (args.roster_bodies,args.roster_effects,args.complete_jungle):report['source_manifests'][str(manifest.relative_to(ROOT))]=sha(manifest)
    if water is not None:
        report['source_manifests'][str((args.water/'manifest.json').relative_to(ROOT))]=sha(args.water/'manifest.json')
        report['water']={'frame_count':water['frame_count'],'frame_ticks':water['frame_ticks'],'texture_bytes':water_frames.nbytes+water_palette.nbytes,'mask_tiles':sum(bool(x) for x in water_offsets),'mask_bytes':sum(bool(x) for x in water_offsets)*16384,'coverage_levels':4,'shore_strength_levels':64}
    args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(report))

if __name__=='__main__':main()
