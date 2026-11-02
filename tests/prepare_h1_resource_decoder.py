"""Extract real H1R chunks and build bounded decoder EXEC modules locally.

This script never opens USB. It uses resident h1_debug.build_module and accepts
valid cached heap arena bases. --base only selects an offline compile address;
the default is an offline sizing address, never a claimed device allocation.
For physical execution use ../tools/h1_debug.py exec run <module.c> --directory
<new-output> --opcode 0 --args-hex a0 --budget-ticks 240. That command gets the
fresh resident EXEC_INFO base and rebuilds there; do not use old RunnerClient.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys,zlib
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pack',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--base',type=lambda x:int(x,0),default=0x83c00020);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
 sys.path.insert(0,str(ROOT.parent/'tools'));from h1_debug import build_module
 pack=a.pack.resolve();d=pack.read_bytes();h=struct.unpack_from('<32I',d);assert d[:8]==b'DOTA2H1R' and h[2]==1 and h[4]==len(d);records=[]
 for page in range(h[9]):
  first,count,at,size,crc,res=struct.unpack_from('<6I',d,h[10]+page*24);assert res==0 and crc==zlib.crc32(d[at:at+size]);records.extend(struct.unpack_from('<5I',d,at+i*20) for i in range(count))
 receipt=dict(pack_sha256=sha(pack),source_sha256=sha(ROOT/'tests/h1_resource_decoder.c'),prepare_sha256=sha(Path(__file__)),builder='h1_debug.build_module',builder_sha256=sha(ROOT.parent/'tools/h1_debug.py'),offline_compile_base=a.base,physical_h1_tested=False,USB_opened=False,scope=__doc__,vectors=[])
 for codec in range(3):
  candidates=[(i,r) for i,r in enumerate(records) if r[4]==codec];assert candidates
  # Largest raw and then largest compressed input is conservative for module
  # memory and useful for timing real detailed chunks rather than all zeros.
  ident,record=max(candidates,key=lambda ir:(ir[1][2],ir[1][1]));at,stored,raw,crc,_=record;body=d[at:at+stored];canonical=body if codec==0 else zlib.decompress(body,-15)
  if codec==2:
   n=raw//2;previous=0;result=bytearray()
   for i in range(n):
    z=canonical[i]|canonical[n+i]<<8;delta=(z>>1)^(-(z&1));previous=(previous+delta)&65535;result+=struct.pack('<H',previous)
   canonical=result
  assert len(canonical)==raw and zlib.crc32(canonical)==crc
  folder=out/f'codec{codec}';folder.mkdir();source=folder/'module.c';source.write_text(f'#define BENCH_CODEC {codec}\n#define BENCH_CHUNK_ID {ident}u\n#define BENCH_RAW_BYTES {raw}u\n#define BENCH_CANONICAL_CRC 0x{crc:08x}u\nstatic const unsigned char bench_vector[{stored}]={{\n'+''.join(' '+','.join(f'0x{x:02x}' for x in body[i:i+24])+',\n' for i in range(0,len(body),24))+'};\n#include "'+(ROOT/'tests/h1_resource_decoder.c').as_posix()+'"\n',encoding='ascii')
  metadata,binary=build_module(source,a.base,folder/'module');assert metadata['memory_size']<=65536
  receipt['vectors'].append(dict(codec=codec,chunk_id=ident,physical_offset=at,stored_bytes=stored,raw_bytes=raw,canonical_crc32=crc,canonical_sha256=hashlib.sha256(canonical).hexdigest(),stored_sha256=hashlib.sha256(body).hexdigest(),source=str(source),source_sha256=sha(source),module=metadata));print(f'codec{codec}: raw={raw}, stored={stored}, image={metadata["image_size"]}, memory={metadata["memory_size"]}')
 receipt['output_u32_fields']=['version','codec','chunk_id','raw_bytes','stored_bytes','expected_crc32','actual_crc32','iterations','begin_raw_tick','end_raw_tick','elapsed_raw_ticks','sum_decode_raw_ticks','max_decode_raw_ticks','decoded_bytes','flags','opcode','decoder_state_bytes','iteration_cap','requested_raw_ticks','reserved'];receipt['execution']='Opcode 0 includes canonical CRC every iteration; opcode 1 times decode+inverse and verifies final CRC once. Optional one-byte args is duration 1..240 raw ticks, default 160. Use runner budget > duration, e.g. 240 for default. Every chunk is <=16384 bytes and polls before/after; fixed cap 4096 iterations. EXEC guard flags must be checked. ABI tick_80hz is a historical name, not a verified frequency; physical H1 previously measured about40 ticks/second. Preserve raw ticks and PC completion intervals; convert throughput only with measured timer frequency or measured wall time. Individual decode tick counts are coarse. No FPS claim.'
 receipt['source_current']=receipt['source_sha256']==sha(ROOT/'tests/h1_resource_decoder.c')and receipt['prepare_sha256']==sha(Path(__file__))and receipt['builder_sha256']==sha(ROOT.parent/'tools/h1_debug.py')
 historical=ROOT/'build/v030/decoder-exec/preparation.json'
 if historical.exists():
  old=json.loads(historical.read_text());receipt['historical_preparation']=dict(path=str(historical),sha256=sha(historical),recorded_prepare_sha256=old['prepare_sha256'],matches_current_prepare=old['prepare_sha256']==receipt['prepare_sha256'],offline_base_only=True,physical_h1_tested=False,note='Historical fixed offline base sizing and MIPS correctness evidence; not evidence of the current preparation script or physical arena allocation.')
 assert receipt['source_current'];(out/'preparation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print(out/'preparation.json')
if __name__=='__main__':main()
