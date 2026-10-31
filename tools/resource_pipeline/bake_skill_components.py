"""Append missing original skill components without touching previous bakes.

The independent Healing Ward uses all original bones and its original opening,
opened, moving and closing clips. Laguna's original particle images form a
complete white lightning core with separately positioned endpoint and scorch
sprites. Original Hunger skull/Dragon Slave models replace missing silhouettes.
Particle operators are reconstructed offline, not executed as Source2 effects.
"""
from pathlib import Path
import argparse, concurrent.futures, json, math, re, subprocess
from PIL import Image, ImageDraw
from gltf_assets import Gltf, np, ROOT
from source2_vpk import VpkArchive
from bake_effects import Canvas, sample, linear, STEAM, ZOOM, PITCH
from bake_skills import SkillCanvas, NAMES, ROOT_GRAPHS
from bake_sprites import Model
from bake_sprite_raster import Raster
from bake_projectiles import sha, relative, write_frame

OUT = ROOT/'private/baked/skill-components'
SOURCE = ROOT/'private/source2/exports/skill-components'
TEXTURES = {
    'beam': 'materials/particle/beam_hotblue.vtex_c',
    'arc': 'materials/particle/electrical_arc2/electrical_arc_2_05.vtex_c',
    'blue': 'materials/particle/particle_flares/aircraft_blue2.vtex_c',
    'flare': 'materials/particle/particle_flares/particle_flare_006.vtex_c',
    'fire': 'materials/particle/fire_particle_8/fire_particle_8_low.vtex_c',
    'burn': 'materials/particle/fire_burning_character/fire_burning_character.vtex_c',
    'spark': 'materials/particle/sparks/sparks.vtex_c',
    'smoke': 'materials/particle/smoke3/smoke3.vtex_c',
    'glow': 'materials/particle/softglow.vtex_c',
    'green': 'materials/particle/particle_flares/aircraft_green.vtex_c',
    'spin': 'materials/particle/juggernaut/juggernaut_blade_fury.vtex_c',
    'scorch': 'materials/particle/lina/lina_scorch_mark_color.vtex_c',
}
WARD_CLIPS = [
    (0, 'jugg_healing_ward_idle_opened', 4, True),
    (1, 'jugg_healing_ward_idle_opened_run', 4, True),
    (3, 'jugg_healing_ward_opening', 5, False),
    (4, 'jugg_healing_ward_closing', 2, False),
]


def textures(archive, tool):
    vpk = VpkArchive(archive); folder = SOURCE/'textures'; folder.mkdir(parents=True, exist_ok=True)
    def export(item):
        name, entry = item; sub = folder/name; sub.mkdir(exist_ok=True)
        raw = sub/(name+'.vtex_c'); original = vpk.read(entry)
        if raw.exists():
            assert raw.read_bytes() == original, entry
        else:
            raw.write_bytes(original)
        png = sub/(name+'.png'); log = sub/'export.log'
        if not log.exists():
            p = subprocess.run([str(tool), '-d', '-i', str(raw), '-o', str(png)], capture_output=True, text=True)
            log.write_text(p.stdout+p.stderr, encoding='utf8')
            if p.returncode:
                raise RuntimeError('Original texture export failed: '+entry)
        paths = [png] if png.exists() and png.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n' else sorted(sub.glob(name+'_seq*.png'))
        assert paths, entry
        images = []
        for path in paths[:16]:
            rgba = np.array(Image.open(path).convert('RGBA'), dtype=np.float32)/255
            rgb = linear(rgba[:, :, :3]); peak = rgb.max(2)
            if name == 'scorch':
                images.append(np.dstack((rgb, rgba[:, :, 3])).astype(np.float32))
            else:
                images.append(np.dstack((rgb/np.maximum(peak[:, :, None], 1e-8), rgba[:, :, 3]*peak)).astype(np.float32))
        return name, images, {'name': name, 'source': vpk.identity(entry), 'raw': relative(raw),
            'raw_sha256': sha(raw), 'tool_sha256': sha(tool),
            'decoded': [{'path': relative(p), 'sha256': sha(p)} for p in paths]}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        result = list(pool.map(export, TEXTURES.items()))
    return {name: images for name, images, _ in result}, [receipt for _, _, receipt in result]


