"""Bake original secret-shop merchants and Tormentor for the fixed H1 camera.

The installed Valve meshes and complete original skeletons are evaluated on the
PC. Only finished RGBA/depth cells are packed for H1. The Tormentor particle
images are original; their emission/ribbons are reconstructed offline rather
than executing Source2 particle operators on the device.
"""
from pathlib import Path
import argparse, concurrent.futures, json, math, re, subprocess
from PIL import Image, ImageDraw
from gltf_assets import Gltf, np, ROOT
from source2_vpk import VpkArchive
from bake_sprites import Model, source_normals
from bake_effects import linear, STEAM
from bake_skills import SkillCanvas
from bake_skill_components import add_mesh, save_preview
from bake_projectiles import sha, relative, write_frame
from bake_world_instances import read_world

SOURCE = ROOT/'private/source2/exports/jungle'
OUT = ROOT/'private/baked/jungle'
GLTF_TO_WORLD = 100/2.54/4
SPECS = [
    dict(id=45, name='shop_radiant', model='models/heroes/shopkeeper/shopkeeper.vmdl_c',
         reference='shpkpr_idle_loop_alt', scale=1., yaw=299.999939,
         world=[777.973877, 1561.011749, 30.8254395], report='jungle-shop-radiant'),
    dict(id=46, name='shop_dire', model='models/heroes/shopkeeper_dire/shopkeeper_dire.vmdl_c',
         reference='idle', scale=1., yaw=230.999893,
         world=[3269.5354005, 2349.91540525, 31.0754395], report='jungle-shop-dire'),
    dict(id=47, name='tormentor', model='models/props_gameplay/divine_sentinel/divine_sentinel_cube.vmdl_c',
         reference='divine_sentinel_cube_idle_loop', scale=.7, yaw=0.,
         world=None, report='jungle-tormentor'),
]
TOR_CLIPS = [
    (0, 'divine_sentinel_cube_idle_loop', 8, True, 'idle_shield'),
    (1, 'divine_sentinel_cube_flinch_a', 8, False, 'shield_hit'),
    (2, 'divine_sentinel_cube_flinch_c', 8, False, 'damage_reflect'),
    (3, 'divine_sentinel_cube_spawn', 8, False, 'spawn'),
    (4, 'divine_sentinel_cube_die', 8, False, 'death'),
]
TEXTURES = {
    'electric': 'materials/particle/electricity/electricity_22_a.vtex_c',
    'tile': 'materials/particle/tile/tile_electric_01.vtex_c',
    'ring': 'materials/particle/particle_heroring_6_crisp.vtex_c',
    'glow': 'materials/particle/particle_glow_08.vtex_c',
    'cross': 'materials/particle/particle_flares/anam_white_cross.vtex_c',
    'beam': 'materials/particle/bendibeam4.vtex_c',
    'sparky': 'materials/particle/mask/sparky_flash.vtex_c',
    'spark': 'materials/particle/spark_03.vtex_c',
    'shock': 'materials/particle/valkyrie/valkyrie_ring.vtex_c',
    'flare': 'materials/particle/flare/flare_01.vtex_c',
}
GRAPHS = ['miniboss_shield', 'miniboss_shield_hit', 'miniboss_damage_impact',
          'miniboss_damage_reflect', 'miniboss_death']


