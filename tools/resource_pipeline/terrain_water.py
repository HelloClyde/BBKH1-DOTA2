"""Add original Steam water geometry as a separate, opaque low-end pass.

This offline module never invents water polygons and never contributes to the
terrain-height or terrain-albedo bake. Full water_dota reflection/refraction,
opacity, flow normals and waterfalls are deliberately outside this small pass.
"""
from __future__ import annotations
import collections
import hashlib
import json
import re
from pathlib import Path

from gltf_assets import Gltf, np, ROOT
from terrain_cells import split_cells

MAP_SCALE = 4096 / (16384 * .0254)
MAP_BASIS = np.array([[0, 0, 1], [-1, 0, 0], [0, 1, 0]], dtype=float)
MATERIALS = frozenset(("water_generic_000", "water_generic_000_slope"))


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _weld(points, faces):
    """Weld a quarter-world grid before final int16 rounding, without filling holes."""
    _, first, inverse = np.unique(np.rint(points * 4).astype(np.int64), axis=0,
                                  return_index=True, return_inverse=True)
    points = points[first]
    faces = inverse[faces].astype(np.int32)
    nondegenerate = (faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 2] != faces[:, 0])
    faces = faces[nondegenerate]
    if len(faces):
        # Adjacent original fragments can contain the same coplanar triangle.
        # Remove exact duplicate topology, never add a convex-hull/flat patch.
        _, unique = np.unique(np.sort(faces, axis=1), axis=0, return_index=True)
        faces = faces[np.sort(unique)]
        triangles = points[faces]
        faces = faces[np.any(np.cross(triangles[:, 1] - triangles[:, 0],
                                      triangles[:, 2] - triangles[:, 0]) != 0, axis=1)]
    return points, faces


def _water_texture(builder, material, source_path):
    vmat = material.get("source", {}).get("extras", {}).get("vmat", {})
    vectors = vmat.get("VectorParams", {})
    parameter = next((name for name in ("g_vLowEndSurfaceColor", "g_vRefractionTint")
                      if name in vectors), None)
    if parameter is None:
        raise ValueError(f"Original water color parameter unavailable: {material.get('name')}")
    value = np.asarray(vectors[parameter], dtype=float)
    if len(value) < 3 or not np.isfinite(value[:3]).all():
        raise ValueError("Invalid original water color parameter")
    rgb = np.rint(np.clip(value[:3], 0, 1) * 255).astype(np.uint16)
    pixel = int(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3))
    if pixel == 0xf81f:
        pixel = 0xf81e  # Runtime key is reserved for transparent cutouts.
    key = ("original-water-solid-opaque", pixel)
    if key not in builder.texture_keys:
        index = len(builder.textures)
        builder.texture_keys[key] = index
        builder.textures.append({
            "pixels": np.array([[pixel]], dtype=np.uint16), "size": 1,
            "name": "steam_water_low_end_surface", "source": str(source_path),
            "material_tint": value.tolist(), "tint_parameter": parameter,
            "image_sha256": None,
        })
    return builder.texture_keys[key], {
        "name": material["name"], "shader": vmat.get("ShaderName"),
        "selected_parameter": parameter, "selected_value": value.tolist(),
        "rgb8": rgb.tolist(), "rgb565": pixel,
        "original_VectorParams": vectors,
        "original_FloatParams": vmat.get("FloatParams", {}),
        "original_IntParams": vmat.get("IntParams", {}),
        "runtime_approximation": "Opaque 1x1 RGB565 authored low-end color; no dynamic water shader",
    }


