"""Execute the exact BDA's match persistence on a modeled writable H1 filesystem.

No application entry, LCD/audio, physical USB or device filesystem is accessed.
Completed games are explicit initial fixtures; this is not a played victory.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from elftools.elf.elffile import ELFFile
from mips_core_model import CoreMachine
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

class WritableH1(CoreMachine):
 def __init__(self,bda,elf):
  super().__init__(bda,elf,{})
  self.limit=None;self.written=0;self.service_calls=0
  for offset,fn in ((12,self.file_write),):
   at=0x80001500;self.callbacks[at]=fn;self.word(0x80003000+offset,at)
  self.callbacks[0x80001540]=self.service
 def service(self,*_):self.service_calls+=1;return 0
 def file_open(self,path,mode,*_):
  name=self.string(path).replace('\\','/').upper();access=self.string(mode)
  assert access in ('rb','wb'),access
  if access=='wb':self.assets[name]=bytearray()
  elif name not in self.assets:return 0
  h=self.next_handle;self.next_handle+=4;self.handles[h]=[name,0,access];return h
 def file_read(self,dest,size,count,handle,*_):
  rec=self.handles[handle];data=self.assets[rec[0]]
  if not size or not count:return 0
  got=min(size*count,max(0,len(data)-rec[1]))//size*size
  if got:self.write(dest,bytes(data[rec[1]:rec[1]+got]))
  rec[1]+=got;self.bytes_read+=got;return got//size
 def file_write(self,source,size,count,handle,*_):
  rec=self.handles[handle];assert rec[2]=='wb'
  if not size or not count:return 0
  wanted=size*count
  got=wanted if self.limit is None else min(wanted,max(0,self.limit-self.written))
  got=got//size*size;data=self.assets[rec[0]];end=rec[1]+got
  if len(data)<end:data.extend(bytes(end-len(data)))
  if got:data[rec[1]:end]=self.read(source,got)
  rec[1]=end;self.written+=got;return got//size

def game_layout(elf):
 with Path(elf).open('rb')as stream:
  e=ELFFile(stream)
  for cu in e.get_dwarf_info().iter_CUs():
   for die in cu.iter_DIEs():
    n=die.attributes.get('DW_AT_name')
    if die.tag!='DW_TAG_typedef' or not n or n.value!=b'DotaGame':continue
    target=die.get_DIE_from_attribute('DW_AT_type')
    if 'DW_AT_byte_size' not in target.attributes:continue
    fields={d.attributes['DW_AT_name'].value.decode():d.attributes['DW_AT_data_member_location'].value for d in target.iter_children() if d.tag=='DW_TAG_member'}
    return fields,target.attributes['DW_AT_byte_size'].value
 raise AssertionError('Missing final Game DWARF')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir(parents=True)
 build=a.build.resolve();meta=json.loads((build/'DOTA2.build.json').read_text());assert sha(build/'DOTA2.bda')==meta['bda_sha256'] and sha(build/'DOTA2.elf')==meta['elf_sha256']
 compiled={name:wanted for name,wanted in meta['source_sha256'].items() if Path(name).suffix.lower()in('.c','.h','.s','.ld')}
 assert all(sha(ROOT/name)==wanted for name,wanted in compiled.items()),'Compiled source identity changed'
 report=dict(passed=False,scope=__doc__,physical_h1_tested=False,device_accessed=False,bda_sha256=meta['bda_sha256'],elf_sha256=meta['elf_sha256'],compiled_source_sha256=compiled,test_source_sha256={p.relative_to(ROOT).as_posix():sha(p)for p in (Path(__file__).resolve(),ROOT/'tests/mips_core_model.py')},checks=[])
 def check(name,condition,**details):
  report['checks'].append(dict(name=name,passed=bool(condition),**details));assert condition,name
 m=WritableH1(build/'DOTA2.bda',build/'DOTA2.elf')
 try:
  layout,game_bytes=game_layout(build/'DOTA2.elf');gp=m.symbols['dota_game'];check('Final Game layout',game_bytes==m.symbol_sizes['dota_game'])
  m.call('match_storage_set_service',0x80001540,0)
  check('Open empty history',m.call('match_storage_open')==1)
  hp=m.call('match_storage_history')&0xffffffff;check('History allocated outside BSS',hp in m.allocations and m.integer(hp)==0)
  def finished(seed):
   m.call('dota_init',gp,0,seed);ident=m.call('match_storage_begin_match');check('Nonzero match identity',ident>0)
   m.word(gp+layout['match_stats'],ident);m.word(gp+layout['state'],1);m.word(gp+layout['tick'],24000)
   return ident
  first=finished(1101);check('Completed fixture saved',m.call('match_storage_complete',gp)==1)
  check('One completed match only',m.integer(hp)==1)
  first_files={n:bytes(data)for n,data in m.assets.items()}
  check('Same result not appended again',m.call('match_storage_complete',gp)==0 and m.integer(hp)==1 and first_files=={n:bytes(data)for n,data in m.assets.items()})
  check('Result available for UI',m.call('match_storage_result')!=0)
  check('Streaming I/O services background audio',m.service_calls>50,service_calls=m.service_calls)
  m.call('match_storage_close');check('Close frees history and file handles',not m.allocations and not m.handles)
  check('Reopen first record',m.call('match_storage_open')==1);hp=m.call('match_storage_history')&0xffffffff;check('First persists',m.integer(hp)==1)
  second=finished(1102);check('Unique next identity',second!=first)
  m.limit=2048;m.written=0;failed=m.call('match_storage_complete',gp)
  check('Short write reported',failed<0,result=failed)
  check('Last valid slot preserved',all(bytes(m.assets[n])==data for n,data in first_files.items()))
  m.limit=None;m.call('match_storage_close');check('Failed commit closes handles',not m.handles and not m.allocations)
  check('Recover after partial inactive slot',m.call('match_storage_open')==1);hp=m.call('match_storage_history')&0xffffffff;check('Recovery retains first history',m.integer(hp)==1)
  third=finished(1103);check('New completion saved after recovery',m.call('match_storage_complete',gp)==1)
  check('Two persisted records',m.integer(hp)==2)
  files={n:bytes(data)for n,data in m.assets.items()if len(data)==52064};check('Two complete independent slots',len(files)==2)
  m.call('match_storage_close');check('Successful close frees owned heap',not m.allocations and not m.handles)
  # Corrupt the latest fully written slot in the modeled files only. A valid
  # older slot must still load; no user data is used or touched by this model.
  latest=max(files,key=lambda n:struct.unpack_from('<I',files[n],32)[0])
  raw=bytearray(files[latest]);raw[-1]^=1;m.assets[latest]=raw
  check('Reopen with corrupt newest slot',m.call('match_storage_open')==1);hp=m.call('match_storage_history')&0xffffffff
  check('CRC falls back to one intact record',m.integer(hp)==1)
  m.call('match_storage_close');check('Final cleanup',not m.allocations and not m.handles)
  current=all(sha(ROOT/name)==wanted for name,wanted in compiled.items());assert current
  report.update(passed=True,source_current=current,actual_mips_api_calls=m.calls,service_calls=m.service_calls,game_bytes=game_bytes,files={n:dict(bytes=len(d),sha256=hashlib.sha256(d).hexdigest())for n,d in files.items()})
 except Exception as exc:report['error']=str(exc);raise
 finally:
  (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');m.dispose()
 print(json.dumps(dict(passed=True,checks=len(report['checks']),actual_mips_api_calls=report['actual_mips_api_calls'],report=str(out/'verification.json'),device_accessed=False)))
if __name__=='__main__':main()
