"""Read-only installed Steam native evidence for the Turbo match clock.

No DLL is executed, no Valve asset pack is written, and no H1/USB is accessed.
The fixed build must match before any address or instruction is accepted.
"""
from pathlib import Path
import argparse, hashlib, json, struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
ROOT=Path(__file__).resolve().parents[1]
SERVER_SHA='af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server',type=Path,default=Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota/bin/win64/server.dll'))
    parser.add_argument('--output',type=Path,default=ROOT/'provenance/match-time/source.json')
    args=parser.parse_args();data=args.server.read_bytes();assert sha(args.server)==SERVER_SHA
    pe=pefile.PE(data=data,fast_load=True);base=pe.OPTIONAL_HEADER.ImageBase;md=Cs(CS_ARCH_X86,CS_MODE_64)
    def raw(va):return pe.get_offset_from_rva(va-base)
    def instruction(va):return next(md.disasm(data[raw(va):raw(va)+15],va))
    def rip(va):
        op=instruction(va);assert 'rip' in op.op_str
        return op.address+op.size+struct.unpack_from('<i',op.bytes,len(op.bytes)-4)[0]
    def float_at_load(va):return struct.unpack_from('<f',data,raw(rip(va)))[0]
    def text(va):
        at=raw(va);return data[at:data.index(0,at)].decode('ascii')
    def block(va,size):
        at=raw(va);payload=data[at:at+size]
        return dict(va=hex(va),file_offset=hex(at),bytes_hex=payload.hex(),asm=[f'{i.address:x} {i.mnemonic} {i.op_str}' for i in md.disasm(payload,va)])
    assert text(rip(0x1802729a2))=='dota_pregame_time' and float_at_load(0x18027299a)==90
    assert text(rip(0x18175b859))=='SetPreGameTime'
    assert rip(0x18175b8a9)==0x181848320
    setter=block(0x181848320,66)
    assert instruction(0x181848326).op_str=='rbx, [rcx + 0x424]'
    assert instruction(0x181848353).mnemonic=='movss' and instruction(0x181848353).op_str=='dword ptr [rbx], xmm6'
    assert float_at_load(0x1818e988e)==60
    call=instruction(0x1818e9899);assert call.mnemonic=='call' and int(call.op_str,16)==0x181848320
    # Bind the real Turbo initialization to its primary MSVC RTTI vtable.
    type_raw=data.index(b'.?AVCDOTATurboGameMode@@')-16;type_rva=pe.get_rva_from_offset(type_raw)
    candidates=[];cursor=0
    while (cursor:=data.find(struct.pack('<I',type_rva),cursor))>=0:
        if cursor>=12:
            values=struct.unpack_from('<6I',data,cursor-12)
            if values[:4]==(1,0,0,type_rva) and values[5]==pe.get_rva_from_offset(cursor-12):candidates.append(cursor-12)
        cursor+=1
    assert len(candidates)==1
    locator=candidates[0];table=data.index(struct.pack('<Q',base+pe.get_rva_from_offset(locator)))+8
    assert rip(0x1818c5dac)==base+pe.get_rva_from_offset(table)
    assert instruction(0x1818c5db3).op_str=='qword ptr [rbx], rax'
    assert instruction(0x1818c5eb6).op_str=='0x1818e9770'
    assert text(rip(0x180273ac2))=='dota_time_of_day_rate'
    rate=float_at_load(0x180273aba);assert abs(rate-1/600)<0.00000001
    assert text(rip(0x1803d3382))=='dota_neutral_initial_spawn_delay'
    assert float_at_load(0x1803d337a)==60
    assert struct.unpack_from('<f',data,0x2ef34fc)[0]==30
    assert rip(0x1826060d1)==base+pe.get_rva_from_offset(0x2ef34fc)
    assert instruction(0x1826060dc).op_str=='0x182600790'
    assert text(rip(0x1803d318c))=='dota_effective_creep_spawn_time'
    assert instruction(0x1803d31ca).op_str=='xmm2, xmm2' # native default0
    result=dict(passed=True,read_only=True,server_path=str(args.server.resolve()),server_sha256=SERVER_SHA,
                primary_turbo=dict(type_descriptor_raw=hex(type_raw),complete_object_locator_raw=hex(locator),vtable_raw=hex(table),constructor_va='0x1818c5d90',initializer_va='0x1818e9770'),
                facts=dict(turbo_pregame_seconds=60,ordinary_pregame_default_seconds=90,
                           lane_wave_interval_seconds=30,effective_creep_spawn_time_default_seconds=0,
                           neutral_first_spawn_seconds=60,daynight_full_cycle_seconds=600,day_phase_seconds=300,
                           native_day_rate=rate),
                proof=dict(ordinary_pregame_constructor=block(0x180272990,151),
                           turbo_constructor=block(0x1818c5d90,313),turbo_initialization=block(0x1818e9770,362),
                           script_SetPreGameTime_binding=block(0x18175b843,120),
                           SetPreGameTime=setter,day_rate_constructor=block(0x180273ab0,158),
                           neutral_initial_constructor=block(0x1803d3370,158),
                           lane_spawner_cadence=block(0x18260608d,100),
                           effective_creep_spawn_constructor=block(0x1803d3180,149)),
                model=dict(elapsed_tick='Nonnegative simulation time since playable pregame begins; relative durations keep this clock.',
                           signed_match_tick='elapsed_tick -60*DOTA_HZ; negative before horn, zero at horn.',
                           displayed_seconds='Ceiling remaining magnitude before horn, floor after horn; no negative-zero second.',
                           match_schedules='Lane0/30s; neutral60s; existing Lotus, wisdom, rune, Tormentor and Roshan growth/migration anchors are shifted to horn-relative time.',
                           existing_limits='This change preserves the existing bounded lane composition, camp blocking, Roshan/Tormentor configured mode rules and random behavior; no new unverified Turbo multipliers.'),
                extractor_sha256=sha(__file__),USB_opened=False,DLL_executed=False)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(passed=True,turbo_pregame_seconds=60,day_phase_seconds=300,source_sha256=sha(args.output))))
if __name__=='__main__':main()