class ComponentModel(Model):
    def __init__(self, slug, height, reference=None):
        self.slug = slug; self.model_id = 44; self.height = height; self.runtime_scale = 256
        self.textures = {}; self.paths = [SOURCE/slug/(slug+'.gltf')]
        self.docs = [Gltf(self.paths[0])]; self.body = self.docs[0]
        self.names = {a['name']: i for i, a in enumerate(self.body.doc.get('animations', []))}
        self.clips = [reference]; self.root_name = None
        pose = self.pose(reference, 0, raw=True); points = np.concatenate([p['points'] for p in pose])
        lo = points.min(0); hi = points.max(0); self.floor = lo[1]
        self.scale = height/max(1e-9, hi[1]-lo[1])
        joint = self.body.doc['skins'][0]['joints'][0] if self.body.doc.get('skins') else None
        if joint is not None:
            self.root_name = self.body.doc['nodes'][joint]['name']
            self.root0 = self.body.globals(self.names.get(reference), 0)[joint][:3, 3]
        self.original_triangles = sum(len(p['faces']) for p in pose)
        self.receipt = {'gltf': relative(self.paths[0]), 'sha256': sha(self.paths[0]),
                        'buffers': self.body.buffer_sha256, 'triangles': self.original_triangles,
                        'scale': self.scale, 'reference_height_world': height, 'reference_clip': reference}


def add_mesh(canvas, model, clip, seconds, direction, x=0, y=0, z=0, fade=1., tint=(1., 1., 1.), unlit=False):
    parts = model.pose(clip, seconds); angle = direction*math.tau/16; co = math.cos(angle); si = math.sin(angle)
    raster = Raster(canvas.width, canvas.height, canvas.ss)
    for p in parts:
        xyz = p['points']; source_normals = p['normals']; q = xyz.copy(); normals = source_normals.copy()
        q[:, 0] = xyz[:, 0]*co-xyz[:, 1]*si+x
        q[:, 1] = xyz[:, 0]*si+xyz[:, 1]*co+y; q[:, 2] += z
        normals[:, 0] = source_normals[:, 0]*co-source_normals[:, 1]*si
        normals[:, 1] = source_normals[:, 0]*si+source_normals[:, 1]*co
        projected = np.column_stack((canvas.ax+q[:, 0]*ZOOM, canvas.ay+(q[:, 1]-q[:, 2])*PITCH, -q[:, 1]-q[:, 2]))
        image, cutoff, factor = model.texture(p['doc'], p['material'])
        factor = np.asarray(factor).copy(); factor[:3] *= tint
        raster.draw(projected, p['faces'], p['uv'], normals, image, cutoff=cutoff, factor=factor, unlit=unlit)
    canvas.rgb += raster.colour[:, :, :3]*fade
    alpha = raster.colour[:, :, 3]*fade
    canvas.alpha = np.maximum(canvas.alpha, alpha)
    canvas.depth = np.minimum(canvas.depth, np.where(alpha > .005, raster.depth, np.inf))


def white_beam(canvas, texture, direction, width, strength, tint, phase, whiten=False):
    angle = direction*math.tau/16; dx = -math.sin(angle); dy = math.cos(angle)
    ax = -100*dx*ZOOM; ay = -100*dy*PITCH; bx = -ax; by = -ay
    vx = bx-ax; vy = by-ay; length = math.hypot(vx, vy)
    along = ((canvas.px-ax)*vx+(canvas.py-ay)*vy)/(length*length)
    across = ((canvas.px-ax)*vy-(canvas.py-ay)*vx)/length
    image = sample(texture, along, .5+across/(2*max(width, .5)))
    # Geometry supplies a continuous core; the actual original beam image
    # modulates it. Its black sequence margins must not break the lightning.
    band = np.exp(-(across/width)**2)*((along >= 0)&(along <= 1))
    image[:, :, 3] = band*(.74+.26*image[:, :, 3])
    if whiten:
        image[:, :, :3] = .91+.09*image[:, :, :3]
    else:
        image[:, :, :3] = .38+.62*image[:, :, :3]
    canvas.add(image, -(-100*dy+200*dy*np.clip(along, 0, 1)), tint, strength)


