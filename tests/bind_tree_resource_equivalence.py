"""Bind unchanged candidate3 MIPS outcomes to candidate4's sparse TREE asset.

Read-only verification of binaries, current source inputs, geometry and previous
receipts. This does not execute MIPS, render a new frame or open USB.
"""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8'))
def main():
 old=ROOT/'build/v029/candidate3';new=ROOT/'build/v029/candidate4';out=ROOT/'provenance/verification/v0.29/candidate4';out.mkdir(parents=True,exist_ok=True);assert not(out/'binding.json').exists()
 oldmeta=read(old/'DOTA2.build.json');meta=read(new/'DOTA2.build.json');expected_bda='fe492e687875ec31168778f266d5010e4479b89a0ae901acc061b8722d95bcca';expected_elf='56c8d9f169c379223b2b0040a999840d34613ec51833586f45d35e249130efa7';binary={}
 for name,wanted in [('DOTA2.bda',expected_bda),('DOTA2.elf',expected_elf)]:
  a,b=(old/name).read_bytes(),(new/name).read_bytes();assert a==b and hashlib.sha256(b).hexdigest()==wanted
  binary[name]=dict(bytes=len(b),candidate3_sha256=sha(old/name),candidate4_sha256=sha(new/name),full_bytes_compared=True,byte_identical=True)
 assert oldmeta['bda_sha256']==meta['bda_sha256']==expected_bda and oldmeta['elf_sha256']==meta['elf_sha256']==expected_elf
 assert not meta.get('diagnostic_only',False)and meta['binary']['abi']=={'DotaGame':178044,'DotaUnit':240,'DotaView':112}
 assert set(oldmeta['source_sha256'])==set(meta['source_sha256'])
 current={n:sha(ROOT/n)for n in meta['source_sha256']};assert current==meta['source_sha256'];changes=[dict(path=n,candidate3_sha256=oldmeta['source_sha256'][n],candidate4_sha256=v)for n,v in meta['source_sha256'].items()if oldmeta['source_sha256'][n]!=v]
 assert {d['path']for d in changes}=={'assets/runtime/manifest.json','tools/bake_tree_layers.py','tools/common.py'}
 for n in oldmeta['source_sha256']:
  if n.startswith(('game/','platform/','private/generated/')):assert oldmeta['source_sha256'][n]==meta['source_sha256'][n]
 for n,record in meta['runtime_assets'].items():assert sha(ROOT/record['path'])==record['sha256']and(ROOT/record['path']).stat().st_size==record['bytes']
 original=ROOT/'build/v029/tree-bake/pack/TREE.H1T';asset=ROOT/'assets/runtime/TREE.H1T';oldtree=original.read_bytes();tree=asset.read_bytes();assert len(tree)==17539264 and sha(asset)=='cb665855d9b4d8c20f2e0603492ed4425cb11a2c3763f51eadf30c5d9162aa80';assert sha(original)==oldmeta['runtime_assets']['TREE.H1T']['sha256']
 header=list(struct.unpack_from('<32I',tree));crc=header[31];header[31]=0;assert zlib.crc32(struct.pack('<32I',*header))==crc;assert header[:3]==[0x31525448,1,128]and header[3]==len(tree)and header[11:17]==[2113,128,8580,25484,41324,24];assert zlib.crc32(tree[128:41324])==header[19]
 canonical=ROOT/'provenance/vision/trees.json';frames=ROOT/'provenance/tree-visual/frame-manifest.json';records=read(canonical)['trees'];ids=read(frames)['ids'];assert len(ids)==len(records)==2113
 xy=b''.join(struct.pack('<2H',t['x_q8']//256,t['y_q8']//256)for t in records);bbox=b''.join(struct.pack('<4H',*t['bbox'])for t in ids)
 assert all(t['x_q8']%256==t['y_q8']%256==0 for t in records)
 assert tree[128:8580]==oldtree[128:8580]==xy and tree[8580:25484]==oldtree[8580:25484]==bbox
 for i,(a,b)in enumerate(zip(records,ids)):assert all(a[k]==b[k]for k in('hammer_id','x_q8','y_q8','radius_q8'))
 proof_path=ROOT/'build/v029/tree-bake/pack-sparse/active-pixel-equivalence.json';proof=read(proof_path);qa_path=ROOT/'build/v029/tree-bake/pack-sparse/qa-verification.json';qa=read(qa_path)
 assert proof['passed']and proof['old_pack_sha256']==sha(original)and proof['new_pack_sha256']==sha(asset)and proof['id_and_bbox_tables_identical']and proof['all_layer_bytes_identical']and proof['all_active_base_values_identical']and proof['all_cut_regrow_patterns_equivalent_under_runtime_mask_contract']
 assert qa['passed']and qa['sha256']==sha(asset)and qa['all_nonempty_tiles_inverse_crc_verified']==364
 inherited=[]
 for name in ('tree-gameplay-mips.json','vision-mips.json'):
  p=ROOT/'provenance/verification/v0.29/candidate3'/name;d=read(p);assert d['passed']and d['source_current']and d['actual_mips32']and d['bda_sha256']==expected_bda and d['elf_sha256']==expected_elf and d['metadata_sha256']==sha(old/'DOTA2.build.json')
  inherited.append(dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),original_candidate='candidate3',cases=len(d['cases']),checks=d['checks'],public_ELF_calls=d['public_ELF_calls'],actual_MIPS_execution_inherited=True,reexecuted_for_candidate4=False,original_metadata_sha256=d['metadata_sha256'],original_TREE_sha256=d['assets_sha256'].get('TREE.H1T'),basis='Identical complete BDA and ELF bytes; identical2113 IDs/xy/bbox; SCENE/VISION provider assets unchanged.'))
 for name,record in meta['runtime_assets'].items():
  if name!='TREE.H1T':assert record==oldmeta['runtime_assets'][name]
 copies={}
 for source,name in [(proof_path,'sparse-tree-active-pixel-equivalence.json'),(qa_path,'sparse-tree-qa.json'),(new/'DOTA2.build.json','candidate4-build-metadata.json'),(old/'DOTA2.build.json','candidate3-build-metadata.json')]:
  target=out/name
  with target.open('xb')as f:f.write(source.read_bytes())
  assert sha(target)==sha(source);copies[name]=dict(source=source.relative_to(ROOT).as_posix(),sha256=sha(target),exact_copy=True)
 report=dict(passed=True,scope=__doc__,binding_only=True,actual_MIPS_executed_for_candidate4=False,physical_H1_tested=False,USB_opened=False,previous_reports_modified=False,binaries=binary,metadata=dict(candidate3_sha256=sha(old/'DOTA2.build.json'),candidate4_sha256=sha(new/'DOTA2.build.json'),source_current=True,input_count=len(current),all_current_input_sha256=current,changed_input_receipts=changes,runtime_C_H_ASM_generated_bytes_unchanged=True),tree=dict(path=asset.relative_to(ROOT).as_posix(),bytes=len(tree),sha256=sha(asset),previous_bytes=len(oldtree),previous_sha256=sha(original),saved_bytes=len(oldtree)-len(tree),header_and_table_CRC_verified=True,canonical_ID_count=2113,canonical_tree_source_sha256=sha(canonical),canonical_frame_manifest_sha256=sha(frames),xy_plane_byte_identical_to_original_and_canonical=True,xy_plane_sha256=hashlib.sha256(xy).hexdigest(),bbox_plane_byte_identical_to_original_and_canonical=True,bbox_plane_sha256=hashlib.sha256(bbox).hexdigest(),borrowed_prior_sparse_pixel_proof_sha256=sha(proof_path),new_renderer_execution_performed=False),inherited_actual_MIPS_reports=inherited,archived_exact_copies=copies,verifier_source_sha256=sha(__file__),limitations='This receipt inherits candidate3 actual-MIPS gameplay/vision outcomes. It does not claim a candidate4 MIPS rerun, new provider execution, physical-H1 upload validation, FPS, audio timing, or a fresh full renderer comparison. Sparse rendering equivalence is the separately archived prior pixel/CRC proof.')
 target=out/'binding.json';target.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert current=={n:sha(ROOT/n)for n in current};print(json.dumps(dict(passed=True,binding=target.relative_to(ROOT).as_posix(),sha256=sha(target),source_current=True,MIPS_reexecuted=False)))
if __name__=='__main__':main()