def add_water(builder, per_cell=48):
    """Append real water chunks and provenance receipts; return summary receipt.

    Uses the existing Builder interface. A delayed import avoids a converter
    import cycle. The roughly 3k-triangle total is a budget aim; preserved source
    boundaries take priority and any excess is reported rather than hidden.
    """
    from prepare_3d_assets import decimate

    if not 2 <= int(per_cell) <= 512:
        raise ValueError("per_cell must be in 2..512")
    if any(receipt.get("kind") == "terrain_water" for receipt in builder.receipts):
        raise ValueError("Original water pass was already added to this builder")
    manifest_path = ROOT / "private/source2/reports/water-manifest.json"
    manifest = json.loads(manifest_path.read_bytes())
    if not manifest.get("complete"):
        raise ValueError("Original water export manifest is incomplete")
    buckets = collections.defaultdict(list)
    source_records, material_records = [], {}
    source_triangles = clipped_triangles = 0
    height_count = len(builder.height_triangles)
    for entry in manifest["assets"]:
        if "water_generic" not in entry.get("entry", ""):
            continue
        if entry.get("layer_index") != 0 or not entry.get("world_baked"):
            continue
        path = Path(entry["gltf"])
        if not path.is_absolute():
            path = ROOT / path
        source_sha = _sha(path)
        if entry.get("gltf_sha256") and source_sha != entry["gltf_sha256"]:
            raise ValueError(f"Water glTF differs from source receipt: {path}")
        document = Gltf(path)
        included, names = 0, set()
        for primitive in document.primitives():
            material = document.material(primitive["material"])
            if material["name"] not in MATERIALS:
                continue
            vmat = material.get("source", {}).get("extras", {}).get("vmat", {})
            if vmat.get("ShaderName") != "water_dota.vfx":
                raise ValueError("Expected original water_dota material")
            texture, record = _water_texture(builder, material, path)
            material_records[material["name"]] = record
            names.add(material["name"])
            points = primitive["points"] @ MAP_BASIS.T * MAP_SCALE + np.array([2048, 2048, 0])
            faces = primitive["faces"][:, [0, 2, 1]]  # Canonical map basis has negative determinant.
            included += len(faces)
            source_triangles += len(faces)
            for cx, cy, cell_points, cell_faces in split_cells(points, faces, 256):
                buckets[cx, cy, texture].append((cell_points, cell_faces))
                clipped_triangles += len(cell_faces)
        if included:
            source_records.append({
                "kind": "water_source", "source_gltf": str(path.relative_to(ROOT)),
                "source_sha256": source_sha, "source_buffer_sha256": document.buffer_sha256,
                "source_entry": entry["entry"], "source_resource": entry.get("source"),
                "layer_index": 0, "world_baked": True, "source_triangles": included,
                "materials": sorted(names),
            })
    if not source_records:
        raise ValueError("No original default-layer water_generic geometry was found")

    prepared = []
    for (cx, cy, texture), pieces in sorted(buckets.items()):
        all_points, all_faces, offset = [], [], 0
        for points, faces in pieces:
            all_points.append(points)
            all_faces.append(faces + offset)
            offset += len(points)
        points, faces = _weld(np.concatenate(all_points), np.concatenate(all_faces))
        if len(faces):
            prepared.append((cx, cy, texture, points, faces))
    welded_faces = sum(len(row[4]) for row in prepared)
    target_total = min(welded_faces, 3000)
    output_triangles = output_vertices = chunks = 0
    for cx, cy, texture, points, faces in prepared:
        target = max(2, min(int(per_cell), round(target_total * len(faces) / max(welded_faces, 1))))
        uv = np.zeros((len(points), 2), dtype=float)
        reduced, triangles, _, _, _ = decimate(points, faces, uv, min(len(faces), target), preserve_border=True)
        # Even highly fragmented preserved boundaries must fit the runtime's
        # 1024-vertex mesh limit; partition faces without changing triangles.
        parts = [triangles] if len(reduced) <= 1024 else [triangles[i:i + 300] for i in range(0, len(triangles), 300)]
        for partition, part in enumerate(parts):
            if not len(part):
                continue
            used = np.unique(part)
            remap = np.full(len(reduced), -1, dtype=np.int32)
            remap[used] = np.arange(len(used))
            local = reduced[used] - np.array([cx * 256 + 128, cy * 256 + 128, 0])
            cell_x = f"n{-cx}" if cx < 0 else f"p{cx}"
            cell_y = f"n{-cy}" if cy < 0 else f"p{cy}"
            name = f"water_{cell_x}_{cell_y}_{texture}_{partition}"
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
                raise ValueError(f"Generated water mesh name is not a C identifier: {name}")
            builder.add_mesh(name, local, remap[part], np.zeros((len(used), 2)),
                             [texture] * len(part), repeat=False)
            builder.meshes[-1]["flags"] |= 2  # S3D_UNLIT, preserve authored solid color.
            builder.chunks.append((name, cx * 256 + 128, cy * 256 + 128, 0, 256, 0, 0))
            output_triangles += len(part)
            output_vertices += len(used)
            chunks += 1
    if len(builder.height_triangles) != height_count:
        raise AssertionError("Water pass must not contribute terrain height samples")
    builder.receipts.extend(source_records)
    receipt = {
        "kind": "terrain_water", "manifest_sha256": _sha(manifest_path),
        "source_files": len(source_records), "source_triangles": source_triangles,
        "clipped_triangles": clipped_triangles, "welded_triangles": welded_faces,
        "output_chunks": chunks, "output_vertices": output_vertices, "output_triangles": output_triangles,
        "triangle_budget_aim": 3000, "triangle_budget_exceeded": output_triangles > 3000,
        "per_cell_budget": int(per_cell), "materials": list(material_records.values()),
        "mapping": "game=(2048+gltfZ*s,2048-gltfX*s,gltfY*s), s=9.842519685; original world-baked positions",
        "geometry_processing": "Real triangle/cell clipping, quarter-world weld, preserved-boundary QEM; no invented water plane",
        "height_samples_added": 0, "terrain_albedo_bake_samples_added": 0,
        "limitations": "Opaque static low-end water color only; flow overlays, waterfalls, refraction, reflection and original opacity are not reproduced",
    }
    builder.receipts.append(receipt)
    return receipt
