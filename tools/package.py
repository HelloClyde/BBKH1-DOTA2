"""Package only the explicit H1 delivery members; never scans a development tree."""
from pathlib import Path
import argparse,shutil,zipfile
from common import *
from verify import verify_build,verify_package

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,default=ROOT/'build/native');p.add_argument('--output',type=Path,default=ROOT/'dist');a=p.parse_args()
 build=local(a.build);m=verify_build(build);out=local(a.output);require(not out.exists(),'Existing package directory preserved; choose a new --output')
 reference=release_reference();require(reference is not None and reference==m['bda_sha256'],'Pin this verified release before packaging')
 pin=release_pin_identity()
 members={'应用/程序/DOTA2.bda':build/'DOTA2.bda','DOTA2/manifest.json':ROOT/'assets/runtime/manifest.json','DOTA2.build.json':build/'DOTA2.build.json','README.md':ROOT/'README.md','NOTICE.md':ROOT/'NOTICE.md','UPDATE-NOTES.txt':ROOT/'UPDATE-NOTES.txt','licenses/GPL-2.0.txt':ROOT/'licenses/GPL-2.0.txt','EQUIPMENT-MECHANISM-AUDIT.json':ROOT/'provenance/EQUIPMENT-MECHANISM-AUDIT.json','SPECIAL-MECHANISM-AUDIT.json':ROOT/'provenance/SPECIAL-MECHANISM-AUDIT.json'}
 members.update({'LICENSE':ROOT/'LICENSE','licenses/FONT-NOTICE.md':ROOT/'licenses/FONT-NOTICE.md'})
 members.update({'DOTA2/'+name:ROOT/'assets/runtime'/name for name in ASSETS})
 members.update({name:ROOT/name for name in PACKAGE_DOCS})
 for path in members.values():require(path.is_file(),'Missing explicit package input: '+relative(path))
 hashes={name:sha(path) for name,path in members.items()};out.mkdir(parents=True)
 app_names=('DOTA2.bda','DOTA2.elf','DOTA2.bin','DOTA2.map','DOTA2.build.json','DOTA2.bda.sha256')
 for name in app_names:shutil.copy2(build/name,out/name)
 sums=''.join(wanted+'  '+name+'\n' for name,wanted in sorted(hashes.items()))
 with zipfile.ZipFile(out/'DOTA2-H1-install.zip','w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for name,path in members.items():
   info=zipfile.ZipInfo(name,date_time=(2026,11,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
   with path.open('rb') as source,z.open(info,'w') as target:shutil.copyfileobj(source,target,1024*1024)
  info=zipfile.ZipInfo('SHA256SUMS.txt',date_time=(2026,11,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;z.writestr(info,sums)
 files={name:sha(out/name) for name in (*app_names,'DOTA2-H1-install.zip')}
 (out/'SHA256SUMS.txt').write_text(''.join(wanted+'  '+name+'\n' for name,wanted in sorted(files.items())),encoding='utf8');files['SHA256SUMS.txt']=sha(out/'SHA256SUMS.txt')
 require(hashes=={name:sha(path) for name,path in members.items()},'Package inputs changed during compression')
 require(release_pin_identity()==pin,'Release identity changed during packaging')
 write(out/'PACKAGE.json',dict(version=VERSION,bda_sha256=reference,release_pin=pin,sdk_commit=SDK_COMMIT,files=files,members=hashes,physical_h1_tested=False,scope='Explicit install ZIP and standalone files from the current pinned build. Functional/device evidence is separate; this packaging tool executes no device or emulator.'))
 verify_package(out);print(json.dumps(dict(passed=True,bda_sha256=reference,install_zip_sha256=files['DOTA2-H1-install.zip'],zip_members=len(members)+1,output=relative(out),device_accessed=False)))
if __name__=='__main__':main()
