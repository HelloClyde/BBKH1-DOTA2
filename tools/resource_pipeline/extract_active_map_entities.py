"""Extract every initial active Source map entity layer with exact transforms."""
from pathlib import Path
import argparse,hashlib,json,math,subprocess
from source2_vpk import VpkArchive
from source2_kv3_text import parse
from gltf_assets import Gltf,np,ROOT
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
CHANGE=np.array([[0,1,0],[0,0,1],[1,0,0]],float)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def entity_matrix(e):
    pitch,yaw,roll=np.radians(np.asarray(e.get('angles',[0,0,0]),float));cp,sp=np.cos(pitch),np.sin(pitch);cy,sy=np.cos(yaw),np.sin(yaw);cr,sr=np.cos(roll),np.sin(roll)
    rz=np.array([[cy,-sy,0],[sy,cy,0],[0,0,1]]);ry=np.array([[cp,0,sp],[0,1,0],[-sp,0,cp]]);rx=np.array([[1,0,0],[0,cr,-sr],[0,sr,cr]])
    m=np.eye(4);m[:3,:3]=CHANGE@rz@ry@rx@np.diag(e.get('scales',[1,1,1]))@CHANGE.T;m[:3,3]=CHANGE@np.asarray(e['origin'])*.0254;return m
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,default=ROOT/'private/source2-v022/active-entities');a=ap.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    tool=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe';vpk=VpkArchive(STEAM/'maps/dota.vpk');layers={};sources={}
    names=['default_ents',*[f'world_layer_{side}_{part}' for side in ('radiant','dire') for part in ('base','destruction')]]
    for name in names:
        entry=f'maps/dota/entities/{name}.vents_c';raw=out/(name+'.vents_c');raw.write_bytes(vpk.read(entry));sources[name]=vpk.identity(entry)
        result=subprocess.run([str(tool),'-i',str(raw),'-b','DATA'],text=True,capture_output=True,encoding='utf8',errors='strict',check=True)
        text=out/(name+'-DATA.txt');text.write_text(result.stdout,encoding='utf8');data=parse(result.stdout)
        entities=[]
        for item in data['m_entityKeyValues']:
            if 'keyValues3Data' not in item:raise ValueError('Expected original KV3 entity values')
            e=dict(item['keyValues3Data']['values'])
            for k in ('model','ambientfx','destroyfx','damagefx_lvl1','damagefx_lvl2','destruction_lvl1','destruction_lvl2'):
                if k in e:e[k]='resource_name:'+json.dumps(e[k],ensure_ascii=False)
            e['_source_layer']='default' if name=='default_ents' else name
            entities.append(e)
        layers[e['_source_layer']]=entities
    default=layers['default'];activation=[e for e in default if e.get('classname')=='info_world_layer']
    active=['default']+[e['layerName'] for e in activation if int(e.get('spawnflags',0))&1]
    assert set(active)=={'default','world_layer_radiant_base','world_layer_dire_base'}
    combined=[e for layer in active for e in layers[layer]]
    trees=[];seen={}
    for e in combined:
        if e.get('classname')!='ent_dota_tree':continue
        m=entity_matrix(e);key=(e['model'],*np.round(m.reshape(-1),5))
        if key in seen:raise ValueError(f'Duplicate source tree placement {seen[key]} / {e["hammerUniqueId"]}')
        seen[key]=e['hammerUniqueId'];trees.append(dict(e,matrix_column_major=m.T.reshape(-1).tolist(),world_xyz=[2048+e['origin'][0]/4,2048-e['origin'][1]/4,e['origin'][2]/4]))
    assert len(trees)==2475
    for name,data in [('layers.json',layers),('entities.json',combined),('trees.json',trees)]:
        (out/name).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    report=dict(complete=True,active_layers=active,activation_evidence=activation,entities=len(combined),trees=len(trees),per_layer_trees={k:sum(e.get('classname')=='ent_dota_tree' for e in layers[k]) for k in layers},sources=sources,
        source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),ROOT/'tools/source2_kv3_text.py']},
        files={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'},matrix_convention='SourceQAngle Rz(yaw)*Ry(pitch)*Rx(roll), source scaleXYZ, conjugated by Source(X,Y,Z)->glTF(Y,Z,X), original source origin*0.0254; independent2306default-tree glTF validation max6e-5metres in audit-v022.')
    (out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(complete=True,trees=len(trees),entities=len(combined),active_layers=active,output=str(out))))
if __name__=='__main__':main()
