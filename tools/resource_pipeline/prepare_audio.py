"""Prepare original installed Dota2 sounds for bounded native H1 playback.

Only the new private/audio tree is written. Original VPK resources are CRC
verified; Source2Viewer and FFmpeg run offline. H1 receives no Valve decoder.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, re, struct, subprocess, sys, zlib
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from source2_vpk import VpkArchive

RATE, BLOCK, SAMPLES = 32000, 256, 505
HEROES = ['axe', 'lina', 'juggernaut', 'crystalmaiden', 'slark', 'necrolyte', 'pudge', 'sniper', 'sven', 'tidehunter']
# Attack, impact, and CAST/STRIKE pairs for Q/W/E/R/shard. None means an
# intrinsic passive or an event which has no independent original sound.
EVENTS = [
 ['Hero_Axe.Attack','Hero_Axe.Attack.Impact',None,'Hero_Axe.Berserkers_Call',None,'Hero_Axe.Battle_Hunger',None,'Hero_Axe.CounterHelix',None,'Hero_Axe.Culling_Blade_Success',None,None],
 ['Hero_Lina.attack','Hero_Lina.ProjectileImpact','Hero_Lina.DragonSlave.Cast','Hero_Lina.DragonSlave','Ability.PreLightStrikeArray','Ability.LightStrikeArray',None,None,'Ability.LagunaBlade','Ability.LagunaBladeImpact',None,None],
 ['Hero_Juggernaut.Attack','Hero_Juggernaut.Attack.Rip','Hero_Juggernaut.BladeFuryStart',None,'Hero_Juggernaut.HealingWard.Cast',None,None,'Hero_Juggernaut.BladeDance','Hero_Juggernaut.OmniSlash','Hero_Juggernaut.OmniSlash.Damage',None,None],
 ['hero_Crystal.attack','hero_Crystal.projectileImpact','hero_Crystal.CrystalNovaCast','Hero_Crystal.CrystalNova',None,'hero_Crystal.frostbite',None,None,'hero_Crystal.freezingField.wind','hero_Crystal.freezingField.explosion',None,None],
 ['Hero_Slark.Attack',None,'Hero_Slark.DarkPact.PreCast','Hero_Slark.DarkPact.Cast','Hero_Slark.Pounce.Cast','Hero_Slark.Pounce.Impact','Hero_Slark.SaltwaterShiv.PreAttack','Hero_Slark.SaltwaterShiv.Target','Hero_Slark.ShadowDance',None,'Hero_Slark.DepthShroud',None],
 ['Hero_Necrolyte.Attack','Hero_Necrolyte.ProjectileImpact',None,'Hero_Necrolyte.DeathPulse','Hero_Necrolyte.SpiritForm.Cast',None,None,None,'Hero_Necrolyte.ReapersScythe.Cast','Hero_Necrolyte.ReapersScythe.Target','Hero_Necrolyte.DeathSeeker',None],
 ['Hero_Pudge.Attack','Hero_Pudge.Attack.Rip','Hero_Pudge.AttackHookExtend','Hero_Pudge.AttackHookImpact','Hero_Pudge.Rot',None,None,None,'Hero_Pudge.Dismember.Cast','Hero_Pudge.Dismember',None,None],
 ['Hero_Sniper.attack','Hero_Sniper.ProjectileImpact','Hero_Sniper.ShrapnelShoot','Hero_Sniper.ShrapnelShatter',None,'Hero_Sniper.HeadShot','Hero_Sniper.TakeAim.Cast',None,'Ability.AssassinateLoad','Ability.Assassinate','Hero_Sniper.ConcussiveGrenade.Cast','Hero_Sniper.ConcussiveGrenade.Target'],
 ['Hero_Sven.Attack','Hero_Sven.Attack.Impact','Hero_Sven.StormBolt','Hero_Sven.StormBoltImpact',None,'Hero_Sven.GreatCleave','Hero_Sven.WarCry',None,'Hero_Sven.GodsStrength',None,None,None],
 ['hero_tidehunter.Attack','Hero_Tidehunter.Attack.Post','Ability.GushCast','Ability.GushImpact',None,'Hero_Tidehunter.KrakenShell',None,'Hero_Tidehunter.AnchorSmash','Ability.Ravage','Hero_Tidehunter.RavageDamage','Hero_Tidehunter.DeadInTheWater.Cast','Hero_Tidehunter.DeadInTheWater.Target'],
]
MUSIC = ['music/ui_main_01','music/laning_01_layer_01','music/battle_01','stingers/radiant_win','stingers/dire_win']
GENERIC = ['sounds/ui/buttonclick.vsnd_c','sounds/ui/buttonclickrelease.vsnd_c','sounds/ui/coins.vsnd_c',
           'sounds/ui/notification_team_kill.vsnd_c','sounds/ui/select_unit.vsnd_c',
           'sounds/ambient/soundscapes/waves_close_lp_1.vsnd_c']

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(args):
    p = subprocess.run(list(map(str,args)),capture_output=True,text=True)
    if p.returncode: raise RuntimeError(p.stdout+p.stderr)
    return p.stdout
def wav_chunks(data):
    assert data[:4]==b'RIFF' and data[8:12]==b'WAVE'
    at=12; chunks={}
    while at+8<=len(data):
        n=struct.unpack_from('<I',data,at+4)[0];assert at+8+n<=len(data)
        chunks[data[at:at+4]]=data[at+8:at+8+n];at+=8+n+(n&1)
    return chunks
def parse_events(text):
    # Only one-tab outer event keys; nested values have two or more tabs.
    starts=list(re.finditer(r'^\t([^\s][^=\n]*?)\s*=\s*$',text,re.M)); out={}
    for i,m in enumerate(starts):
        body=text[m.end():starts[i+1].start() if i+1<len(starts) else len(text)]
        paths=re.findall(r'"(sounds[^"\n]+?\.vsnd)"',body)
        paths=list(dict.fromkeys(x.replace('\\','/')+'_c' for x in paths))
        volume=re.search(r'^\t\tvolume\s*=\s*([0-9.]+)',body,re.M)
        out[m[1].strip()]={'files':paths,'volume':float(volume[1]) if volume else 1.0}
    return out
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vpk',type=Path,default=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/pak01_dir.vpk'))
    p.add_argument('--cli',type=Path,default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    p.add_argument('--ffmpeg',type=Path);p.add_argument('--workers',type=int,default=4)
    a=p.parse_args()
    if not a.ffmpeg:
        import imageio_ffmpeg
        a.ffmpeg=Path(imageio_ffmpeg.get_ffmpeg_exe())
    v=VpkArchive(a.vpk);src=ROOT/'private/audio/source';assets=ROOT/'private/audio/assets'
    converted=assets/'converted';src.mkdir(parents=True,exist_ok=True);converted.mkdir(parents=True,exist_ok=True)
    identities={}; group_specs=[]; hero_groups=[]; unique=[]; asset_map={}
    def group(paths,name,volume=1.0):
        ids=[]
        for path in paths[:3]:
            if path not in v.entries:raise RuntimeError('Missing original sound '+path)
            if path not in asset_map:asset_map[path]=len(unique);unique.append(path)
            ids.append(asset_map[path])
        if not ids:return 65535
        group_specs.append(dict(event=name,assets=ids,volume=volume));return len(group_specs)-1
    # Music entries must precede effects. Retain exact source stems; the single
    # H1 lane/battle choice is an explicit adaptation of Valve's layered score.
    music_ids=[]
    for name in MUSIC:
        path='sounds/music/valve_dota_001/'+name+'.vsnd_c';music_ids.append(len(unique));asset_map[path]=len(unique);unique.append(path)
    for h,hero in enumerate(HEROES):
        entry='soundevents/game_sounds_heroes/game_sounds_'+hero+'.vsndevts_c';identities[entry]=v.identity(entry)
        event_file=src/(hero+'.txt');identity_file=src/(hero+'.identity.json')
        previous=json.loads(identity_file.read_text()) if identity_file.exists() else {}
        if not event_file.exists() or previous.get('source_sha256')!=identities[entry]['sha256'] or previous.get('decompiled_sha256')!=sha(event_file) or previous.get('cli_sha256')!=sha(a.cli):
            run([a.cli,'-i',a.vpk,'-f',entry,'-d','-o',event_file])
            identity_file.write_text(json.dumps(dict(source_sha256=identities[entry]['sha256'],decompiled_sha256=sha(event_file),cli_sha256=sha(a.cli)),indent=2)+'\n')
        events=parse_events(event_file.read_text(encoding='utf8')); row=[]
        for name in EVENTS[h]:
            if name is None:row.append(65535);continue
            if name not in events:raise RuntimeError(hero+': missing original event '+name)
            e=events[name];row.append(group(e['files'],name,e['volume']))
        assert row[0]!=65535 and any(x!=65535 for x in row[2:]),hero
        hero_groups.append(row)
    generic=[group([x],x) for x in GENERIC]
    # Original music event provenance, rather than inferred filename-only music.
    entry='soundevents/music/valve_dota_001/soundevents_music.vsndevts_c';identities[entry]=v.identity(entry)
    music_event=src/'classic-music.txt'
    run([a.cli,'-i',a.vpk,'-f',entry,'-d','-o',music_event])
    entries=[None]*len(unique)
    def convert(pair):
        index,path=pair; identity=v.identity(path);slug=hashlib.sha256(path.encode()).hexdigest()[:20]
        source=src/(slug+'.decoded');pcm=converted/(slug+'.pcm');adpcm=converted/(slug+'.wav');proof=converted/(slug+'.json')
        if not proof.exists() or json.loads(proof.read_text())['source_sha256']!=identity['sha256']:
            run([a.cli,'-i',a.vpk,'-f',path,'-d','-o',source])
            run([a.ffmpeg,'-v','error','-nostdin','-y','-i',source,'-ac','1','-ar',RATE,'-f','s16le',pcm])
            frames=pcm.stat().st_size//2
            run([a.ffmpeg,'-v','error','-nostdin','-y','-f','s16le','-ar',RATE,'-ac','1','-i',pcm,'-c:a','adpcm_ima_wav','-block_size',BLOCK,adpcm])
            chunks=wav_chunks(adpcm.read_bytes());fmt=chunks[b'fmt ']
            assert struct.unpack_from('<HHI',fmt)==(0x11,1,RATE) and struct.unpack_from('<H',fmt,12)[0]==BLOCK
            encoded=chunks[b'data'];assert len(encoded)%BLOCK==0 and 0<frames<=len(encoded)//BLOCK*SAMPLES
            proof.write_text(json.dumps({'source_sha256':identity['sha256'],'frames':frames,'pcm_sha256':sha(pcm),'adpcm_sha256':hashlib.sha256(encoded).hexdigest()},indent=2)+'\n')
        meta=json.loads(proof.read_text());encoded=wav_chunks(adpcm.read_bytes())[b'data']
        assert hashlib.sha256(encoded).hexdigest()==meta['adpcm_sha256']
        return index,path,identity,meta,encoded
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,a.workers)) as pool:
        for index,path,identity,meta,encoded in pool.map(convert,enumerate(unique)):
            identities[path]=identity;entries[index]=(path,meta,encoded);print(index,path,meta['frames'],len(encoded),flush=True)
    data_at=(64+len(entries)*32+255)&~255;payload=bytearray(data_at);records=[];manifest=[]
    for index,(path,meta,encoded) in enumerate(entries):
        offset=len(payload);assert offset%256==0;payload.extend(encoded)
        music=index<len(MUSIC);loop=int(music and index<3)
        records.append(struct.pack('<8I',offset,len(encoded),meta['frames'],loop,128 if music else 256,int(not music),zlib.crc32(encoded)&0xffffffff,index))
        manifest.append(dict(id=index,path=path,**meta,offset=offset,bytes=len(encoded),family='music' if music else 'effect',loop=bool(loop)))
    index_bytes=b''.join(records);payload[64:64+len(index_bytes)]=index_bytes
    struct.pack_into('<16I',payload,0,0x31414448,1,len(payload),len(entries),64,32,data_at,RATE,BLOCK,SAMPLES,zlib.crc32(index_bytes)&0xffffffff,0,0,0,0,0)
    pack=assets/'SOUND.H1A';pack.write_bytes(payload)
    flattened=[]; groups=[]
    for item in group_specs:groups.append((len(flattened),len(item['assets']),max(0,min(1024,round(item['volume']*256))),0));flattened.extend(item['assets'])
    header=['#ifndef DOTA_AUDIO_DATA_H','#define DOTA_AUDIO_DATA_H','#include <stdint.h>',f'#define DOTA_AUDIO_ASSET_COUNT {len(entries)}',f'#define DOTA_AUDIO_PACK_BYTES {len(payload)}u','#define DOTA_AUDIO_HEROES 10','#define DOTA_AUDIO_GROUP_NONE 65535u',f'#define DOTA_AUDIO_GROUP_COUNT {len(groups)}',
            'typedef struct {uint16_t first,count,gain,pad;} DotaAudioGroup;',
            'static const uint16_t audio_music[5]={'+','.join(map(str,music_ids))+'};',
            'static const uint16_t audio_generic[6]={'+','.join(map(str,generic))+'};',
            'static const uint16_t audio_hero[10][12]={'+','.join('{'+','.join(map(str,row))+'}' for row in hero_groups)+'};',
            'static const DotaAudioGroup audio_groups[]={'+','.join('{%d,%d,%d,%d}'%x for x in groups)+'};',
            'static const uint16_t audio_group_assets[]={'+','.join(map(str,flattened))+'};','#endif','']
    (assets/'audio_data.h').write_text('\n'.join(header),encoding='utf8')
    report=dict(complete=True,heroes=HEROES,music_tracks=5,effect_assets=len(entries)-5,total_assets=len(entries),groups=group_specs,
                sample_rate=RATE,channels=1,pcm_bits=16,block_bytes=BLOCK,block_frames=SAMPLES,pack_bytes=len(payload),pack_sha256=sha(pack),
                assets=manifest,source_resources=identities,source_event_maps=hero_groups,
                source_sha256={'tools/prepare_audio.py':sha(__file__)},source2_cli_sha256=sha(a.cli),ffmpeg_sha256=sha(a.ffmpeg),
                adaptations=['Single original menu/laning/battle stem instead of the complete adaptive Valve music-layer engine.',
                             '32kHz mono IMA ADPCM prepared offline; native output is PCM.',
                             'None entries are passive/no-independent-sound events, never substituted with invented hero effects.'])
    (assets/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:report[k] for k in ['complete','total_assets','effect_assets','pack_bytes','pack_sha256']}))
if __name__=='__main__':main()
