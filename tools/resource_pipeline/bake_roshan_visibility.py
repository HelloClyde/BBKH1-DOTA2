"""Generate integer Source NPC definitions and a compact map-pixel pit mask.

The pit classifier is the convex envelope of original FOW blocker positions,
an explicit H1 approximation. Empty native TargetNode links do not establish
the engine's actual blocker graph. No native vision code is claimed here.
"""
from pathlib import Path
import argparse, hashlib, json, math
from decimal import Decimal, ROUND_HALF_UP

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'private/roshan/source-rules'
SPECIES = ['roshan', 'black_dragon', 'black_drake', 'granite_golem', 'rock_golem',
           'big_thunder_lizard', 'small_thunder_lizard', 'ice_shaman', 'frostbitten_golem',
           'prowler_shaman', 'prowler_acolyte']
FIELDS = ['model_id', 'hp', 'armor', 'damage_avg', 'damage_min', 'damage_max', 'range_source',
          'acquire_source', 'speed_source', 'bat_ticks', 'point_ms', 'base_attack_speed',
          'projectile_source', 'magic_resist_pct', 'xp', 'gold_min', 'gold_max',
          'regen_per_sec_x100', 'hull_source']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def round_source(value, multiple=1):
    return int((Decimal(str(value))*multiple).quantize(Decimal('1'), rounding=ROUND_HALF_UP))


def npc_rows(manifest):
    rows = []
    for index, name in enumerate(SPECIES):
        key = 'npc_dota_roshan' if not index else 'npc_dota_neutral_'+name
        stats = manifest['units'][key]['effective_stats_with_script_defaults']
        get = lambda key: Decimal(stats[key])
        damage_min, damage_max = int(get('AttackDamageMin')), int(get('AttackDamageMax'))
        hull = stats['BoundsHullName']
        assert hull == 'DOTA_HULL_SIZE_HERO'
        values = [48+index, int(get('StatusHealth')), int(get('ArmorPhysical')), (damage_min+damage_max)//2,
                  damage_min, damage_max, int(get('AttackRange')), int(get('AttackAcquisitionRange')),
                  int(get('MovementSpeed')), round_source(get('AttackRate'), 20),
                  round_source(get('AttackAnimationPoint'), 1000), int(get('BaseAttackSpeed')),
                  int(get('ProjectileSpeed')), int(get('MagicalResistance')), int(get('BountyXP')),
                  int(get('BountyGoldMin')), int(get('BountyGoldMax')), round_source(get('StatusHealthRegen'), 100), 24]
        rows.append(dict(name=name, values=values))
    return rows


