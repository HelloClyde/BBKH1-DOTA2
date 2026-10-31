"""PC-only source-mesh hero shadows, sampled at the body's exact poses.

The output shadow is an independent alpha rect with its own feet anchor.
Original body pixels and crop are copied exactly, never composited with
the shadow: H1 can project the precomputed mask onto its ground-height grid,
draw all shadows before any actors and leave unit hit bounds unchanged.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import shutil
import time
from PIL import Image, ImageDraw, ImageFilter
from gltf_assets import np, ROOT
from bake_sprite_raster import Raster

SUN_TO_LIGHT = (-.45, -.35, .82)
SOFTEN_PIXELS = 1.1
OPACITY = .42
SHADOW_FLAG = 4


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def body_digest(rgba, depth):
    return hashlib.sha256(rgba.tobytes()+depth.astype('<i2').tobytes()).hexdigest()


def projection_metadata(ss=2):
    return {'kind': 'original skinned-triangle directional ground projection',
            'sun_to_light_world': list(SUN_TO_LIGHT),
            'ground_projection': 'x -= light.x/light.z*max(z,0); y -= light.y/light.z*max(z,0)',
            'zoom_q8': 180, 'pitch_q8': 181, 'supersamples': ss,
            'soften_radius_pixels': SOFTEN_PIXELS, 'opacity': OPACITY,
            'alpha': 'original UV and material alpha/cutoff; union of original triangles',
            'runtime': 'original body RGB565/alpha8/depth16 then8-byte <HHhh> shadow width,height,anchorX,anchorY then independent shadow_alpha8 rect; no runtime mesh or light calculation'}


def project_shadow(model, pose, direction, ss=2):
    """Rasterize the actual posed silhouette as a world-lit ground mask."""
    angle = direction*math.tau/16
    co, si = math.cos(angle), math.sin(angle)
    prepared, bounds = [], []
    for part in pose:
        q = part['points']
        x = q[:, 0]*co-q[:, 1]*si
        y = q[:, 0]*si+q[:, 1]*co
        z = np.maximum(q[:, 2], 0)
        gx = x-SUN_TO_LIGHT[0]/SUN_TO_LIGHT[2]*z
        gy = y-SUN_TO_LIGHT[1]/SUN_TO_LIGHT[2]*z
        projected = np.column_stack((gx*180/256, gy*181*180/65536, -gy))
        bounds.append(projected[np.unique(part['faces']), :2])
        prepared.append((part, projected))
    bound = np.concatenate(bounds)
    # Keep enough empty border for the full Gaussian penumbra before union
    # cropping; the body and the mask share the exact existing feet origin.
    left, top = np.floor(bound.min(0)).astype(int)-5
    right, bottom = np.ceil(bound.max(0)).astype(int)+5
    raster = Raster(right-left+1, bottom-top+1, ss)
    for part, projected in prepared:
        projected[:, :2] -= [left, top]
        texture, cutoff, factor = model.texture(part['doc'], part['material'])
        raster.draw(projected, part['faces'], part['uv'],
                    np.tile([0., 0., 1.], (len(projected), 1)), texture,
                    cutoff=cutoff, factor=(0, 0, 0, factor[3]), unlit=True)
    rgba, _ = raster.finish()
    mask = np.asarray(Image.fromarray(rgba[:, :, 3]).filter(ImageFilter.GaussianBlur(SOFTEN_PIXELS)))
    alpha = np.rint(mask*OPACITY).astype(np.uint8)
    return alpha, int(-left), int(-top)


def combine_shadow(rgba, depth, anchor_x, anchor_y, shadow, shadow_ax, shadow_ay):
    """Union the crops, preserving the unmodified original body subarray."""
    bh, bw = rgba.shape[:2]
    sh, sw = shadow.shape
    left, top = min(-anchor_x, -shadow_ax), min(-anchor_y, -shadow_ay)
    right, bottom = max(bw-anchor_x, sw-shadow_ax), max(bh-anchor_y, sh-shadow_ay)
    width, height = right-left, bottom-top
    if width > 256 or height > 256:
        raise ValueError(f'Hero body plus shadow exceeds H1 limit: {width}x{height}')
    colour = np.zeros((height, width, 4), np.uint8)
    relative = np.zeros((height, width), dtype='<i2')
    alpha = np.zeros((height, width), np.uint8)
    bx, by = -anchor_x-left, -anchor_y-top
    sx, sy = -shadow_ax-left, -shadow_ay-top
    colour[by:by+bh, bx:bx+bw] = rgba
    relative[by:by+bh, bx:bx+bw] = depth
    alpha[sy:sy+sh, sx:sx+sw] = shadow
    assert np.array_equal(colour[by:by+bh, bx:bx+bw], rgba)
    assert np.array_equal(relative[by:by+bh, bx:bx+bw], depth)
    return colour, relative, alpha, -left, -top, [bx, by, bw, bh]


def compact_shadow(shadow, anchor_x, anchor_y):
    """Trim zero mask borders while keeping every projected shadow texel."""
    ys, xs = np.where(shadow > 0)
    if not len(xs):
        raise ValueError('Original hero pose generated an empty shadow')
    x0, x1 = int(xs.min()), int(xs.max())+1
    y0, y1 = int(ys.min()), int(ys.max())+1
    return shadow[y0:y1, x0:x1].copy(), int(anchor_x-x0), int(anchor_y-y0)


def augment_existing(output, snapshot, ss=2):
    """Add shadows without recomputing or changing any existing body pixels."""
    from bake_sprites import Model, HEROES
    output, snapshot = Path(output).resolve(), Path(snapshot).resolve()
    path = output/'manifest.json'
    original_hash = sha(path)
    catalog_hash = sha(output/'skill-motion-catalog.json')
    manifest = json.loads(path.read_text(encoding='utf8'))
    if not manifest.get('complete'):
        raise ValueError('Complete original body bake is required')
    snapshot.mkdir(parents=True, exist_ok=True)
    if not (snapshot/'manifest.json').exists():
        shutil.copy2(path, snapshot/'manifest.json')
        shutil.copy2(output/'skill-motion-catalog.json', snapshot/'skill-motion-catalog.json')
    else:
        # Restart from the immutable original body records. Reading an already
        # expanded frame as a fresh body would otherwise shift its feet origin
        # again after an interrupted or repeated augmentation.
        expected = sha(snapshot/'manifest.json')
        if original_hash != expected and manifest.get('shadow', {}).get('source_manifest_sha256') != expected:
            raise ValueError('Snapshot belongs to a different body bake; use a fresh --snapshot directory')
        manifest = json.loads((snapshot/'manifest.json').read_text(encoding='utf8'))
        original_hash = expected
    npc_before = {r['npz']: sha(ROOT/r['npz']) for r in manifest['frames'] if r['model'] >= 4}
    receipts, contact = [], []
    start = time.perf_counter()
    for hero, slug in enumerate(HEROES):
        model = Model(slug, hero)
        rows = [r for r in manifest['frames'] if r['model'] == hero]
        pose_key, pose = None, None
        # The old manifest is direction-major within each frame; this reuses
        # each expensive original skeleton evaluation for all16 directions.
        for index, record in enumerate(rows):
            key = record['source_clip'], record['source_seconds']
            if pose_key != key:
                pose = model.pose(*key)
                pose_key = key
            source = ROOT/record['npz']
            backup = snapshot/slug/source.name
            backup.parent.mkdir(exist_ok=True)
            if not backup.exists():
                shutil.copy2(source, backup)
            if sha(backup) != record['sha256']:
                raise ValueError(f'Original body snapshot changed: {backup}')
            with np.load(backup) as data:
                body, depth = data['rgba'].copy(), data['depth'].astype('<i2')
            digest = body_digest(body, depth)
            shadow, ax, ay = project_shadow(model, pose, record['direction'], ss)
            alpha, sax, say = compact_shadow(shadow, ax, ay)
            colour, relative = body, depth
            ax, ay = record['anchor_x'], record['anchor_y']
            box = [0, 0, body.shape[1], body.shape[0]]
            assert digest == body_digest(colour, relative)
            np.savez_compressed(source, rgba=colour, depth=relative, shadow=alpha)
            old = {name: record[name] for name in ('width', 'height', 'anchor_x', 'anchor_y', 'sha256')}
            record.update(width=colour.shape[1], height=colour.shape[0], anchor_x=int(ax), anchor_y=int(ay),
                          flags=record.get('flags', 0)|SHADOW_FLAG, sha256=sha(source),
                          shadow_width=alpha.shape[1], shadow_height=alpha.shape[0],
                          shadow_anchor_x=sax, shadow_anchor_y=say,
                          shadow_pixels=int(np.count_nonzero(alpha)), shadow_alpha_max=int(alpha.max()),
                          body_input=old, body_box_in_frame=box, body_rgba_depth_sha256=digest)
            receipts.append({'model': hero, 'clip': record['clip'], 'frame': record['frame'],
                             'direction': record['direction'], 'body_rgba_depth_sha256': digest,
                             'shadow_alpha_sha256': hashlib.sha256(alpha.tobytes()).hexdigest(),
                             'shadow_pixels': record['shadow_pixels'], 'shadow_triangles': model.original_triangles,
                             'npz_sha256': record['sha256']})
            if record['direction'] == 4 and record['clip'] in (0, 1, 2, 4) and record['frame'] == (0 if record['clip'] == 0 else 3):
                preview, _, preview_alpha, pax, pay, _ = combine_shadow(colour, relative, ax, ay, alpha, sax, say)
                contact.append((slug, record['clip'], preview, preview_alpha, pax, pay))
            if index % 256 == 0:
                print(json.dumps({'hero': slug, 'frames_completed': index+1, 'frames': len(rows)}), flush=True)
        per_hero = json.loads((output/slug/'manifest.json').read_text(encoding='utf8'))
        per_hero['frames'] = rows
        per_hero['shadow'] = projection_metadata(ss)
        per_hero['original_body_source_sha256'] = per_hero.get('original_body_source_sha256', per_hero.get('source_sha256', {})).copy()
        per_hero.setdefault('source_sha256', {}).update(
            {'bake_hero_shadows.py': sha(__file__), 'bake_sprites.py': sha(Path(__file__).with_name('bake_sprites.py'))})
        (output/slug/'manifest.json').write_text(json.dumps(per_hero, indent=2)+'\n', encoding='utf8')
    assert npc_before == {name: sha(ROOT/name) for name in npc_before}, 'Non-hero source frames changed'
    assert catalog_hash == sha(output/'skill-motion-catalog.json'), 'Original skill-motion catalog changed'
    shadow_info = dict(projection_metadata(ss), frames=len(receipts), source_manifest_sha256=original_hash,
                       all_body_pixels_preserved=True, all_nonhero_frames_preserved=True, skill_motion_catalog_sha256=catalog_hash,
                       source_sha256={'bake_hero_shadows.py': sha(__file__),
                                      'bake_sprites.py': sha(Path(__file__).with_name('bake_sprites.py'))}, receipts=receipts)
    (output/'shadows.json').write_text(json.dumps(shadow_info, indent=2)+'\n', encoding='utf8')
    manifest['shadow'] = {key: value for key, value in shadow_info.items() if key != 'receipts'}
    manifest['shadow']['receipts_sha256'] = sha(output/'shadows.json')
    path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf8')
    if contact:
        sheet = Image.new('RGB', (960, 184*((len(contact)+1)//2)), (20, 25, 24))
        for i, (slug, clip, colour, alpha, ax, ay) in enumerate(contact):
            row = Image.new('RGB', (480, 184), (20, 25, 24))
            ImageDraw.Draw(row).text((4, 4), f'{slug} clip{clip}: body / source-pose shadow', fill='white')
            for side in range(2):
                canvas = Image.new('RGB', (240, 160), (92, 103, 94))
                draw = ImageDraw.Draw(canvas)
                for y in range(0, 160, 16):
                    draw.line((0, y, 240, y), fill=(104, 112, 103))
                if side:
                    shadow_image = np.zeros_like(colour)
                    shadow_image[:, :, 3] = alpha
                    image = Image.fromarray(shadow_image)
                    canvas.paste(image, (96-ax, 120-ay), image)
                image = Image.fromarray(colour)
                canvas.paste(image, (96-ax, 120-ay), image)
                row.paste(canvas, (side*240, 24))
            sheet.paste(row, (i % 2*480, i//2*184))
        sheet.save(output/'shadow-contact.png')
    result = dict(passed=True, hero_shadow_frames=len(receipts), nonhero_frames_preserved=len(npc_before),
                  all_body_pixels_preserved=True, elapsed_seconds=time.perf_counter()-start,
                  maximum_frame_pixels=max(r['width']*r['height'] for r in manifest['frames'] if r['model'] < 4),
                  manifest_sha256=sha(path), receipts_sha256=sha(output/'shadows.json'))
    (output/'shadow-verification.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(json.dumps(result), flush=True)
    return result


def compact_existing(output, snapshot):
    """Losslessly convert the first union-crop bake to compact mask rects."""
    output, snapshot = Path(output).resolve(), Path(snapshot).resolve()
    manifest = json.loads((output/'manifest.json').read_text(encoding='utf8'))
    original = json.loads((snapshot/'manifest.json').read_text(encoding='utf8'))
    key = lambda r: tuple(r[k] for k in ('model', 'clip', 'direction', 'frame'))
    before = {key(r): r for r in original['frames']}
    old_receipts = {key(r): r for r in json.loads((output/'shadows.json').read_text(encoding='utf8'))['receipts']}
    npc = {r['npz']: sha(ROOT/r['npz']) for r in manifest['frames'] if r['model'] >= 4}
    catalog_hash = sha(output/'skill-motion-catalog.json')
    receipts, checks, alpha_bytes, body_pixels, largest = [], 0, 0, 0, 0
    for record in manifest['frames']:
        if record['model'] >= 4:
            continue
        source, old = ROOT/record['npz'], before[key(record)]
        slug = manifest['models'][record['model']]['name']
        with np.load(snapshot/slug/source.name) as data:
            body, depth = data['rgba'], data['depth'].astype('<i2')
        with np.load(source) as data:
            rgba, z, shadow = data['rgba'], data['depth'], data['shadow']
        bx, by, bw, bh = record['body_box_in_frame']
        assert np.array_equal(rgba[by:by+bh, bx:bx+bw], body) and np.array_equal(z[by:by+bh, bx:bx+bw], depth)
        checks += 2
        if 'shadow_anchor_x' in record:
            alpha, sax, say = compact_shadow(shadow, record['shadow_anchor_x'], record['shadow_anchor_y'])
        else:
            alpha, sax, say = compact_shadow(shadow, record['anchor_x'], record['anchor_y'])
        np.savez_compressed(source, rgba=body, depth=depth, shadow=alpha)
        record.update(width=old['width'], height=old['height'], anchor_x=old['anchor_x'], anchor_y=old['anchor_y'],
                      shadow_width=alpha.shape[1], shadow_height=alpha.shape[0], shadow_anchor_x=sax, shadow_anchor_y=say,
                      body_box_in_frame=[0, 0, old['width'], old['height']], sha256=sha(source))
        receipt = old_receipts[key(record)].copy()
        receipt.update(npz_sha256=record['sha256'], shadow_alpha_sha256=hashlib.sha256(alpha.tobytes()).hexdigest(),
                       shadow_width=alpha.shape[1], shadow_height=alpha.shape[0], shadow_anchor_x=sax, shadow_anchor_y=say)
        receipts.append(receipt)
        alpha_bytes += alpha.size
        body_pixels += body.shape[0]*body.shape[1]
        largest = max(largest, body.shape[0]*body.shape[1]*5+8+alpha.size)
    assert npc == {name: sha(ROOT/name) for name in npc}
    assert catalog_hash == sha(output/'skill-motion-catalog.json')
    info = dict(projection_metadata(manifest.get('ss', 2)), frames=len(receipts), source_manifest_sha256=sha(snapshot/'manifest.json'),
                skill_motion_catalog_sha256=catalog_hash, all_body_pixels_preserved=True, all_nonhero_frames_preserved=True,
                source_sha256={'bake_hero_shadows.py': sha(__file__), 'bake_sprites.py': sha(Path(__file__).with_name('bake_sprites.py'))}, receipts=receipts)
    (output/'shadows.json').write_text(json.dumps(info, indent=2)+'\n', encoding='utf8')
    manifest['shadow'] = {k: v for k, v in info.items() if k != 'receipts'}
    manifest['shadow']['receipts_sha256'] = sha(output/'shadows.json')
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf8')
    for hero in range(4):
        path = output/manifest['models'][hero]['name']/'manifest.json'
        per_hero = json.loads(path.read_text(encoding='utf8'))
        per_hero.update(frames=[r for r in manifest['frames'] if r['model'] == hero], shadow=projection_metadata(manifest.get('ss', 2)))
        per_hero['source_sha256'].update(info['source_sha256'])
        path.write_text(json.dumps(per_hero, indent=2)+'\n', encoding='utf8')
    result = dict(passed=True, checks=checks, hero_shadow_frames=len(receipts), nonhero_frames_preserved=len(npc),
                  all_body_pixels_preserved=True, all_body_crop_anchors_preserved=True, body_payload_bytes=body_pixels*5,
                  shadow_alpha_bytes=alpha_bytes, shadow_header_bytes=len(receipts)*8,
                  hero_combined_payload_bytes=body_pixels*5+alpha_bytes+len(receipts)*8, maximum_frame_payload_bytes=largest,
                  manifest_sha256=sha(output/'manifest.json'), receipts_sha256=sha(output/'shadows.json'))
    (output/'shadow-verification.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(json.dumps(result), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'private/baked/sprites')
    parser.add_argument('--snapshot', type=Path, default=ROOT/'build/conversion/hero-shadow-before')
    parser.add_argument('--ss', type=int, default=2)
    parser.add_argument('--compact-existing', action='store_true', help='Losslessly compact a previously produced union-crop bake')
    args = parser.parse_args()
    if args.compact_existing:
        compact_existing(args.output, args.snapshot)
    else:
        augment_existing(args.output, args.snapshot, args.ss)


if __name__ == '__main__':
    main()
