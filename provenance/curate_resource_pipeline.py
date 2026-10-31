#!/usr/bin/env python3
"""Archive readable offline resource recipes; never execute the recipes.

The ordinary application build consumes checked-in generated headers and the
three runtime packages. This archive needs the unbundled original Steam assets
and original extraction/intermediate environment for any rebake.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
from pathlib import Path

PREFIXES = ("bake_", "extract_", "prepare_", "source2_", "terrain_")
EXTRAS = {"alpha_cards.py", "gltf_assets.py", "pack_baked.py", "compact_skill_fx.py",
          "skill_motion_catalog.py", "height_grid.c", "nav_collision.c",
          "refresh_baked_frames.py", "encode_barracks_patch.py"}
# The separate old live-3D prototype does not produce the adopted baked runtime.
EXCLUDE = {"prepare_3d_assets.py"}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    src, dst = args.source.resolve(), args.destination.resolve()
    if src == dst or src in dst.parents or dst in src.parents:
        raise SystemExit("Source and destination must be independent directories")
    toolroot = src / "tools"
    allfiles = {p.name: p for p in toolroot.iterdir() if p.is_file() and p.suffix in {".py", ".c"}}
    selected = {name for name in allfiles if name.startswith(PREFIXES) or name in EXTRAS} - EXCLUDE
    # Include sibling Python imports and quoted script references transitively.
    while True:
        additions = set()
        for name in sorted(selected):
            text = allfiles[name].read_text(encoding="utf-8-sig")
            if name.endswith(".py"):
                tree = ast.parse(text, filename=name)
                for node in ast.walk(tree):
                    if isinstance(node, ast.Import):
                        modules = [alias.name.split(".")[0] for alias in node.names]
                    elif isinstance(node, ast.ImportFrom):
                        modules = [(node.module or "").split(".")[0]]
                    else:
                        continue
                    for module in modules:
                        if module + ".py" in allfiles:
                            additions.add(module + ".py")
            additions.update(value for value in re.findall(r"['\"]([\w.-]+\.(?:py|c))['\"]", text) if value in allfiles)
        additions -= EXCLUDE
        if additions <= selected:
            break
        selected.update(additions)
    metadata = json.loads((src / "build/v027/candidate/DOTA2.build.json").read_text(encoding="utf-8"))
    sourcemap = metadata["source_sha256"]
    archive = dst / "tools/resource_pipeline"
    archive.mkdir(parents=True, exist_ok=True)
    records, external = [], set()
    for name in sorted(selected):
        original = allfiles[name]
        raw = original.read_bytes()
        curated = raw.replace(b"\r\n", b"\n")
        text = curated.decode("utf-8-sig")
        expected = sourcemap.get("tools/" + name)
        if expected is not None and expected != sha(raw):
            raise RuntimeError(f"Frozen final source changed: tools/{name}")
        if name.endswith(".py"):
            tree = ast.parse(text, filename=name)
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    modules = [alias.name.split(".")[0] for alias in node.names]
                elif isinstance(node, ast.ImportFrom):
                    modules = [(node.module or "").split(".")[0]]
                else:
                    continue
                external.update(module for module in modules if module and module + ".py" not in allfiles)
        absolute = [{"line": number, "text": line.strip()} for number, line in enumerate(text.splitlines(), 1)
                    if re.search(r"[A-Za-z]:[/\\]", line)]
        target = archive / name
        if target.exists() and target.read_bytes() != curated:
            raise RuntimeError(f"Refusing overwrite of a differing archived recipe: {name}")
        target.write_bytes(curated)
        records.append({"original_path": "tools/" + name, "archive_path": "tools/resource_pipeline/" + name,
                        "original_raw_sha256": sha(raw), "curated_lf_sha256": sha(curated),
                        "original_bytes": len(raw), "curated_bytes": len(curated),
                        "release_source_map_sha256": expected,
                        "absolute_historical_paths": absolute})
    report = {"schema": 1, "purpose": "Read-only source archive, not a build/rebake entry point.",
              "normal_build_dependency": False,
              "rebake_from_compact_repository_supported": False,
              "missing_external_inputs": ["User-owned original Steam Dota 2 VPK/source assets", "Extracted/converted source2 glTF, materials, animations and intermediate manifests", "Original offline renderer/build environment and optional source extraction tools"],
              "archive_path_semantics": "Recipes are unchanged except CRLF to LF. Their ROOT=parents[1] and historical paths describe their original tools/ location. They require adaptation before any separate rebake. Do not run in the archive directory.",
              "recipes": records, "imported_modules_including_stdlib": sorted(external),
              "excluded_legacy_live_3d": sorted(EXCLUDE),
              "excluded_historical_build_entry_points": [{"path": "tools/" + name, "original_raw_sha256": sha((toolroot / name).read_bytes())} for name in ("build.py", "build_baked.py")],
              "summary": {"recipes": len(records), "curated_bytes": sum(x["curated_bytes"] for x in records),
                          "source_map_matched_recipes": sum(x["release_source_map_sha256"] is not None for x in records),
                          "recipes_with_historical_absolute_paths": sum(bool(x["absolute_historical_paths"]) for x in records)}}
    output = dst / "provenance/resource-pipeline-curation.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report["summary"]))


if __name__ == "__main__":
    main()
