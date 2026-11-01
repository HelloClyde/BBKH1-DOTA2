"""Offline tree-free panorama from the original, read-only Source2 exports.

This optional asset tool needs the historical local export workspace. Ordinary
BDA/CI builds do not invoke it. All output, helpers and temporary files remain
inside the selected new-repository build directory; no Steam export is run.
"""
from pathlib import Path
import argparse
import ctypes
import gc
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n',
                          encoding='utf8', newline='\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source-root', type=Path, default=ROOT.parent/'h1-dota2')
    ap.add_argument('--output', type=Path, default=ROOT/'build/v029/tree-bake/no-trees')
    ap.add_argument('--cc', type=Path, default=Path('C:/msys64/ucrt64/bin/gcc.exe'))
    ap.add_argument('--ss', type=int, default=2)
    args = ap.parse_args()
    source, out = args.source_root.resolve(), args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.is_relative_to(source):
        raise ValueError('Output must be inside this repository build/, outside the source workspace')
    if out.exists():
        raise ValueError('Preserve existing output; choose a fresh directory')
    if args.ss not in (1, 2, 3, 4):
        raise ValueError('Supersampling must be 1..4')
    sys.dont_write_bytecode = True

    # Imported historical functions keep their source ROOT for reads. Fail
    # closed if any Python path accidentally attempts to write under it.
    def guard(event, values):
        if event == 'open' and isinstance(values[0], (str, bytes, os.PathLike)):
            mode, flags = values[1], values[2]
            writing = (isinstance(mode, str) and any(c in mode for c in 'wax+')) or (
                isinstance(flags, int) and flags & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND))
            if writing and Path(os.fsdecode(values[0])).resolve().is_relative_to(source):
                raise PermissionError('Historical source workspace is read-only: '+str(values[0]))
    sys.addaudithook(guard)
    sys.path.insert(0, str(source/'tools'))
    import bake_map as bm
    import bake_map_completion as completion
    import bake_sprite_raster as raster
    from bake_effects import linear, srgb, sample
    from gltf_assets import Gltf, np
    from PIL import Image
    assert bm.ROOT.resolve() == source
    assert (bm.WIDTH, bm.HEIGHT, bm.ORIGIN) == (3840, 2816, (-512, -512))
    start = time.monotonic()
    out.mkdir(parents=True)
    base = out/'base'
    (base/'albedo').mkdir(parents=True)
    watched = [source/'private/baked/v022/map-lit'/name for name in
               ('map.png', 'map-depth.npy', 'ground-grid.npy', 'manifest.json')]
    watched += [source/'private/source2/reports'/name for name in
                ('map-static-manifest.json', 'map-landmarks-manifest.json',
                 'map-completion-manifest.json', 'base-layers-manifest.json')]
    before = {str(p.relative_to(source)).replace('\\', '/'): sha(p) for p in watched}
    albedo = source/'build/conversion/baked-map/albedo'
    for name in ('source-albedo.png', 'manifest.json'):
        shutil.copy2(albedo/name, base/'albedo'/name)
        before[str((albedo/name).relative_to(source)).replace('\\', '/')] = sha(albedo/name)
    modules = ('bake_map.py', 'bake_map_completion.py', 'bake_sprite_raster.py',
               'bake_sprite_raster.c', 'bake_surface.py', 'terrain_bake.py',
               'gltf_assets.py', 'bake_world_instances.py', 'height_grid.c',
               'bake_effects.py', 'bake_fountain_v022.py')
    helpers = {name: sha(source/'tools'/name) for name in modules}
    env = dict(os.environ)
    env['PATH'] = str(args.cc.parent)+os.pathsep+env.get('PATH', '')

    def local_library():
        c = source/'tools/bake_sprite_raster.c'
        folder = out/'helpers'
        folder.mkdir(exist_ok=True)
        path = folder/('raster-'+sha(c)[:12]+'.dll')
        if not path.exists():
            subprocess.run([str(args.cc), '-std=c99', '-O3', '-shared', '-Wall', '-Wextra',
                            str(c), '-o', str(path), '-lm'], check=True, env=env, capture_output=True)
        dll = ctypes.CDLL(str(path))
        ptr = ctypes.c_void_p
        dll.bake_clear.argtypes = [ctypes.c_int, ctypes.c_int, ptr, ptr]
        dll.bake_draw.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ptr, ptr, ptr, ptr,
            ctypes.c_int, ptr, ctypes.c_int, ctypes.c_int, ctypes.c_float, ptr, ctypes.c_int, ctypes.c_int]
        return dll

    excluded = []
    def no_trees(scene):
        matrices = scene.globals()
        for i, node in enumerate(scene.doc['nodes']):
            name = node.get('name', '')
            if ('mesh' not in node or not name.startswith(('tree_', 'dire_tree')) or
                    not re.search(r'_model(?:\.\d+)?$', name) or 'stump' in name):
                continue
            pos = bm.BASIS@matrices[i][:3, 3]*bm.SCALE+[2048, 2048, 0]
            if -512 < pos[0] < 4608 and -512 < pos[1] < 4608:
                excluded.append({'stage': 'base-tree-source', 'node': i, 'name': name})
        print(json.dumps({'excluded_default_tree_instances': len(excluded)}), flush=True)
        return {}, {}

    bm.OUT = base
    bm.tree_sources = no_trees
    raster.library = local_library
    argv = sys.argv
    try:
        sys.argv = ['bake_map.py', '--reuse-albedo', '--ss', str(args.ss)]
        bm.main()
    finally:
        sys.argv = argv
    base_report = read(base/'manifest.json')
    base_report.update(tree_free=True, excluded_tree_instances=len(excluded),
                       scope='Original full terrain/water/static models; dedicated tree source loop disabled.')
    write(base/'manifest.json', base_report)
    gc.collect()

    # Seed the same supersampled original-prop renderer with our own no-tree
    # terrain. Do not call completion.main(), which writes historical headers.
    completion.BEFORE = base
    renderer = completion.CompletionMap(args.ss)
    catalog_path = source/'private/source2/reports/map-completion-manifest.json'
    catalog = read(catalog_path)
    assert catalog['complete']
    skin_path = source/catalog['lotus_dire_skin']['path']
    skin = read(skin_path)
    renderer.dire_body = np.asarray(Image.open(source/skin['png']).convert('RGBA')).copy()
    renderer.dire_body[:, :, 3] = 255
    scene = Gltf(source/'private/source2/exports/map-geometry/dota.gltf')
    instances, sources, skipped_fillers, omitted_fruit = [], [], 0, 0
    for asset in catalog['assets']:
        tree = ('tree' in asset['name'].lower() or asset.get('kind') == 'entity_tree')
        if tree:
            excluded.append({'stage': 'completion', 'asset': asset['name'],
                             'placements': len(asset['placements'])})
            continue
        g = Gltf(source/asset['gltf'])
        assert sha(g.path) == asset['sha256']
        parts = list(g.primitives())
        for placement in asset['placements']:
            if 'filler_index' in placement:
                skipped_fillers += 1
                continue
            entity = placement.get('entity', {})
            if entity.get('classname') == 'ent_dota_tree':
                excluded.append({'stage': 'completion-entity', 'entity': entity})
                continue
            matrix = np.asarray(placement['matrix_column_major']).reshape(4, 4).T
            selected = None
            if asset['kind'] == 'aggregate':
                node = placement['node']
                original = scene.doc['meshes'][scene.doc['nodes'][node]['mesh']]['primitives'][0]
                faces = scene.accessor(original['indices']).reshape(-1, 3).astype(np.int32)
                selected = [dict(parts[placement['draw']], faces=faces)]
            if entity.get('classname') == 'npc_dota_lotus_pool':
                selected = [p for p in parts if not p['name'].rsplit('.', 1)[-1].startswith('mango')]
                omitted_fruit += len(parts)-len(selected)
                renderer.lotus_dire = str(entity['skin']) == '1'
            renderer.model(g, matrix, visible=completion.visible, parts=selected, tint=placement['tint'])
            renderer.lotus_dire = False
            instances.append({'asset': asset['name'], 'entity': entity, 'node': placement.get('node')})
        sources.append({'gltf': asset['gltf'], 'sha256': sha(g.path), 'buffers': g.buffer_sha256,
                        'provenance': asset['provenance'], 'provenance_sha256': sha(source/asset['provenance'])})
        print(json.dumps({'completion_asset': asset['name'], 'static_instances': len(instances)}), flush=True)
    rgba, depth = renderer.raster.finish()
    rgb = rgba[:, :, :3].copy()
    depth = np.rint(depth).clip(0, 65535).astype('<u2')
    triangles, total = renderer.triangles, renderer.instances
    del renderer, rgba, scene
    gc.collect()
    Image.fromarray(rgb).save(out/'map-unlit.png')
    np.save(out/'map-depth.npy', depth)
    shutil.copy2(base/'ground-grid.npy', out/'ground-grid.npy')

    # Re-evaluate the already-audited v022 fountain light recipe on our newly
    # exposed ground. No material decoder or original light_map writer runs.
    lit_manifest = source/'private/baked/v022/map-lit/manifest.json'
    light = read(lit_manifest)['fountain_deferred_light']
    cookie_path = source/'private/source2-v022/fountain/particles-light/particle_light_mask_circfade.png'
    cookie = np.asarray(Image.open(cookie_path).convert('RGBA')).astype(np.float32)/255
    cookie = np.dstack((linear(cookie[:, :, :3]), cookie[:, :, 3]))
    output = rgb.copy()
    radius = light['world_radius']
    center = np.asarray(light['world_center'])
    tint = np.asarray(light['source_tint'])/255
    h, w = depth.shape
    for first in range(0, h, 256):
        end = min(first+256, h)
        lo, hi = max(0, first-1), min(h, end+1)
        yy, xx = np.mgrid[lo:hi, :w]
        wx = (xx-512+.5)/(180/256)
        diff = (yy-512+.5)/(180*181/65536)
        sm = 32768-depth[lo:hi].astype(float)
        wy, wz = (sm+diff)/2, (sm-diff)/2
        dx, dy, dz = wx-center[0], wy-center[1], wz-center[2]
        distance = np.sqrt(dx*dx+dy*dy+dz*dz)
        rr = distance/radius
        t = np.clip((rr-light['source_start_falloff'])/(1-light['source_start_falloff']), 0, 1)
        fall = 1-t*t*(3-2*t)
        mask = sample(cookie, .5+dx/(2*radius), .5+dy/(2*radius))[:, :, 0]
        dyx, dzx = np.gradient(wy, axis=1), np.gradient(wz, axis=1)
        dyy, dzy = np.gradient(wy, axis=0), np.gradient(wz, axis=0)
        normal = np.stack((dyx*dzy-dzx*dyy, -dzy/(180/256), dyy/(180/256)), axis=2)
        normal /= np.maximum(np.linalg.norm(normal, axis=2)[:, :, None], 1e-8)
        toward = np.stack((-dx, -dy, -dz), axis=2)/np.maximum(distance[:, :, None], 1e-8)
        lambert = np.maximum(0, (normal*toward).sum(2))
        gain = (radius*.5/np.maximum(distance, 1e-6))**2*light['source_alpha']*light['source_alpha_scale']*fall*mask*lambert
        gain[rr >= 1] = 0
        energy = linear(rgb[lo:hi]/255.)*(1+gain[:, :, None]*tint)
        part = np.rint(np.clip(srgb(energy), 0, 1)*255).astype(np.uint8)
        output[first:end] = part[first-lo:end-lo]
    Image.fromarray(output).save(out/'map.png')
    preview = Image.fromarray(output)
    preview.thumbnail((960, 704), Image.Resampling.LANCZOS)
    preview.save(out/'preview.png')
    for name, pos in [('radiant', (650,3370,80)), ('dire', (3370,780,80)),
                      ('river', (2000,2000,30)), ('jungle', (1200,3000,64))]:
        x, y, z = pos
        px, py = round(x*180/256+512), round((y-z)*180*181/65536+512)
        Image.fromarray(output).crop((px-240, py-136, px+240, py+136)).save(out/(name+'-preview.png'))
    assert before == {p: sha(source/p) for p in before}
    assert helpers == {p: sha(source/'tools'/p) for p in helpers}
    report = dict(base_report, complete=True, tree_free=True,
        source_root=str(source), supersamples=args.ss, completion_sources=sources,
        completion_static_instances=instances, completion_added_triangles=triangles,
        source_triangles_drawn=base_report['source_triangles_drawn']+triangles,
        source_instances=base_report['source_instances']+total,
        excluded_tree_records=excluded, skipped_dynamic_fillers=skipped_fillers,
        omitted_lotus_fruit_parts=omitted_fruit, initial_layer_tree_pass_not_run=True,
        map_sha256=sha(out/'map.png'), depth_sha256=sha(out/'map-depth.npy'),
        ground_grid_sha256=sha(out/'ground-grid.npy'), preview_sha256=sha(out/'preview.png'),
        fountain_light=dict(recipe_manifest=str(lit_manifest), recipe_manifest_sha256=sha(lit_manifest),
            cookie_sha256=sha(cookie_path), parameters=light,
            changed_pixels=int(np.any(output != rgb, axis=2).sum()), depth_unchanged=True),
        original_inputs_before_after_sha256=before, original_workspace_unchanged=True,
        historical_helper_sha256=helpers, generator_sha256=sha(__file__),
        elapsed_seconds=time.monotonic()-start,
        scope='Tree-free original geometry background only. No runtime pack, game source or old resource modified. Dedicated trees, tree aggregates and entity trees omitted; static structures and original terrain retained. Fixed-camera RGB/depth for later cut-tree restoration, not a claim of unrelated-pixel identity with the existing baked panorama.')
    write(out/'manifest.json', report)
    print(json.dumps({'complete': True, 'output': str(out), 'map_sha256': report['map_sha256'],
                      'depth_sha256': report['depth_sha256'], 'seconds': report['elapsed_seconds']}), flush=True)


if __name__ == '__main__':
    main()
