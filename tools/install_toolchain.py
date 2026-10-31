"""Install the checksum-pinned Windows GNU MIPS15.2.0 distribution.

The fixed H1 SDK exposes H1_GNU_BIN but has no toolchain downloader. This
application-owned installer does not use the 9588 SDK or any firmware dump.
"""
from pathlib import Path,PurePosixPath
import argparse,os,shutil,tempfile,urllib.request,zipfile
from common import ROOT,require,sha,local,run

URL='https://static.grumpycoder.net/pixel/mips/g++-mipsel-none-elf-15.2.0.zip'
ARCHIVE_SHA='8ba866e25c9826ee04ab4310365d264e3e73769e3738bb58ae38fd6740b7ee8d'

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,help='Existing verified ZIP; input may be outside this repository');p.add_argument('--output',type=Path,default=ROOT/'.tools/toolchain');a=p.parse_args()
 require(os.name=='nt','The pinned binary archive is Windows-only; supply native MIPS GCC15.2.0 with build.py --toolchain on Linux')
 out=local(a.output);require(not out.exists(),'Existing toolchain preserved; configure --toolchain for reuse')
 archive=a.archive.resolve() if a.archive else ROOT/'.tools/g++-mipsel-none-elf-15.2.0.zip'
 if a.archive:require(archive.is_file(),'--archive input missing')
 elif not archive.exists():
  archive.parent.mkdir(parents=True,exist_ok=True);temporary=archive.with_suffix('.download');require(not temporary.exists(),'Existing partial download preserved')
  request=urllib.request.Request(URL,headers={'User-Agent':'BBKH1-DOTA2/0.27 (+https://github.com/HelloClyde/BBKH1-DOTA2)','Accept':'application/octet-stream'})
  with urllib.request.urlopen(request,timeout=60) as response,temporary.open('xb') as target:shutil.copyfileobj(response,target,1024*1024)
  require(sha(temporary)==ARCHIVE_SHA,'Downloaded toolchain SHA mismatch; no extraction performed');temporary.replace(archive)
 require(sha(archive)==ARCHIVE_SHA,'Toolchain SHA mismatch; no extraction performed');out.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(dir=out.parent,prefix='h1-toolchain-stage-') as folder:
  stage=Path(folder).resolve();require(stage.is_relative_to(ROOT),'Toolchain stage escaped repository')
  with zipfile.ZipFile(archive) as z:
   for member in z.infolist():
    name=PurePosixPath(member.filename.replace('\\','/'));target=(stage/Path(*name.parts)).resolve()
    require(not name.is_absolute() and '..' not in name.parts and ':' not in member.filename and target.is_relative_to(stage),'Unsafe toolchain ZIP path')
    require(((member.external_attr>>16)&0o170000)!=0o120000,'Toolchain ZIP symlink refused')
   z.extractall(stage)
  compilers=list(stage.rglob('bin/mipsel-none-elf-gcc.exe'));require(len(compilers)==1,'Unexpected toolchain layout')
  require('15.2.0' in run([compilers[0],'--version']).splitlines()[0],'Toolchain compiler version differs')
  shutil.copytree(compilers[0].parent.parent,out)
 print('SHA-verified GNU MIPS15.2.0 installed in '+str(out))
if __name__=='__main__':main()
