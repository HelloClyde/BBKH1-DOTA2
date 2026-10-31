"""Offline Steam attack projectiles, launch/impact FX and animated NPC bodies.

This adds a separate manifest: existing hero, terrain, water and skill bakes are
never rewritten. Source2 particle operators are reconstructed for the small
screen; all sampled particle images and the Dire tower rock mesh are original
installed Steam assets. No particle simulation or mesh rendering runs on H1.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, math, re, subprocess
from PIL import Image, ImageDraw
from gltf_assets import Gltf, np, ROOT
from source2_vpk import VpkArchive
from bake_effects import Canvas, sample, linear, STEAM, ZOOM, PITCH
from bake_skills import SkillCanvas
from bake_sprites import Model
from bake_sprite_raster import Raster

OUT = ROOT/'private/baked/projectiles'
SOURCE = ROOT/'private/source2/exports/projectile-textures'
TEXTURES = {
    'glow': 'materials/particle/softglow.vtex_c',
    'fire': 'materials/particle/fire_particle_2/fire_particle_2.vtex_c',
    'red': 'materials/particle/particle_flares/aircraft_red.vtex_c',
    'blue': 'materials/particle/particle_flares/aircraft_blue2.vtex_c',
    'crystal': 'materials/particle/crystal/crystal.vtex_c',
    'good': 'materials/particle/particle_flares/particle_flare_001.vtex_c',
    'bad': 'materials/particle/particle_flares/particle_flare_004.vtex_c',
    'flare': 'materials/particle/particle_flares/particle_flare_007b.vtex_c',
    'electric': 'materials/particle/ribbon_electric2.vtex_c',
    'yellow': 'materials/particle/yellowflare2.vtex_c',
    'bluebeam': 'materials/particle/beam_hotblue2.vtex_c',
    'lava': 'materials/particle/lava/lava_trails/lava_trail_liquid_center.vtex_c',
    'sparks': 'materials/particle/sparks/sparks.vtex_c',
    'fleks': 'materials/particle/impact/fleks3.vtex_c',
    'ring': 'materials/particle/particle_ring_wavy6.vtex_c',
}
STYLES = [
    ('lina_attack', 'particles/units/heroes/hero_lina/lina_base_attack.vpcf_c', (1., .30, .045), 'fire', 14),
    ('cm_attack', 'particles/units/heroes/hero_crystalmaiden/maiden_base_attack.vpcf_c', (.30, .69, 1.), 'crystal', 13),
    ('radiant_ranged_attack', 'particles/base_attacks/ranged_goodguy.vpcf_c', (.41, 1., .18), 'good', 11),
    ('dire_ranged_attack', 'particles/base_attacks/ranged_badguy.vpcf_c', (.76, .30, 1.), 'bad', 12),
    ('satyr_trickster_attack', 'particles/neutral_fx/satyr_trickster_projectile.vpcf_c', (.92, .65, 1.), 'yellow', 12),
    ('radiant_tower_attack', 'particles/base_attacks/ranged_tower_good.vpcf_c', (.27, .65, 1.), 'blue', 23),
    ('dire_tower_attack', 'particles/base_attacks/ranged_tower_bad.vpcf_c', (1., .35, .065), 'lava', 24),
]
UNITS = [
    ('radiant_ranged', 40, ['idle', 'run', 'attack', 'death'], 85, 129),
    ('dire_ranged', 41, ['idle', 'run', 'attack', 'death'], 85, 129),
    ('satyr_trickster', 42, ['satyr_b_idle', 'satyr_b_run', 'satyr_b_attack', 'satyr_b_death'], 105, 129),
    ('centaur_outrunner', 43, ['idle', 'runN', 'attack', 'death'], 120, 129),
    # Only the missing attack/death clips are added for existing melee bodies.
    ('radiant_creep', 8, ['idle', 'run', 'attack', 'death'], 85, 129),
    ('dire_creep', 9, ['idle', 'run', 'attack', 'die'], 85, 129),
]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def relative(path):
    return Path(path).resolve().relative_to(ROOT).as_posix()


def graph_sources(vpk):
    result = []
    for _, root, *_ in STYLES:
        seen = set(); pending = [root]; textures = set(); models = set(); records = []
        while pending:
            entry = pending.pop()
            if entry in seen:
                continue
            if entry not in vpk.entries:
                raise ValueError('Missing original particle graph '+entry)
            seen.add(entry); raw = vpk.read(entry); records.append(vpk.identity(entry))
            for match in re.findall(rb'(?:materials|particles|models)/[a-zA-Z0-9_./-]+', raw):
                ref = match.decode(); ref = ref if ref.endswith('_c') else ref+'_c'
                if ref not in vpk.entries:
                    continue
                if ref.endswith('.vtex_c'):
                    textures.add(ref)
                elif ref.endswith('.vmdl_c'):
                    models.add(ref)
                elif ref.endswith(('.vpcf_c', '.vmat_c')):
                    pending.append(ref)
        result.append({'root': root, 'compiled_graphs': records,
                       'referenced_textures': sorted(textures), 'referenced_models': sorted(models)})
    return result


def export_textures(archive, tool):
    vpk = VpkArchive(archive); SOURCE.mkdir(parents=True, exist_ok=True)
    graphs = graph_sources(vpk)
    def export(item):
        key, entry = item; folder = SOURCE/key; folder.mkdir(exist_ok=True)
        raw = folder/(key+'.vtex_c'); original = vpk.read(entry)
        if not raw.exists():
            raw.write_bytes(original)
        elif raw.read_bytes() != original:
            raise ValueError('Installed original particle texture changed: '+entry)
        target = folder/(key+'.png'); log = folder/'export.log'
        if not log.exists():
            p = subprocess.run([str(tool), '-d', '-i', str(raw), '-o', str(target)], capture_output=True, text=True)
            log.write_text(p.stdout+p.stderr, encoding='utf8')
            if p.returncode:
                raise RuntimeError('Texture decode failed: '+entry)
        paths = [target] if target.exists() and target.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n' else sorted(folder.glob(key+'_seq*.png'))
        if not paths:
            raise ValueError('No original decoded texture: '+entry)
        sequence = []
        for path in paths[:16]:
            rgba = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32)/255
            rgb = linear(rgba[:, :, :3]); peak = rgb.max(2)
            alpha = rgba[:, :, 3]*peak
            rgb /= np.maximum(peak[:, :, None], 1e-8)
            sequence.append(np.dstack((rgb, alpha)).astype(np.float32))
        return key, sequence, {'name': key, 'source': vpk.identity(entry), 'raw': relative(raw),
            'raw_sha256': sha(raw), 'tool_sha256': sha(tool),
            'decoded': [{'path': relative(p), 'sha256': sha(p), 'size': list(Image.open(p).size)} for p in paths]}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        items = list(pool.map(export, TEXTURES.items()))
    return {k: frames for k, frames, _ in items}, [r for _, _, r in items], graphs


class AttackCanvas(SkillCanvas):
    def __init__(self, ss):
        # Small local window, compared with the wide area used by spells.
        self.ss = ss; self.width = 96; self.height = 96; self.ax = 48; self.ay = 48
        y, x = np.mgrid[:self.height*ss, :self.width*ss]
        self.px = (x+.5)/ss-self.ax; self.py = (y+.5)/ss-self.ay
        self.x = self.px/ZOOM; self.y = self.py/PITCH
        self.rgb = np.zeros((*x.shape, 3), dtype=np.float32)
        self.alpha = np.zeros(x.shape, dtype=np.float32)
        self.depth = np.full(x.shape, np.inf, dtype=np.float32)


class NpcModel(Model):
    def __init__(self, slug, model_id, clips, height, runtime_scale):
        self.slug = slug; self.model_id = model_id; self.clips = clips
        self.height = height; self.runtime_scale = runtime_scale; self.textures = {}
        self.paths = [ROOT/'private/source2/exports/projectile-units'/slug/(slug+'.gltf')]
        self.docs = [Gltf(self.paths[0])]; self.body = self.docs[0]
        self.names = {a.get('name'): i for i, a in enumerate(self.body.doc.get('animations', []))}
        for clip in clips:
            if clip not in self.names:
                raise ValueError(f'{slug}: original clip missing {clip}')
        reference = self.pose(clips[0], 0, raw=True)
        points = np.concatenate([p['points'] for p in reference]); lo = points.min(0); hi = points.max(0)
        self.floor = lo[1]; self.scale = height/max(hi[1]-lo[1], 1e-9)*runtime_scale/256
        joint = self.body.doc['skins'][0]['joints'][0]
        self.root_name = self.body.doc['nodes'][joint]['name']
        self.root0 = self.body.globals(self.names[clips[0]], 0)[joint][:3, 3]
        self.receipts = [{'gltf': relative(self.paths[0]), 'sha256': sha(self.paths[0]),
                          'buffers': self.body.buffer_sha256}]
        self.original_triangles = sum(len(p['faces']) for p in reference)


ROCK = None


def rock(canvas, direction, phase):
    global ROCK
    if ROCK is None:
        path = ROOT/'private/source2/exports/projectile-mesh/tower_dire_projectile/tower_dire_projectile.gltf'
        g = Gltf(path); p = list(g.primitives())[0]
        xyz = p['points']; center = (xyz.min(0)+xyz.max(0))/2
        points = (xyz-center)*15/max(np.ptp(xyz, axis=0))
        points = np.column_stack((-points[:, 0], points[:, 2], points[:, 1]))
        node = g.doc['nodes'][p['node_id']]; at = g.doc['meshes'][node['mesh']]['primitives'][p['primitive_id']]['attributes']
        normals = g.accessor(at['NORMAL']).astype(float)
        normals = np.column_stack((-normals[:, 0], normals[:, 2], normals[:, 1]))
        material = g.material(p['material']); texture = np.array(Image.open(material['image']).convert('RGBA'))
        ROCK = (g, p, points, normals, texture, material)
    g, p, points, normal, texture, material = ROCK
    # Original graph contains five independent rock variants at the same origin.
    # Render its first intact variant, rather than overlapping all five meshes.
    angle = direction*math.tau/16; co = math.cos(angle); si = math.sin(angle)
    q = points.copy(); n = normal.copy()
    q[:, 0] = points[:, 0]*co-points[:, 1]*si
    q[:, 1] = points[:, 0]*si+points[:, 1]*co
    n[:, 0] = normal[:, 0]*co-normal[:, 1]*si
    n[:, 1] = normal[:, 0]*si+normal[:, 1]*co
    # The graphite rock tumbles vertically while retaining its original shape.
    q[:, 2] += math.sin(phase*math.tau)*.8
    projected = np.column_stack((canvas.ax+q[:, 0]*ZOOM, canvas.ay+(q[:, 1]-q[:, 2])*PITCH, -q[:, 1]-q[:, 2]))
    raster = Raster(canvas.width, canvas.height, canvas.ss)
    raster.draw(projected, p['faces'], p['uv'], n, texture, cutoff=.01, factor=material['factor'])
    colour = raster.colour; alpha = colour[:, :, 3]
    # Raster's RGB is already premultiplied in linear light.
    canvas.rgb += colour[:, :, :3]; canvas.alpha = np.maximum(canvas.alpha, alpha)
    canvas.depth = np.minimum(canvas.depth, np.where(alpha > .005, raster.depth, np.inf))


def fx_frame(style, clip, number, count, direction, textures, ss):
    p = number/count if clip == 0 else number/max(count-1, 1)
    canvas = AttackCanvas(ss); glow = textures['glow'][0]
    tint = STYLES[style][2] if style < 7 else (1., .79, .38)
    primary = STYLES[style][3] if style < 7 else 'fleks'
    seq = textures[primary]; tex = seq[number % len(seq)]
    angle = direction*math.tau/16; dx = -math.sin(angle); dy = math.cos(angle)
    if clip == 0:
        size = [4.5, 4., 3.7, 4., 4., 6.3, 6.8][style]
        length = [28., 20., 17., 20., 22., 36., 34.][style]
        # Sample a short moving texture trail behind the projectile's head.
        trail = textures['electric' if style == 4 else 'bluebeam' if style == 5 else 'lava' if style == 6 else primary][number % len(textures['electric' if style == 4 else 'bluebeam' if style == 5 else 'lava' if style == 6 else primary])]
        for j in range(6):
            distance = -(j+.3)*length/6
            fade = (1-j/6)*(.63+.13*math.cos(p*math.tau+j))
            canvas.glow(trail, dx*distance, dy*distance, 0, size*(1-j*.09), tint, fade)
        canvas.glow(glow, 0, 0, 0, size*1.45, tint, .72+.15*math.sin(p*math.tau))
        if style == 1:
            # Crystalline lance with two icy facets, not a recoloured fireball.
            screen_angle = math.atan2(dy*PITCH, dx*ZOOM)
            canvas.sprite(tex, 0, 0, 0, 6.2, 2.6, tint, 1.6, screen_angle)
            canvas.sprite(tex, -dx*8, -dy*8, 0, 3.2, 1.5, (.75, .93, 1.), 1.0, screen_angle+.45)
        elif style == 6:
            rock(canvas, direction, p)
        else:
            canvas.sprite(tex, 0, 0, 0, size, size*(1.3 if style == 0 else 1.), tint, 1.35, p*.7)
        for j in range(3):
            a = p*math.tau+j*math.tau/3; distance = -length*(j+1)/4
            canvas.glow(textures['sparks'][0], dx*distance+math.cos(a)*2.3, dy*distance+math.sin(a)*2.3,
                        1.5*math.sin(a), 1.5, tint, .70)
    elif clip == 1:
        size = (7 if style < 5 else 11)*(1-.65*p)
        canvas.glow(glow, 0, 0, 0, size, tint, (1-p)*.80+.08)
        canvas.sprite(tex, 0, 0, 0, size*.85, size*.85, tint, (1-p)*1.25+.04, p*2.1)
        for j in range(4):
            a = j*math.tau/4+p; r = 2+p*5
            canvas.glow(textures['sparks'][0], math.cos(a)*r, math.sin(a)*r, 0, 1.8, tint, (1-p)*.6+.02)
    else:
        radius = (13 if style < 5 else 22) if style < 7 else 12
        fade = (1-p)**1.25
        canvas.glow(glow, 0, 0, 0, 3+(1-p)*radius*.55, tint, .95*fade+.025)
        if style != 7:
            canvas.sprite(tex, 0, 0, 0, 3+radius*.42*p, 3+radius*.42*p, tint, fade*1.2+.02, p*2)
        for j in range(7 if style < 5 else 10):
            a = j*2.399963+p*.8; r = 1+radius*p*(.55+.4*((j*7)%11)/11)
            x = math.cos(a)*r; y = math.sin(a)*r
            particle = textures['crystal' if style == 1 else 'fleks' if style == 7 else 'sparks'][j % len(textures['crystal' if style == 1 else 'fleks' if style == 7 else 'sparks'])]
            canvas.sprite(particle, x, y, math.sin(j)*p*7, max(1, 2.8*(1-p)), max(1, 1.8*(1-p)), tint, fade*.9+.025, a)
        if style in (2, 3, 4, 5):
            canvas.ring(textures['ring'][0], 1+radius*p, 1.8, tint, fade*.35, p)
    return canvas.finish()


def write_frame(folder, model, clip, direction, number, count, ticks, loop, rgba, depth, ax, ay, **extra):
    path = folder/f'{clip}-{direction:02d}-{number:02d}.npz'
    np.savez_compressed(path, rgba=rgba, depth=depth)
    return {'model': model, 'clip': clip, 'direction': direction, 'frame': number,
        'frames': count, 'duration_ticks': ticks, 'loop': loop,
        'anchor_x': int(ax), 'anchor_y': int(ay), 'width': rgba.shape[1], 'height': rgba.shape[0],
        'npz': relative(path), 'sha256': sha(path), **extra}


def preview(folder, frames, name, contact, contact_index=None):
    pictures = []
    for rgba, ax, ay in frames:
        image = Image.new('RGB', (128, 112), (25, 39, 35)); src = Image.fromarray(rgba)
        image.paste(src, (64-ax, 72-ay), src); pictures.append(image)
    pictures[0].save(folder/'preview.gif', save_all=True, append_images=pictures[1:], duration=100, loop=0, optimize=False)
    pictures[len(pictures)//2].save(folder/'preview.png')
    contact.append((name, pictures[len(pictures)//2 if contact_index is None else contact_index]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--archive', type=Path, default=STEAM/'pak01_dir.vpk')
    parser.add_argument('--tool', type=Path, default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--ss', type=int, default=2)
    parser.add_argument('--only', choices=['fx', 'units', 'melee', 'all'], default='all')
    args = parser.parse_args(); args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=True)
    if args.ss not in (1, 2, 3):
        parser.error('Supersampling must be 1..3')
    previous = json.loads((args.output/'manifest.json').read_text()) if (args.output/'manifest.json').exists() else {}
    def selected(model):
        return args.only == 'all' or args.only == 'fx' and 32 <= model < 40 or args.only == 'units' and model >= 40 or args.only == 'melee' and model < 10
    records = [r for r in previous.get('frames', []) if not selected(r['model'])]
    models = [r for r in previous.get('models', []) if not selected(r['id'])]
    textures_receipts = previous.get('source_textures', []); graphs = previous.get('source_particle_graphs', [])
    contact = []
    if args.only in ('fx', 'all'):
        textures, textures_receipts, graphs = export_textures(args.archive, args.tool)
        for style in range(8):
            model = 32+style; name = STYLES[style][0] if style < 7 else 'neutral_melee_impact'
            folder = args.output/name; folder.mkdir(exist_ok=True); previews = []
            radius = STYLES[style][4] if style < 7 else 12
            clips = (0, 1, 2) if style < 7 else (2,)
            models.append({'id': model, 'name': name, 'reference_radius_world': radius,
                'clips': [{'id': c, 'role': ['flight', 'muzzle', 'impact'][c], 'duration_ticks': 8 if c == 0 else 6,
                           'frames': 4 if c == 0 else 6, 'directions': 16 if c == 0 else 1, 'loop': c == 0} for c in clips],
                'original_particle_root': STYLES[style][1] if style < 7 else None,
                'anchor': 'runtime elevated launch/head/hit position, sprites centred at localXYZ0'})
            for clip in clips:
                count = 4 if clip == 0 else 6; ticks = 8 if clip == 0 else 6
                for direction in range(16 if clip == 0 else 1):
                    for number in range(count):
                        rgba, depth, ax, ay = fx_frame(style, clip, number, count, direction, textures, args.ss)
                        records.append(write_frame(folder, model, clip, direction, number, count, ticks, clip == 0,
                            rgba, depth, ax, ay, source_clip=name+':'+['flight', 'muzzle', 'impact'][clip],
                            source_seconds=number/(count if clip == 0 else max(1, count-1))*ticks/20, height_world=radius,
                            flags=2 if style == 6 and clip == 0 else 3))
                        if direction == 0:
                            previews.append((rgba, ax, ay))
            preview(folder, previews, name, contact, 2)
            print(json.dumps({'projectile': name, 'model': model, 'frames': len([r for r in records if r['model'] == model])}), flush=True)
    if args.only != 'fx':
        for spec in UNITS:
            if not selected(spec[1]):
                continue
            npc = NpcModel(*spec); folder = args.output/npc.slug; folder.mkdir(exist_ok=True); previews = []
            sources = []
            for clip_id, source_clip in zip((0, 1, 2, 4), npc.clips):
                if npc.model_id < 10 and clip_id < 2:
                    continue
                count = 4 if clip_id == 4 else 6; loop = clip_id in (0, 1)
                duration = npc.body.duration(npc.names[source_clip]); ticks = max(count, round(duration*20))
                sources.append({'clip': clip_id, 'source_clip': source_clip, 'source_duration': duration,
                                'duration_ticks': ticks, 'frames': count, 'loop': loop})
                for number in range(count):
                    seconds = duration*number/(count if loop else max(1, count-1))
                    pose = npc.pose(source_clip, seconds)
                    for direction in range(16):
                        rgba, depth, ax, ay = npc.frame(pose, direction, args.ss)
                        records.append(write_frame(folder, npc.model_id, clip_id, direction, number, count, ticks, loop,
                            rgba, depth, ax, ay, source_clip=source_clip, source_seconds=seconds,
                            height_world=round(npc.height*npc.runtime_scale/256), flags=0))
                        if direction == 0:
                            previews.append((rgba, ax, ay))
                print(json.dumps({'unit': npc.slug, 'clip': source_clip, 'frames': count, 'directions': 16}), flush=True)
            models.append({'id': npc.model_id, 'name': npc.slug, 'source_triangles': npc.original_triangles,
                'source': npc.receipts, 'canonical_scale': npc.scale, 'source_floor': float(npc.floor), 'clips': sources})
            preview(folder, previews, npc.slug, contact, 2 if npc.model_id < 10 else 14)
    tool_names = ['bake_projectiles.py', 'bake_effects.py', 'bake_skills.py', 'bake_sprites.py',
                  'gltf_assets.py', 'bake_sprite_raster.py', 'bake_sprite_raster.c', 'source2_export.py', 'source2_vpk.py']
    source_hashes = {'tools/'+name: sha(ROOT/'tools'/name) for name in tool_names}
    source_reports = ['projectile_radiant_ranged', 'projectile_dire_ranged', 'projectile_satyr',
                      'projectile_centaur', 'projectile_dire_tower_bolt',
                      'projectile_radiant_melee', 'projectile_dire_melee']
    export_receipts = []
    for name in source_reports:
        path = ROOT/'private/source2/reports'/(name+'-provenance.json')
        export_receipts.append({'path': relative(path), 'sha256': sha(path)})
    rock_gltf = ROOT/'private/source2/exports/projectile-mesh/tower_dire_projectile/tower_dire_projectile.gltf'
    vpk = VpkArchive(args.archive)
    rules = ['scripts/npc/npc_units.txt', 'scripts/npc/heroes/npc_dota_hero_lina.txt',
             'scripts/npc/heroes/npc_dota_hero_crystal_maiden.txt']
    manifest = {'complete': len(records) == 2266, 'models': sorted(models, key=lambda x: x['id']),
        'frames': sorted(records, key=lambda x: (x['model'], x['clip'], x['direction'], x['frame'])),
        'source_textures': textures_receipts, 'source_particle_graphs': graphs,
        'source_export_receipts': export_receipts,
        'source_attack_rules': [vpk.identity(name) for name in rules],
        'source_models': [{'gltf': relative(rock_gltf), 'sha256': sha(rock_gltf),
                           'buffers': Gltf(rock_gltf).buffer_sha256,
                           'selected_original_primitive': 0, 'triangles': 100}],
        'projection': {'zoom_q8': 180, 'vertical_q8': 181, 'directions': 16, 'direction0': '+worldY',
                       'depth': 'signed relative -(localY+localZ); runtime adds true elevated projectile origin'},
        'source_sha256': source_hashes, 'ss': args.ss,
        'limitations': 'Original source meshes, animations and particle images. Particle operator motion is reconstructed offline; no Source2 renderer, dynamic lighting or audio is executed.'}
    (args.output/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    if records:
        # A partial rebuild still leaves a complete catalogue thumbnail.
        contact = []
        for model in manifest['models']:
            mid = model['id']; clip = 0 if 32 <= mid < 39 else 2
            record = next(r for r in records if (r['model'], r['clip'], r['direction'], r['frame']) == (mid, clip, 0, 2))
            data = np.load(ROOT/record['npz']); src = Image.fromarray(data['rgba'])
            picture = Image.new('RGB', (128, 112), (25, 39, 35))
            picture.paste(src, (64-record['anchor_x'], 72-record['anchor_y']), src)
            contact.append((model['name'], picture))
        sheet = Image.new('RGB', (512, math.ceil(len(contact)/4)*136), (20, 29, 26)); draw = ImageDraw.Draw(sheet)
        for i, (name, picture) in enumerate(contact):
            x = i % 4*128; y = i//4*136; sheet.paste(picture, (x, y+24)); draw.text((x+3, y+4), name, fill='white')
        sheet.save(args.output/'contact.png')
    print(json.dumps({'manifest': relative(args.output/'manifest.json'), 'frames': len(records), 'complete': manifest['complete']}), flush=True)


if __name__ == '__main__':
    main()
