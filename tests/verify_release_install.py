"""Check extracted release layout and exact MIPS A/B resource lookup.

Filesystem/heap callbacks are modeled. No H1 device or application entry runs.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,sys,zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from mips_core_model import CoreMachine

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

class DriveMachine(CoreMachine):
    def __init__(self,drive,*args):
        self.drive=drive;self.attempts=[];self.successes=[]
        super().__init__(*args)
    def file_open(self,path,mode,*rest):
        name=self.string(path);self.attempts.append(name)
        if not name.startswith(self.drive+':'):return 0
        result=super().file_open(path,mode,*rest)
        if result:self.successes.append(name)
        return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    inventory=json.loads((a.package/'PACKAGE.json').read_bytes())
    version=(ROOT/'VERSION').read_text().strip()
    assert inventory['version']==version
    pin=json.loads((ROOT/'provenance/releases'/('v'+version+'.json')).read_bytes())
    assert pin['version']==version
    archive=a.package/'DOTA2-H1-install.zip'
    assert sha(archive)==inventory['files'][archive.name]
    with zipfile.ZipFile(archive)as z:
        assert not any(Path(n).suffix.upper()in('.H1S','.HS0','.HS1')for n in z.namelist())
        for name in ['应用/程序/DOTA2.bda','DOTA2/DOTA2.H1R']:
            dest=out/'extracted'/name;dest.parent.mkdir(parents=True,exist_ok=True)
            with z.open(name)as source,dest.open('wb')as target:shutil.copyfileobj(source,target)
            assert sha(dest)==inventory['members'][name]
    bda=out/'extracted/应用/程序/DOTA2.bda';bundle=out/'extracted/DOTA2/DOTA2.H1R'
    assert sha(bda)==pin['bda_sha256'] and bundle.stat().st_size==pin['runtime_assets']['DOTA2.H1R']['bytes']
    assert sha(bundle)==pin['runtime_assets']['DOTA2.H1R']['sha256']
    results=[];expected=None
    for drive in ['A','B']:
        m=DriveMachine(drive,bda,a.build/'DOTA2.elf',{'DOTA2.H1R':bundle})
        try:
            assert m.call('resource_mount_default')==1
            handle=m.call('resource_open',0);assert handle!=0
            assert m.call('resource_read_at',handle,0,0x81508000,64)==1
            data=m.read(0x81508000,64)
            if expected is None:expected=data
            else:assert expected==data
            assert m.attempts[0]=='B:\\DOTA2\\DOTA2.H1R'
            assert all(x==drive+':\\DOTA2\\DOTA2.H1R'for x in m.successes)
            if drive=='A':assert 'A:\\DOTA2\\DOTA2.H1R'in m.attempts
            assert m.call('resource_close',handle)==0
            assert m.call('resource_unmount')==1
            assert not m.allocations and not m.handles
            results.append(dict(drive=drive,passed=True,open_attempts=m.attempts,
                successful_opens=m.successes,first64_sha256=hashlib.sha256(data).hexdigest(),
                allocations_after_close=0,handles_after_close=0))
        finally:m.dispose()
    report=dict(passed=True,scope=__doc__,physical_h1_tested=False,actual_mips32=True,
        install_zip_sha256=sha(archive),bda_sha256=sha(bda),resource_sha256=sha(bundle),
        script_sha256=sha(Path(__file__)),model_sha256=sha(ROOT/'tests/mips_core_model.py'),
        extracted_root_layout_valid=True,existing_saves_included=False,results=results)
    (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(passed=True,drive_cases=len(results),bda_sha256=sha(bda))))

if __name__=='__main__':main()
