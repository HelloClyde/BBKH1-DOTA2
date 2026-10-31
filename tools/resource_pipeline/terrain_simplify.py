"""Remove redundant collinear, coplanar boundary vertices from baked floor cells.

Only existing boundary edges collapse, to an unchanged existing endpoint. This
does not weld components, fill holes, move corners, or simplify curved interiors.
Source material/UV seams must already have been baked into world-XY albedo.
Do not apply this geometry-only helper to authored side-wall UVs/materials.
"""
from __future__ import annotations
from collections import Counter,defaultdict,deque
from gltf_assets import np

def simplify_boundaries(points,faces,tolerance=.01,return_stats=False):
    """Return compact float64 points/int32 faces, optionally a third stats dict.

    A removable vertex must have a connected, manifold, open incident fan and
    exactly two boundary neighbors. Its complete original boundary chain must
    lie within ``tolerance`` world units of the replacement segment, and its
    incident star must be coplanar within that absolute tolerance. The manifold
    edge-link condition, unique/nonzero retained faces, winding, and local/total
    3D area conservation (relative 1e-8) are checked before every collapse.
    The actual maximum outline/plane error and total area error are reported.
    Default .01 world is below the runtime's one-world-unit vertex quantization.
    """
    p=np.asarray(points,dtype=np.float64);f=np.asarray(faces)
    if p.ndim!=2 or p.shape[1]!=3 or f.ndim!=2 or f.shape[1]!=3:raise ValueError('Expected (N,3) points and (F,3) faces')
    if f.dtype.kind not in 'iu':raise TypeError('faces must contain integer indices')
    if not np.isfinite(p).all() or not np.isfinite(tolerance) or tolerance<0:raise ValueError('Coordinates/tolerance must be finite; tolerance nonnegative')
    if len(f) and (f.min()<0 or f.max()>=len(p)):raise ValueError('Face index outside points')
    p=p.copy();f=f.astype(np.int32,copy=True);original=p.copy();rejected=Counter()
    def cross(t):return np.cross(p[t[:,1]]-p[t[:,0]],p[t[:,2]]-p[t[:,0]])
    def area(t):return float(np.linalg.norm(cross(t),axis=1).sum()*.5) if len(t) else 0.
    before_area=area(f);input_faces=len(f);input_vertices=len(p)
    if len(f):f=f[np.any(cross(f)!=0,axis=1)]
    alive=np.ones(len(f),dtype=bool);vertex_faces=[set() for _ in p];edges=defaultdict(set);face_keys=defaultdict(set)
    def edge_key(a,b):return (a,b) if a<b else (b,a)
    def triangle_edges(t):return (edge_key(int(t[0]),int(t[1])),edge_key(int(t[1]),int(t[2])),edge_key(int(t[2]),int(t[0])))
    def add(fid,t):
        for v in t:vertex_faces[int(v)].add(fid)
        for edge in triangle_edges(t):edges[edge].add(fid)
        face_keys[tuple(sorted(map(int,t)))].add(fid)
    def remove(fid,t):
        for v in t:vertex_faces[int(v)].discard(fid)
        for edge in triangle_edges(t):
            edges[edge].discard(fid)
            if not edges[edge]:del edges[edge]
        key=tuple(sorted(map(int,t)));face_keys[key].discard(fid)
        if not face_keys[key]:del face_keys[key]
    for fid,t in enumerate(f):add(fid,t)
    def neighbors(v):return set(int(w) for fid in vertex_faces[v] for w in f[fid] if w!=v)
    def boundary_neighbors(v):return {w for w in neighbors(v) if len(edges[edge_key(v,w)])==1}
    queue=deque(sorted({v for edge,inc in edges.items() if len(inc)==1 for v in edge}));queued=set(queue)
    vertex_support=[{i} for i in range(len(p))];edge_support={};collapsed=0;max_outline=0.;max_plane=0.;total_area_delta=0.
    def attempt(v,a,b):
        nonlocal collapsed,max_outline,max_plane,total_area_delta
        star=set(vertex_faces[v]);star_ids=sorted(star);star_faces=f[star_ids]
        # The vertex link must be one open chain, not a bow tie or an attached
        # closed fan that happens to leave exactly two boundary neighbors.
        link=defaultdict(set)
        for t in star_faces:
            others=[int(x) for x in t if x!=v]
            if len(others)!=2:return 'fan'
            x,y=others;link[x].add(y);link[y].add(x)
        if {x for x,n in link.items() if len(n)==1}!={a,b} or any(len(n)>2 for n in link.values()):return 'fan'
        seen=set();todo=[a]
        while todo:
            x=todo.pop()
            if x not in seen:seen.add(x);todo.extend(link[x]-seen)
        if len(seen)!=len(link) or len(star)!=len(link)-1:return 'fan'
        segment=p[b]-p[a];length2=float(segment@segment)
        if length2==0:return 'collinear'
        chain=edge_support.get(edge_key(a,v),{a,v})|edge_support.get(edge_key(v,b),{v,b})
        chain_points=original[sorted(chain)];parameter=(chain_points-p[a])@segment/length2
        tv=float((p[v]-p[a])@segment/length2)
        if not 0<tv<1 or np.any(parameter < -1e-12) or np.any(parameter > 1+1e-12):return 'segment'
        distances=np.linalg.norm(chain_points-(p[a]+np.clip(parameter,0,1)[:,None]*segment),axis=1)
        outline_error=float(distances.max(initial=0))
        if outline_error>tolerance:return 'collinear'
        ns=cross(star_faces);length=np.linalg.norm(ns,axis=1)
        if np.any(length==0):return 'zero'
        normal=ns[int(np.argmax(length))]/length.max()
        support=set().union(*(vertex_support[int(w)] for w in np.unique(star_faces)))
        plane_error=float(np.max(np.abs((original[sorted(support)]-p[v])@normal),initial=0))
        if plane_error>tolerance or np.any(ns@normal<=0):return 'plane'
        shared=edges.get(edge_key(v,a),set())
        if len(shared)!=1:return 'link'
        opposite=set(map(int,f[next(iter(shared))]))-{v,a}
        if neighbors(v)&neighbors(a)!=opposite:return 'link'
        kept=[fid for fid in star_ids if a not in f[fid]]
        new=f[kept].copy();new[new==v]=a
        if not len(new):return 'empty'
        new_normals=cross(new);new_length=np.linalg.norm(new_normals,axis=1)
        if np.any(new_length==0) or np.any(np.sum(cross(f[kept])*new_normals,axis=1)<=0):return 'flip'
        keys=[tuple(sorted(map(int,t))) for t in new]
        if len(set(keys))!=len(keys) or any(face_keys.get(key,set())-star for key in keys):return 'duplicate'
        old_area=float(length.sum()*.5);new_area=float(new_length.sum()*.5)
        if abs(new_area-old_area)>max(1e-9,old_area*1e-8):return 'area'
        if abs(total_area_delta+new_area-old_area)>max(1e-9,before_area*1e-8):return 'total_area'
        affected=set(map(int,star_faces.ravel()))|neighbors(a)|{a,b}
        for fid in star_ids:remove(fid,f[fid]);alive[fid]=False
        for fid,t in zip(kept,new):f[fid]=t;alive[fid]=True;add(fid,t)
        vertex_support[a]|=vertex_support[v];vertex_support[v]=set()
        edge_support.pop(edge_key(a,v),None);edge_support.pop(edge_key(v,b),None)
        edge_support[edge_key(a,b)]=chain
        collapsed+=1;max_outline=max(max_outline,outline_error);max_plane=max(max_plane,plane_error)
        total_area_delta+=new_area-old_area
        for w in sorted(affected):
            if w not in queued and vertex_faces[w]:queue.append(w);queued.add(w)
        return None
    while queue:
        v=queue.popleft();queued.discard(v)
        if not vertex_faces[v]:continue
        boundary=boundary_neighbors(v)
        if len(boundary)!=2:continue
        a,b=sorted(boundary)
        result=attempt(v,a,b)
        if result is not None:
            alternate=attempt(v,b,a)
            if alternate is not None:rejected[result]+=1
    result_faces=f[alive]
    used=np.unique(result_faces) if len(result_faces) else np.empty(0,dtype=np.int32)
    remap=np.full(len(p),-1,dtype=np.int32);remap[used]=np.arange(len(used),dtype=np.int32)
    result_points=np.ascontiguousarray(p[used]);result_faces=np.ascontiguousarray(remap[result_faces],dtype=np.int32)
    after_area=area(f[alive]);stats={'input_vertices':input_vertices,'input_faces':input_faces,'vertices':len(result_points),'faces':len(result_faces),
        'collapsed_boundary_vertices':collapsed,'tolerance_world':float(tolerance),'max_outline_error_world':max_outline,'max_plane_error_world':max_plane,
        'area_before':before_area,'area_after':after_area,'relative_area_error':abs(after_area-before_area)/max(before_area,1e-30),
        'rejected':dict(rejected),'scope':'Baked floor geometry only; unchanged endpoint collapses, no hole filling or component welding'}
    return (result_points,result_faces,stats) if return_stats else (result_points,result_faces)

collapse_collinear_boundaries=simplify_boundaries
