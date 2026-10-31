"""CRC-checked installed Roshan/Ancient definitions and original pit geometry.

Read-only against Steam. Writes research receipts only, never production rules.
Unknown engine algorithms remain explicitly unknown rather than guessed values.
"""
from pathlib import Path
import datetime, hashlib, importlib.util, json, re, struct, sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'private/roshan/source-rules'
STEAM = Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
sys.path.insert(0, str(ROOT/'tools'))
from source2_vpk import VpkArchive

spec = importlib.util.spec_from_file_location('valve_keyvalues_audit', ROOT/'private/passives/extract_sources.py')
kv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kv)

GROUPS = {
    'black_dragon': ['black_dragon', 'black_drake', 'black_drake'],
    'granite_golem': ['granite_golem', 'rock_golem', 'rock_golem'],
    'thunder_lizard': ['big_thunder_lizard', 'small_thunder_lizard', 'small_thunder_lizard'],
    'ice_shaman': ['ice_shaman', 'frostbitten_golem', 'frostbitten_golem'],
    'prowler': ['prowler_shaman', 'prowler_acolyte', 'prowler_acolyte'],
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def rel(path):
    return Path(path).resolve().relative_to(ROOT).as_posix()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf8')


def world(origin):
    return [2048+origin[0]/4, 2048-origin[1]/4, origin[2]/4]


def flat(definition):
    values = {k: v.get('value') if isinstance(v, dict) else v for k, v in definition.items() if k != 'AbilityValues'}
    values.update({k: v.get('value') if isinstance(v, dict) else v for k, v in definition.get('AbilityValues', {}).items()})
    return values


def hull(points):
    """Monotonic convex envelope; not a claim of native blocker connectivity."""
    points = sorted(set(map(tuple, points)))
    cross = lambda o, a, b: (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    low, high = [], []
    for point in points:
        while len(low) >= 2 and cross(low[-2], low[-1], point) <= 0:
            low.pop()
        low.append(point)
    for point in reversed(points):
        while len(high) >= 2 and cross(high[-2], high[-1], point) <= 0:
            high.pop()
        high.append(point)
    return [list(p) for p in low[:-1]+high[:-1]]


def native_spawn_table():
    """Independently read a source-version-pinned original native data table."""
    import pefile
    server = STEAM/'bin/win64/server.dll'
    binary = server.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    assert digest == 'af4622bd405f1a465926e20a47d97bbf9988ff0cd11b63901fff1d6dc6a4c834'
    image = pefile.PE(data=binary, fast_load=True)
    image_base = image.OPTIONAL_HEADER.ImageBase
    table_offset, ancient_offset, stride = 0x4a84180, 0x4a84558, 0x148
    getter_va = 0x18188a9d0
    getter_offset = image.get_offset_from_rva(getter_va-image_base)
    getter = binary[getter_offset:getter_offset+8]
    assert getter[:3] == b'\x48\x8d\x05' and getter[7] == 0xc3
    table_va = getter_va+7+struct.unpack_from('<i', getter, 3)[0]
    assert image.get_offset_from_rva(table_va-image_base) == table_offset
    count_getter = binary[getter_offset+0x10:getter_offset+0x16]
    assert count_getter == b'\xb8\x04\x00\x00\x00\xc3'
    assert table_offset+3*stride == ancient_offset
    count = struct.unpack_from('<I', binary, ancient_offset)[0]
    assert count == 7
    rows, strings, checks = [], {}, 6
    for group in range(count):
        names, pointers = [], []
        for slot in range(5):
            offset = ancient_offset+8+group*40+slot*8
            address = struct.unpack_from('<Q', binary, offset)[0]
            string_offset = image.get_offset_from_rva(address-image_base)
            end = binary.index(0, string_offset)
            value = binary[string_offset:end].decode('ascii')
            assert not value or value.startswith('npc_dota_neutral_')
            names.append(value)
            pointers.append({'file_offset': offset, 'pointer_va': hex(address), 'string_file_offset': string_offset})
            strings[value] = {'file_offset': string_offset, 'bytes_hex': binary[string_offset:end+1].hex()}
            checks += 1
        rows.append({'subtype_index': group, 'entries': names, 'pointers': pointers})
    for index, species in enumerate(GROUPS.values()):
        expected = ['npc_dota_neutral_'+species[1], 'npc_dota_neutral_'+species[2], 'npc_dota_neutral_'+species[0], '', '']
        assert rows[index]['entries'] == expected
        checks += 1
    assert rows[5]['entries'] == ['', '', '', '', '']
    assert rows[6]['entries'] == ['npc_dota_neutral_ancient_frog_mage', 'npc_dota_neutral_ancient_frog', 'npc_dota_neutral_ancient_frog', '', '']
    checks += 2
    dispatch_va = 0x182602ff0
    dispatch_offset = image.get_offset_from_rva(dispatch_va-image_base)
    dispatch = binary[dispatch_offset:dispatch_offset+0x19d]
    assert dispatch[0x16:0x1d] == bytes.fromhex('83b9d004000000')
    assert dispatch[0x49:0x4b] == bytes.fromhex('7e4d')
    assert dispatch[0x8f:0x93] == bytes.fromhex('418d40ff')
    # The original reflected private field is paired with offset0x4d0.
    reflection_offset = 0x4b13c90
    reflected = binary[reflection_offset:reflection_offset+24]
    reflected_name, attributes, reflected_field = struct.unpack_from('<QQI', reflected)
    reflected_name_offset = image.get_offset_from_rva(reflected_name-image_base)
    assert binary[reflected_name_offset:reflected_name_offset+19] == b'm_iForcedSpawnType\0'
    assert reflected_field == 0x4d0
    checks += 5
    return {'complete': True, 'scope': 'Read-only original native data and getter/dispatch bytes; no code is executed or modified.',
            'server_dll': str(server), 'server_sha256': digest, 'image_base': hex(image_base),
            'table_getter': {'virtual_address': hex(getter_va), 'file_offset': getter_offset, 'bytes_hex': getter.hex(),
                             'meaning': 'LEA RAX,[RIP+disp32];RET points to the neutral-spawn category table.'},
            'category_count_getter': {'virtual_address': hex(getter_va+0x10), 'file_offset': getter_offset+0x10, 'bytes_hex': count_getter.hex(), 'value': 4},
            'category_stride_bytes': stride, 'category_index': 3, 'ancient_table_file_offset': ancient_offset,
            'declared_subtype_count': count, 'data_bytes_hex': binary[ancient_offset:ancient_offset+stride].hex(),
            'subtypes': rows, 'original_strings': strings, 'checks': checks,
            'selection_dispatch': {'virtual_address': hex(dispatch_va), 'file_offset': dispatch_offset,
                'bytes_hex': dispatch.hex(),
                'forced_field_reflection': {'file_offset': reflection_offset, 'bytes_hex': reflected.hex(),
                    'name': 'm_iForcedSpawnType', 'name_file_offset': reflected_name_offset, 'offset': reflected_field},
                'interpretation': 'At0x182603006 forced field+0x4d0 is compared with0; JLE0x182603088 sends0/negative to random selection. Positive forced subtype is decremented at0x18260307f. Min/Max fields+0x4bc/+0x4c0 are both decremented then passed to imported RandomInt at0x1826030a6. If range is not fixed and result equals previous subtype+0x4c8,0x1826030c0..0x1826030f2 rerolls until different. Neutral category+0x598 chooses table stride0x148; subtype chooses40-byte row.',
                'ordinary_ancient_map_settings': 'Both actual NeutralType3 spawners have Min1 Max5 Forced-1, selecting original Ancient rows0..4 and rejecting immediate repeats.'},
            'proven': 'The original native Ancient table includes the five classic/Ice/Prowler triplets, an empty reserved row5, and FrogMage+2Frogs row6.',
            'not_proven': 'Imported RandomInt internal probability distribution is not reconstructed; no uniformity claim beyond the native inclusive integer range API and original rejection of immediate repeats.'}


def native_roshan_health():
    """Pin original health growth code/constants and its HP-fraction preservation."""
    import pefile
    server = STEAM/'bin/win64/server.dll'
    binary = server.read_bytes()
    image = pefile.PE(data=binary, fast_load=True)
    base = image.OPTIONAL_HEADER.ImageBase
    def capture(va, length):
        offset = image.get_offset_from_rva(va-base)
        return {'virtual_address': hex(va), 'file_offset': offset, 'bytes_hex': binary[offset:offset+length].hex()}
    constants = {}
    for name, va, expected in [('growth_per_interval', 0x183341064, 130.0),
                                ('base_health', 0x183341098, 6000.0),
                                ('interval_seconds_default', 0x182eedba0, 60.0)]:
        row = capture(va, 4)
        row['value'] = struct.unpack('<f', bytes.fromhex(row['bytes_hex']))[0]
        assert row['value'] == expected
        constants[name] = row
    return {'complete': True, 'server_sha256': sha(server),
            'upgrade_cvar_registration': capture(0x1800e7310, 0x97),
            'health_update_code': capture(0x180f34020, 0x129), 'constants': constants,
            'interpretation': 'Original health update divides current health by previous maxHP, computes floor(gameTime/dota_roshan_upgrade_rate)*130*modeFactor+6000, applies maxHP and restores the previous health fraction. Default upgrade interval60seconds. H1 uses normal modeFactor1.0.',
            'checks': 3,
            'not_proven': 'Other native modifier getters contain7.0 and0.4 multipliers but their exported property-role dispatch is not reconstructed; they are not supplied as verified damage/armor growth.'}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    vpk, map_vpk = VpkArchive(STEAM/'pak01_dir.vpk'), VpkArchive(STEAM/'maps/dota.vpk')
    entries = ['scripts/npc/npc_units.txt', 'scripts/npc/npc_abilities.txt', 'scripts/npc/items.txt']
    entries += [f'resource/localization/{base}_{lang}.txt' for base in ('abilities', 'dota') for lang in ('english', 'schinese')]
    sources, documents, files, checks = {}, {}, {}, 0
    for entry in entries:
        raw = vpk.read(entry)
        target = OUT/'original'/entry
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        identity = vpk.identity(entry)
        assert identity['crc32_verified'] and sha(target) == identity['sha256']
        sources[entry] = {**identity, 'snapshot': rel(target)}
        files[rel(target)] = sha(target)
        documents[entry] = kv.parse(raw.decode('utf-8-sig'))
        checks += 1
    entry = 'maps/dota/entities/default_ents.vents_c'
    target = OUT/'original/default_ents.vents_c'
    target.write_bytes(map_vpk.read(entry))
    sources[entry] = {**map_vpk.identity(entry), 'snapshot': rel(target)}
    files[rel(target)] = sha(target)
    layout = json.loads((ROOT/'private/source2/reports/layout.json').read_text(encoding='utf8'))
    assert sources[entry]['sha256'] == layout['source']['sha256']
    entities_path = ROOT/'private/source2/reports/entity-default-parsed.json'
    assert sha(ROOT/'private/source2/reports/entity-default-DATA.txt') == layout['entity_dump_sha256']
    entities = json.loads(entities_path.read_text(encoding='utf8'))
    checks += 2

    definitions = documents['scripts/npc/npc_units.txt']['DOTAUnits']
    defaults = definitions['npc_dota_units_base']
    units = {}
    active_species = sorted({species for group in GROUPS.values() for species in group})
    for key in ['npc_dota_roshan']+['npc_dota_neutral_'+s for s in active_species]:
        definition = definitions[key]
        assert definition['IsAncient'] == '1'
        effective = {**defaults, **definition}
        stats = {k: v for k, v in effective.items() if re.match(r'^(Attack|BaseAttack|Bounds|Status|Movement|Armor|Magical|Bounty|Vision|Projectile|Model|Level)', k)}
        units[key] = {'definition': definition, 'effective_stats_with_script_defaults': stats,
                      'source_key': 'DOTAUnits/'+key, 'source': 'scripts/npc/npc_units.txt'}
        checks += 1
    r = units['npc_dota_roshan']['effective_stats_with_script_defaults']
    assert [r[k] for k in ('StatusHealth', 'ArmorPhysical', 'MagicalResistance', 'AttackRate', 'BaseAttackSpeed', 'AttackAnimationPoint', 'AttackRange', 'AttackAcquisitionRange', 'MovementSpeed', 'StatusHealthRegen')] == ['6000', '30', '55', '2.0', '100', '0.6', '150', '150', '270', '20']
    checks += 1
    abilities = documents['scripts/npc/npc_abilities.txt']['DOTAAbilities']
    skill_ids = sorted({value for row in units.values() for key, value in row['definition'].items() if re.fullmatch(r'Ability\d+', key) and value})
    skill_rows = {key: {'definition': abilities[key], 'base_values': flat(abilities[key]), 'source_key': 'DOTAAbilities/'+key} for key in skill_ids}
    assert skill_rows['roshan_slam']['base_values']['damage_growth'] == '10'
    checks += 1
    items = documents['scripts/npc/items.txt']['DOTAAbilities']
    item_rows = {key: items[key] for key in ('item_aegis', 'item_cheese', 'item_refresher_shard', 'item_roshans_banner')}
    assert item_rows['item_aegis']['AbilityValues']['reincarnate_time'] == '5.0'
    assert item_rows['item_aegis']['AbilityValues']['disappear_time'] == '300.0'
    checks += 2
    tooltips = {}
    for lang in ('english', 'schinese'):
        table = {k: v for base in ('abilities', 'dota') for k, v in documents[f'resource/localization/{base}_{lang}.txt']['lang']['Tokens'].items()}
        tooltips[lang] = {k: v for k, v in table.items() if k.lower().startswith('dota_tooltip_ability_roshan_') or k.startswith('DOTA_Glossary_Map_Roshan') or k == 'DOTA_ContextualTips_RoshanDeath_Annotation'}
    en = tooltips['english']
    assert '8-11 minutes' in en['DOTA_ContextualTips_RoshanDeath_Annotation']
    assert 'Whenever' in en['DOTA_Glossary_Map_Roshan_Aegis_Desc']
    assert "second time" in en['DOTA_Glossary_Map_Roshan_Cheese_Desc']
    assert 'third time' in en['DOTA_Glossary_Map_Roshan_Shards_Desc']
    checks += 4

    selected = [e for e in entities if 'roshan' in e.get('targetname', '').lower() or e['classname'] == 'npc_dota_roshan_spawner' or (e['classname'] == 'npc_dota_neutral_spawner' and e.get('NeutralType') == '3')]
    save(OUT/'selected-original-entities.json', selected)
    files[rel(OUT/'selected-original-entities.json')] = sha(OUT/'selected-original-entities.json')
    camps, locations, blockers = [], [], {'top': [], 'bottom': []}
    for entity in selected:
        point = world(entity['origin'])
        if entity['classname'] == 'npc_dota_neutral_spawner':
            camps.append({'entity': entity, 'world_xyz': point, 'playable_in_current_4096_world': 0 <= point[0] < 4096 and 0 <= point[1] < 4096})
        elif entity['classname'] == 'ent_fow_blocker_node':
            blockers['top' if entity['origin'][0] < 0 else 'bottom'].append({'world_xyz': point, 'entity': entity})
        else:
            locations.append({'world_xyz': point, 'entity': entity})
    assert len(camps) == 2 and all(c['playable_in_current_4096_world'] for c in camps)
    assert all(c['entity']['MinSpawnType'] == '1' and c['entity']['MaxSpawnType'] == '5' and c['entity']['ForcedSubType'] == '-1' for c in camps)
    assert len(blockers['top']) == 82 and len(blockers['bottom']) == 77
    checks += 3
    geometry = {}
    for name, rows in blockers.items():
        polygon = hull([row['world_xyz'][:2] for row in rows])
        assert len(polygon) >= 3
        geometry[name] = {'source_blockers': rows, 'source_blocker_count': len(rows), 'convex_envelope_world_xy': polygon,
                          'world_bounds_xy': [[min(row['world_xyz'][i] for row in rows), max(row['world_xyz'][i] for row in rows)] for i in (0, 1)],
                          'limits': 'TargetNode is empty in every exported node. Connectivity, raster radius and exact native cave-volume classification are not supplied. Convex envelope is a source-derived H1 approximation, not native fog graph reconstruction.'}
        checks += 1
    save(OUT/'pit-geometry.json', geometry)
    files[rel(OUT/'pit-geometry.json')] = sha(OUT/'pit-geometry.json')

    receipt = json.loads((ROOT/'private/jungle/official-patches/receipt.json').read_text(encoding='utf8'))
    official, notes = [], {}
    for row in receipt['sources']:
        if row['patch'] not in ('7.38', '7.39', '7.41', '7.41c'):
            continue
        path = ROOT/row['path']
        assert sha(path) == row['sha256']
        copied = OUT/'official'/path.name
        copied.parent.mkdir(parents=True, exist_ok=True)
        copied.write_bytes(path.read_bytes())
        official.append({**row, 'snapshot': rel(copied)})
        files[rel(copied)] = sha(copied)
        notes[row['patch']] = json.loads(copied.read_text(encoding='utf8'))
        checks += 1
    n38, n41, n41c = notes['7.38'], notes['7.41'], notes['7.41c']
    assert '15:00' in n38['general_notes'][2]['generic'][5]['note']
    assert n41['general_notes'][1]['generic'][15]['info'] == 'Now begins in the Top Pit'
    assert 'vice versa' in n41c['general_notes'][0]['generic'][0]['note']
    prowler_note = next(row for row in n38['neutral_creeps'] if row.get('localized_name') == 'Ancient Prowlers')
    assert '2 Prowler Acolytes and 1 Prowler Shaman' in prowler_note['neutral_creep_notes'][1]['note']
    checks += 4
    web_evidence = OUT/'official/7.31-web-evidence.json'
    save(web_evidence, {'url': 'https://www.dota2.com/datafeed/patchnotes?version=7.31&language=english',
                        'transport': 'web.run parsed original Valve JSON, not a raw HTTP byte snapshot',
                        'json_location': 'general_notes/0/generic, near final entry',
                        'short_original_excerpt': 'New Ancient Camp: Ancient Ice Shaman and 2 Ancient Frostbitten Golems',
                        'interpretation': 'One Ice Shaman and two Frostbitten Golems; historical introduction, current installed definitions independently retain these species.'})
    files[rel(web_evidence)] = sha(web_evidence)
    native = native_spawn_table()
    save(OUT/'native-ancient-spawn-table.json', native)
    files[rel(OUT/'native-ancient-spawn-table.json')] = sha(OUT/'native-ancient-spawn-table.json')
    checks += native['checks']
    native_health = native_roshan_health()
    save(OUT/'native-roshan-health.json', native_health)
    files[rel(OUT/'native-roshan-health.json')] = sha(OUT/'native-roshan-health.json')
    checks += native_health['checks']
    manifest = {'complete': True, 'observed_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'sources': sources, 'unit_defaults': defaults, 'units': units, 'abilities': skill_rows,
                'items': item_rows, 'tooltips': tooltips, 'original_roshan_locations': locations,
                'original_ancient_camps': camps, 'source_pit_geometry': rel(OUT/'pit-geometry.json'),
                'ancient_species': active_species,
                'suggested_h1_ancient_groups': GROUPS,
                'native_ancient_spawn_table': {'snapshot': rel(OUT/'native-ancient-spawn-table.json'),
                    'sha256': sha(OUT/'native-ancient-spawn-table.json'), 'server_sha256': native['server_sha256'],
                    'triplet_compositions_proven': True, 'source_category': 'NeutralType3',
                    'source_map_subtype_range_1based': [1, 5], 'forced_subtype': -1,
                    'native_rejects_immediate_group_repeat': True, 'selection_probabilities_recovered': False},
                'native_roshan_health': {'snapshot': rel(OUT/'native-roshan-health.json'),
                    'sha256': sha(OUT/'native-roshan-health.json'), 'growth_hp_per_minute_normal_mode': 130,
                    'preserves_health_fraction': True},
                'camp_composition_evidence': {'prowler': 'Official7.38 neutral_creeps AncientProwlers notes explicitly1shaman+2acolytes.',
                    'ice_shaman': 'Official7.31 web.run primary JSON states1IceShaman+2FrostbittenGolems. See official/7.31-web-evidence.json.',
                    'classic_groups': 'Original server.dll Ancient table rows0..4 independently prove one-large/two-small triplets for all five groups. Getter bytes, original pointer-table/string bytes and original DLL hash are sealed in native-ancient-spawn-table.json. Random-selection probabilities remain unknown.'},
                'current_pit_policy': {'source': '7.41/general_notes/1/generic/15', 'initial_pit': 'top',
                    'switching_starts_seconds': 900, 'movement_evidence': '7.38/general_notes/2/generic/5 says walk between pits from15:00, Top/night Bottom/day;7.41 switches preference, so Top/day Bottom/night.',
                    'top_anchor': 'Original info_target roshan_location_2 world1262.341614,1448.445679,z25.590717.',
                    'bottom_anchor': 'Original npc_dota_roshan_spawner world2755.625122,2733.001892,z16; static spawner does not override7.41 first-pit preference.'},
                'vision_evidence': {'source': '7.41c/general_notes/0/generic/0', 'rule': 'Flying vision cannot see into Roshan pits from outside or vice versa; ordinary pit restrictions remain.',
                                    'geometry_limit': 'The point envelope is not an exact native FOW reconstruction.'},
                'known_unknowns': ['Native Ancient table triplets/category/forced/random range and no-immediate-repeat selection are recovered; imported RandomInt internal probability distribution is not reconstructed.',
                    'Roshan HP130/min is recovered from native health update; armor/physical damage growth property roles and native team-gold numeric constant remain unresolved.',
                    'Roshan initial native guard/leash bounds and precise AI spell triggers are not exposed.',
                    'Native exact Roshan FOW topology/collision-to-vision graph is not reconstructed.',
                    'Actual Ancient camps are the two NeutralType3 entities; MinSpawnType/MaxSpawnType are subtype indices and must not be mistaken for the neutral tier.',
                    'Frog subtype6 is outside ordinary Ancient Min1/Max5. Six Min7/Max7 spawners belong to special Flooded camps, not the original Ancient category.'],
                'files': files, 'extractor_sha256': sha(__file__)}
    save(OUT/'manifest.json', manifest)
    verify = {'passed': True, 'checks': checks, 'unit_definitions': len(units), 'ability_definitions': len(skill_rows),
              'original_ancient_camps': 2, 'playable_ancient_camps': 2, 'fow_blocker_nodes': 159,
              'manifest_sha256': sha(OUT/'manifest.json'), 'source_sha256': {**files, 'tools/extract_roshan_rules.py': sha(__file__),
                   'private/source2/reports/entity-default-parsed.json': sha(entities_path),
                   'private/passives/extract_sources.py': sha(ROOT/'private/passives/extract_sources.py'),
                   'tools/source2_vpk.py': sha(ROOT/'tools/source2_vpk.py')},
              'original_game_files_modified': False, 'native_engine_executed': False}
    save(OUT/'verification.json', verify)
    print(json.dumps({k: verify[k] for k in ('passed', 'checks', 'unit_definitions', 'ability_definitions', 'fow_blocker_nodes', 'manifest_sha256')}))


if __name__ == '__main__':
    main()