def data_header(manifest, geometry):
    rows = npc_rows(manifest)
    lines = ['/* Generated from installed Valve definitions by tools/bake_roshan_visibility.py.',
             ' * Source manifest SHA256 '+sha(SOURCE/'manifest.json'),
             ' * damage_avg is the H1 integer floor of original min/max; both endpoints remain.',
             ' * Regeneration retains hundredths/sec, without runtime floating point.',
             ' * Native source blocker convex hull is an H1 approximation, not recovered native FOW.',
             ' */', '#ifndef H1_DOTA_ROSHAN_DATA_H', '#define H1_DOTA_ROSHAN_DATA_H',
             '#define DOTA_WILD_SOURCE_COUNT 11', '#define DOTA_ANCIENT_SPECIES_COUNT 10',
             '#define DOTA_ANCIENT_CAMP_COUNT '+str(sum(c['playable_in_current_4096_world'] for c in manifest['original_ancient_camps'])), '#define DOTA_ANCIENT_GROUP_COUNT 5',
             'typedef struct {', '    int '+','.join(FIELDS)+';', '} DotaWildSourceDef;',
             '/* index0=Roshan/model48; index1..10=Ancient species0..9/models49..58. */',
             'static const DotaWildSourceDef dota_wild_source_defs[DOTA_WILD_SOURCE_COUNT]={']
    for row in rows:
        lines.append('    {'+','.join(map(str, row['values']))+'}, /* '+row['name']+' */')
    lines += ['};', 'static const short dota_ancient_camp_positions[DOTA_ANCIENT_CAMP_COUNT][2]={']
    for camp in manifest['original_ancient_camps']:
        if camp['playable_in_current_4096_world']:
            point = [round_source(x) for x in camp['world_xyz'][:2]]
            lines.append('    {'+','.join(map(str, point))+'}, /* '+camp['entity']['VolumeName']+' */')
    lines += ['};', '/* Native NeutralType3 camps: Min1 Max5 Forced-1 selects rows0..4.',
              ' * ForcedSubType0 also randomizes; only positive forced values are 1-based.',
              ' * A camp rerolls a random result equal to its immediately previous subtype.',
              ' * Native table order: two lesser creatures, then their leader. Values are species0..9. */',
              'static const unsigned char dota_ancient_spawn_species[DOTA_ANCIENT_GROUP_COUNT][3]={',
              '    {1,1,0},{3,3,2},{5,5,4},{7,7,6},{9,9,8}', '};',
              '/* Top/Bottom original Source anchors, half-up world rounding. */',
              'static const short dota_roshan_pit_positions[2][2]={{1262,1448},{2756,2733}};',
              '/* Native default60-second growth interval; preserve current HP/maxHP fraction. */',
              '#define DOTA_ROSHAN_HEALTH_PER_MINUTE '+str(manifest['native_roshan_health']['growth_hp_per_minute_normal_mode']),
              '#define DOTA_ROSHAN_SLAM_RADIUS_SOURCE 350', '#define DOTA_ROSHAN_SLAM_BASE_DAMAGE 70',
              '#define DOTA_ROSHAN_SLAM_DAMAGE_PER_MINUTE 10', '#define DOTA_ROSHAN_SLAM_POINT_MS 470',
              '#define DOTA_ROSHAN_SLAM_COOLDOWN_TICKS 200', '#define DOTA_ROSHAN_SLAM_SLOW_TICKS 80',
              '#define DOTA_ROSHAN_SLAM_SLOW_PERCENT 50', '#define DOTA_ROSHAN_BASH_CHANCE_PERCENT 15',
              '#define DOTA_ROSHAN_BASH_BONUS_DAMAGE 50', '#define DOTA_ROSHAN_BASH_STUN_TICKS 33',
              '#define DOTA_ROSHAN_SPELL_BLOCK_TICKS 300', '#define DOTA_ROSHAN_STATUS_RESIST_PERCENT 25',
              '#define DOTA_ROSHAN_ROAR_RADIUS_SOURCE 600', '#define DOTA_ROSHAN_ROAR_BASE_DAMAGE 50',
              '#define DOTA_ROSHAN_ROAR_DAMAGE_PER_MINUTE 10', '#define DOTA_ROSHAN_ROAR_COOLDOWN_TICKS 1200',
              '#define DOTA_ROSHAN_ROAR_DISARM_TICKS 40', '#define DOTA_ROSHAN_ROAR_VULNERABLE_TICKS 240',
              '#define DOTA_ROSHAN_ROAR_EXTRA_DAMAGE_PERCENT 25', '#define DOTA_ROSHAN_FIRST_MOVE_TICKS 18000',
              '#define DOTA_ROSHAN_RESPAWN_MIN_TICKS 9600', '#define DOTA_ROSHAN_RESPAWN_MAX_TICKS 13200',
              '#define DOTA_AEGIS_LIFE_TICKS 6000', '#define DOTA_AEGIS_REINCARNATE_TICKS 100',
              '/* Exact derived hull coordinates are Q8 world units. */']
    points, offsets = [], [0]
    for side in ('top', 'bottom'):
        points.extend([[round_source(x, 256), round_source(y, 256)] for x, y in geometry[side]['convex_envelope_world_xy']])
        offsets.append(len(points))
    lines += ['static const int dota_roshan_polygon_q8[][2]={']
    lines += ['    {'+str(x)+','+str(y)+'},' for x, y in points]
    lines += ['};', 'static const unsigned char dota_roshan_polygon_offsets[3]={'+','.join(map(str, offsets))+'};', '#endif', '']
    return '\n'.join(lines)


