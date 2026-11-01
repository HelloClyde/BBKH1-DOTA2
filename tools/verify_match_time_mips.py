"""Six narrow actual-final-BDA MIPS match-clock acceptance cases. No USB."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,struct,subprocess,sys,traceback
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path);p.add_argument('--output',type=Path);p.add_argument('--expected-bda-sha256');p.add_argument('--expected-elf-sha256');p.add_argument('--check-interface',action='store_true');args=p.parse_args()
    if args.check_interface:print(json.dumps(dict(prepared=True,bda_executed=False,entry_executed=False,USB_opened=False,cases=6,outcome_state_writes_after_setup=0)));return
    assert args.build and args.output,'Provide frozen final --build and fresh --output';build=args.build.resolve();out=args.output.resolve();assert out.is_relative_to(ROOT/'build/tests')and not out.exists();out.mkdir(parents=True);(out/'objects').mkdir();(out/'fixtures').mkdir()
    meta_path=build/'DOTA2.build.json';meta=json.loads(meta_path.read_text(encoding='utf8'));bda,elf=build/'DOTA2.bda',build/'DOTA2.elf';bda_sha,elf_sha,meta_sha=sha(bda),sha(elf),sha(meta_path)
    assert meta['title']=='DOTA2'and not meta.get('diagnostic_only',False)and bda_sha==meta['bda_sha256']and elf_sha==meta['elf_sha256']
    if args.expected_bda_sha256:assert bda_sha==args.expected_bda_sha256
    if args.expected_elf_sha256:assert elf_sha==args.expected_elf_sha256
    production=meta['source_sha256']
    for name,value in production.items():assert sha(ROOT/name)==value,('Stale compiled input',name)
    pack=ROOT/'assets/runtime/SCENE.H1P';vision=ROOT/'assets/runtime/VISION.H1V'
    fixtures_source=ROOT/'tests/match_time_mips_fixtures.c';model_source=ROOT/'tests/mips_core_model.py'
    inputs=[Path(__file__).resolve(),fixtures_source,model_source,ROOT/'provenance/match-time/source.json'];before={x.relative_to(ROOT).as_posix():sha(x)for x in inputs}
    cc=Path('C:/msys64/ucrt64/bin/gcc.exe');env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','');logs=[]
    def run(command):
        result=subprocess.run(list(map(str,command)),env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=150);logs.append(result.stdout+result.stderr);assert not result.returncode,result.stdout+result.stderr;return result.stdout
    sources=[*sorted((ROOT/'game').glob('*.c')),ROOT/'platform/item_assets.c',ROOT/'platform/baked_io.c',fixtures_source]
    def compile_one(source):
        obj=out/'objects'/(source.stem+'.o');run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DBAKED_IO_HOST','-I',ROOT/'game','-I',ROOT/'platform','-c',source,'-o',obj]);return obj
    with ThreadPoolExecutor(max_workers=4)as pool:objects=list(pool.map(compile_one,sources))
    generator=out/'fixtures.exe';run([cc,*objects,'-o',generator]);layout=json.loads(run([generator,out/'fixtures',pack]));(out/'layout.json').write_text(json.dumps(layout,indent=2)+'\n',encoding='utf8');(out/'host-compile.txt').write_text(''.join(logs),encoding='utf8')
    assert(layout['game_size'],layout['unit_size'],layout['input_size'])==(178044,240,20)
    dependencies=ROOT.parent/'.tools/dota3d-deps'
    if dependencies.is_dir():sys.path.insert(0,str(dependencies))
    sys.path.insert(0,str(ROOT/'tests'));from mips_core_model import CoreMachine
    assets={'SCENE.H1P':pack}
    if vision.is_file():assets['VISION.H1V']=vision
    asset_hashes={name:sha(path)for name,path in assets.items()}
    for name,receipt in meta.get('runtime_assets',{}).items():
        if name in assets and isinstance(receipt,dict)and 'sha256'in receipt:assert asset_hashes[name]==receipt['sha256'],('Runtime asset differs from final build',name)
    machine=CoreMachine(bda,elf,assets);assert machine.symbol_sizes['view']==112;gptr=machine.symbols['dota_game'];input_ptr=0x81500000;checks=setup_writes=0;records=[]
    def check(condition,label):
        nonlocal checks;checks+=1
        if not condition:raise AssertionError((label,records[-1:]))
    def core(name,*arguments):return machine.call(name,*arguments)
    def game(field):return machine.integer(gptr+layout[field])
    def unit(index,field):return machine.integer(gptr+layout['units']+index*layout['unit_size']+layout['unit'][field])
    def setup(name):
        nonlocal setup_writes
        data=(out/'fixtures'/name).read_bytes();check(len(data)==layout['game_size']==machine.symbol_sizes['dota_game'],'Initial Game ABI equals final ELF');machine.write(gptr,data);machine.write(input_ptr,bytes(layout['input_size']));setup_writes+=2;core('dota_sight_reset')
    def tick(n=1):
        for _ in range(n):core('dota_tick',gptr,input_ptr)
    def count(kind):return sum(unit(i,'active')and unit(i,'type')==layout['types'][kind] for i in range(layout['unit_count']))
    def rune(spot):return machine.integer(gptr+layout['rune_kind']+spot*4)
    def pass_case(name):records.append(dict(case=name,passed=True));print('CASE '+name,flush=True)
    try:
        check(core('baked_io_open_default')==1 and core('item_assets_open')==1,'Real item provider opens');check(core('item_assets_reserve_rules_cache',65536)>0,'Original provider cache reserved')
        setup('pregame.bin');check(game('tick')==0 and core('dota_match_seconds',gptr)==-60,'Nonnegative elapsed zero displays original Turbo-1:00');gold,mana=unit(0,'gold'),unit(0,'mana');check(core('dota_cast',gptr,0,1800,2500)==1 and unit(0,'mana')<mana,'Actual pregame cast consumes mana');cooldown=unit(0,'cooldown');tick(20);check(game('tick')==20 and unit(0,'cooldown')==cooldown-20 and unit(0,'gold')==gold,'Relative cooldown runs while pregame gold stays frozen');check(game('wave')==0 and count('creep')==0 and all(rune(i)<0 for i in range(4)),'No lane units or runes before horn');core('dota_set_paused',gptr,1);previous=machine.read(gptr,layout['game_size']);tick(4);check(machine.read(gptr,layout['game_size'])==previous,'Pause freezes the complete Game byte for byte');pass_case('playable-pregame-no-early-spawns-gold-and-pause')
        setup('horn.bin');check(core('dota_match_seconds',gptr)==-1,'Final pre-horn tick still displays-0:01');tick();check(core('dota_match_seconds',gptr)==0 and game('wave')==1 and count('creep')==18,'Horn starts first balanced18-unit wave');check(all(rune(i)==5 for i in range(4)),'Four initial Bounties appear exactly at horn');check(unit(layout['roshan_index'],'active')and unit(layout['roshan_index'],'max_hp')==6000,'Roshan begins at zero growth');setup('wave.bin');core('dota_set_paused',gptr,1);tick(2);check(game('wave')==1,'Paused next wave waits');core('dota_set_paused',gptr,0);tick();check(game('wave')==2 and core('dota_match_seconds',gptr)==30,'Second wave occurs at game0:30');pass_case('horn-first-wave-four-bounty-and-thirty-second-next-wave')
        setup('camps.bin');check(count('neutral')==0 and count('ancient')==0,'No initial normal or ancient camps before1:00');tick();check(count('neutral')>0 and count('ancient')==6 and core('dota_match_seconds',gptr)==60,'Normal and ancient camps spawn at match1:00');check(unit(layout['roshan_index'],'max_hp')==6130,'Roshan grows by one game minute only');pass_case('normal-and-ancient-camps-at-game-one-minute')
        setup('lotus.bin');check(machine.integer(gptr+layout['lotus_count'])==0,'Lotus absent before1:30');tick();check(machine.integer(gptr+layout['lotus_count'])==1 and machine.integer(gptr+layout['lotus_count']+4)==1,'Turbo Lotus spawns at match1:30');pass_case('turbo-lotus-horn-relative-ninety-seconds')
        setup('water.bin');tick();check(core('dota_match_seconds',gptr)==120 and rune(0)==7 and rune(1)==7,'Water runes at match2:00 after minute-zero Bounties');pass_case('water-runes-at-match-two-minutes')
        setup('night.bin');check(core('dota_match_is_night',gptr)==0 and core('dota_match_seconds',gptr)==299,'Day lasts until4:59.95');tick();check(core('dota_match_is_night',gptr)==1 and core('dota_match_seconds',gptr)==300,'Night begins at match5:00');pass_case('day-night-five-minute-boundary-after-horn')
        check(len(records)==6,'Six final cases complete')
        for name,value in production.items():assert sha(ROOT/name)==value,('Production changed during MIPS acceptance',name)
        check(before=={x.relative_to(ROOT).as_posix():sha(x)for x in inputs},'Runner source remains current');check(sha(bda)==bda_sha and sha(elf)==elf_sha and sha(meta_path)==meta_sha,'Exact final BDA ELF metadata unchanged')
        check(asset_hashes=={name:sha(path)for name,path in assets.items()},'Actual runtime assets unchanged during MIPS acceptance')
        result=dict(passed=True,actual_core=True,actual_mips32=True,source_current=True,physical_h1_tested=False,USB_opened=False,entry_executed=False,checks=checks,cases=records,public_ELF_calls=machine.calls,initial_setup_writes=setup_writes,outcome_state_writes_after_setup=0,bda_sha256=bda_sha,elf_sha256=elf_sha,metadata_sha256=meta_sha,compiled_source_sha256=production,source_sha256=before,assets_sha256=asset_hashes,fixture_sha256={x.relative_to(ROOT).as_posix():sha(x)for x in(out/'fixtures').glob('*.bin')},fixture_generator_sha256=sha(generator),layout_sha256=sha(out/'layout.json'),game_bytes=layout['game_size'],unit_bytes=layout['unit_size'],scope='Exact final BDA public core APIs with modeled read-only file/heap services. Host fixtures establish initial boundary conditions only; no entry/MXU/audio/native controller or physical H1/FPS claim.')
        (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(passed=True,cases=len(records),checks=checks,public_ELF_calls=machine.calls,bda_sha256=bda_sha)))
    except Exception as error:
        (out/'failure.json').write_text(json.dumps(dict(passed=False,error=str(error),cases_completed=records,source_sha256=before,bda_sha256=bda_sha,elf_sha256=elf_sha,USB_opened=False,outcome_state_writes_after_setup=0),indent=2)+'\n',encoding='utf8');(out/'failure.txt').write_text(traceback.format_exc(),encoding='utf8');raise
    finally:machine.dispose()
if __name__=='__main__':main()
