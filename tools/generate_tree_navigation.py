"""Regenerate navigation with independent original terrain/PHYS and live trees.

The optional import uses read-only extracted Source evidence. Normal generation
uses the compact audited planes in provenance/tree-navigation, never reopens a
tree-blocked cell by OR-ing the old walk map. Existing certified barracks passage
carves retain the same Q5 swept-circle guard.
"""
from pathlib import Path
import argparse, hashlib, json, math, os, subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'.tools/dota3d-deps'))
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
P=ROOT/'provenance/tree-navigation'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def packed(a):return np.packbits(a.astype(np.uint8).ravel(),bitorder='little').tobytes()
def unpack(p):return np.unpackbits(np.frombuffer(p.read_bytes(),np.uint8),bitorder='little').reshape(-1,256,256).astype(bool)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--import-source',type=Path);a=ap.parse_args();P.mkdir(parents=True,exist_ok=True)
    if a.import_source:
        s=a.import_source.resolve();out=ROOT/'build/tree-navigation-import';out.mkdir(parents=True,exist_ok=False)
        raw=((np.load(s/'source-flags.npy')&1)!=0);(out/'walk.u8').write_bytes(raw.astype(np.uint8).tobytes())
        env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','');exe=out/'collision.exe'
        cc=['C:/msys64/ucrt64/bin/gcc.exe','-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',str(ROOT/'tools/resource_pipeline/nav_collision.c'),'-lm','-o',str(exe)]
        subprocess.run(cc,env=env,check=True,capture_output=True)
        r=subprocess.run([str(exe),str(s/'physics-triangles.f32'),str(s/'height.f32'),str(out/'walk.u8'),str(out/'edges.u8'),str(out/'physics.csv')],env=env,check=True,capture_output=True,text=True)
        edges=np.frombuffer((out/'edges.u8').read_bytes(),np.uint8).reshape(2,256,256).astype(bool)
        # PHYS on every previously legal edge must agree with the old receipt.
        oldbase=np.frombuffer((s/'source-walk.u8').read_bytes(),np.uint8).reshape(256,256).astype(bool)
        oldedges=np.ones_like(edges);oldedges[0,:,:-1]=~(oldbase[:,:-1]&oldbase[:,1:]);oldedges[1,:-1,:]=~(oldbase[:-1,:]&oldbase[1:,:])
        import csv
        for row in csv.DictReader((s/'physics-edges.csv').open()):oldedges[int(row['down']),int(row['cell_y']),int(row['cell_x'])]=True
        eligible=np.zeros_like(edges);eligible[0,:,:-1]=oldbase[:,:-1]&oldbase[:,1:];eligible[1,:-1,:]=oldbase[:-1,:]&oldbase[1:,:]
        assert np.array_equal(edges[eligible],oldedges[eligible])
        (P/'gnv-walk.bin').write_bytes(packed(raw));(P/'physics-edges.bin').write_bytes(packed(edges));(P/'source-buildings.json').write_bytes((s/'buildings.json').read_bytes());(P/'legacy-standing-walk.bin').write_bytes(packed(np.load(s/'walk.npy')))
        receipt={'read_only_source':str(s),'source_sha256':{n:sha(s/n) for n in ['source-flags.npy','physics-triangles.f32','height.f32','buildings.json','physics-edges.csv','source-walk.u8']},'collision_converter_sha256':sha(ROOT/'tools/resource_pipeline/nav_collision.c'),'new_full_gnv_physics':json.loads(r.stdout),'previously_legal_edge_count':int(eligible.sum()),'previously_legal_edges_equal':True}
        (P/'import-audit.json').write_text(json.dumps(receipt,indent=2)+'\n')
    base=unpack(P/'gnv-walk.bin')[0];phys=unpack(P/'physics-edges.bin');info=json.loads((P/'source-buildings.json').read_bytes())
    yy,xx=np.mgrid[:256,:256];wx=xx*16+8;wy=yy*16+8
    def hull(row):
        x,y=row['world_xy'];r=row['collision_radius_world'];return np.maximum(abs(wx-x)-8,0)**2+np.maximum(abs(wy-y)-8,0)**2<=r*r
    rows=sorted(info['static_barracks'],key=lambda r:(0 if 'goodguys' in r['npc_unit'] else 1,('top','mid','bot').index(r['npc_unit'].rsplit('_',1)[1]),0 if '_melee_' in r['npc_unit'] else 1))
    owners=np.zeros((256,256),np.uint16)
    for i,row in enumerate(rows):owners[hull(row)]|=1<<i
    passages=info['source_passages'];walk=base&~(owners!=0)
    for cell in passages['cells']:assert base.flat[cell];walk.flat[cell]=True
    edge=phys.copy();edge[0,:,:-1]|=~(walk[:,:-1]&walk[:,1:]);edge[1,:-1,:]|=~(walk[:-1,:]&walk[1:,:])
    def safe(x,y,xx,yy):
        ax=x*32;ay=y*32;vx=(xx-x)*32;vy=(yy-y)*32;length=vx*vx+vy*vy
        def trunc(n,d):return abs(n)//d*(1 if n>=0 else -1)
        for c in passages['source_circles']:
            cx,cy=c['centre_q5'];p=(cx-ax)*vx+(cy-ay)*vy
            if not length or p<=0:px,py=ax,ay
            elif p>=length:px,py=xx*32,yy*32
            else:px,py=ax+trunc(vx*p,length),ay+trunc(vy*p,length)
            if (px-cx)**2+(py-cy)**2<=c['radius_q5']**2:return False
        return True
    for cell in passages['cells']:
        y,x=divmod(cell,256)
        for xx,yy in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
            if 0<=xx<256 and 0<=yy<256 and walk[yy,xx] and not safe(x*16+8,y*16+8,xx*16+8,yy*16+8):
                if yy==y:edge[0,y,min(x,xx)]=True
                else:edge[1,min(y,yy),x]=True
    cells=[];offsets=[0]
    for row in info['dynamic_towers']+info['dynamic_fillers']:cells+=np.flatnonzero(hull(row)&walk).astype(int).tolist();offsets.append(len(cells))
    h=['/* Generated by tools/generate_tree_navigation.py; trees are independent. */','#ifndef H1_DOTA_NAV_DATA_H','#define H1_DOTA_NAV_DATA_H','#define DOTA_NAV_SIZE 256','#define DOTA_NAV_CELL 16','#define DOTA_NAV_BITMAP_BYTES 8192']
    for name,data in [('walk',walk),('right',edge[0]),('down',edge[1])]:
        h+=[f'static {"" if name=="walk" else "const "}unsigned char dota_nav_{name}_bits[8192] = {{']
        b=packed(data);h += [','.join(map(str,b[i:i+32]))+',' for i in range(0,len(b),32)];h+=['};']
    h+=['#define DOTA_NAV_TOWER_COUNT 22','#define DOTA_NAV_FILLER_COUNT 14','#define DOTA_NAV_STRUCTURE_COUNT 36',f'#define DOTA_NAV_TOWER_CELL_COUNT {len(cells)}']
    for name,values in [('tower_offsets',offsets),('tower_cells',cells)]:
        h+=[f'static const unsigned short dota_nav_{name}[{len(values)}] = {{'];h += [','.join(map(str,values[i:i+16]))+',' for i in range(0,len(values),16)];h+=['};']
    pc=passages['cells'];cs=passages['source_circles']
    h += [f'#define DOTA_NAV_PASSAGE_CELLS {len(pc)}',f'#define DOTA_NAV_PASSAGE_CIRCLES {len(cs)}',f'#define DOTA_NAV_PASSAGE_MIN_X {min(v%256 for v in pc)*16}',f'#define DOTA_NAV_PASSAGE_MAX_X {(max(v%256 for v in pc)+1)*16}',f'#define DOTA_NAV_PASSAGE_MIN_Y {min(v//256 for v in pc)*16}',f'#define DOTA_NAV_PASSAGE_MAX_Y {(max(v//256 for v in pc)+1)*16}',f'static const unsigned short dota_nav_passage_cells[{len(pc)}]={{'+','.join(map(str,pc))+'};',f'static const int dota_nav_passage_circles[{len(cs)}][3]={{']
    h += ['{'+','.join(map(str,(*c['centre_q5'],c['radius_q5'])))+'},' for c in cs];h+=['};','#endif'];(ROOT/'private/generated/dota_nav.h').write_text('\n'.join(h)+'\n',newline='\n')
    restore=[]
    for cell in np.flatnonzero(base&~walk):
        y,x=divmod(int(cell),256);tm=fm=0
        for i,row in enumerate(info['dynamic_towers']):
            if hull(row)[y,x]:tm|=1<<i
        for i,row in enumerate(info['dynamic_fillers']):
            if hull(row)[y,x]:fm|=1<<i
        assert owners[y,x];restore.append((int(cell),int(owners[y,x]),tm,fm))
    restore_edges=[]
    for down,y,x in zip(*np.where(edge&~phys)):
        mask=int(owners[y,x]|owners[y+int(down),x+int(not down)]);assert mask;restore_edges.append((int(down)*65536+int(y)*256+int(x),mask))
    restore_edges.sort();circleowners=[next(i for i,r in enumerate(rows) if r['hammer_id']==c['hammer_id']) for c in cs]
    h=['/* Tree-free GNV/PHYS baseline; original live hull owners. */','#ifndef H1_DOTA_BARRACKS_NAV_H','#define H1_DOTA_BARRACKS_NAV_H',f'#define DOTA_BARRACKS_NAV_CELLS {len(restore)}',f'#define DOTA_BARRACKS_NAV_EDGES {len(restore_edges)}','typedef struct {unsigned short cell,owners;unsigned towers,fillers;} DotaBarracksNavCell;','static const DotaBarracksNavCell dota_barracks_nav_cells[DOTA_BARRACKS_NAV_CELLS]={']
    h+=['{'+','.join(map(str,r))+'},' for r in restore];h+=['};','typedef struct {unsigned edge;unsigned short owners;} DotaBarracksNavEdge;','static const DotaBarracksNavEdge dota_barracks_nav_edges[DOTA_BARRACKS_NAV_EDGES]={'];h+=['{'+','.join(map(str,r))+'},' for r in restore_edges];h+=['};','static const unsigned char dota_barracks_passage_owners[]={'+','.join(map(str,circleowners))+'};','#endif'];(ROOT/'private/generated/dota_barracks_nav.h').write_text('\n'.join(h)+'\n',newline='\n')
    trees=json.loads((ROOT/'provenance/vision/trees.json').read_bytes())['trees'];mask=np.zeros_like(base)
    for t in trees:mask|=(wx-t['x_q8']/256)**2+(wy-t['y_q8']/256)**2<=22**2
    assert np.array_equal(walk&~mask,unpack(P/'legacy-standing-walk.bin')[0])
    b=packed(mask);h=['/* Static-tree fallback only; live mask is rebuilt by canonical ID. */','#ifndef H1_DOTA_NAV_TREE_FALLBACK_H','#define H1_DOTA_NAV_TREE_FALLBACK_H','static const unsigned char dota_nav_source_tree_bits[8192]={'];h += [','.join(map(str,b[i:i+32]))+',' for i in range(0,len(b),32)];h+=['};','#endif'];(ROOT/'private/generated/dota_tree_nav.h').write_text('\n'.join(h)+'\n',newline='\n')
    report={'passed':True,'base_gnv_cells':int(base.sum()),'tree_free_barracks_alive_cells':int(walk.sum()),'standing_trees_barracks_alive_cells':int((walk&~mask).sum()),'tower_filler_cells':len(cells),'barracks_restore_cells':len(restore),'barracks_restore_edges':len(restore_edges),'tree_count':len(trees),'inputs':{str(p.relative_to(ROOT)):sha(p) for p in [P/'gnv-walk.bin',P/'physics-edges.bin',P/'source-buildings.json',ROOT/'provenance/vision/trees.json']},'outputs':{str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'private/generated'/n for n in ['dota_nav.h','dota_barracks_nav.h','dota_tree_nav.h']]},'limitations':'Preserves the original H1 swept-ray/cell approximation, original two certified passage cells and exact Q5 guards; does not claim native continuous hull equivalence.'}
    report['standing_cells_byte_equal_legacy']=True
    report['inputs']['provenance/tree-navigation/legacy-standing-walk.bin']=sha(P/'legacy-standing-walk.bin')
    (P/'generation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