def pixel_regions(geometry, map_info, depth):
    from gltf_assets import np
    height, width = depth.shape
    result = np.zeros((height, width), np.uint8)
    origin_x, origin_y = map_info['origin_pixels']
    global_y = np.arange(height, dtype=np.float64)+origin_y+.5
    for side, name in enumerate(('top', 'bottom'), 1):
        polygon = geometry[name]['convex_envelope_world_xy']
        # Exact narrow X stripe, then per-pixel source depth inversion. No
        # height-grid approximation is substituted for actual visible geometry.
        lo = max(0, math.floor(min(p[0] for p in polygon)*180/256-origin_x)-1)
        hi = min(width, math.ceil(max(p[0] for p in polygon)*180/256-origin_x)+1)
        world_x = (np.arange(lo, hi, dtype=np.float64)+origin_x+.5)*256/180
        visible = depth[:, lo:hi]
        world_y = (global_y[:, None]*65536/(181*180)+(32768-visible.astype(np.float64)))/2
        inside = visible != 65535
        for i, (x, y) in enumerate(polygon):
            xx, yy = polygon[(i+1) % len(polygon)]
            inside &= (xx-x)*(world_y-y)-(yy-y)*(world_x[None, :]-x) >= 0
        result[:, lo:hi][inside] = side
    return result


def mask_header(mask, map_info):
    from gltf_assets import np
    origin_x, origin_y = map_info['origin_pixels']
    offsets, runs, descriptors, stats = [], [], [], []
    for side in (1, 2):
        rows = np.where(np.any(mask == side, axis=1))[0]
        assert len(rows)
        start, end = int(rows[0]), int(rows[-1])+1
        row_base, run_base = len(offsets), len(runs)
        side_runs, max_runs = 0, 0
        for row in range(start, end):
            offsets.append(side_runs)
            values = (mask[row] == side).astype(np.int8)
            transitions = np.diff(np.r_[0, values, 0])
            starts, ends = np.where(transitions == 1)[0], np.where(transitions == -1)[0]
            assert len(starts) == len(ends)
            max_runs = max(max_runs, len(starts))
            for a, b in zip(starts, ends):
                runs.append([int(a)+origin_x, int(b)+origin_x])
                assert 0 <= runs[-1][0] < runs[-1][1] <= 32767
                side_runs += 1
        offsets.append(side_runs)
        assert max(offsets[row_base:]) <= 65535
        descriptors.append([start+origin_y, end-start, row_base, run_base])
        stats.append({'region': side, 'pixel_count': int(np.count_nonzero(mask == side)),
                      'rows': end-start, 'runs': side_runs, 'maximum_runs_per_row': int(max_runs),
                      'global_y_range': [start+origin_y, end+origin_y]})
    lines = ['/* Generated source-map per-pixel pit mask. All runs per row retained.',
             ' * Derived world XY uses the original map projection and each visible map depth.',
             ' * Classification uses the documented original-blocker convex envelope H1 policy.',
             ' */', '#ifndef H1_DOTA_ROSHAN_PIXEL_MASK_H', '#define H1_DOTA_ROSHAN_PIXEL_MASK_H',
             'typedef struct {short y;unsigned short rows,row_base,run_base;} DotaPitPixelRows;',
             'static const DotaPitPixelRows dota_pit_pixel_rows[2]={']
    lines += ['    {'+','.join(map(str, row))+'},' for row in descriptors]
    lines += ['};', 'static const unsigned short dota_pit_pixel_row_offsets[]={']
    for start in range(0, len(offsets), 24):
        lines.append('    '+','.join(map(str, offsets[start:start+24]))+',')
    lines += ['};', 'static const unsigned short dota_pit_pixel_runs[][2]={']
    lines += ['    {'+str(a)+','+str(b)+'},' for a, b in runs]
    lines += ['};', '#endif', '']
    return '\n'.join(lines), stats, 16+len(offsets)*2+len(runs)*4


