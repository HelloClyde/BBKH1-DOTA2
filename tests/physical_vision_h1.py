"""Opt-in physical H1 acceptance through normal inputs and read-only RAM.

Requires the separately installed H1Debug workspace/client. This is not part
of the standalone build or CI and never writes gameplay outcomes to RAM.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--debug-workspace',type=Path,required=True);a=p.parse_args();app=a.build.resolve();out=a.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    (out/'driver-executed.py').write_bytes(Path(__file__).read_bytes())
    sys.path.insert(0,str(a.debug_workspace.resolve()/'tools'));from h1_debug import Client
    with (app/'DOTA2.elf').open('rb') as stream:
        elf=ELFFile(stream);symbols={s.name:(int(s['st_value']),int(s['st_size'])) for s in elf.get_section_by_name('.symtab').iter_symbols()};layouts={};sizes={}
        wanted={'DotaGame','DotaUnit','DotaView','DotaItemsState','DotaItemWorldState','DotaItemWorldUnit','SightWork'}
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                name=die.attributes.get('DW_AT_name')
                if die.tag!='DW_TAG_typedef' or not name or name.value.decode() not in wanted:continue
                d=die.get_DIE_from_attribute('DW_AT_type')
                while d.tag=='DW_TAG_typedef':d=d.get_DIE_from_attribute('DW_AT_type')
                if 'DW_AT_byte_size' not in d.attributes:continue
                key=name.value.decode();sizes[key]=d.attributes['DW_AT_byte_size'].value
                layouts[key]={x.attributes['DW_AT_name'].value.decode():x.attributes['DW_AT_data_member_location'].value for x in d.iter_children() if x.tag=='DW_TAG_member'}
    assert set(layouts)==wanted and [sizes[k] for k in ('DotaGame','DotaUnit','DotaView')]==[173544,240,100]
    meta=json.loads((app/'DOTA2.build.json').read_text(encoding='utf8'));assert meta['bda_sha256']==sha(app/'DOTA2.bda')
    r=dict(passed=False,physical_h1_tested=True,normal_input_only=True,game_state_ram_writes=0,bda_sha256=sha(app/'DOTA2.bda'),elf_sha256=sha(app/'DOTA2.elf'),driver_sha256=sha(__file__),layouts=layouts,sizes=sizes,checks=[],actions=[],screenshots=[],profiles=[])
    def save():(out/'verification.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    save()
    with Client() as c:
        def words(name,count):return list(struct.unpack('<'+'I'*count,c.read_memory(symbols[name][0],count*4)))
        def field(typ,name,index=0):
            base=symbols['view' if typ=='DotaView' else 'dota_game'][0]
            if typ=='DotaUnit':base+=layouts['DotaGame']['units']+index*sizes[typ]
            return struct.unpack('<i',c.read_memory(base+layouts[typ][name],4))[0]
        def inventory():return list(struct.unpack('<6i',c.read_memory(symbols['dota_game'][0]+layouts['DotaGame']['units']+layouts['DotaUnit']['inventory'],24)))
        def charges(slot):return struct.unpack('<H',c.read_memory(symbols['dota_game'][0]+layouts['DotaGame']['items']+layouts['DotaItemsState']['charges']+2*slot,2))[0]
        def wards():
            result=[]
            for i in range(32):
                u=223+i
                if not field('DotaUnit','active',u) or field('DotaUnit','hp',u)<=0:continue
                base=symbols['dota_game'][0]+layouts['DotaGame']['item_world']+layouts['DotaItemWorldState']['unit']+i*sizes['DotaItemWorldUnit']
                state={n:struct.unpack('<i',c.read_memory(base+layouts['DotaItemWorldUnit'][n],4))[0] for n in ('kind','owner','until')}
                if state['kind'] in (2,3):result.append(dict(slot=u,hp=field('DotaUnit','hp',u),x=field('DotaUnit','x',u),y=field('DotaUnit','y',u),**state))
            return result
        def wait(test,label,seconds=45):
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                if test():return
                time.sleep(.08)
            raise AssertionError(label)
        def drawn():
            frames=words('h1_dota_debug',8)[1];wait(lambda:words('h1_dota_debug',8)[1]>=frames+2,'two completed frames')
        def key(name,draw=True):
            r['actions'].append(dict(key=name,receipt=c.press([name],ticks=20)));time.sleep(.18);c.release()
            if draw:drawn()
            save()
        def tap(x,y):
            r['actions'].append(dict(tap=[x,y],receipt=c.tap(x,y,hold_seconds=.18)));drawn();save()
        def screen(n):wait(lambda:words('h1_dota_debug',8)[2]==n,'screen '+str(n));drawn()
        def check(name,condition,**evidence):
            r['checks'].append(dict(name=name,passed=bool(condition),**evidence));save();print(json.dumps(dict(check=name,passed=bool(condition))),flush=True);assert condition,name
        def shot(name):
            path=out/(name+'.png');receipt=c.screenshot(path);r['screenshots'].append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),receipt=receipt));save()
        def profile(name,seconds=6):
            audio_before=words('h1_dota_audio_debug',32);raw_before=c.status()['tick']
            t0=time.monotonic();before=words('h1_dota_debug',8);t1=time.monotonic();start=(t0+t1)/2
            time.sleep(seconds)
            t0=time.monotonic();after=words('h1_dota_debug',8);t1=time.monotonic();elapsed=(t0+t1)/2-start
            raw_after=c.status()['tick'];audio_after=words('h1_dota_audio_debug',32)
            entry=dict(name=name,frames=after[1]-before[1],pc_seconds=elapsed,fps=(after[1]-before[1])/elapsed,pc_timing='Midpoints of completed-frame counter reads; status RPCs excluded',raw_device_ticks=raw_after-raw_before,game_ticks=after[6]-before[6],audio_before=audio_before,audio=audio_after,fog=words('fog_stats',5))
            if 'fog_cache_stats' in symbols:entry['fog_cache']=words('fog_cache_stats',6)
            if 'h1_shared_shadow_profile' in symbols:entry['shared_shadow']=words('h1_shared_shadow_profile',5)
            r['profiles'].append(entry);save();print(json.dumps(dict(profile=name,fps=entry['fps'])),flush=True)
        try:
            r['resident_before']=c.status();r['desktop_before']=c.desktop_status();assert r['desktop_before']['menu_idle']==1 and r['resident_before']['state']==1
            r['launch']=c.desktop_run(r'A:\应用\程序\DOTA2.bda');save();screen(0)
            sys.path.insert(0,str(ROOT/'sdk'));from h1_bda.resources import PAYLOAD_OFFSET
            payload=(app/'DOTA2.bda').read_bytes()[PAYLOAD_OFFSET:];identity={}
            for name in ('h1_dota_present','dota_sight_attach','vision_assets_open','dota_extra_tick'):
                offset=symbols[name][0]-0x83c00020
                identity[name]=c.read_memory(symbols[name][0],32)==payload[offset:offset+32]
            check('Loaded code samples match the device-CRC-verified deployment',all(identity.values()),samples=identity)
            r['cache_live']=words('h1_dota_cache',16);r['vision_live']=words('vision_stats',16)
            if 'fog_cache_stats' in symbols:r['fog_cache_live']=words('fog_cache_stats',6)
            vision_header=struct.unpack('<16I',(ROOT/'assets/runtime/VISION.H1V').read_bytes()[:64]);expected_heap=vision_header[10]+sizes['SightWork']
            check('Required terrain sidecar validated, closed and resident on heap',r['vision_live'][1]==1 and r['vision_live'][2]==expected_heap and r['vision_live'][5]==1 and r['vision_live'][6:9]==[0,0,0] and r['vision_live'][10]==sizes['SightWork'] and r['vision_live'][12]==vision_header[11] and r['cache_live'][1]==3 and r['cache_live'][15]==expected_heap,expected_heap=expected_heap)
            check('MXU exact conversion remains selected without RGB table',words('mxu_ready',1)[0]==1 and words('rgb565_table',1)[0]==0)
            key('ENTER');screen(1);key('K')
            tick=field('DotaGame','tick')
            check('Pregame has no lane wave or passive income before horn',tick<1200 and field('DotaGame','wave')==0 and field('DotaUnit','gold')==600,tick=tick,gold=field('DotaUnit','gold'))
            profile('pregame-fountain')
            key('BACK');screen(2);before=c.read_memory(symbols['dota_game'][0],sizes['DotaGame']);time.sleep(2);after=c.read_memory(symbols['dota_game'][0],sizes['DotaGame'])
            check('Pause freezes complete simulation',before==after,tick=field('DotaGame','tick'));key('BACK');screen(1)
            key('M');screen(3);key('L');check('Shop vision shortcut selects Observer',field('DotaView','shop_item')==55);shot('ward-shop');key('ENTER');key('L');check('Repeated shop shortcut selects Sentry',field('DotaView','shop_item')==56);key('ENTER')
            inv=inventory();check('Both purchased wards combine in equipment',406 in inv,inventory=inv);slot=inv.index(406);packed=charges(slot);check('Combined ward contains one of each kind',packed==257,packed=packed)
            key('BACK');screen(1);button=('Z','X','C','V','B','N')[slot]
            cooldown=symbols['dota_game'][0]+layouts['DotaGame']['items']+layouts['DotaItemsState']['cooldown']+2*slot
            key(button);shot('forbidden-ward-preview');key('ENTER')
            check('Fountain placement rejected without charge, cooldown or ward allocation',field('DotaView','aim_item')==slot and charges(slot)==257 and struct.unpack('<H',c.read_memory(cooldown,2))[0]==0 and not wards())
            key('ESCAPE')
            # Leave the actual fountain polygon through ordinary movement.
            r['actions'].append(dict(key_hold=['RIGHT','UP'],receipt=c.press(['RIGHT','UP'],ticks=240)));save()
            wait(lambda:field('DotaUnit','x')>=540,'walk out of fountain ward exclusion',6);c.release();key('K');key('SPACE')
            key(button);check('Ward begins Observer point aiming',field('DotaView','aim_item')==slot and field('DotaView','ward_mode')==0)
            key(button);check('Same inventory key switches to Sentry',field('DotaView','ward_mode')==1);shot('sentry-placement-preview');key('ENTER')
            placed=wards();check('Sentry placed as 200 HP entity, consumes only Sentry charge',any(w['kind']==3 and w['hp']==200 and w['owner']==0 for w in placed) and charges(slot)==1,wards=placed,packed=charges(slot))
            wait(lambda:struct.unpack('<H',c.read_memory(cooldown,2))[0]==0,'ward cooldown elapsed')
            key(button);check('Remaining Observer mode selected',field('DotaView','ward_mode')==0);key('RIGHT');key('ENTER');placed=wards();check('Observer placed as distinct 200 HP entity',sum(w['owner']==0 for w in placed)==2 and {w['kind'] for w in placed}=={2,3},wards=placed);shot('placed-wards')
            key('SPACE');profile('fountain-with-wards')
            # Observe the natural horn before the remaining map screenshots.
            # Shop screenshots do not consume countdown time because it pauses.
            assert field('DotaGame','tick')<1180,'USB/test setup exhausted the pregame observation window'
            while field('DotaGame','tick')<1180:
                check('No early lane wave',field('DotaGame','wave')==0,tick=field('DotaGame','tick'));time.sleep(1)
            wait(lambda:field('DotaGame','wave')>=1,'first natural lane wave',15);horn_tick=field('DotaGame','tick')
            check('First lane wave begins at the natural Turbo horn',1200<=horn_tick<1250,tick=horn_tick,wave=field('DotaGame','wave'));shot('after-horn');profile('post-horn-fountain')
            tap(40,234);shot('midlane-fog');profile('midlane-panned-fog')
            # These counters are published during rendering, before present.
            # An arbitrary USB read may observe the next frame half-filled.
            fog_samples=[]
            def completed_fog():
                value=words('fog_stats',5);fog_samples.append(value)
                return value[0]==1440 and 0<value[1]<=1440 and value[2]>0
            wait(completed_fog,'completed fog counters',3)
            fog=fog_samples[-1];check('Terrain fog darkens hidden map with bounded grid queries',True,fog=fog,samples=fog_samples)
            key('SPACE')
            r['audio_before_exit']=words('h1_dota_audio_debug',32)
            check('Audio stays ready and playback advances without underruns',r['audio_before_exit'][1]==1 and r['audio_before_exit'][2]==0 and r['audio_before_exit'][6]==0 and all(x['audio'][5]>x['audio_before'][5] for x in r['profiles']),audio=r['audio_before_exit'])
            key('BACK');screen(2);key('X',draw=False);wait(lambda:c.desktop_status()['menu_idle']==1,'normal exit',30)
            r['audio_after']=words('h1_dota_audio_debug',32);r['cache_after']=words('h1_dota_cache',16);r['vision_after']=words('vision_stats',16);r['desktop_after']=c.desktop_status();r['resident_after']=c.status()
            if 'fog_cache_stats' in symbols:
                r['fog_cache_after']=words('fog_cache_stats',6)
                check('Fog background cache heap released',r['fog_cache_after'][0]==0)
            check('Normal exit releases vision, scene and audio heap',r['cache_after'][9]==0 and r['cache_after'][11]==1 and r['vision_after'][1:3]==[0,0] and r['vision_after'][15]==1 and r['audio_after'][1]==0 and r['audio_after'][18]==0)
            check('Debugger healthy on idle desktop',r['resident_after']['state']==1 and r['resident_after']['usb_fault']==r['resident_after']['bad_frames']==r['resident_after']['last_error']==0)
            r.update(passed=True,scope='Physical H1, exact installed BDA identity, normal inputs, natural countdown/horn, combined wards, fog/profile/audio and normal cleanup. Slark and five-minute boundaries covered by separate actual-MIPS fixtures.');save()
        except Exception as error:
            r['error']=str(error);save();raise
        finally:
            c.release();c.release_touch()

if __name__=='__main__':main()
