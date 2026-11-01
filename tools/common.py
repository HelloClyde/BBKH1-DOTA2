"""Shared standalone build contract; no legacy workspace or device access."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'sdk'
SDK_COMMIT='067fe072477861dfc8949d7b1a55279fb92d2548'
REFERENCE_BDA='608b9fdf3c2751b0c14378663e638819a749e7603b45d231001f9b097f43184d'
VERSION=(ROOT/'VERSION').read_text(encoding='ascii').strip()
if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?',VERSION):raise ValueError('VERSION must be a numeric release version')
BUILD_DATE='2026-11-01 00:00:00';ENTRY=0x83c00020;RAM_LIMIT=0x83ff0000
PLATFORM_SOURCES=('entry.S','native.c','render.c','status_effects.c','baked_scene.c','baked_io.c','item_assets.c','shop_catalog.c','dota_audio.c','map_codec.c','map_format.c','vision_assets.c')
PACKAGE_DOCS=('assets/NOTICE.md','docs/BUILDING.md','docs/CONTROLS.md','docs/provenance/fonts.json','docs/verification/v0.27-h1.json','docs/screenshots/h1-gameplay.png','docs/screenshots/h1-shop-recommended.png','docs/screenshots/h1-pause-help.png','docs/screenshots/h1-shop-upgrades.png','docs/screenshots/host-necro-five-skills.png','docs/screenshots/manifest.json','sdk/LICENSE','sdk/NOTICE')
PACKAGE_DOCS+=('docs/verification/v0.28-h1.json','docs/screenshots/h1-v028-wards.png','docs/screenshots/h1-v028-fog.png')
ASSETS={
 'SCENE.H1P':(406028212,'c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd'),
 'SOUND.H1A':(10226944,'a2be46eb35f2a86a483d154332aeb9cf44f3a24b6427e52e7a5619df01283d30'),
 'MAP12.H1C':(16593196,'bcc35594f9d48edda2befb16fdef29f220763a7c2fc96b0c11487f6d86d9c31a'),
 'VISION.H1V':(139328,'f23c061e9735e02bd098fe49adb723c56641c7ab0cc946e97b82c44ef9e7dc28')}

def require(ok,message):
 if not ok:raise ValueError(message)
def sha(path):
 with Path(path).open('rb') as stream:return digest(stream)
def digest(stream):
 h=hashlib.sha256()
 for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf8'))

def release_reference():
 if VERSION=='0.27':return REFERENCE_BDA
 path=ROOT/'provenance/releases'/('v'+VERSION+'.json')
 if not path.is_file():return None
 record=read(path);value=record.get('bda_sha256','')
 require(record.get('version')==VERSION and re.fullmatch('[0-9a-f]{64}',value),'Invalid release identity')
 return value

def release_pin_identity():
 value=release_reference()
 if value is None:return None
 if VERSION=='0.27':return dict(version=VERSION,bda_sha256=value,scope='Historical fixed v0.27 identity')
 path=ROOT/'provenance/releases'/('v'+VERSION+'.json')
 return dict(path=relative(path),sha256=sha(path),version=VERSION,bda_sha256=value)
def write(path,value):Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def local(path):
 p=Path(path);p=(p if p.is_absolute() else ROOT/p).resolve();require(p.is_relative_to(ROOT),'Output escapes this repository: '+str(p));return p
def relative(path):return Path(path).resolve().relative_to(ROOT).as_posix()
def run(command):
 r=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace')
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 if r.stderr:print(r.stderr,file=sys.stderr)
 return r.stdout
def sdk_check():
 require((SDK/'h1_bda/header.py').is_file() and (SDK/'sdk/include/h1_sdk.h').is_file(),'Initialize the fixed H1 SDK: git submodule update --init sdk')
 command=['git','-c','safe.directory='+SDK.as_posix(),'-C',SDK]
 commit=run([*command,'rev-parse','HEAD']).strip();require(commit==SDK_COMMIT,'H1 SDK commit differs: '+commit)
 require(not run([*command,'diff','HEAD','--']).strip(),'Tracked H1 SDK modifications are not reproducible')
 sys.path.insert(0,str(SDK));return commit
def find_tool(folder,name):
 folder=Path(folder).expanduser().resolve()
 for suffix in ('.exe',''):
  p=folder/('mipsel-none-elf-'+name+suffix)
  if p.is_file():return p
 raise FileNotFoundError('Missing mipsel-none-elf-'+name+' in '+str(folder)+'; run tools/install_toolchain.py or configure --toolchain')
def source_files():
 sources=[ROOT/'platform'/name for name in PLATFORM_SOURCES]+sorted((ROOT/'game').glob('*.c'))
 require(len(sources)==31,'Unexpected translation-unit inventory')
 for p in sources:require(p.is_file(),'Missing source '+relative(p))
 return sources
def input_hashes():
 files=set(source_files())|{ROOT/'platform/h1.ld',ROOT/'private/generated/icon.png'}
 for folder in ('game','platform','private/generated','private/audio/assets'):
  files.update((ROOT/folder).glob('*.h'))
 files.update((SDK/'sdk/include').glob('*.h'))
 files.update((ROOT/'tools').glob('*.py'));files.add(ROOT/'tools/requirements-build.txt')
 files.update((ROOT/'VERSION',ROOT/'provenance/source-curation.json',ROOT/'assets/runtime/manifest.json'))
 # A release pin is created after candidate/device verification. Binding it as
 # a compiler input would make that same verified candidate stale on promotion.
 # Packaging binds the pin's own SHA separately without rebuilding the binary.
 return {relative(p):sha(p) for p in sorted(files)}
def curation_check():
 path=ROOT/'provenance/source-curation.json';m=read(path)
 require(m['original_bda_sha256']==REFERENCE_BDA and m['sdk_submodule_commit']==SDK_COMMIT and m['original_build_metadata_sha256']=='44790bf35c5f8e5f82aefde796af5c6c85ddbe2f87c000637c9203076f4a83bd','Source origin differs')
 require(len(m['files'])==128,'Curated closure differs')
 changed=[]
 for item in m['files']:
  path=local(item['path']);matches=path.is_file() and path.stat().st_size==item['curated_bytes'] and sha(path)==item['curated_lf_sha256']
  if not matches:changed.append(item['path'])
  if VERSION=='0.27':require(matches,'Curated LF source changed: '+item['path'])
  if VERSION=='0.28' and item['path'] in ('platform/dota_audio.c','platform/dota_audio.h','platform/audio_music_windows.h'):
   require(matches,'Frozen v0.28 audio source changed: '+item['path'])
 if VERSION=='0.27':require([item['source'] for item in m['translation_units']]==[relative(p) for p in source_files()],'Source order differs from original build')
 return dict(path='provenance/source-curation.json',sha256=sha(ROOT/'provenance/source-curation.json'),original_bda_sha256=REFERENCE_BDA,original_metadata_sha256=m['original_build_metadata_sha256'],newline_policy=m['newline_policy'],changed_since_v027=changed,scope='Historical v0.27 source origin. Current build inputs are independently bound in source_sha256.')
def assets_check():
 folder=ROOT/'assets/runtime';manifest=read(folder/'manifest.json')
 require(manifest.get('format_version')==1 and manifest['historical_bda_sha256']==REFERENCE_BDA,'Runtime manifest contract differs')
 require({item['path']:(item['bytes'],item['sha256']) for item in manifest['assets']}==ASSETS,'Runtime manifest differs from the verified original assets')
 for name,(size,wanted) in ASSETS.items():
  path=folder/name
  require(path.is_file() and path.stat().st_size==size and sha(path)==wanted,'Missing, Git LFS pointer or changed runtime asset: '+name+'; run git lfs pull')
 return {name:dict(path='assets/runtime/'+name,bytes=size,sha256=wanted) for name,(size,wanted) in ASSETS.items()}
def binary_check(bda,elf,payload_bytes):
 from elftools.elf.elffile import ELFFile
 from h1_bda.validate import validate_bda
 from h1_bda.header import decode_header,read_c_string,BUILD_TIME_OFFSET,BUILD_TIME_SIZE
 from h1_bda.resources import PAYLOAD_OFFSET
 validation=validate_bda(bda);require(validation['ok'] and validation['title']=='DOTA2','Invalid DOTA2 BDA envelope')
 raw=bda.read_bytes();require(read_c_string(decode_header(raw)[BUILD_TIME_OFFSET:BUILD_TIME_OFFSET+BUILD_TIME_SIZE])==BUILD_DATE,'BDA date differs')
 sizes={name:set() for name in ('DotaGame','DotaUnit','DotaView')}
 with elf.open('rb') as stream:
  e=ELFFile(stream);require(e.little_endian and e.elfclass==32 and e['e_machine']=='EM_MIPS','ELF target differs')
  syms={s.name:int(s['st_value']) for s in e.get_section_by_name('.symtab').iter_symbols()}
  chunks=[(int(s['sh_addr']),s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS' and s['sh_size']]
  require(min(start for start,_ in chunks)==ENTRY,'Application load address differs')
  reconstructed=bytearray(max(start+len(data) for start,data in chunks)-ENTRY)
  for start,data in chunks:reconstructed[start-ENTRY:start-ENTRY+len(data)]=data
  for cu in e.get_dwarf_info().iter_CUs():
   for die in cu.iter_DIEs():
    n=die.attributes.get('DW_AT_name');name=n.value.decode() if n and isinstance(n.value,bytes) else ''
    if name in sizes and die.tag=='DW_TAG_typedef':
     target=die.get_DIE_from_attribute('DW_AT_type');size=target.attributes.get('DW_AT_byte_size')
     if size:sizes[name].add(int(size.value))
 require(sizes=={'DotaGame':{173544},'DotaUnit':{240},'DotaView':{100}},'Actual ELF ABI differs')
 require(syms['__app_stack_top']-syms['__bss_end']==65536 and syms['__app_stack_top']<RAM_LIMIT,'Private stack/application RAM window differs')
 require(bytes(reconstructed)==raw[PAYLOAD_OFFSET:PAYLOAD_OFFSET+payload_bytes] and len(reconstructed)==payload_bytes,'ELF allocated sections differ from actual BDA payload')
 require('bs_render_game' in syms and 's3d_render_game' not in syms and 'dota_mxu_exact_rgb565' in syms and 'dota_mxu_rgb565' not in syms,'Wrong rendering/presentation implementation')
 require('h1_cold_io_profile' not in syms and 'h1_native_profile' not in syms,'Diagnostic build cannot be packaged')
 return dict(abi={name:next(iter(values)) for name,values in sizes.items()},entry=hex(ENTRY),stack_bytes=65536,application_ram_end=hex(syms['__app_stack_top']),ram_limit=hex(RAM_LIMIT),ram_margin_bytes=RAM_LIMIT-syms['__app_stack_top'],static_bss_bytes=syms['__bss_end']-syms['__bss_start'],payload_matches_elf=True)