def laguna(clip, number, direction, tex, ss):
    canvas = SkillCanvas(ss); p = number/3
    if clip == 0:
        power = [1., 1., .58, .24][number]
        white_beam(canvas, tex['beam'][0], direction, 7.2, .75*power, (.13, .48, 1.), p)
        white_beam(canvas, tex['arc'][number % len(tex['arc'])], direction, 3.6, .85*power, (1., .48, .16), p)
        white_beam(canvas, tex['beam'][0], direction, 1.95, 3.4*power, (1., 1., 1.), p, True)
    elif clip in (1, 2):
        # Both endpoint sprites are centred at localXYZ0. The runtime positions
        # them at a hand/chest, without an additional baked +40 world lift.
        power = [1., .85, .45, .14][number]
        radius = 11 if clip == 1 else 20
        canvas.glow(tex['blue'][0], 0, 0, 0, radius*(1-.45*p), (.34, .68, 1.), power*1.65)
        canvas.glow(tex['flare'][0], 0, 0, 0, radius*.46, (1., 1., 1.), power*2.6)
        for j in range(5 if clip == 1 else 10):
            a = j*2.399963+p*.45; r = (4+25*p)*(1 if clip == 2 else .45)
            canvas.ray(tex['arc'][0], 0, 0, 0, math.cos(a)*r, math.sin(a)*r, 0,
                       1.6, (.76, .91, 1.), power*.8)
            canvas.glow(tex['spark'][0], math.cos(a)*r, math.sin(a)*r, 0, 1.8, (1., .63, .20), power)
        if clip == 2:
            canvas.sprite(tex['fire'][number % len(tex['fire'])], 0, 0, 0, 8+5*p, 8+5*p,
                          (1., .45, .12), power*.55, p)
    else:
        canvas.ground(tex['scorch'][0], 27, .2, (.31, .25, .19), [.50, .46, .30, .10][number])
        canvas.ground(tex['glow'][0], 22, 0, (1., .25, .03), [.45, .22, .08, .015][number])
    return canvas.finish()


def hunger(clip, number, tex, skull, ss):
    canvas = SkillCanvas(ss); p = number/8; strength = .68+.32*math.sin(p*math.tau)**2
    # The real dragon skull replaces the old abstract red diamond marker.
    add_mesh(canvas, skull, None, 0, 0, y=0, z=73+math.sin(p*math.tau)*3,
             tint=(1.35, 1.10, .95), fade=min(1., .90+number*.1) if clip == 0 else 1., unlit=True)
    canvas.glow(tex['glow'][0], 0, 0, 88, 8, (1., .04, .015), strength*.18)
    for j in range(6):
        a = j*math.tau/6+p*math.tau
        canvas.sprite(tex['burn'][number % len(tex['burn'])], math.cos(a)*12, math.sin(a)*10,
                      75+24*((p+j*.17)%1), 4.5, 7.5, (1., .08, .015), .25*strength, a*.2)
    return canvas.finish()


def dragon(clip, number, direction, tex, model, ss):
    canvas = SkillCanvas(ss); p = number/3; angle = direction*math.tau/16
    dx = -math.sin(angle); dy = math.cos(angle)
    if clip == 0:
        distance = -75+150*p
        duration = model.body.duration(model.names['dragonslave_attack'])
        add_mesh(canvas, model, 'dragonslave_attack', duration*p, direction,
                 x=dx*distance, y=dy*distance, z=12, tint=(1.8, 1.25, .8), unlit=True)
        for j in range(5):
            across = (j-2)*17
            canvas.sprite(tex['fire'][(number+j) % len(tex['fire'])], dx*distance+dy*across,
                          dy*distance-dx*across, 12+4*math.sin(j), 11, 17, (1., .22, .035), .55)
        canvas.glow(tex['glow'][0], dx*distance, dy*distance, 15, 14, (1., .38, .05), .45)
    else:
        power = [1., .8, .4, .1][number]
        for j in range(8):
            a = j*math.tau/8; r = 6+45*p
            canvas.sprite(tex['fire'][(number+j) % len(tex['fire'])], math.cos(a)*r, math.sin(a)*r,
                          12+24*p, 8, 12, (1., .24, .025), power, a)
        canvas.glow(tex['glow'][0], 0, 0, 15, 18*(1-.65*p), (1., .6, .15), power)
    return canvas.finish()


