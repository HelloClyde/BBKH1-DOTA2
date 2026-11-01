"""Verify a standalone build or a downloaded package; static checks only."""
from pathlib import Path
import argparse,zipfile
from common import *

def verify_build(folder):
 sdk_check();folder=local(folder);m=read(folder/'DOTA2.build.json')
 require(m['version']==VERSION and m['sdk_commit']==SDK_COMMIT,'Build identity differs')
 require(m['source_sha256']==input_hashes(),'Build inputs are stale')
 require(sha(folder/'DOTA2.bda')==m['bda_sha256'] and sha(folder/'DOTA2.elf')==m['elf_sha256'],'Build BDA/ELF changed')
 reference=release_reference()
 if reference:require(m['bda_sha256']==reference,'Build differs from pinned release')
 require((folder/'DOTA2.bda.sha256').read_text(encoding='ascii')==m['bda_sha256']+'  DOTA2.bda\n','Checksum sidecar differs')
 require((folder/'DOTA2.bin').stat().st_size==m['payload_bytes'],'BIN size differs')
 from h1_bda.resources import PAYLOAD_OFFSET
 require((folder/'DOTA2.bin').read_bytes()==(folder/'DOTA2.bda').read_bytes()[PAYLOAD_OFFSET:PAYLOAD_OFFSET+m['payload_bytes']],'BIN differs from actual BDA payload')
 binary=binary_check(folder/'DOTA2.bda',folder/'DOTA2.elf',m['payload_bytes']);require(binary==m['binary'],'Build ABI/layout metadata differs')
 require(m['runtime_assets']==assets_check(),'External runtime assets changed')
 require(m['source_origin']==curation_check(),'Source-origin contract changed')
 return m

def verify_package(folder):
 folder=local(folder);inventory=read(folder/'PACKAGE.json')
 reference=release_reference();require(reference is not None,'Pin the verified release SHA before packaging')
 require(inventory.get('release_pin')==release_pin_identity(),'Package release pin changed')
 require(inventory['version']==VERSION and inventory['bda_sha256']==reference,'Package identity differs')
 expected_files={'DOTA2.bda','DOTA2.elf','DOTA2.bin','DOTA2.map','DOTA2.build.json','DOTA2.bda.sha256','DOTA2-H1-install.zip','SHA256SUMS.txt'}
 expected_members={'应用/程序/DOTA2.bda','DOTA2/SCENE.H1P','DOTA2/SCENE.json','DOTA2/SOUND.H1A','DOTA2/MAP12.H1C','DOTA2.build.json','README.md','NOTICE.md','UPDATE-NOTES.txt','LICENSE','licenses/GPL-2.0.txt','licenses/FONT-NOTICE.md','EQUIPMENT-MECHANISM-AUDIT.json','SPECIAL-MECHANISM-AUDIT.json'}
 expected_members.update(PACKAGE_DOCS)
 expected_members.update('DOTA2/'+name for name in ASSETS)
 require(set(inventory['files'])==expected_files and set(inventory['members'])==expected_members,'Explicit package inventory differs')
 require(inventory['files']['DOTA2.bda']==inventory['members']['应用/程序/DOTA2.bda']==reference,'Package BDA is not the pinned release')
 for name,(_,wanted) in ASSETS.items():require(inventory['members']['DOTA2/'+name]==wanted,'Package runtime asset identity differs')
 require(inventory['members']['DOTA2.build.json']==inventory['files']['DOTA2.build.json'],'ZIP and outer metadata differ')
 for name,wanted in inventory['files'].items():require(sha(local(folder/name))==wanted,'Package output changed: '+name)
 require((folder/'SHA256SUMS.txt').read_text(encoding='utf8')==''.join(value+'  '+name+'\n' for name,value in sorted(inventory['files'].items()) if name!='SHA256SUMS.txt'),'Outer checksum inventory differs')
 m=read(folder/'DOTA2.build.json');require(m['version']==VERSION and m['sdk_commit']==SDK_COMMIT and m['bda_sha256']==reference and m['elf_sha256']==inventory['files']['DOTA2.elf'],'Packaged build metadata differs')
 with zipfile.ZipFile(folder/'DOTA2-H1-install.zip') as z:
  require(len(z.namelist())==len(set(z.namelist())) and set(z.namelist())==set(inventory['members'])|{'SHA256SUMS.txt'},'ZIP members differ')
  for name,wanted in inventory['members'].items():
   with z.open(name) as stream:require(digest(stream)==wanted,'ZIP member differs: '+name)
  require(z.read('SHA256SUMS.txt').decode('utf8')==''.join(value+'  '+name+'\n' for name,value in sorted(inventory['members'].items())),'ZIP checksum inventory differs')
 return inventory

def main():
 p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group();g.add_argument('--build',type=Path,default=None);g.add_argument('--package',type=Path);a=p.parse_args()
 m=verify_package(a.package) if a.package else verify_build(a.build or ROOT/'build/native')
 print(json.dumps(dict(passed=True,version=VERSION,bda_sha256=m['bda_sha256'],static_only=True,physical_h1_tested=False,device_accessed=False)))
if __name__=='__main__':main()
