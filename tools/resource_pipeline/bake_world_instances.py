"""Read original world-node instance tints; match transforms independently.

Input is VRF's existing textual DATA dump, not glTF material defaults. The
parser only accepts balanced arrays/objects and explicitly named source fields.
"""
from pathlib import Path
import re
from gltf_assets import np,ROOT

def balanced(text,start):
    closing={'[':']','{':'}'};stack=[];quoted=False;escape=False
    for i in range(start,len(text)):
        c=text[i]
        if quoted:
            if escape:escape=False
            elif c=='\\':escape=True
            elif c=='"':quoted=False
            continue
        if c=='"':quoted=True;continue
        if c in closing:stack.append(closing[c])
        elif c in ']}':
            if not stack or stack.pop()!=c:raise ValueError('Unbalanced source DATA')
            if not stack:return text[start:i+1],i+1
    raise ValueError('Unterminated source DATA')

def array(text,key):
    match=re.search(r'\b'+re.escape(key)+r'\s*=\s*\[',text)
    if not match:return '[]'
    return balanced(text,match.end()-1)[0]

def objects(text):
    pos=1
    while pos<len(text)-1:
        match=re.search(r'\{',text[pos:])
        if not match:break
        block,pos=balanced(text,pos+match.start());yield block

def scalar(text,key,default=None):
    m=re.search(r'\b'+re.escape(key)+r'\s*=\s*([^\r\n]+)',text)
    return m.group(1).strip() if m else default

def numbers(text):return [float(x) for x in re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[Ee][-+]?\d+)?',text)]

def read_world():
    path=ROOT/'private/source2/reports/worldnode-DATA.txt';text=path.read_text(encoding='utf8');result={}
    change=np.array([[0,1,0],[0,0,1],[1,0,0]],float)
    for block in objects(array(text,'m_aggregateSceneObjects')):
        model=scalar(block,'m_renderableModel','');match=re.search(r'"([^"]+)"',model)
        if not match:continue
        stem=Path(match.group(1)).stem
        transforms=np.asarray(numbers(array(block,'m_fragmentTransforms')),float).reshape(-1,3,4)
        entries=[];transform_index=0
        for index,item in enumerate(objects(array(block,'m_aggregateMeshes'))):
            has=scalar(item,'m_bHasTransform')=='true';matrix=np.eye(4)
            if has:
                raw=transforms[transform_index];transform_index+=1
                matrix[:3,:3]=change@raw[:,:3]@change.T;matrix[:3,3]=change@raw[:,3]*.0254
            tint=numbers(array(item,'m_vTintColor'))
            if len(tint) not in (3,4):raise ValueError('Missing aggregate tint')
            entries.append({'index':index,'draw':int(scalar(item,'m_nDrawCallIndex')),'matrix':matrix,'tint':np.asarray(tint[:3])/255.,'has_transform':has})
        if transform_index!=len(transforms):raise ValueError('Unmatched source fragment transforms')
        result[stem]=entries
    return result


def read_scene_tints():
    """Original ordinary placement tint, keyed by model resource stem.

    These are normalized sRGB values, unlike aggregate byte tints. The fourth
    component can modulate specular on opaque Dota materials; callers receive
    RGB separately and must not turn opaque stairs into translucent surfaces.
    """
    path=ROOT/'private/source2/reports/worldnode-DATA.txt'
    text=path.read_text(encoding='utf8');result={}
    change=np.array([[0,1,0],[0,0,1],[1,0,0]],float)
    for index,block in enumerate(objects(array(text,'m_sceneObjects'))):
        resource=scalar(block,'m_renderableModel','');match=re.search(r'"([^"]+)"',resource)
        if not match or not match.group(1):continue
        raw=np.asarray(numbers(array(block,'m_vTransform')),float)
        if raw.size!=12:raise ValueError(f'Scene object {index} has no original 3x4 matrix')
        raw=raw.reshape(3,4);matrix=np.eye(4)
        matrix[:3,:3]=change@raw[:,:3]@change.T;matrix[:3,3]=change@raw[:,3]*.0254
        tint=np.asarray(numbers(array(block,'m_vTintColor')),float)
        if tint.size!=4 or not np.isfinite(tint).all():raise ValueError(f'Scene object {index} has invalid original tint')
        if tint[3]==0:tint=np.ones(4) # Match VRF's ignored zero-alpha tint.
        if np.any(tint<0) or np.any(tint>1):raise ValueError(f'Scene object {index}: expected normalized sRGB tint')
        stem=Path(match.group(1)).stem
        result.setdefault(stem,[]).append({'source_index':index,'resource':match.group(1),'matrix':matrix,
                                         'tint':tint[:3],'source_tint_alpha':float(tint[3])})
    return result


def tree_entity_tints(scene):
    """Match every original tree node to its ent_dota_tree rendercolor.

    Full-map glTF omits materials in this pipeline. Entity model and Source
    origin provide an independent, unambiguous match to each placement. Source
    origin converts to glTF (Y,Z,X)*.0254; integer bytes remain sRGB until the
    linear-light map renderer applies them.
    """
    import json
    entities=json.loads((ROOT/'private/source2/reports/entity-default-parsed.json').read_text(encoding='utf8'))
    lookup={}
    for entity in entities:
        if entity.get('classname')!='ent_dota_tree' or 'origin' not in entity:continue
        match=re.search(r'"([^"]+)"',entity.get('model',''))
        if not match:continue
        origin=np.asarray(entity['origin'],float)
        if origin.shape!=(3,):raise ValueError('Original tree origin is not XYZ')
        slug=Path(match.group(1)).stem;position=origin[[1,2,0]]*.0254
        key=(slug,*np.round(position,2));lookup.setdefault(key,[]).append((entity,position))
    result={}
    for index,(node,matrix) in enumerate(zip(scene.doc['nodes'],scene.globals())):
        name=node.get('name','')
        if 'mesh' not in node or not name.startswith(('tree_','dire_tree')) or not re.search(r'_model(?:\.\d+)?$',name) or 'stump' in name:continue
        slug=name.split('.')[0];position=matrix[:3,3]
        candidates=lookup.get((slug,*np.round(position,2)),[])
        if len(candidates)!=1:raise ValueError(f'Tree {index} {name}: expected one original entity, got {len(candidates)}')
        entity,original=candidates[0]
        if np.max(np.abs(original-position))>.001:raise ValueError(f'Tree {index}: original entity/glTF placement mismatch')
        tint=np.asarray(entity.get('rendercolor',[255,255,255]),float)
        if tint.shape!=(3,) or not np.isfinite(tint).all() or np.any(tint<0) or np.any(tint>255):raise ValueError(f'Tree {index}: invalid original RGB bytes')
        result[index]=tint/255.
    if not result:raise ValueError('No original tree entity tints were matched')
    return result

if __name__=='__main__':
    data=read_world();e=data['n0_lr0_agg_prop_cliff_wall001_0'];print(len(e))
    for row in e[:3]:print({k:(v.tolist() if isinstance(v,np.ndarray) else v) for k,v in row.items()})
