"""Derive conservative shadow culling data from the exact installed HBK12.

Offline only: scans the frame directory, eight-byte shadow headers and ground
grid. No H1 client, device path, upload or runtime memory operation is used.
The generated bounds are valid only for the recorded pack SHA256.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import random
import struct

ROOT = Path(__file__).resolve().parents[1]
PACK_SHA256 = 'c312840584571c0958b8409da729ea945ecbf557a55ab6030ff0d2d9cbef78dd'
HEADER = ROOT / 'platform/shadow_cull_data.h'
MANIFEST = ROOT / 'private/baked/shadow-cull/manifest.json'
DIAGNOSTIC = ROOT / 'private/diagnostics/v024-shadow-culling/verification.json'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def relative(path):
    return Path(path).resolve().relative_to(ROOT).as_posix()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf8')


def clamp(value, minimum, maximum):
    return min(maximum, max(minimum, value))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT / 'build/release-v023/DOTA2/SCENE.H1P')
    args = parser.parse_args()
    pack = args.pack.resolve()
    assert sha(pack) == PACK_SHA256, 'The generated table must match the exact final HBK12'
    rows = []
    audited = []
    with pack.open('rb') as source:
        h = struct.unpack('<16I', source.read(64))
        assert h[0] == 0x314b4248 and h[1] == 12 and h[2] == pack.stat().st_size
        assert h[3] == 128 and h[10] == 44278 and h[13:15] == (180, 181)
        source.seek(h[9])
        frames = list(struct.iter_unpack('<IIHHhh8H', source.read(h[10] * 32)))
        source.seek(h[11])
        ground_bytes = source.read(h[12] * h[12] * 2)
        ground = struct.unpack('<' + 'h' * (h[12] * h[12]), ground_bytes)
        for index, frame in enumerate(frames):
            offset, size, fw, fh, fax, fay, model, clip, direction, phase, duration, flags, height, reserved = frame
            if not flags & 4:
                continue
            at = offset + fw * fh * 5
            assert offset >= 64 and at + 8 <= offset + size <= h[2]
            source.seek(at)
            raw = source.read(8)
            w, hh, ax, ay = struct.unpack('<HHhh', raw)
            assert 0 < w <= 256 and 0 < hh <= 256 and size == fw * fh * 5 + 8 + w * hh
            row = dict(index=index, header_offset=at, model=model, clip=clip,
                       direction=direction, phase=phase, payload_bytes=size,
                       body_width=fw, body_height=fh, body_ax=fax, body_ay=fay,
                       width=w, height=hh, anchor_x=ax, anchor_y=ay,
                       left=-ax, right=w-ax, top=-ay, bottom=hh-ay)
            rows.append(row)
            audited.append([index, at, w, hh, ax, ay])
        axe = next(row for row in rows if (row['model'], row['clip'], row['direction'], row['phase']) == (0, 0, 0, 0))
        source.seek(axe['header_offset'] + 8)
        axe_mask = source.read(axe['width'] * axe['height'])

    extrema_fields = ('left', 'right', 'top', 'bottom', 'width', 'height', 'anchor_x', 'anchor_y', 'payload_bytes')

    def extrema(group):
        return {key: [min(row[key] for row in group), max(row[key] for row in group)] for key in extrema_fields}

    by_model = collections.defaultdict(list)
    for row in rows:
        by_model[row['model']].append(row)
    all_extrema = extrema(rows)
    left = all_extrema['left'][0]
    right = all_extrema['right'][1]
    top = all_extrema['top'][0]
    bottom = all_extrema['bottom'][1]
    ground_min, ground_max = min(ground), max(ground)
    coef = h[13] * h[14]
    # A header's rectangle bounds every nonzero mask sample. Terrain is a
    # bilinear interpolation of grid values, so it cannot exceed grid extrema.
    # Monotonicity of integer floor preserves the following interval nesting:
    # global_y0 <= original_y0 <= original_y1 <= global_y1, for every h.z.
    def candidate(sx, sy, hz):
        return (sx + left, sy + top + ((hz-ground_max-1)*coef)//65536 - 2,
                sx + right, sy + bottom + ((hz-ground_min-1)*coef)//65536 + 2)

    def local_heights(row, wx, wy):
        x0 = clamp(wx + (-row['anchor_x'] * 256)//h[13] - 2, 0, 4095)
        x1 = clamp(wx + ((row['width']-row['anchor_x']) * 256)//h[13] + 2, 0, 4095)
        y0 = clamp(wy + (-row['anchor_y'] * 65536)//coef - 2, 0, 4095)
        y1 = clamp(wy + ((row['height']-row['anchor_y']) * 65536)//coef + 2, 0, 4095)
        gx0, gy0 = x0*(h[12]-1)//4095, y0*(h[12]-1)//4095
        gx1 = clamp(x1*(h[12]-1)//4095 + 1, 0, h[12]-1)
        gy1 = clamp(y1*(h[12]-1)//4095 + 1, 0, h[12]-1)
        heights = [ground[y*h[12]+x] for y in range(gy0, gy1+1) for x in range(gx0, gx1+1)]
        return min(heights), max(heights)

    def original(row, sx, sy, hz, low, high):
        return (sx-row['anchor_x'], sy-row['anchor_y']+((hz-high-1)*coef)//65536-2,
                sx-row['anchor_x']+row['width'], sy-row['anchor_y']+row['height']+((hz-low-1)*coef)//65536+2)

    def outside(rect):
        x0, y0, x1, y1 = rect
        return x0 >= 480 or x1 <= 0 or y0 >= 216 or y1 <= 24

    rng = random.Random(0x240531)
    checks = accepted = rejected = 0
    for row in rows:
        # Every frame is covered with both an actual world/camera projection
        # and an independent screen-edge case, using its actual local terrain.
        wx, wy = rng.randrange(4096), rng.randrange(4096)
        low, high = local_heights(row, wx, wy)
        for mode in range(2):
            hz = rng.randrange(-256, 641)
            if mode == 0:
                cx = clamp(wx + rng.randrange(-800, 801), 0, 4095)
                cy = clamp(wy + rng.randrange(-800, 801), 0, 4095)
                cz = rng.randrange(ground_min, ground_max+1)
                sx = 240 + (wx*h[13])//256 - (cx*h[13])//256
                sy = 120 + ((wy-hz)*coef)//65536 - ((cy-cz)*coef)//65536
            else:
                sx, sy = rng.randrange(-300, 781), rng.randrange(-600, 761)
            outer, inner = candidate(sx, sy, hz), original(row, sx, sy, hz, low, high)
            for condition in (outer[0] <= inner[0], outer[1] <= inner[1], outer[2] >= inner[2], outer[3] >= inner[3]):
                checks += 1
                assert condition, (row, outer, inner)
            checks += 1
            assert not outside(outer) or outside(inner), (row, outer, inner)
            rejected += outside(outer)
            accepted += not outside(outer)
        checks += 1
        assert -128 + row['right'] <= 0 and -128-row['body_ax']+row['body_width'] <= 0
    assert accepted > 0 and rejected > 0
    checks += 1
    assert -50-axe['body_ax']+axe['body_width'] <= 0 and -50+axe['right'] > 0
    # Confirm real nonzero penumbra samples, not only a padded bounding box.
    visible_mask_samples = sum(axe_mask[y*axe['width']+x] != 0
                               for y in range(axe['height']) for x in range(axe['width'])
                               if 0 <= -50-axe['anchor_x']+x < 480)
    checks += 1
    assert visible_mask_samples > 0 and not outside(candidate(-50, 120, 0))

    generated = f'''/* Generated by tools/prepare_shadow_cull.py. Exact HBK12 only.
 * Shadow-foot intervals are half-open and include every baked mask sample.
 * Apply terrain height displacement with integer floor and the original
 * +/-2 pixel margin. Never replace shadow bounds with the body rectangle. */
#ifndef H1_DOTA_SHADOW_CULL_DATA_H
#define H1_DOTA_SHADOW_CULL_DATA_H
#define BS_SHADOW_CULL_PACK_SHA256 "{PACK_SHA256}"
#define BS_SHADOW_CULL_PACK_MAGIC 0x314b4248u
#define BS_SHADOW_CULL_PACK_VERSION {h[1]}u
#define BS_SHADOW_CULL_PACK_BYTES {h[2]}u
#define BS_SHADOW_CULL_PACK_FRAMES {h[10]}u
#define BS_SHADOW_CULL_GROUND_GRID {h[12]}u
#define BS_SHADOW_CULL_PROJECTION_ZOOM {h[13]}u
#define BS_SHADOW_CULL_PROJECTION_PITCH {h[14]}u
#define BS_SHADOW_CULL_FRAME_COUNT {len(rows)}u
#define BS_SHADOW_CULL_LEFT_MIN ({left})
#define BS_SHADOW_CULL_RIGHT_MAX ({right})
#define BS_SHADOW_CULL_TOP_MIN ({top})
#define BS_SHADOW_CULL_BOTTOM_MAX ({bottom})
#define BS_SHADOW_CULL_GROUND_MIN ({ground_min})
#define BS_SHADOW_CULL_GROUND_MAX ({ground_max})
#endif
'''
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text(generated, encoding='ascii')
    headers_path = MANIFEST.with_name('headers.json')
    headers_path.parent.mkdir(parents=True, exist_ok=True)
    headers_path.write_text(json.dumps(dict(columns=['frame', 'header_offset', 'width', 'height', 'anchor_x', 'anchor_y'], rows=audited), separators=(',', ':'))+'\n', encoding='ascii')
    examples = dict(
        entirely_offscreen=dict(feet_x=-128, frames=len(rows), all_bodies_and_shadows_outside=True, unnecessary_payload_example_bytes=axe['payload_bytes']),
        axe_edge=dict(feet_x=-50, feet_y=120, frame=axe['index'], header_offset=axe['header_offset'],
                      body_x=[-50-axe['body_ax'], -50-axe['body_ax']+axe['body_width']],
                      shadow_x=[-50-axe['anchor_x'], -50-axe['anchor_x']+axe['width']],
                      nonzero_visible_mask_samples=visible_mask_samples, must_not_cull=True))
    proof = dict(passed=True, checks=checks, random_seed=0x240531,
                 actual_local_terrain_cases=len(rows)*2, global_culled_cases=rejected,
                 retained_cases=accepted, false_culls=0, x_proven=True, y_proven=True,
                 basis='Each original per-frame candidate rectangle is contained in the generated global candidate; floor is monotone and every local bilinear height lies within global grid extrema.',
                 y_min_formula='sy + TOP_MIN + floor((h.z - GROUND_MAX - 1) * pitch * zoom / 65536) - 2',
                 y_max_formula='sy + BOTTOM_MAX + floor((h.z - GROUND_MIN - 1) * pitch * zoom / 65536) + 2',
                 viewport=dict(x=[0, 480], y=[24, 216]), examples=examples)
    manifest = dict(passed=True, offline_only=True, physical_h1_tested=False,
                    pack=relative(pack), pack_sha256=PACK_SHA256, pack_bytes=h[2], pack_version=h[1], pack_frames=h[10],
                    shadow_frames=len(rows), shadow_header_bytes_read=len(rows)*8, shadow_mask_example_bytes_read=len(axe_mask),
                    frame_directory=dict(offset=h[9], bytes=h[10]*32),
                    ground_grid=dict(offset=h[11], side=h[12], bytes=len(ground_bytes), sha256=hashlib.sha256(ground_bytes).hexdigest(), minimum=ground_min, maximum=ground_max),
                    global_extrema=all_extrema,
                    model_extrema={str(model): dict(frames=len(group), extrema=extrema(group)) for model, group in sorted(by_model.items())},
                    headers=dict(path=relative(headers_path), sha256=sha(headers_path), count=len(audited)),
                    generated_header=dict(path=relative(HEADER), sha256=sha(HEADER)),
                    source_sha256={relative(Path(__file__)): sha(__file__)}, proof=proof,
                    scope='Conservative rejection only, bound to the exact recorded pack SHA. Global X/Y mask-foot envelopes and global bilinear-ground extrema preserve the original candidate rectangle, including its integer-floor and two-pixel margins. This does not prove runtime speed or replace the original per-frame shadow drawing/depth tests.')
    write_json(MANIFEST, manifest)
    receipt = dict(manifest)
    receipt.update(manifest=dict(path=relative(MANIFEST), sha256=sha(MANIFEST)), checks=checks,
                   source_sha256={relative(Path(__file__)): sha(__file__), relative(HEADER): sha(HEADER)})
    write_json(DIAGNOSTIC, receipt)
    print(json.dumps(dict(passed=True, checks=checks, shadow_frames=len(rows), headers_bytes=len(rows)*8,
                          x_bounds=[left, right], y_bounds=[top, bottom], ground=[ground_min, ground_max],
                          x_proven=True, y_proven=True, generated_header=relative(HEADER), diagnostic=relative(DIAGNOSTIC))))


if __name__ == '__main__':
    main()