def original_normals(g, p, matrices):
    try:
        return source_normals(g, p, matrices)
    except np.linalg.LinAlgError:
        # The original death clip intentionally collapses some chain bones to
        # exactly zero scale. Their geometry remains collapsed; a pseudoinverse
        # only makes its now-degenerate normal transform finite for the raster.
        node = g.doc['nodes'][p['node_id']]
        primitive = g.doc['meshes'][node['mesh']]['primitives'][p['primitive_id']]
        at = primitive['attributes']; normals = g.accessor(at['NORMAL']).astype(float)
        if 'skin' in node and 'JOINTS_0' in at and 'WEIGHTS_0' in at:
            skin = g.doc['skins'][node['skin']]
            joints = g.accessor(at['JOINTS_0']).astype(np.int32)
            weights = g.accessor(at['WEIGHTS_0']).astype(float)
            weights /= np.maximum(weights.sum(1, keepdims=True), 1e-12)
            binds = g.accessor(skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
            transforms = np.array([np.linalg.pinv((matrices[j]@b)[:3, :3]).T for j, b in zip(skin['joints'], binds)])
            out = np.zeros_like(normals)
            for slot in range(joints.shape[1]):
                out += np.einsum('nij,nj->ni', transforms[joints[:, slot]], normals)*weights[:, slot, None]
        else:
            out = normals@np.linalg.pinv(matrices[p['node_id']][:3, :3])
        out /= np.maximum(np.linalg.norm(out, axis=1)[:, None], 1e-12)
        return out


class JungleModel(Model):
    """Keep original Source unit scale/origin and bake the actual map yaw."""
    def __init__(self, spec):
        self.spec = spec; self.slug = spec['name']; self.model_id = spec['id']; self.textures = {}
        self.paths = [SOURCE/self.slug/(self.slug+'.gltf')]
        self.docs = [Gltf(self.paths[0])]; self.body = self.docs[0]
        self.names = {a['name']: i for i, a in enumerate(self.body.doc.get('animations', []))}
        self.scale = GLTF_TO_WORLD*spec['scale']; self.root_name = None
        original = list(self.body.primitives(self.names[spec['reference']], 0))
        self.original_triangles = sum(len(p['faces']) for p in original)
        pts = np.concatenate([p['points'] for p in original])
        self.height = int(math.ceil((pts[:, 1].max()-pts[:, 1].min())*self.scale))
        self.receipt = dict(id=self.model_id, name=self.slug, gltf=relative(self.paths[0]),
            sha256=sha(self.paths[0]), buffers=self.body.buffer_sha256,
            source_triangles=self.original_triangles, source_vmdl=spec['model'],
            source_model_scale=spec['scale'], gltf_metres_to_world=GLTF_TO_WORLD,
            source_yaw_degrees=spec['yaw'], height_world=self.height,
            source_origin_preserved=True, reference_clip=spec['reference'])
        self.stall_parts = []
        if spec['id'] < 47:
            side = self.slug.removeprefix('shop_'); stall = 'stall_'+side
            path = SOURCE/stall/(stall+'.gltf'); scene_path = ROOT/'private/source2/exports/map-geometry/dota.gltf'
            # The map glTF retains the exact placement of the aggregate mesh.
            # Its base prop has the same local bounds, but includes original
            # higher-detail geometry omitted by the map export's selected LOD.
            scene = json.loads(scene_path.read_text(encoding='utf8'))
            name = 'n0_lr0_agg_prop_secretshop_'+side+'001_0'
            candidates = [n for n in scene['nodes'] if n.get('name') == name]
            assert len(candidates) == 1, name
            matrix = np.asarray(candidates[0]['matrix']).reshape(4, 4).T
            tint = read_world()[name][0]['tint']; g = Gltf(path)
            self.docs.append(g); self.paths.append(path)
            basis = np.array([[0, 0, 1], [-1, 0, 0], [0, 1, 0]], float)
            matrices = g.globals()
            for original in g.primitives():
                p = dict(original, doc=1)
                xyz = p['points']@matrix[:3, :3].T+matrix[:3, 3]
                p['points'] = xyz@basis.T*GLTF_TO_WORLD+[2048, 2048, 0]-np.asarray(spec['world'])
                n = original_normals(g, p, matrices)@np.linalg.inv(matrix[:3, :3])@basis.T
                n /= np.maximum(np.linalg.norm(n, axis=1)[:, None], 1e-12)
                p['normals'] = n; self.stall_parts.append(p)
            self.stall_tint = linear(tint)
            self.receipt['stall'] = dict(gltf=relative(path), sha256=sha(path), buffers=g.buffer_sha256,
                source_triangles=sum(len(p['faces']) for p in self.stall_parts),
                source_worldnode=name, original_map_matrix_gltf=matrix.tolist(),
                original_instance_srgb_tint=tint.tolist(), source_scene=relative(scene_path), source_scene_sha256=sha(scene_path))

    def texture(self, di, mi):
        image, cutoff, factor = super().texture(di, mi)
        factor = np.asarray(factor).copy()
        if di == 1:
            factor[:3] *= self.stall_tint
        return image, cutoff, factor

    def pose(self, clip, seconds, raw=False):
        animation = self.names.get(clip); matrices = self.body.globals(animation, seconds)
        angle = math.radians(-self.spec['yaw']); co = math.cos(angle); si = math.sin(angle)
        result = []
        for original in self.body.primitives(animation, seconds):
            p = dict(original, doc=0)
            if raw:
                result.append(p); continue
            xyz = p['points']; n = original_normals(self.body, p, matrices)
            q = np.column_stack((xyz[:, 2], -xyz[:, 0], xyz[:, 1]))*self.scale
            normals = np.column_stack((n[:, 2], -n[:, 0], n[:, 1]))
            q[:, 0], q[:, 1] = q[:, 0]*co-q[:, 1]*si, q[:, 0]*si+q[:, 1]*co
            normals[:, 0], normals[:, 1] = normals[:, 0]*co-normals[:, 1]*si, normals[:, 0]*si+normals[:, 1]*co
            p['points'] = q; p['normals'] = normals; result.append(p)
        return self.stall_parts+result


class PlatformModel(Model):
    """Original static chasm lining; separate from the rotating/despawning cube."""
    def __init__(self, top):
        self.slug = 'tormentor_platform_'+('top' if top else 'bottom'); self.model_id = 47; self.textures = {}; self.top = top
        self.variant_textures = {}
        self.paths = [SOURCE/'pedestal/pedestal.gltf']; self.docs = [Gltf(self.paths[0])]; self.body = self.docs[0]
        self.names = {}; self.root_name = None; self.reference = None
        scene_path = ROOT/'private/source2/exports/map-geometry/dota.gltf'
        scene = json.loads(scene_path.read_text(encoding='utf8'))
        name = 'divine_sentinel.unnamed_1.1' if top else 'divine_sentinel.unnamed_1'
        nodes = [n for n in scene['nodes'] if n.get('name') == name]; assert len(nodes) == 1
        matrix = np.asarray(nodes[0]['matrix']).reshape(4, 4).T
        basis = np.array([[0, 0, 1], [-1, 0, 0], [0, 1, 0]], float)
        anchor = np.array([128, 464, 0] if top else [3984, 3600, 0])
        self.parts = []; g = self.body; matrices = g.globals()
        for original in g.primitives():
            p = dict(original, doc=0); xyz = p['points']@matrix[:3, :3].T+matrix[:3, 3]
            p['points'] = xyz@basis.T*GLTF_TO_WORLD+[2048, 2048, 0]-anchor
            n = original_normals(g, p, matrices)@np.linalg.inv(matrix[:3, :3])@basis.T
            n /= np.maximum(np.linalg.norm(n, axis=1)[:, None], 1e-12)
            p['normals'] = n; self.parts.append(p)
        xyz = np.concatenate([p['points'] for p in self.parts]); self.height = int(math.ceil(np.ptp(xyz[:, 2])))
        self.receipt = dict(id=47, name=self.slug, gltf=relative(self.paths[0]), sha256=sha(self.paths[0]),
            buffers=g.buffer_sha256, source_triangles=sum(len(p['faces']) for p in self.parts),
            source_vmdl='models/props_gameplay/divine_sentinel/divine_sentinel.vmdl_c',
            source_scene=relative(scene_path), source_scene_sha256=sha(scene_path),
            source_worldnode=name, original_map_matrix_gltf=matrix.tolist(),
            anchor_world=anchor.tolist(), draw_z=0, height_world=self.height,
            world_bounds_min=(xyz.min(0)+anchor).tolist(), world_bounds_max=(xyz.max(0)+anchor).tolist(),
            method='Exact source static map node placement, original full9653 triangles, original materials/normals; source origin Z retained.')
        self.receipt['source_skin'] = 1 if top else 0
        if top:
            receipt = SOURCE/'pedestal_dire/provenance.json'
            self.receipt['skin_material_override'] = dict(path=relative(receipt), sha256=sha(receipt),
                assignments={'divine_sentinel_base_dormant': 'divine_sentinel_base_dire_dormant',
                             'divine_sentinel_base': 'divine_sentinel_dire_base'},
                source_entity='[PR#]sentinel_base_dire', original_entity_skin='1', colour_tint=[1, 1, 1])

    def pose(self, clip, seconds, raw=False):
        return self.parts


    def texture(self, di, mi):
        if not self.top:
            return super().texture(di, mi)
        if mi not in self.variant_textures:
            name = self.body.doc['materials'][mi]['name']; dormant = name == 'divine_sentinel_base_dormant'
            assert dormant or name == 'divine_sentinel_base'
            image = np.asarray(Image.open(SOURCE/'pedestal_dire'/('dormant.png' if dormant else 'base.png')).convert('RGBA')).copy()
            # Exact Source skin1 uses opaque dormant stone and a0.99 alpha-test
            # lit overlay, both with white material tint. Normal/specular maps
            # and UVs are shared with skin0; geometry/normals are unchanged.
            if dormant:
                image[:, :, 3] = 255
            self.variant_textures[mi] = (image, 0 if dormant else .99, np.ones(4))
        return self.variant_textures[mi]


def append_platforms(out, records, models, sources, source_reports, ss):
    report = ROOT/'private/source2/reports/jungle-pedestal-provenance.json'
    if not any(r['path'] == relative(report) for r in source_reports):
        source_reports.append(dict(path=relative(report), sha256=sha(report)))
    report = SOURCE/'pedestal_dire/provenance.json'
    if not any(r['path'] == relative(report) for r in source_reports):
        source_reports.append(dict(path=relative(report), sha256=sha(report)))
    catalog = next(m for m in models if m['id'] == 47)
    for clip, top in [(5, True), (6, False)]:
        model = PlatformModel(top); folder = out/model.slug; folder.mkdir(exist_ok=True)
        rgba, depth, ax, ay = model.frame(model.pose(None, 0), 0, ss)
        records.append(write_frame(folder, 47, clip, 0, 0, 1, 1, True,
            rgba, depth, ax, ay, source_clip='static:'+model.receipt['source_worldnode'],
            source_seconds=0, height_world=model.height, flags=0))
        sources.append(model.receipt)
        catalog['clips'].append(dict(id=clip, source_clip='static:'+model.receipt['source_worldnode'],
            phase='permanent_platform', frames=1, duration_ticks=1, loop=True, directions=1,
            anchor_world=model.receipt['anchor_world'], draw_z=0, height_world=model.height))
        save_preview(folder, [(rgba, ax, ay)])
        print(json.dumps(dict(model=47, clip=clip, source=model.receipt['source_worldnode'],
            frames=1, height_world=model.height, draw_z=0, flags=0)), flush=True)


def export_textures(vpk, tool):
    folder = SOURCE/'textures'; folder.mkdir(parents=True, exist_ok=True)
    def export(item):
        name, entry = item; sub = folder/name; sub.mkdir(exist_ok=True)
        raw = sub/(name+'.vtex_c'); original = vpk.read(entry)
        if raw.exists():
            assert raw.read_bytes() == original, entry
        else:
            raw.write_bytes(original)
        target = sub/(name+'.png'); log = sub/'export.log'
        command = [str(tool), '-d', '-i', str(raw), '-o', str(target)]
        if not log.exists():
            p = subprocess.run(command, capture_output=True, text=True)
            log.write_text(p.stdout+p.stderr, encoding='utf8')
            if p.returncode:
                raise RuntimeError('VRF texture export failed: '+entry)
        paths = [target] if target.exists() and target.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n' else sorted(sub.glob(name+'_seq*.png'))
        assert paths, entry
        images = []
        for path in paths[:16]:
            rgba = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32)/255
            rgb = linear(rgba[:, :, :3]); peak = rgb.max(2)
            images.append(np.dstack((rgb/np.maximum(peak[:, :, None], 1e-8), rgba[:, :, 3]*peak)).astype(np.float32))
        return name, images, dict(name=name, source=vpk.identity(entry), raw=relative(raw),
            raw_sha256=sha(raw), command=command, tool_sha256=sha(tool),
            decoded=[dict(path=relative(p), sha256=sha(p)) for p in paths])
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        exported = list(pool.map(export, TEXTURES.items()))
    return {name: image for name, image, _ in exported}, [r for _, _, r in exported]


