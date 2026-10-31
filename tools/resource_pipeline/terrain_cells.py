"""Clip real XYZ terrain triangles into an XY cell grid, offline only.

Positions remain in world coordinates. Each output triangle uses three private
vertices; callers may weld/QEM afterwards. Winding and interpolated height are
preserved. No XY-area test is used, so vertical walls survive.
"""
from __future__ import annotations
import math
from collections import defaultdict
from gltf_assets import np


def _clip_plane(polygon,axis,boundary,minimum):
    if not polygon:return []
    result=[];previous=polygon[-1]
    previous_inside=previous[axis]>=boundary if minimum else previous[axis]<=boundary
    for current in polygon:
        inside=current[axis]>=boundary if minimum else current[axis]<=boundary
        if inside!=previous_inside:
            t=(boundary-previous[axis])/(current[axis]-previous[axis])
            # Preserve exact endpoints and force the clipped coordinate onto
            # the plane, avoiding cracks from independently rounded boundaries.
            if t<=0:crossing=previous.copy()
            elif t>=1:crossing=current.copy()
            else:crossing=previous+(current-previous)*t
            crossing[axis]=boundary;result.append(crossing)
        if inside:result.append(current)
        previous=current;previous_inside=inside
    if not result:return result
    compact=[result[0]]
    for point in result[1:]:
        if not np.array_equal(point,compact[-1]):compact.append(point)
    if len(compact)>1 and np.array_equal(compact[0],compact[-1]):compact.pop()
    return compact


def split_cells(points,faces,cell_size=256):
    """Yield ``(cx, cy, float64_positions, int32_triangles)`` per occupied cell.

    ``cx/cy`` are signed integer cell indices (floor of world XY/cell_size).
    Cross-cell faces are clipped against four planes with linear XYZ/height
    interpolation, then triangulated as a fan. Exactly zero 3D-area faces are
    removed; small nonzero faces and vertical walls are retained.

    A face lying wholly on an internal grid plane belongs to its positive-side
    cell. At the mesh's outer maximum it belongs to the existing final cell,
    so exact extent boundaries do not create an additional cell. Closed output
    boundaries allow ordinary neighboring clipped faces to share their edges.
    Inputs are never modified. Output order is deterministic, sorted by cx,cy.
    """
    cell_size=float(cell_size)
    if not math.isfinite(cell_size) or cell_size<=0:raise ValueError('cell_size must be finite and positive')
    points=np.asarray(points,dtype=np.float64);source_faces=np.asarray(faces)
    if points.ndim!=2 or points.shape[1]!=3:raise ValueError('points must have shape (N,3)')
    if source_faces.ndim!=2 or source_faces.shape[1]!=3:raise ValueError('faces must have shape (F,3)')
    if source_faces.dtype.kind not in 'iu':raise TypeError('faces must contain integer indices')
    if not np.isfinite(points).all():raise ValueError('points must be finite')
    if not len(source_faces):return
    if source_faces.min()<0 or source_faces.max()>=len(points):raise ValueError('face index is outside points')
    triangles=points[source_faces]
    normals=np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0])
    triangles=triangles[np.any(normals!=0,axis=1)]
    if not len(triangles):return
    lower_xy=triangles[:,:,:2].min(axis=1);upper_xy=triangles[:,:,:2].max(axis=1)
    grid_min=np.floor(lower_xy.min(axis=0)/cell_size)
    grid_max=np.maximum(grid_min,np.ceil(upper_xy.max(axis=0)/cell_size)-1)
    if np.max(np.abs(np.r_[grid_min,grid_max]))>=2**62:raise OverflowError('cell indices exceed supported int64 range')
    lower=np.floor(lower_xy/cell_size).astype(np.int64)
    upper=np.maximum(lower,np.ceil(upper_xy/cell_size).astype(np.int64)-1)
    lower=np.clip(lower,grid_min.astype(np.int64),grid_max.astype(np.int64))
    upper=np.clip(upper,grid_min.astype(np.int64),grid_max.astype(np.int64))
    single=np.all(lower==upper,axis=1);buckets=defaultdict(list)
    # The common case (an original small triangle wholly inside one cell) is
    # classified/grouped in NumPy rather than clipped in a Python face loop.
    indices=np.flatnonzero(single)
    if len(indices):
        order=np.lexsort((lower[indices,1],lower[indices,0]));indices=indices[order]
        cells=lower[indices];starts=np.r_[0,np.flatnonzero(np.any(cells[1:]!=cells[:-1],axis=1))+1,len(indices)]
        for start,end in zip(starts[:-1],starts[1:]):
            key=tuple(map(int,cells[start]));buckets[key].append(triangles[indices[start:end]].reshape(-1,3))
    for index in np.flatnonzero(~single):
        triangle=triangles[index]
        for cx in range(int(lower[index,0]),int(upper[index,0])+1):
            x0=cx*cell_size;x1=(cx+1)*cell_size
            # Clip X once, reuse it for every intersecting Y cell.
            strip=_clip_plane(list(triangle),0,x0,True)
            strip=_clip_plane(strip,0,x1,False)
            if len(strip)<3:continue
            for cy in range(int(lower[index,1]),int(upper[index,1])+1):
                polygon=_clip_plane(strip,1,cy*cell_size,True)
                polygon=_clip_plane(polygon,1,(cy+1)*cell_size,False)
                if len(polygon)<3:continue
                pieces=[]
                for j in range(1,len(polygon)-1):
                    a,b,c=polygon[0],polygon[j],polygon[j+1]
                    if np.any(np.cross(b-a,c-a)!=0):pieces.extend((a,b,c))
                if pieces:buckets[cx,cy].append(np.asarray(pieces,dtype=np.float64))
    for (cx,cy),parts in sorted(buckets.items()):
        positions=np.concatenate(parts,axis=0)
        if len(positions)>=2**31:raise OverflowError('one cell exceeds int32 vertex indexing')
        output_faces=np.arange(len(positions),dtype=np.int32).reshape(-1,3)
        yield cx,cy,positions,output_faces