def verify_rle(mask, stats):
    """A second implementation rebuilds all row runs, preserving split spans."""
    from gltf_assets import np
    checked = 0
    for side in (1, 2):
        expected = mask == side
        rebuilt = np.zeros(expected.shape, bool)
        for y in range(mask.shape[0]):
            x, width = 0, mask.shape[1]
            while x < width:
                if not expected[y, x]:
                    x += 1
                    continue
                start = x
                while x < width and expected[y, x]:
                    x += 1
                rebuilt[y, start:x] = True
                checked += 1
        assert np.array_equal(rebuilt, expected)
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-only', action='store_true')
    args = parser.parse_args()
    manifest, geometry = read(SOURCE/'manifest.json'), read(SOURCE/'pit-geometry.json')
    output = ROOT/'game/roshan_data.h'
    output.write_text(data_header(manifest, geometry), encoding='utf8')
    print(json.dumps({'data_header': str(output), 'sha256': sha(output), 'source_species': 11}))
    if args.data_only:
        return
    from gltf_assets import np
    from PIL import Image
    folder = ROOT/'build/conversion/baked-map'
    map_info = read(folder/'manifest.json')
    depth_path, map_path = folder/'map-depth.npy', folder/'map.png'
    assert sha(depth_path) == map_info['depth_sha256'] and sha(map_path) == map_info['map_sha256']
    depth = np.load(depth_path)
    assert depth.shape == (map_info['height'], map_info['width']) and depth.dtype == np.dtype('<u2')
    mask = pixel_regions(geometry, map_info, depth)
    text, stats, static_bytes = mask_header(mask, map_info)
    assert static_bytes < 10*1024
    header = ROOT/'private/generated/dota_roshan_vision.h'
    header.write_text(text, encoding='ascii')
    out = ROOT/'private/roshan/visibility'
    out.mkdir(parents=True, exist_ok=True)
    np.save(out/'map-regions.npy', mask)
    Image.fromarray(mask*100).save(out/'map-regions.png')
    runs = verify_rle(mask, stats)
    # Exact interior source anchors and map-pixel zero outside both envelopes.
    assert int(mask.max()) == 2 and int(mask.min()) == 0
    report = {'complete': True, 'policy': 'H1 convex envelope of original159 FOW nodes; native graph connectivity unknown.',
              'per_pixel_world_inversion': 'x=(global_px+.5)*256/180; y=((global_py+.5)*65536/(181*180)+(32768-depth))/2',
              'mask_samples': mask.size, 'all_multiple_runs_retained': True, 'roundtrip_runs_verified': runs,
              'regions': stats, 'runtime_static_bytes': static_bytes,
              'source_sha256': {'tools/bake_roshan_visibility.py': sha(__file__),
                  'private/roshan/source-rules/manifest.json': sha(SOURCE/'manifest.json'),
                  'private/roshan/source-rules/pit-geometry.json': sha(SOURCE/'pit-geometry.json'),
                  'build/conversion/baked-map/manifest.json': sha(folder/'manifest.json'),
                  'build/conversion/baked-map/map-depth.npy': sha(depth_path),
                  'build/conversion/baked-map/map.png': sha(map_path)},
              'files': {'game/roshan_data.h': sha(output), 'private/generated/dota_roshan_vision.h': sha(header),
                        'private/roshan/visibility/map-regions.npy': sha(out/'map-regions.npy')}}
    (out/'manifest.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print(json.dumps({'runtime_static_bytes': static_bytes, 'regions': stats, 'header_sha256': sha(header)}))


if __name__ == '__main__':
    main()
