"""Verify default wearable bone merges using original DATA and rigged glTF.

No bone index is inferred from the part's numeric index. Each JOINTS_0 value
selects a part skin joint, then its original Source 2 bone symbol selects the
base hero bone (ASCII-case-insensitive, as VRF's StringToken/Skeleton lookup).
Outputs original parent/bind information and exported inverse bind matrices.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from urllib.parse import unquote

from source2_prepare import CONFIG,ROOT

FOLD=str.maketrans('ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz')
COMPONENTS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
FORMATS={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}


def digest(path:Path)->str:return hashlib.sha256(path.read_bytes()).hexdigest()
def fold(name:str)->str:return name.translate(FOLD)


class Gltf:
    def __init__(self,path:Path):
        self.path=path;self.data=json.loads(path.read_text(encoding='utf8'))
        self.buffers=[]
        for buffer in self.data.get('buffers',[]):
            uri=buffer.get('uri','')
            if not uri or uri.startswith('data:'):raise ValueError('Expected external glTF BIN from VRF')
            file=(path.parent/unquote(uri)).resolve()
            if not file.is_relative_to(path.parent.resolve()):raise ValueError('Buffer URI escapes export directory')
            self.buffers.append(file.read_bytes())
    def accessor(self,index):
        a=self.data['accessors'][index]
        if 'sparse' in a:raise ValueError('Sparse accessors require explicit support')
        view=self.data['bufferViews'][a['bufferView']];raw=self.buffers[view['buffer']]
        kind,size=FORMATS[a['componentType']];width=COMPONENTS[a['type']]
        start=view.get('byteOffset',0)+a.get('byteOffset',0);stride=view.get('byteStride',size*width)
        values=[struct.unpack_from('<'+kind*width,raw,start+i*stride) for i in range(a['count'])]
        if a.get('normalized'):
            divisor={5120:127,5121:255,5122:32767,5123:65535}.get(a['componentType'])
            if divisor:values=[tuple(max(-1,v/divisor) for v in row) for row in values]
        return values


def kv_array(text:str,key:str):
    match=re.search(r'\b'+re.escape(key)+r'\s*=\s*\[',text)
    if not match:return None
    start=match.end()-1;depth=0;quoted=False;escaped=False
    for end in range(start,len(text)):
        c=text[end]
        if quoted:
            if escaped:escaped=False
            elif c=='\\':escaped=True
            elif c=='"':quoted=False
            continue
        if c=='"':quoted=True
        elif c=='[':depth+=1
        elif c==']':
            depth-=1
            if depth==0:
                value=re.sub(r',\s*([\]])',r'\1',text[start:end+1])
                return json.loads(value)
    raise ValueError('Unclosed KV3 array: '+key)


def source_skeleton(name:str,tool:Path,reports:Path)->dict:
    proof_path=reports/(name+'-provenance.json');proof=json.loads(proof_path.read_text(encoding='utf8'))
    raw=Path(proof['source_raw_file']);dest=reports/'rigging-data'/(name+'-DATA.txt')
    dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():
        process=subprocess.run([str(tool),'-i',str(raw),'-b','DATA'],capture_output=True,text=True,encoding='utf8',errors='replace',check=False)
        if process.returncode:raise RuntimeError(process.stderr or process.stdout)
        dest.write_text(process.stdout,encoding='utf8')
    text=dest.read_text(encoding='utf8')
    result={'bone_names':kv_array(text,'m_boneName'),'parents':kv_array(text,'m_nParent'),
            'local_positions_source_units':kv_array(text,'m_bonePosParent'),
            'local_quaternions_source_basis_xyzw':kv_array(text,'m_boneRotParent'),
            'm_remappingTable':kv_array(text,'m_remappingTable'),
            'm_remappingTableStarts':kv_array(text,'m_remappingTableStarts'),
            'source':proof['source'],'data_dump':str(dest),'data_dump_sha256':digest(dest)}
    count=len(result['bone_names'] or [])
    if not count or any(len(result[k] or [])!=count for k in ('parents','local_positions_source_units','local_quaternions_source_basis_xyzw')):
        raise ValueError('Incomplete original model skeleton: '+name)
    return result


def unique_names(nodes):
    mapping={}
    for index,node in enumerate(nodes):
        name=node.get('name','');key=fold(name)
        if not name:continue
        if key in mapping:raise ValueError('Ambiguous case-insensitive base node name: '+name)
        mapping[key]=index
    return mapping


def bind_part(hero:str,part:str,base:Gltf,body_source:dict,part_source:dict,reports:Path)->dict:
    path=ROOT/'private/source2/exports/rigged'/hero/(part+'.gltf');piece=Gltf(path)
    base_nodes=base.data['nodes'];part_nodes=piece.data['nodes'];lookup=unique_names(base_nodes)
    source_part={fold(n):i for i,n in enumerate(part_source['bone_names'])}
    source_body={fold(n):i for i,n in enumerate(body_source['bone_names'])}
    source_skins=piece.data.get('skins',[])
    if not source_skins:raise ValueError('Part has no exported skin: '+part)
    part_parents={child:i for i,node in enumerate(part_nodes) for child in node.get('children',[])}
    weighted={i:set() for i in range(len(source_skins))};weight_checks=[]
    for node_index,node in enumerate(part_nodes):
        if 'mesh' not in node:continue
        for primitive in piece.data['meshes'][node['mesh']]['primitives']:
            attrs=primitive.get('attributes',{})
            if 'JOINTS_0' not in attrs:continue
            if 'skin' not in node:raise ValueError('Weighted mesh node has no skin')
            skin_index=node['skin'];joints=piece.accessor(attrs['JOINTS_0']);weights=piece.accessor(attrs['WEIGHTS_0'])
            if len(joints)!=len(weights):raise ValueError('Joint/weight vertex count mismatch')
            max_error=0
            for js,ws in zip(joints,weights):
                max_error=max(max_error,abs(sum(ws)-1.0))
                for j,w in zip(js,ws):
                    if w>0:
                        if j<0 or j>=len(source_skins[skin_index]['joints']):raise ValueError('Weighted JOINTS_0 index exceeds skin joints')
                        weighted[skin_index].add(j)
            weight_checks.append({'node':node_index,'skin':skin_index,'vertices':len(joints),'max_weight_sum_error':max_error})
    all_unmatched=[];skins=[]
    for skin_index,skin in enumerate(source_skins):
        inverse=piece.accessor(skin['inverseBindMatrices'])
        if len(inverse)!=len(skin['joints']):raise ValueError('Inverse bind count mismatch')
        mappings=[]
        for part_joint_index,node_index in enumerate(skin['joints']):
            node=part_nodes[node_index];name=node['name'];key=fold(name)
            base_node=lookup.get(key);source_index=source_part.get(key);body_source_index=source_body.get(key)
            if source_index is None:raise ValueError('Exported joint is absent from original part skeleton: '+name)
            if base_node is None or body_source_index is None:all_unmatched.append(name)
            membership=[]
            for base_skin_index,base_skin in enumerate(base.data.get('skins',[])):
                for base_joint_index,base_joint_node in enumerate(base_skin['joints']):
                    if base_joint_node==base_node:membership.append({'base_skin_index':base_skin_index,'base_joint_index':base_joint_index})
            parent_index=part_source['parents'][source_index]
            mappings.append({'part_joint_index':part_joint_index,'part_node_index':node_index,'part_bone_name':name,
                'weighted':part_joint_index in weighted[skin_index],
                'source_part_bone_index':source_index,'source_part_parent_index':parent_index,
                'source_part_parent_name':part_source['bone_names'][parent_index] if parent_index>=0 else None,
                'source_base_bone_index':body_source_index,'base_node_index':base_node,
                'base_bone_name':base_nodes[base_node].get('name') if base_node is not None else None,
                'base_skin_joint_membership':membership,
                'matching':'exact' if base_node is not None and base_nodes[base_node].get('name')==name else 'Source2_ASCII_case_insensitive_symbol',
                'part_inverse_bind_matrix_column_major':inverse[part_joint_index],
                'part_gltf_parent_node':part_parents.get(node_index),
                'part_gltf_local_bind':{k:node[k] for k in ('matrix','translation','rotation','scale') if k in node},
                'source_local_position_source_units':part_source['local_positions_source_units'][source_index],
                'source_local_quaternion_source_basis_xyzw':part_source['local_quaternions_source_basis_xyzw'][source_index]})
        skins.append({'part_skin_index':skin_index,'joints':mappings,'weighted_joint_indices':sorted(weighted[skin_index]),
                      'part_joint_to_base_node':[m['base_node_index'] for m in mappings],
                      'part_inverse_bind_matrices_column_major':inverse})
    result={'hero':hero,'part':part,'part_gltf':str(path),'part_gltf_sha256':digest(path),
            'base_gltf':str(base.path),'base_gltf_sha256':digest(base.path),'source_part':part_source,
            'source_body':{'source':body_source['source'],'data_dump':body_source['data_dump'],'data_dump_sha256':body_source['data_dump_sha256']},
            'mapping_complete':not all_unmatched,'unmatched_bone_names':all_unmatched,'skins':skins,'weight_checks':weight_checks,
            'binding_equation_column_vectors':'p_hero = sum(weight_i * base_global_joint_pose[part_joint_to_base_node[joint_i]] * part_inverse_bind[joint_i] * p_part)',
            'binding_warning':'Use the PART inverse bind matrices, not the base-body inverse binds. Keep exact glTF basis (metres/Y-up). This maps bone motion; it does not reproduce Source 2 cloth physics.'}
    dest=reports/'wearable-bindings'/(part+'.json');dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return {'hero':hero,'part':part,'mapping_file':str(dest),'mapping_complete':not all_unmatched,'joint_count':sum(len(s['joints']) for s in skins),
            'weighted_joint_count':sum(len(s['weighted_joint_indices']) for s in skins),'unmatched':all_unmatched}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--workers',type=int,default=4);args=parser.parse_args()
    reports=ROOT/'private/source2/reports'
    names=list(CONFIG)+[part+'_rigged' for config in CONFIG.values() for part in config['parts']]
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.workers,4))) as pool:
        skeletons=dict(zip(names,pool.map(lambda name:source_skeleton(name,args.tool,reports),names)))
    results=[]
    for hero,config in CONFIG.items():
        base=Gltf(ROOT/'private/source2/exports'/hero/(hero+'.gltf'))
        for part in config['parts']:results.append(bind_part(hero,part,base,skeletons[hero],skeletons[part+'_rigged'],reports))
    manifest={'complete':all(r['mapping_complete'] for r in results),'parts':results,
              'method':'Original compiled model DATA m_modelSkeleton + VRF-exported skin.joints/inverseBindMatrices. ASCII-case-insensitive source bone symbols; no numeric-index guessing.',
              'references':['https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/Resource/ResourceTypes/ModelAnimation/Skeleton.cs',
                            'https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/Utils/StringToken.cs'],
              'retarget_equation':'base animated global joint matrix multiplied by part-specific inverse bind matrix.'}
    dest=reports/'wearable-bindings-manifest.json';dest.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    print(json.dumps(manifest,indent=2))
    if not manifest['complete']:raise SystemExit(1)


if __name__=='__main__':main()
