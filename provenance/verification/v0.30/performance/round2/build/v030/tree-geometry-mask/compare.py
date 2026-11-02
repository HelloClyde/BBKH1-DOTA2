"""Read-only comparison of uncut canonical tree raster and original VISION tree bits."""
from pathlib import Path
import hashlib,json,re,struct,zlib
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
SOURCES=[ROOT/'game/sight.c',ROOT/'game/trees.h',ROOT/'provenance/vision/trees.json',ROOT/'build/v030/legacy/VISION.H1V',ROOT/'build/v030/legacy/TREE.H1T',Path(__file__),ROOT/'game/sight.h']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
before={str(p.relative_to(ROOT)):sha(p)for p in SOURCES}
sight=SOURCES[0].read_text();header=SOURCES[1].read_text();sight_header=SOURCES[6].read_text()
assert '#define GRID DOTA_SIGHT_GRID_SIDE' in sight and '#define CELL DOTA_SIGHT_CELL_WORLD' in sight
grid=int(re.search(r'#define DOTA_SIGHT_GRID_SIDE\s+(\d+)',sight_header)[1]);cell=int(re.search(r'#define DOTA_SIGHT_CELL_WORLD\s+(\d+)',sight_header)[1]);radius=int(re.search(r'#define DOTA_TREE_RADIUS_WORLD\s+(\d+)',header)[1])
assert (grid,cell,radius)==(256,16,16)
records=json.loads(SOURCES[2].read_bytes())['trees'];assert len(records)==2113
xy=[(t['x_q8']//256,t['y_q8']//256)for t in records]
packed=b''.join(struct.pack('<HH',x,y)for x,y in xy)
tree=SOURCES[4].read_bytes();th=struct.unpack_from('<32I',tree)
assert th[11:14]==(2113,128,8580) and tree[128:8580]==packed
vision=SOURCES[3].read_bytes();h=struct.unpack_from('<16I',vision)
assert h[:6]==(0x31563148,1,64,4096,256,16) and (h[8],h[9])==(131136,8192)
assert len(vision)==64+h[10] and zlib.crc32(vision[64:])&0xffffffff==h[11]
old=vision[h[8]:h[8]+h[9]];new=bytearray(len(old))
for x,y in xy:
 for yy in range(y//cell-1,y//cell+2):
  for xx in range(x//cell-1,x//cell+2):
   dx=xx*cell+cell//2-x;dy=yy*cell+cell//2-y
   if 0<=xx<grid and 0<=yy<grid and dx*dx+dy*dy<=radius*radius:
    index=yy*grid+xx;new[index>>3]|=1<<(index&7)
new=bytes(new)
(OUT/'original-vision-tree-mask.bin').write_bytes(old)
(OUT/'uncut-canonical-tree-mask.bin').write_bytes(new)
pop=lambda data:sum(x.bit_count()for x in data)
report=dict(passed=True,scope=__doc__,physical_H1_tested=False,USB_opened=False,game_state_mutated=False,source_current=before=={str(p.relative_to(ROOT)):sha(p)for p in SOURCES},source_sha256=before,tree_count=len(xy),grid=grid,cell_world=cell,radius_world=radius,natural_tree_state='all canonical trees standing; no item/planted trees',original_vision_payload_CRC_verified=True,canonical_xy_matches_TREE_H1T=True,canonical_xy_sha256=hashlib.sha256(packed).hexdigest(),bytes=len(old),original_set_bits=pop(old),canonical_set_bits=pop(new),canonical_added_cells=sum((b&~a).bit_count()for a,b in zip(old,new)),canonical_removed_cells=sum((a&~b).bit_count()for a,b in zip(old,new)),xor_cells=sum((a^b).bit_count()for a,b in zip(old,new)),byte_identical=old==new,original_mask_sha256=hashlib.sha256(old).hexdigest(),canonical_mask_sha256=hashlib.sha256(new).hexdigest(),limitations='Compares only raster content for uncut canonical natural trees. Does not measure per-revision raster construction, control flow, shadow traversal runtime or H1 FPS. Cut/regrown and planted trees intentionally change obstruction state.')
assert report['source_current']
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print(json.dumps({k:report[k]for k in ['passed','byte_identical','original_set_bits','canonical_set_bits','canonical_added_cells','canonical_removed_cells','xor_cells','original_mask_sha256']}))
