"""Bake the map's original three flowing fountain meshes and ambient images.

Original triangle positions, UVs and full map matrices are evaluated on the PC.
The Source UV scroll speeds are quantized to integral repeats in a 1.6s loop.
Tintable Radiant mist/spray is reconstructed from the original child graphs;
this is a small-screen interpretation, not a Source particle/shader interpreter.
"""
from pathlib import Path
import argparse, hashlib, json, math, re, subprocess
from PIL import Image
from gltf_assets import Gltf, np, ROOT
from source2_vpk import VpkArchive
from bake_sprite_raster import Raster
from bake_effects import Canvas, linear, srgb, sample, ZOOM, PITCH

OUT = ROOT/'private/baked/fountain'
STEAM = Path('D:/Program Files (x86)/Steam/steamapps/common/dota 2 beta/game/dota')
SCENE = ROOT/'private/source2/exports/map-geometry/dota.gltf'
ENTITIES = ROOT/'private/source2/reports/entity-default-parsed.json'
BASIS = np.array([[0, 0, 1], [-1, 0, 0], [0, 1, 0]], float)
WORLD_SCALE = 100/2.54/4
MODELS = [('radiant', 'fountain_radiant_00', 1), ('waterfall', 'fountain_waterfall', 4),
          ('dire', 'fountain_dire_00', 3)]
