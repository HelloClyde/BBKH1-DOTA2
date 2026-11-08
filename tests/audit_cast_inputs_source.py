"""Audit implemented cast inputs against CRC-verified installed Valve KV.

Read-only Steam data; no Dota or H1 execution. Runtime behavior is covered by
cast_order_test.c and verify_item_input_mips.py, not by this metadata audit.
"""
from pathlib import Path
import argparse, hashlib, json, re, struct, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/resource_pipeline'))
from source2_vpk import VpkArchive

HEROES = [
    ('axe', 'berserkers_call battle_hunger counter_helix culling_blade'),
    ('lina', 'dragon_slave light_strike_array fiery_soul laguna_blade'),
    ('juggernaut', 'blade_fury healing_ward blade_dance omni_slash'),
    ('crystal_maiden', 'crystal_nova frostbite brilliance_aura freezing_field'),
    ('slark', 'dark_pact pounce saltwater_shiv shadow_dance'),
    ('necrolyte', 'death_pulse ghost_shroud heartstopper_aura reapers_scythe'),
    ('pudge', 'meat_hook rot flesh_heap dismember'),
    ('sniper', 'shrapnel headshot take_aim assassinate'),
    ('sven', 'storm_bolt great_cleave warcry gods_strength'),
    ('tidehunter', 'gush kraken_shell anchor_smash ravage'),
]
EXTRAS = [(3, 'crystal_clone', 1), (4, 'depth_shroud', 1),
          (5, 'death_seeker', 3), (6, 'eject', 0),
          (7, 'concussive_grenade', 1), (9, 'dead_in_the_water', 2)]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse(text):
    # Valve's quoted KeyValues format; comments outside strings are ignored.
    words = []
    for m in re.finditer(r'//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])', text):
        if not m[0].startswith('//'):
            words.append(m[2] or re.sub(r'\\(.)', r'\1', m[1]))
    index = 0

    def block(nested=False):
        nonlocal index
        result = {}
        while index < len(words):
            key = words[index]
            index += 1
            if key == '}':
                assert nested
                return result
            value = words[index]
            index += 1
            result[key] = block(True) if value == '{' else value
        assert not nested
        return result
    return block()


def kind(definition):
    behavior = definition.get('AbilityBehavior', '')
    if 'NO_TARGET' in behavior or 'PASSIVE' in behavior:
        return 0
    if 'POINT' in behavior:
        return 1
    assert 'UNIT_TARGET' in behavior, behavior
    team = definition.get('AbilityUnitTargetTeam', '')
    return 3 if 'FRIENDLY' in team or 'BOTH' in team else 2


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vpk', type=Path, required=True)
    p.add_argument('--scene', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    assert not a.output.exists()
    archive = VpkArchive(a.vpk)
    sources, definitions, rows = [], [], []
    for h, (hero, slots) in enumerate(HEROES):
        path = f'scripts/npc/heroes/npc_dota_hero_{hero}.txt'
        raw = archive.read(path)
        sources.append(archive.identity(path))
        d = parse(raw.decode('utf-8-sig'))['DOTAHeroes']['npc_dota_hero_' + hero]['AbilityDefinitions']
        definitions.append(d)
        for slot, suffix in enumerate(slots.split()):
            name = hero + '_' + suffix
            rows.append(dict(hero=h, slot=slot, name=name, kind=kind(d[name]),
                             behavior=d[name]['AbilityBehavior']))
    code = (ROOT / 'game/cast_order.c').read_text()
    table = code.split('kinds[DOTA_HERO_TYPES][4]={', 1)[1].split('};', 1)[0]
    actual = [int(n) for n in re.findall(r'\d+', table)]
    assert actual == [r['kind'] for r in rows]
    extra_rows = []
    for h, suffix, expected in EXTRAS:
        name = HEROES[h][0] + '_' + suffix
        d = definitions[h][name]
        assert kind(d) == expected
        extra_rows.append(dict(hero=h, name=name, kind=expected, behavior=d['AbilityBehavior']))
    path = 'scripts/npc/items.txt'
    sources.append(archive.identity(path))
    item_defs = parse(archive.read(path).decode('utf-8-sig'))['DOTAAbilities']
    ids = {int(i): n.lower() for n, i in re.findall(r'^#define DOTA_ITEM_(\w+) (\d+)$', (ROOT / 'game/items_ids.h').read_text(), re.M)}
    checked, conflicts, active = [], [], 0
    with a.scene.open('rb') as f:
        def read(off, size):
            f.seek(off)
            return f.read(size)
        h = struct.unpack('<16I', read(0, 64))
        iar = struct.unpack('<8I', read(h[15] + 32, 32))
        cat = struct.unpack('<16I', read(iar[3], 64))
        for i in range(cat[3]):
            record = read(iar[3] + cat[4] + i * 96, 96)
            flags, action = struct.unpack_from('<H', record, 2)[0], record[14]
            if not action:
                continue
            active += 1
            name = 'item_' + ids[i]
            d = item_defs[name]
            behavior, team = d.get('AbilityBehavior', ''), d.get('AbilityUnitTargetTeam', '')
            for text, mask in [('POINT', 4096), ('NO_TARGET', 32768)]:
                assert bool(text in behavior) == bool(flags & mask), name
            checked.append(dict(id=i, name=name, behavior=behavior, team=team))
            if flags & 32768 and flags & (1024 | 2048) and not flags & 4096:
                conflicts.append(dict(id=i, name=name, default='immediate-self',
                                      optional_ally='OPTIONAL_UNIT_TARGET' in behavior,
                                      previously_forced_self=i in (403, 404, 405)))
    report = dict(passed=True, scope=__doc__, physical_h1_tested=False,
                  source_identity=sources, source_parser_sha256=sha(__file__),
                  cast_order_sha256=sha(ROOT / 'game/cast_order.c'),
                  regular_skills=rows, acquired_skills=extra_rows,
                  mode_note='Sven Scepter toggles his existing Storm Hammer; Tide Scepter changes Gush to point input. Pudge Shard additionally permits friendly Dismember.',
                  catalogue_items=cat[3], active_items=active, checked_items=checked,
                  no_target_team_flags=conflicts)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(passed=True, regular_skills=len(rows), acquired_skills=len(extra_rows),
                          catalogue_items=cat[3], active_items=active, no_target_team_flags=conflicts)))


if __name__ == '__main__':
    main()
