"""Opt-in physical H1 Pounce acceptance using normal inputs and read-only RAM.

Run after deploying the specified BDA and observing its menu. Never patches
Game, grants levels/items, fast-forwards time or invokes game code via EXEC.
"""
from pathlib import Path
import argparse, hashlib, json, struct, sys, time
from elftools.elf.elffile import ELFFile
from verify_item_input_mips import layouts_of

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--debug-workspace', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mode', choices=('both', 'keyboard', 'touch'), default='both')
    a = p.parse_args()
    build, out = a.build.resolve(), a.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    (out / 'driver-executed.py').write_bytes(Path(__file__).read_bytes())
    meta = json.loads((build / 'DOTA2.build.json').read_text())
    bda = build / 'DOTA2.bda'
    assert sha(bda) == meta['bda_sha256']
    with (build / 'DOTA2.elf').open('rb') as f:
        elf = ELFFile(f)
        symbols = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
        layouts, sizes = layouts_of(elf, {'DotaGame', 'DotaUnit', 'DotaView', 'DotaExtraHero', 'DotaExtraHeroesState'})
    report = dict(passed=False, scope=__doc__, physical_h1_tested=True,
                  game_state_ram_writes=0, bda_sha256=sha(bda),
                  elf_sha256=sha(build / 'DOTA2.elf'), driver_sha256=sha(__file__),
                  checks=[], actions=[], screenshots=[], cases=[])

    def save():
        (out / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')

    def check(label, ok, **evidence):
        report['checks'].append(dict(check=label, passed=bool(ok), **evidence))
        save()
        print(json.dumps(dict(check=label, passed=bool(ok))), flush=True)
        assert ok, label

    sys.path.insert(0, str(a.debug_workspace.resolve() / 'tools'))
    from h1_debug import Client
    with Client(timeout=10) as c:
        report['debug_identity'] = c.identity
        gp, vp = symbols['dota_game'][0], symbols['view'][0]
        up = gp + layouts['DotaGame']['units']
        ep = gp + layouts['DotaGame']['extra'] + layouts['DotaExtraHeroesState']['hero']

        def named(base, typename):
            data = c.read_memory(base, sizes[typename])
            return {n: struct.unpack_from('<i', data, off)[0] for n, off in layouts[typename].items()
                    if n not in ('learned', 'attribute_bonus_rank', 'autocast_mask', 'talents') and off + 4 <= len(data)}

        def view():
            return named(vp, 'DotaView')

        def unit():
            return named(up, 'DotaUnit')

        def extra():
            return named(ep, 'DotaExtraHero')

        def wait(test, label, seconds=15):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                debug = struct.unpack('<8I', c.read_memory(symbols['h1_dota_debug'][0], 32))
                if debug[7]:
                    raise AssertionError('Game runtime error ' + str(debug[7]))
                if test():
                    return
                time.sleep(.1)
            raise AssertionError(label)

        def key(*names, hold=.18, draw=True):
            report['actions'].append(dict(keys=names, receipt=c.press(names, ticks=80)))
            time.sleep(hold)
            c.release()
            time.sleep(.18)
            if draw:
                frame = struct.unpack('<I', c.read_memory(symbols['h1_dota_debug'][0] + 4, 4))[0]
                wait(lambda: struct.unpack('<I', c.read_memory(symbols['h1_dota_debug'][0] + 4, 4))[0] >= frame + 2,
                     'Two completed frames after input', 30)
            save()

        def shot(label):
            path = out / (label + '.png')
            receipt = c.screenshot(path)
            report['screenshots'].append(dict(path=path.relative_to(ROOT).as_posix(), sha256=sha(path), receipt=receipt))
            save()

        try:
            wait(lambda: struct.unpack('<I', c.read_memory(symbols['h1_dota_debug'][0] + 4, 4))[0] >= 3,
                 'Three fully presented startup frames', 150)
            wait(lambda: view()['screen'] == 0, 'Loaded menu', 150)
            # Loaded read-only code samples bind this device to this candidate.
            sys.path.insert(0, str(ROOT / 'sdk'))
            from h1_bda.resources import PAYLOAD_OFFSET
            payload = bda.read_bytes()[PAYLOAD_OFFSET:]
            for name in ('dota_extra_cast', 'dota_cast_order_skill_kind', 'dota_hero_facing', 'use_inventory'):
                address = symbols[name][0]
                expected = payload[address - 0x83c00020:address - 0x83c00020 + 32]
                check('Loaded code ' + name, c.read_memory(address, 32) == expected)
            for mode in (('keyboard', 'touch') if a.mode == 'both' else (a.mode,)):
                while view()['hero'] != 4:
                    key('RIGHT')
                key('ENTER')
                wait(lambda: view()['screen'] == 1, 'Started Slark game')
                check('Slark selected', unit()['hero_id'] == 4)
                key('SHIFT', 'W')
                rank = c.read_memory(up + layouts['DotaUnit']['learned'] + 1, 1)[0]
                check('Normal Shift W learns Pounce', rank == 1)
                before = unit()
                key('RIGHT', hold=.35)
                key('K')
                start = unit()
                check('Normal movement faces east', start['x'] > before['x'] and start['y'] == before['y'])
                shot(mode + '-before')
                if mode == 'keyboard':
                    key('W')
                else:
                    # HUD: 4 skills in x184..320; W occupies x218..252.
                    report['actions'].append(dict(tap=[244, 216], receipt=c.tap(244, 216, hold_seconds=.15)))
                wait(lambda: extra()['leap_rank'] > 0, 'Pounce starts without target confirmation', 3)
                state, after = extra(), unit()
                check(mode + ' Pounce immediately casts east',
                      view()['aim_skill'] == -1 and state['leap_to_x'] > state['leap_x'] and state['leap_to_y'] == state['leap_y'],
                      start=[state['leap_x'], state['leap_y']], end=[state['leap_to_x'], state['leap_to_y']])
                report['cases'].append(dict(mode=mode, passed=True, before=start, after=after, leap=state))
                shot(mode + '-after')
                wait(lambda: True, 'Scene stays healthy after Pounce lands')
                if mode == 'keyboard':
                    def frame_sample():
                        t0 = time.monotonic()
                        frames = struct.unpack('<I', c.read_memory(symbols['h1_dota_debug'][0] + 4, 4))[0]
                        return frames, (t0 + time.monotonic()) / 2
                    f0, t0 = frame_sample()
                    time.sleep(8)
                    f1, t1 = frame_sample()
                    report['post_pounce_fps_sample'] = dict(frames=f1-f0, seconds=t1-t0, fps=(f1-f0)/(t1-t0), scope='Single eight-second Slark post-landing scene; no baseline or all-scene FPS claim')
                    wait(lambda: True, 'Scene stays healthy during post-jump sample')
                key('ESCAPE')
                wait(lambda: view()['screen'] == 2, 'Pause screen')
                key('C')
                wait(lambda: view()['screen'] == 0, 'Return to menu')
            report['resource_stats'] = list(struct.unpack('<21I', c.read_memory(symbols['resource_status'][0], 84)))
            check('Resources healthy', report['resource_stats'][0] == 1 and report['resource_stats'][5] == 0)
            report['tree_stats'] = list(struct.unpack('<16I', c.read_memory(symbols['tree_stats'][0], 64)))
            check('Tree workspace remains healthy after both jumps', report['tree_stats'][1] == 1 and report['tree_stats'][2] == 0)
            report['cache_stats'] = list(struct.unpack('<16I', c.read_memory(symbols['h1_dota_cache'][0], 64)))
            report['audio_stats'] = list(struct.unpack('<32I', c.read_memory(symbols['h1_dota_audio_debug'][0], 128)))
            check('Audio remains ready without runtime errors', report['audio_stats'][1:3] == [1, 0])
            key('BACK', draw=False)
            wait(lambda: c.desktop_status()['menu_idle'] == 1, 'Normal desktop exit')
            report['desktop_after'] = c.desktop_status()
            report['debug_after'] = c.status()
            report['resources_after_exit'] = list(struct.unpack('<21I', c.read_memory(symbols['resource_status'][0], 84)))
            check('Normal exit releases resource handles and heap',
                  report['resources_after_exit'][0] == 0 and report['resources_after_exit'][3:5] == [0, 0])
            report['passed'] = True
            save()
        except Exception as e:
            report['error'] = str(e)
            report['failure_diagnostics'] = {
                n: list(struct.unpack('<' + 'I' * count, c.read_memory(symbols[n][0], count * 4)))
                for n, count in [('h1_dota_debug', 8), ('resource_status', 21), ('tree_stats', 16), ('h1_dota_cache', 16)]}
            shot('failure')
            save()
            raise
        finally:
            c.release()
    print(json.dumps(dict(passed=True, physical_h1_tested=True, report=str(out / 'verification.json'))))


if __name__ == '__main__':
    main()