ANCHORS = [[184, 3783, 0], [3900, 336, 0]]
TEXTURES = {'orb': 'materials/particle/orb_tintable/particle_circular_orb_tintable.vtex_c',
            'spray': 'materials/particle/spray1/spray1.vtex_c',
            'wave': 'materials/particle/particle_ring_wave_8.vtex_c'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def rel(path):
    return Path(path).resolve().relative_to(ROOT).as_posix()


def export(vpk, tool):
    folder = OUT/'source'
    folder.mkdir(parents=True, exist_ok=True)
    receipts, models, textures = [], {}, {}
    scene = json.loads(SCENE.read_text(encoding='utf8'))
    for name, stem, cycles in MODELS:
        entry = 'models/effects/'+stem+'.vmdl_c'
        target = folder/(name+'_effect')/(name+'_effect.gltf')
        target.parent.mkdir(exist_ok=True)
        if not target.exists():
            command = [str(tool), '-d', '-i', str(vpk.path), '-f', entry, '-o', str(target),
                       '--game', str(STEAM/'gameinfo.gi'), '--gltf_export_format', 'gltf',
                       '--gltf_export_extras', '--gltf_export_materials', '--gltf_textures_adapt']
            result = subprocess.run(command, capture_output=True, text=True)
            (target.parent/'export.log').write_text(result.stdout+result.stderr, encoding='utf8')
            if result.returncode:
                raise RuntimeError(result.stdout+result.stderr)
        g = Gltf(target)
        node = next(n for n in scene['nodes'] if n.get('name') == stem+'.'+stem+'_model')
        matrix = np.array(node['matrix']).reshape(4, 4).T
        parts = []
        for original in g.primitives():
            p = dict(original)
            p['points'] = (p['points']@matrix[:3, :3].T+matrix[:3, 3])@BASIS.T*WORLD_SCALE+[2048, 2048, 0]
            p['normals'] = np.repeat([[0., 0., 1.]], len(p['points']), axis=0)
            material = g.material(p['material'])
            image = np.asarray(Image.open(material['image']).convert('RGBA'))
            rgb = linear(image[:, :, :3].astype(float)/255)
            peak = rgb.max(2)
            # Preserve additive black as transparency, including soft edges.
            normalized = rgb/np.maximum(peak[:, :, None], 1e-8)
            p['texture'] = np.dstack((np.rint(srgb(normalized)*255), np.rint(peak*image[:, :, 3]*.46))).clip(0, 255).astype(np.uint8)
            p['cycles'] = cycles
            parts.append(p)
        model_receipt = dict(name=name, source=vpk.identity(entry), gltf=rel(target), gltf_sha256=sha(target),
                             buffers=g.buffer_sha256, node_name=node['name'], matrix_gltf=matrix.tolist(),
                             triangles=sum(len(p['faces']) for p in parts), loop_uv_cycles=-cycles,
                             original_materials=[m['extras']['vmat'] for m in g.doc['materials']])
        dependencies = []
        for m in g.doc['materials']:
            vmat = m['extras']['vmat']
            for resource in [vmat['Name'], *vmat.get('TextureParams', {}).values()]:
                resource += '_c'
                dependencies.append(vpk.identity(resource))
        model_receipt['dependencies'] = dependencies
        models[name] = parts
        receipts.append(model_receipt)
    graphs = []
    pending = ['particles/addons_gameplay/fountain_tintable_ambient.vpcf_c']
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        raw = vpk.read(name)
        compiled = folder/Path(name).name
        compiled.write_bytes(raw)
        graphs.append(vpk.identity(name))
        decompiled = compiled.with_suffix('.vpcf')
        if not decompiled.exists():
            result = subprocess.run([str(tool), '-d', '-i', str(compiled), '-o', str(decompiled)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout+result.stderr)
        for match in re.findall(rb'particles/[a-zA-Z0-9_./-]+\.vpcf', raw):
            ref = match.decode()+'_c'
            if ref in vpk.entries:
                pending.append(ref)
    texture_receipts = []
    for name, entry in TEXTURES.items():
        directory = folder/name
        directory.mkdir(exist_ok=True)
        raw = directory/(name+'.vtex_c')
        raw.write_bytes(vpk.read(entry))
        target = directory/(name+'.png')
        if not list(directory.glob(name+'_seq*.png')) and not target.exists():
            result = subprocess.run([str(tool), '-d', '-i', str(raw), '-o', str(target)], capture_output=True, text=True)
            (directory/'export.log').write_text(result.stdout+result.stderr, encoding='utf8')
            if result.returncode:
                raise RuntimeError(result.stdout+result.stderr)
        images = sorted(directory.glob(name+'_seq*.png'))
        if not images:
            images = [target]
        textures[name] = []
        for path in images:
            image = np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)/255
            rgb = linear(image[:, :, :3])
            peak = rgb.max(2)
            textures[name].append(np.dstack((rgb/np.maximum(peak[:, :, None], 1e-8), peak*image[:, :, 3])))
        texture_receipts.append(dict(name=name, source=vpk.identity(entry), decoded=[dict(path=rel(p), sha256=sha(p)) for p in images]))
    return models, textures, dict(meshes=receipts, particle_graphs=graphs, particle_textures=texture_receipts)


class FountainCanvas(Canvas):
    def __init__(self, width, height, ax, ay, ss):
        self.width, self.height, self.ax, self.ay, self.ss = width, height, ax, ay, ss
        y, x = np.mgrid[:height*ss, :width*ss]
        self.px, self.py = (x+.5)/ss-ax, (y+.5)/ss-ay
        self.x, self.y = self.px/ZOOM, self.py/PITCH
        self.rgb = np.zeros((*x.shape, 3), np.float32)
        self.alpha = np.zeros(x.shape, np.float32)
        self.depth = np.full(x.shape, np.inf, np.float32)


def cp_world(entity, source_offset):
    # The source graph explicitly orients CP6 by90 degrees. Apply the source
    # yaw for the entity; the control point's source height stays unflattened.
    yaw = math.radians(entity['angles'][1])
    x, y, z = source_offset
    sx = x*math.cos(yaw)-y*math.sin(yaw)
    sy = x*math.sin(yaw)+y*math.cos(yaw)
    origin = entity['origin']
    return np.array([2048+(origin[0]+sx)/4, 2048-(origin[1]+sy)/4, (origin[2]+z)/4])


def frame(team, phase, models, textures, entity, ss):
    phase %= 1.0
    anchor = np.array(ANCHORS[team])
    parts = [p for name in (('radiant', 'waterfall') if team == 0 else ('dire',)) for p in models[name]]
    positions = np.concatenate([p['points']-anchor for p in parts])
    projected = np.column_stack((positions[:, 0]*ZOOM, (positions[:, 1]-positions[:, 2])*PITCH))
    left, top = np.floor(projected.min(0)).astype(int)-5
    right, bottom = np.ceil(projected.max(0)).astype(int)+5
    width, height = int(right-left+1), int(bottom-top+1)
    assert max(width, height) <= 256, (width, height)
    energy = np.zeros((height, width, 3), np.float32)
    alpha = np.zeros((height, width), np.float32)
    depth = np.full((height, width), np.inf, np.float32)
    for p in parts:
        raster = Raster(width, height, ss)
        q = p['points']-anchor
        xyz = np.column_stack((q[:, 0]*ZOOM-left, (q[:, 1]-q[:, 2])*PITCH-top, -(q[:, 1]+q[:, 2])))
        uv = p['uv'].copy()
        uv[:, 0] -= phase*p['cycles']
        raster.draw(xyz, p['faces'], uv, p['normals'], p['texture'], cutoff=0, repeat=True, unlit=True)
        rgba, z = raster.finish()
        a = rgba[:, :, 3].astype(float)/255
        energy += linear(rgba[:, :, :3].astype(float)/255)*a[:, :, None]
        alpha = np.maximum(alpha, a)
        depth = np.minimum(depth, np.where(a > .003, z, np.inf))
    if team == 0:
        canvas = FountainCanvas(width, height, -int(left), -int(top), ss)
        tint = np.array(entity['particle_tint_color'])/255
        for offset, radius, shift in [((-10, -10, -5), 65/4, .1), ((-55, -65, -55), 110/4, .4), ((-75, -130, 50), 163/4, .7)]:
            p = cp_world(entity, offset)-anchor
            texture = textures['orb'][int((phase+shift)*len(textures['orb'])) % len(textures['orb'])]
            canvas.glow(texture, *p, radius*ZOOM, tint, .24+.05*math.sin(math.tau*(phase+shift)))
        # Original graph: six maintained sprays, .25-.3s lifetimes, source
        # gravity-250 and CP3 birth. Small stable phases make a seamless loop.
        base = cp_world(entity, (-10, -10, -5))-anchor
        for j in range(3):
            age = (phase+j/3) % 1
            radius = 3+12*age
            x = canvas.x-base[0]
            y = canvas.y+base[2]-base[1]
            image = sample(textures['wave'][7 % len(textures['wave'])], .5+x/(2*radius), .5+y/(2*radius))
            canvas.add(image, -(canvas.y+2*base[2]), tint, .25*math.sin(age*math.pi)**2)
        for j in range(6):
            age = (phase+j/6) % 1
            fade = math.sin(age*math.pi)**2
            angle = j*math.tau/6
            x, y = base[:2]+np.array([math.cos(angle), math.sin(angle)])*(3+8*age)
            z = base[2]+13*age-9*age*age
            texture = textures['spray'][int(age*len(textures['spray'])) % len(textures['spray'])]
            canvas.glow(texture, x, y, z, 2.4, tint, .6*fade)
        # Combine at the original rectangle. Canvas.finish crops, so retain its
        # full resolved arrays here to keep a stable frame anchor and dimensions.
        a = canvas.alpha.reshape(height, ss, width, ss).mean((1, 3))
        energy += canvas.rgb.reshape(height, ss, width, ss, 3).mean((1, 3))
        alpha = np.maximum(alpha, a)
        depth = np.minimum(depth, canvas.depth.reshape(height, ss, width, ss).min((1, 3)))
    rgb = energy/np.maximum(alpha[:, :, None], 1e-8)
    rgba = np.dstack((np.rint(np.clip(srgb(rgb), 0, 1)*255).astype(np.uint8), np.rint(np.clip(alpha, 0, 1)*255).astype(np.uint8)))
    depth = np.rint(np.where((rgba[:, :, 3] > 0) & np.isfinite(depth), depth, 0)).clip(-32768, 32767).astype('<i2')
    return rgba, depth, -int(left), -int(top)


def map_preview(rgba, depth, ax, ay, anchor, target):
    """Preview against the actual map/depth with the native additive rule."""
    source = np.asarray(Image.open(ROOT/'build/conversion/baked-map/map.png').convert('RGB'))
    zmap = np.load(ROOT/'build/conversion/baked-map/map-depth.npy')
    cx, cy, cz = (250, 3755, 100) if anchor[0] < 1000 else (3880, 355, 100)
    gx = cx*180//256
    gy = (cy-cz)*180*181//65536
    crop = np.zeros((272, 480, 3), np.uint8)
    zz = np.full((272, 480), 65535, np.uint16)
    # Map manifest origin is in absolute projected pixels.
    manifest = json.loads((ROOT/'build/conversion/baked-map/manifest.json').read_text(encoding='utf8'))
    ox, oy = manifest['origin_pixels']
    for yy in range(24, 216):
        sy = gy+yy-120-oy
        xx = np.arange(480)+gx-240-ox
        selected = (xx >= 0) & (xx < source.shape[1])
        if 0 <= sy < source.shape[0]:
            crop[yy, selected] = source[sy, xx[selected]]
            zz[yy, selected] = zmap[sy, xx[selected]]
    x0 = anchor[0]*180//256-gx+240-ax
    y0 = (anchor[1]-anchor[2])*180*181//65536-gy+120-ay
    before = crop.copy()
    for yy in range(rgba.shape[0]):
        py = yy+y0
        if not 24 <= py < 216:
            continue
        for xx in range(rgba.shape[1]):
            px = xx+x0
            if not 0 <= px < 480:
                continue
            a = int(rgba[yy, xx, 3])
            if a and 32768-anchor[1]-anchor[2]+int(depth[yy, xx]) <= int(zz[py, px]):
                crop[py, px] = np.minimum(crop[py, px].astype(np.uint16)+rgba[yy, xx, :3].astype(np.uint16)*a//255, 255)
    pair = np.concatenate((before, crop), axis=1)
    Image.fromarray(pair).save(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frames', type=int, default=16)
    parser.add_argument('--ss', type=int, default=2)
    parser.add_argument('--preview-only', action='store_true')
    args = parser.parse_args()
    assert args.frames == 16 and 1 <= args.ss <= 4
    OUT.mkdir(parents=True, exist_ok=True)
    vpk = VpkArchive(STEAM/'pak01_dir.vpk')
    tool = ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe'
    models, textures, source = export(vpk, tool)
    entities = [e for e in json.loads(ENTITIES.read_bytes()) if e.get('classname') == 'ent_dota_fountain']
    by_team = {e['teamnumber']: e for e in entities}
    records, loops = [], []
    for team, name in enumerate(('radiant', 'dire')):
        folder = OUT/name
        folder.mkdir(exist_ok=True)
        sequence = []
        for number in range(1 if args.preview_only else 16):
            rgba, depth, ax, ay = frame(team, number/16, models, textures, by_team[str(2+team)], args.ss)
            path = folder/f'5-00-{number:02d}.npz'
            np.savez_compressed(path, rgba=rgba, depth=depth)
            Image.fromarray(rgba).save(path.with_suffix('.png'))
            if number == 0:
                map_preview(rgba, depth, ax, ay, ANCHORS[team], folder/'map-before-after.png')
            sequence.append(Image.fromarray(rgba))
            records.append(dict(model=6+team, clip=5, direction=0, frame=number, frames=16,
                                duration_ticks=32, loop=True, anchor_x=ax, anchor_y=ay,
                                width=rgba.shape[1], height=rgba.shape[0], npz=rel(path), sha256=sha(path),
                                source_clip='source_fountain_uv_loop', source_seconds=number*.1,
                                height_world=1, flags=3, source_model_count=2 if team == 0 else 1))
        sequence[0].save(folder/'loop.gif', save_all=True, append_images=sequence[1:], duration=100, loop=0, disposal=2)
        loops.append(dict(team=team, model=6+team, clip=5, anchor_world=ANCHORS[team], frames=16,
                          frame_ticks=2, duration_ticks=32, flags=3,
                          distinct_frames=len({r['sha256'] for r in records if r['model'] == 6+team}),
                          uncompressed_bytes=sum(r['width']*r['height']*5 for r in records if r['model'] == 6+team),
                          source_entity={k: by_team[str(2+team)][k] for k in ('origin', 'angles', 'model', 'hammerUniqueId', 'particle_tint_color', 'ambientfx')}))
    manifest = dict(complete=not args.preview_only, frames=records, fountains=loops, source=source,
                    source_sha256={rel(p): sha(p) for p in (Path(__file__), SCENE, ENTITIES)},
                    method='Original map effect mesh/UV/material pixels and exact map instance matrices. Black additive texels become transparent; no depth writes. Radiant original ambient images are simplified offline from original child graph CP offsets, sizes and tint. Original source UV rate .525/2.25/1.875 cycles per sec becomes seamless .625/2.5/1.875; no runtime triangles, shader or particles.',
                    static_map='These3 source effect meshes are not present in the static/landmark manifests; static fountain structures remain unchanged.')
    (OUT/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(complete=manifest['complete'], frames=len(records), fountains=loops)))


if __name__ == '__main__':
    main()