def ward_aura(clip, number, tex, ss):
    canvas = SkillCanvas(ss); p = number/8; green = (.12, 1., .30)
    canvas.ring(tex['spin'][number % len(tex['spin'])], 58+18*p, 5, green, .70, p, 5)
    canvas.ring(tex['glow'][0], 51+4*math.sin(p*math.tau), 3, green, .45, p, 3)
    canvas.sprite(tex['burn'][number % len(tex['burn'])], 0, 0, 59, 4.8, 10., green, .75)
    canvas.glow(tex['green'][0], 0, 0, 58, 4.3, green, .45)
    # The independent ward supplies solid geometry. Only sparse peripheral
    # motes remain in the aura, preserving its visible silhouette.
    for j in range(6):
        a = j*math.tau/6+p*.5
        canvas.glow(tex['green'][0], math.cos(a)*44, math.sin(a)*44,
                    8+40*((p+j*.17)%1), 2.1, green, .6)
    return canvas.finish()


def particle_audit(vpk):
    result = []
    notes = [
        'Red call/ring reconstructed; Source2 operator and hero attachment simulation is simplified.',
        'FIXED: original dragon-skull model replaces the abstract red marker; flame operators remain reconstructed.',
        'Spiral/blade arcs reconstructed; original Source2 particle and lighting operators are simplified.',
        'Descending blade and impact reconstructed; execute success/failure/sound differences depend on runtime rules.',
        'FIXED: original animated Dragon Slave mesh replaces the texture-only flame wave; smoke/wing emissions remain reconstructed.',
        'Scorch/rings/fire reconstructed; original sphere mesh, collapsing rings, ashes and multiple ray layers are simplified.',
        'Hand flames/motes reconstructed; exact hand-bone attachments and Source2 operators remain simplified.',
        'FIXED: complete white core, separate start/end sparks and ground scorch; smoke/swoop operators remain reconstructed.',
        'Blade spiral reconstructed; exact per-weapon attachment and Source2 layers remain simplified.',
        'FIXED: independent original animated ward body44; model25 contains only aura. Lifetime/health/movement are entity rules.',
        'Critical glint/cross trails reconstructed; exact weapon-bone blur and original attachment positions are simplified.',
        'Slash/impact streaks reconstructed; original poof/blood child particles are not fully reproduced.',
        'Frost flash/ring/shards reconstructed; original multi-flash and shard-trail operator layers are simplified.',
        'Existing original ice cage is retained; original trail/target attachments and refractive ice shading remain simplified.',
        'Blue orbit/motes reconstructed; original hand beam and exact hand attachment are simplified.',
        'Falling ice bursts reconstructed; Source2 randomized field operators, lighting and sound remain simplified.',
    ]
    for index, root in enumerate(ROOT_GRAPHS):
        hero, name = root.split('/'); entry = f'particles/units/heroes/hero_{hero}/{name}.vpcf_c'
        seen = set(); pending = [entry]; children = set(); models = set(); images = set(); identities = []
        while pending:
            item = pending.pop()
            if item in seen or item not in vpk.entries:
                continue
            seen.add(item); identities.append(vpk.identity(item))
            for match in re.findall(rb'(?:materials|particles|models)/[a-zA-Z0-9_./-]+', vpk.read(item)):
                ref = match.decode(); ref = ref if ref.endswith('_c') else ref+'_c'
                if ref not in vpk.entries:
                    continue
                if ref.endswith('.vmdl_c'):
                    models.add(ref)
                elif ref.endswith('.vtex_c'):
                    images.add(ref)
                elif ref.endswith(('.vpcf_c', '.vmat_c')):
                    pending.append(ref)
                    if ref.endswith('.vpcf_c'):
                        children.add(ref)
        result.append({'model': 16+index, 'name': NAMES[index][0], 'root': entry,
            'source_graphs': identities, 'referenced_models': sorted(models),
            'referenced_textures': sorted(images), 'child_particles': sorted(children),
            'audit': notes[index], 'complete_source2_effect': False})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--archive', type=Path, default=STEAM/'pak01_dir.vpk')
    parser.add_argument('--tool', type=Path, default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--ss', type=int, default=2)
    args = parser.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    tex, texture_sources = textures(args.archive, args.tool)
    ward = ComponentModel('healing_ward', 56, 'jugg_healing_ward_idle_opened')
    skull = ComponentModel('hunger_skull', 24)
    dragon_model = ComponentModel('dragon_slave', 50, 'dragonslave_attack')
    records = []; catalog = []; pictures = []
    for mid, name, clips, directions, reference, flags in [
        (17, 'axe_hunger_skull', [(0, 8, 16), (1, 8, 16)], False, 45, 3),
        (20, 'lina_dragon_model', [(0, 4, 18), (2, 4, 10)], True, 100, 3),
        (23, 'lina_laguna_white', [(0, 4, 12), (1, 4, 6), (2, 4, 8), (3, 4, 16)], True, 25, 3),
        (25, 'ward_aura', [(0, 8, 20), (1, 8, 16)], False, 90, 3),
    ]:
        folder = out/name; folder.mkdir(exist_ok=True); previews = []
        for clip, count, ticks in clips:
            for direction in range(16 if directions and clip == 0 else 1):
                for number in range(count):
                    if mid == 17:
                        rgba, depth, ax, ay = hunger(clip, number, tex, skull, args.ss)
                    elif mid == 20:
                        rgba, depth, ax, ay = dragon(clip, number, direction, tex, dragon_model, args.ss)
                    elif mid == 23:
                        rgba, depth, ax, ay = laguna(clip, number, direction, tex, args.ss)
                    else:
                        rgba, depth, ax, ay = ward_aura(clip, number, tex, args.ss)
                    records.append(write_frame(folder, mid, clip, direction, number, count, ticks, clip == 1 and mid in (17, 25),
                        rgba, depth, ax, ay, source_clip=name+':'+str(clip), source_seconds=number/count*ticks/20,
                        height_world=reference, flags=2 if mid == 23 and clip == 3 else flags))
                    if direction == 0:
                        previews.append((rgba, ax, ay))
        catalog.append({'id': mid, 'name': name, 'replacement': True,
            'reference_radius_world': reference, 'reference_length_world': 200 if directions else 0,
            'clips': [{'id': c, 'frames': n, 'duration_ticks': t, 'directions': 16 if directions and c == 0 else 1} for c, n, t in clips]})
        save_preview(folder, previews); pictures.append((name, previews[1]))
        print(json.dumps({'model': mid, 'name': name, 'frames': sum(r['model'] == mid for r in records)}), flush=True)
    folder = out/'healing_ward_body'; folder.mkdir(exist_ok=True); previews = []; ward_clips = []
    for clip, source, count, loop in WARD_CLIPS:
        duration = ward.body.duration(ward.names[source]); ticks = max(count, round(duration*20))
        ward_clips.append({'id': clip, 'source_clip': source, 'frames': count, 'duration_ticks': ticks, 'loop': loop})
        for number in range(count):
            seconds = duration*number/(count if loop else max(1, count-1)); pose = ward.pose(source, seconds)
            for direction in range(16):
                rgba, depth, ax, ay = ward.frame(pose, direction, args.ss)
                records.append(write_frame(folder, 44, clip, direction, number, count, ticks, loop,
                    rgba, depth, ax, ay, source_clip=source, source_seconds=seconds, height_world=56, flags=0))
                if direction == 0:
                    previews.append((rgba, ax, ay))
        print(json.dumps({'model': 44, 'source_clip': source, 'frames': count, 'directions': 16}), flush=True)
    catalog.append({'id': 44, 'name': 'healing_ward_body', 'replacement': False, 'height_world': 56, 'clips': ward_clips})
    save_preview(folder, previews); pictures.append(('ward_body', previews[0]))
    vpk = VpkArchive(args.archive); audit = particle_audit(vpk)
    source_reports = []
    for name in ('healing_ward', 'hunger_skull', 'dragon_slave'):
        report = ROOT/'private/source2/reports'/('skill_component_'+name+'-provenance.json')
        source_reports.append({'path': relative(report), 'sha256': sha(report)})
    files = ['bake_skill_components.py', 'bake_projectiles.py', 'bake_sprites.py', 'bake_skills.py',
             'bake_effects.py', 'gltf_assets.py', 'bake_sprite_raster.py', 'bake_sprite_raster.c', 'source2_vpk.py']
    axes = []
    for direction in range(16):
        angle = direction*math.tau/16; dx = -math.sin(angle); dy = math.cos(angle)
        start = [-100*dx, -100*dy, 0]; end = [100*dx, 100*dy, 0]
        a = [start[0]*ZOOM, start[1]*PITCH]; b = [end[0]*ZOOM, end[1]*PITCH]
        axes.append({'direction': direction, 'canonical_world_endpoints': [start, end],
                     'projected_endpoints_relative_anchor': [a, b], 'source_axis_length_pixels': math.dist(a, b)})
    old_files = [ROOT/'private/baked'/name/'manifest.json' for name in ('sprites', 'effects', 'skills', 'projectiles', 'water')]
    manifest = {'complete': len(records) == 416, 'replace_models': [17, 20, 23, 25],
        'models': catalog, 'frames': sorted(records, key=lambda r: (r['model'], r['clip'], r['direction'], r['frame'])),
        'beam': {'model': 23, 'clip': 0, 'reference_length_world': 200, 'anchor_world': [0, 0, 0],
                 'canonical_z_world': 0, 'core_width_native_pixels': 4, 'bright_flash_ticks': 6,
                 'afterglow_ticks': 6, 'muzzle_clip': 1, 'impact_clip': 2, 'ground_impact_clip': 3,
                 'endpoint_anchor_world': [0, 0, 0], 'directions': axes},
        'source_textures': texture_sources, 'source_models': [ward.receipt, skull.receipt, dragon_model.receipt],
        'source_export_receipts': source_reports, 'skill_audit': audit,
        'source_rules': [vpk.identity('scripts/npc/npc_units.txt')]+[vpk.identity('scripts/npc/heroes/npc_dota_hero_'+h+'.txt') for h in ('axe', 'lina', 'juggernaut', 'crystal_maiden')],
        'previous_source_manifests_unchanged': {relative(p): sha(p) for p in old_files},
        'source_sha256': {'tools/'+name: sha(ROOT/'tools'/name) for name in files},
        'projection': {'zoom_q8': 180, 'vertical_q8': 181, 'directions': 16, 'depth': 'signed relative -(localY+localZ)'},
        'limitations': 'Original meshes, skeleton clips and particle images; reconstructed particle operators. Other fourteen skill effects remain documented approximations, without a claim of complete Source2 parity.'}
    (out/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    (out/'skill-audit.json').write_text(json.dumps(audit, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    sheet = Image.new('RGB', (800, 224), (23, 34, 30)); draw = ImageDraw.Draw(sheet)
    for i, (name, (rgba, ax, ay)) in enumerate(pictures):
        im = Image.fromarray(rgba); sheet.paste(im, (i*160+80-ax, 150-ay), im); draw.text((i*160+3, 4), name, fill='white')
    sheet.save(out/'contact.png')
    print(json.dumps({'manifest': relative(out/'manifest.json'), 'frames': len(records), 'sha256': sha(out/'manifest.json')}), flush=True)


def save_preview(folder, frames):
    images = []
    for rgba, ax, ay in frames:
        image = Image.new('RGB', (240, 224), (23, 34, 30)); src = Image.fromarray(rgba)
        image.paste(src, (120-ax, 150-ay), src); images.append(image)
    images[0].save(folder/'preview.gif', save_all=True, append_images=images[1:], duration=100, loop=0, optimize=False)
    images[0].save(folder/'preview.png')


if __name__ == '__main__':
    main()