def audit_graphs(vpk):
    output = []
    for name in GRAPHS:
        entry = 'particles/neutral_fx/'+name+'.vpcf_c'
        pending = [entry]; seen = set(); images = set(); models = set(); identities = []
        while pending:
            item = pending.pop()
            if item in seen:
                continue
            seen.add(item); identities.append(vpk.identity(item))
            for match in re.findall(rb'(?:materials|particles|models)/[a-zA-Z0-9_./-]+', vpk.read(item)):
                ref = match.decode(); ref = ref if ref.endswith('_c') else ref+'_c'
                if ref not in vpk.entries:
                    continue
                if ref.endswith('.vtex_c'):
                    images.add(ref)
                elif ref.endswith('.vmdl_c'):
                    models.add(ref)
                elif ref.endswith(('.vpcf_c', '.vmat_c')):
                    pending.append(ref)
        output.append(dict(root=entry, source_graphs=identities, referenced_textures=sorted(images),
            referenced_models=sorted(models), complete_source2_effect=False,
            method='Original particle images; deterministic offline shield/electrical/impact/death reconstruction.'))
    return output


def shield(canvas, tex, phase, strength=.18):
    cyan = (.28, .82, 1.)
    canvas.ring(tex['ring'][0], 43, 1.5, cyan, strength*.6, phase, 4)
    # Four revolving conduit arcs reproduce the visible electric shield, while
    # leaving the real textured cube/chain silhouette visible at native 480x272.
    for j in range(4):
        a = j*math.tau/4+phase*math.tau; b = a+.72
        r = 35; z0 = 39+14*math.sin(a*2); z1 = 39+14*math.sin(b*2)
        canvas.ray(tex['electric'][int(phase*16) % len(tex['electric'])],
            math.cos(a)*r, math.sin(a)*r, z0,
            math.cos(b)*r, math.sin(b)*r, z1, 1.15, cyan, strength)
    canvas.glow(tex['glow'][0], 0, 0, 40, 25, cyan, strength*.07)


