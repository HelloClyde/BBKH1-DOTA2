"""Original model material-group resolution for offline map rendering."""
from pathlib import Path
import hashlib,json,subprocess
from PIL import Image
from gltf_assets import np,ROOT
from source2_vpk import VpkArchive
from source2_kv3_text import parse
STEAM=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
class OriginalMaterials:
    def __init__(self,output):
        self.output=Path(output);self.output.mkdir(parents=True,exist_ok=True)
        self.vpk=VpkArchive(STEAM/'pak01_dir.vpk');self.tool=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
        self.data={};self.textures={};self.receipts={}
    def resource(self,path):
        if not path.endswith('_c'):path+='_c'
        if path in self.data:return self.data[path]
        folder=self.output/path.replace('/','__').replace('.','_');folder.mkdir(exist_ok=True)
        raw=folder/Path(path).name;original=self.vpk.read(path)
        if not raw.exists() or raw.read_bytes()!=original:raw.write_bytes(original)
        dump=folder/'DATA.txt'
        if not dump.exists():
            p=subprocess.run([str(self.tool),'-i',str(raw),'-b','DATA'],capture_output=True,text=True,encoding='utf8',errors='strict',check=True);dump.write_text(p.stdout,encoding='utf8')
        data=parse(dump.read_text(encoding='utf8'));self.data[path]=data
        self.receipts[path]=dict(source=self.vpk.identity(path),raw=str(raw.relative_to(ROOT)),raw_sha256=sha(raw),dump=str(dump.relative_to(ROOT)),dump_sha256=sha(dump))
        return data
    def groups(self,model):
        d=self.resource(model);groups=d['m_materialGroups'];default=groups[0]['m_materials']
        return {str(g['m_name']):dict(zip(default,g['m_materials'])) for g in groups}
    def image(self,path):
        if not path.endswith('_c'):path+='_c'
        if path in self.textures:return self.textures[path]
        folder=self.output/path.replace('/','__').replace('.','_');folder.mkdir(exist_ok=True)
        raw=folder/Path(path).name;raw.write_bytes(self.vpk.read(path));png=raw.with_suffix('.png')
        if not png.exists():subprocess.run([str(self.tool),'-d','-i',str(raw),'-o',str(png)],capture_output=True,check=True)
        rgba=np.asarray(Image.open(png).convert('RGBA')).copy();self.textures[path]=rgba
        self.receipts[path]=dict(source=self.vpk.identity(path),png=str(png.relative_to(ROOT)),png_sha256=sha(png),dimensions=list(rgba.shape))
        return rgba
    def material(self,path):
        d=self.resource(path)
        ints={p['m_name']:p['m_nValue'] for p in d.get('m_intParams',[])}
        floats={p['m_name']:p['m_flValue'] for p in d.get('m_floatParams',[])}
        vectors={p['m_name']:p['m_value'] for p in d.get('m_vectorParams',[])}
        textures={p['m_name']:p['m_pValue'] for p in d.get('m_textureParams',[])}
        rgba=self.image(textures['g_tColor']).copy()
        alpha=bool(ints.get('F_ALPHA_TEST',0) or ints.get('F_TRANSLUCENT',0) or ints.get('F_ADDITIVE_BLEND',0))
        if not alpha:rgba[:,:,3]=255
        factor=np.array([*vectors.get('g_vColorTint',[1,1,1,0])[:3],1.],float)
        cutoff=float(floats.get('g_flAlphaTestReference',.5)) if ints.get('F_ALPHA_TEST',0) else .01
        return rgba,cutoff,factor
    def save(self):
        p=self.output/'manifest.json';p.write_text(json.dumps(dict(source_receipts=self.receipts,source_sha256={str(Path(__file__).relative_to(ROOT)):sha(__file__)}),ensure_ascii=False,indent=2)+'\n',encoding='utf8');return p