def tormentor_frame(model, clip, source, number, count, seconds, tex, ss):
    canvas = SkillCanvas(ss); p = number/(count if clip == 0 else max(1, count-1))
    if clip == 4 and number == count-1:
        # The original skeleton collapses to an absent entity at the end.
        return np.zeros((1, 1, 4), dtype=np.uint8), np.zeros((1, 1), dtype='<i2'), 0, 0
    add_mesh(canvas, model, source, seconds, 0, fade=1. if clip != 4 else max(.03, 1-p**3),
             tint=(1.16, 1.19, 1.22))
    if clip in (0, 1, 2):
        shield(canvas, tex, p, .18 if clip == 0 else .33)
    if clip in (1, 2):
        power = (1-p)**1.4
        canvas.glow(tex['cross'][0], -15, 9, 50, 18-9*p, (.8, .97, 1.), power*.85)
        canvas.ring(tex['shock'][0], 24+24*p, 2.2, (.3, .85, 1.), power*.55, p, 4)
        if clip == 2:
            for j in range(6):
                a = j*math.tau/6+.21
                canvas.ray(tex['beam'][0], math.cos(a)*24, math.sin(a)*24, 42,
                    math.cos(a)*(35+38*p), math.sin(a)*(35+38*p), 28+22*(1-p),
                    1.1, (.35, .88, 1.), power*.8)
                canvas.glow(tex['spark'][0], math.cos(a)*(35+38*p), math.sin(a)*(35+38*p),
                    28+22*(1-p), 2., (.65, 1., 1.), power)
    elif clip == 3:
        power = math.sin(p*math.pi)
        canvas.ring(tex['shock'][0], 15+40*p, 2, (.3, .85, 1.), power*.35, p, 4)
        if p > .5:
            shield(canvas, tex, p, .18*(p-.5)*2)
    elif clip == 4:
        power = math.sin(p*math.pi)*(1-p)
        canvas.ring(tex['shock'][0], 20+75*p, 3, (.35, .8, 1.), power*.8, p, 6)
        canvas.glow(tex['flare'][0], 0, 0, 44, 30*(1-p)+3, (.65, .94, 1.), power*.6)
        for j in range(9):
            a = j*2.399963; r = 20+65*p
            canvas.glow(tex['spark'][0], math.cos(a)*r, math.sin(a)*r, 10+65*math.sin(p*math.pi),
                2.1*(1-p)+.4, (.35, .85, 1.), power)
    return canvas.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--archive', type=Path, default=STEAM/'pak01_dir.vpk')
    parser.add_argument('--tool', type=Path, default=ROOT.parent/'.tools/dota-source2/cli-20.1/Source2Viewer-CLI.exe')
    parser.add_argument('--ss', type=int, default=2)
    parser.add_argument('--platforms-only', action='store_true', help='Append only the two static source platforms; preserve all52 previously baked NPZ files.')
    parser.add_argument('--top-skin-only', action='store_true', help='Correct only the Top platform to original material group1, preserving the other53 NPZ files.')
    args = parser.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    if args.top_skin_only:
        path = out/'manifest.json'; manifest = json.loads(path.read_text(encoding='utf8'))
        unchanged = {r['npz']: sha(ROOT/r['npz']) for r in manifest['frames'] if not (r['model'] == 47 and r['clip'] == 5)}
        assert len(unchanged) == 53
        model = PlatformModel(True); folder = out/model.slug
        rgba, depth, ax, ay = model.frame(model.pose(None, 0), 0, args.ss)
        frame = write_frame(folder, 47, 5, 0, 0, 1, 1, 1, rgba, depth, ax, ay,
            source_clip='static:'+model.receipt['source_worldnode'], source_seconds=0, height_world=73, flags=0)
        manifest['frames'] = [frame if r['model'] == 47 and r['clip'] == 5 else r for r in manifest['frames']]
        manifest['source_models'] = [model.receipt if r['name'] == model.slug else r for r in manifest['source_models']]
        receipt = SOURCE/'pedestal_dire/provenance.json'
        if not any(r['path'] == relative(receipt) for r in manifest['source_export_receipts']):
            manifest['source_export_receipts'].append(dict(path=relative(receipt), sha256=sha(receipt)))
        manifest['unchanged_other53_frames'] = unchanged
        manifest['source_sha256']['tools/bake_jungle.py'] = sha(__file__)
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
        save_preview(folder, [(rgba, ax, ay)])
        assert unchanged == {p: sha(ROOT/p) for p in unchanged}
        print(json.dumps(dict(manifest=relative(path), frames=len(manifest['frames']), sha256=sha(path), source_sha256=sha(__file__))), flush=True)
        return
    if args.platforms_only:
        path = out/'manifest.json'; manifest = json.loads(path.read_text(encoding='utf8'))
        keep = [r for r in manifest['frames'] if r['clip'] < 5 or r['model'] != 47]
        assert len(keep) == 52
        unchanged = {r['npz']: sha(ROOT/r['npz']) for r in keep}
        manifest['frames'] = keep
        manifest['source_models'] = [r for r in manifest['source_models'] if not r['name'].startswith('tormentor_platform_')]
        for model in manifest['models']:
            if model['id'] == 47:
                model['clips'] = [c for c in model['clips'] if c['id'] < 5]
        append_platforms(out, manifest['frames'], manifest['models'], manifest['source_models'], manifest['source_export_receipts'], args.ss)
        manifest['complete'] = len(manifest['frames']) == 54
        manifest['frame_budget'] = dict(added=54, existing=9150, total=9204, maximum=9216)
        manifest['unchanged_first52_frames'] = unchanged
        manifest['source_sha256']['tools/bake_jungle.py'] = sha(__file__)
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
        assert unchanged == {p: sha(ROOT/p) for p in unchanged}
        print(json.dumps(dict(manifest=relative(path), frames=len(manifest['frames']), sha256=sha(path))), flush=True)
        return
    vpk = VpkArchive(args.archive); tex, texture_sources = export_textures(vpk, args.tool)
    previous = {relative(p): sha(p) for p in [ROOT/'private/baked'/d/'manifest.json'
        for d in ('sprites', 'effects', 'skills', 'projectiles', 'skill-components', 'water')] if p.exists()}
    records = []; models = []; sources = []; pictures = []; source_reports = []
    for spec in SPECS:
        model = JungleModel(spec); sources.append(model.receipt)
        folder = out/spec['name']; folder.mkdir(exist_ok=True); previews = []
        clips = [(0, spec['reference'], 6, True, 'idle')] if spec['id'] < 47 else TOR_CLIPS
        catalog = []
        for clip, source, count, loop, phase in clips:
            duration = model.body.duration(model.names[source]); ticks = max(count, round(duration*20))
            catalog.append(dict(id=clip, source_clip=source, phase=phase, frames=count,
                duration_ticks=ticks, source_duration_seconds=duration, loop=loop, directions=1))
            for number in range(count):
                seconds = duration*number/(count if loop else max(1, count-1))
                if spec['id'] < 47:
                    rgba, depth, ax, ay = model.frame(model.pose(source, seconds), 0, args.ss)
                else:
                    rgba, depth, ax, ay = tormentor_frame(model, clip, source, number, count, seconds, tex, args.ss)
                records.append(write_frame(folder, spec['id'], clip, 0, number, count, ticks, loop,
                    rgba, depth, ax, ay, source_clip=source, source_seconds=seconds,
                    height_world=model.height, flags=0))
                previews.append((rgba, ax, ay))
            print(json.dumps(dict(model=spec['id'], clip=clip, source=source, frames=count,
                duration_ticks=ticks, source_triangles=model.original_triangles)), flush=True)
        models.append(dict(id=spec['id'], name=spec['name'], height_world=model.height,
            clips=catalog, fixed_direction=0, source_world_position=spec['world'],
            source_yaw_degrees=spec['yaw'], source_model_scale=spec['scale']))
        report = ROOT/'private/source2/reports'/(spec['report']+'-provenance.json')
        source_reports.append(dict(path=relative(report), sha256=sha(report)))
        if spec['id'] < 47:
            report = ROOT/'private/source2/reports'/('jungle-stall-'+spec['name'].removeprefix('shop_')+'-provenance.json')
            source_reports.append(dict(path=relative(report), sha256=sha(report)))
        save_preview(folder, previews); pictures.append((spec['name'], previews[0]))
    files = ['bake_jungle.py', 'bake_sprites.py', 'bake_skill_components.py', 'bake_projectiles.py',
        'bake_skills.py', 'bake_effects.py', 'bake_world_instances.py', 'gltf_assets.py',
        'bake_sprite_raster.py', 'bake_sprite_raster.c', 'source2_vpk.py']
    append_platforms(out, records, models, sources, source_reports, args.ss)
    manifest = dict(complete=len(records) == 54, ss=args.ss, models=models, frames=records,
        source_models=sources, source_export_receipts=source_reports, source_textures=texture_sources,
        particle_audit=audit_graphs(vpk), source_rules=[vpk.identity('scripts/npc/npc_units.txt')],
        previous_source_manifests_unchanged=previous,
        source_sha256={'tools/'+name: sha(ROOT/'tools'/name) for name in files},
        frame_budget=dict(added=54, existing=9150, total=9204, maximum=9216),
        projection=dict(zoom_q8=180, vertical_q8=181, direction_count=1,
            source_axes='worldXYZ=(glTF.Z,-glTF.X,glTF.Y)*(100/2.54/4)*ModelScale; world rotation=-source yaw',
            depth='signed relative -(localY+localZ)', origin='original model origin preserved'),
        limitations='Original source meshes/skeleton clips/textures. Source2 particle operators, refraction, environment cube reflections, sounds and dynamic lights are approximated offline. Fixed camera yaw is already baked; do not turn these sprites at runtime.')
    (out/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    sheet = Image.new('RGB', (720, 224), (23, 34, 30)); draw = ImageDraw.Draw(sheet)
    for i, (name, (rgba, ax, ay)) in enumerate(pictures):
        image = Image.fromarray(rgba); sheet.paste(image, (i*240+120-ax, 150-ay), image)
        draw.text((i*240+4, 5), name, fill='white')
    sheet.save(out/'contact.png')
    print(json.dumps(dict(manifest=relative(out/'manifest.json'), frames=len(records),
        sha256=sha(out/'manifest.json'))), flush=True)


if __name__ == '__main__':
    main()
