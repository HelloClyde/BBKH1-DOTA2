#!/usr/bin/env python3
"""Audit/copy the final v0.27 compiler closure from an explicit source checkout.

This is a provenance replay utility, not the application's build entry point.
It never writes into the original checkout. The original final build did not
retain .d files; this utility regenerates dependencies with that compiler's
preprocessor and the production DOTA_BAKED configuration.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


PLATFORM_SOURCES = (
    "entry.S", "native.c", "render.c", "status_effects.c", "baked_scene.c",
    "baked_io.c", "item_assets.c", "shop_catalog.c", "dota_audio.c",
    "map_codec.c", "map_format.c",
)
FORBIDDEN = {"assets3d.c", "assets3d.h", "scene3d.c", "scene3d.h",
             "dota_3d.h", "dota_mxu.h"}
FLAGS = ["-DDOTA_BAKED", "-EL", "-march=mips32", "-mabi=32", "-msoft-float",
         "-mno-abicalls", "-G0", "-fno-pic", "-O2", "-g", "-std=c99",
         "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
         "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra"]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def lf(data: bytes, suffix: str) -> bytes:
    if suffix in {".c", ".h", ".S", ".ld", ".py", ".md", ".json"}:
        # No formatting, encoding, or code transformation beyond newline policy.
        return data.replace(b"\r\n", b"\n")
    return data


def put_json(path: Path, obj: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--destination", type=Path, required=True)
    p.add_argument("--compiler", type=Path, required=True)
    p.add_argument("--source-sdk-include", type=Path, required=True)
    args = p.parse_args()
    src, dst = args.source.resolve(), args.destination.resolve()
    sdk = args.source_sdk_include.resolve()
    if src == dst or src in dst.parents or dst in src.parents:
        raise SystemExit("Source and destination must be independent directories")
    metadata_path = src / "build/v027/candidate/DOTA2.build.json"
    metadata_raw = metadata_path.read_bytes()
    metadata = json.loads(metadata_raw)
    if metadata["bda_sha256"] != "608b9fdf3c2751b0c14378663e638819a749e7603b45d231001f9b097f43184d":
        raise SystemExit("Unexpected original release identity")
    source_map = metadata["source_sha256"]
    sources = [src / "platform" / name for name in PLATFORM_SOURCES] + sorted((src / "game").glob("*.c"))
    out = dst / "provenance"
    depdir = out / "dependencies"
    depdir.mkdir(parents=True, exist_ok=True)
    include = []
    for folder in (src / "game", src / "platform", src / "private/generated", sdk):
        include += ["-I", str(folder)]
    closure: set[Path] = set()
    sdk_dependencies: set[Path] = set()
    translation_units = []

    def normalized(path: Path) -> str:
        if path == src or src in path.parents:
            return path.relative_to(src).as_posix()
        if path == sdk or sdk in path.parents:
            return "sdk/sdk/include/" + path.relative_to(sdk).as_posix()
        raise RuntimeError(f"Unexpected external compiler dependency: {path}")

    for source in sources:
        rel = source.relative_to(src).as_posix()
        command = [str(args.compiler.resolve()), *FLAGS, *include, "-MM", "-MT", f"build/{source.stem}.o", rel]
        result = subprocess.run(command, cwd=src, capture_output=True, check=True, text=True)
        # GCC make syntax escapes spaces in absolute Windows dependency paths.
        body = result.stdout.replace("\\\n", " ").split(": ", 1)[1]
        tokens = re.findall(r"(?:\\.|[^\s])+", body)
        paths = []
        for token in tokens:
            token = token.replace("\\ ", " ").replace("\\#", "#")
            path = Path(token)
            path = (path if path.is_absolute() else src / path).resolve()
            normal = normalized(path)
            paths.append(normal)
            if path.name in FORBIDDEN:
                raise RuntimeError(f"Forbidden obsolete dependency: {path.name}")
            if path == sdk or sdk in path.parents:
                sdk_dependencies.add(path)
            else:
                closure.add(path)
        paths = list(dict.fromkeys(paths))
        dep_text = f"build/{source.stem}.o: " + (" " + chr(92) + "\n ").join(paths) + "\n"
        (depdir / f"{source.stem}.d").write_text(dep_text, encoding="utf-8", newline="\n")
        translation_units.append({"source": rel, "object": f"build/{source.stem}.o", "dependencies": paths})

    # Explicit link/package inputs are not compiler -MM dependencies.
    closure.update((src / "platform/h1.ld", src / "private/generated/icon.png"))
    records = []
    for path in sorted(closure, key=lambda value: normalized(value)):
        rel = normalized(path)
        raw = path.read_bytes()
        expected = source_map.get(rel)
        if expected is None:
            raise RuntimeError(f"Required original input missing from release source map: {rel}")
        if sha(raw) != expected:
            raise RuntimeError(f"Original release source hash changed: {rel}")
        data = lf(raw, path.suffix)
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and target.read_bytes() != data:
            raise RuntimeError(f"Refusing to overwrite a differing curated file: {rel}")
        target.write_bytes(data)
        records.append({"path": rel, "original_bytes": len(raw), "curated_bytes": len(data),
                        "original_raw_sha256": sha(raw), "curated_lf_sha256": sha(data),
                        "release_source_map_sha256": expected,
                        "newline_normalized": raw != data,
                        "role": "linker_script" if path.name == "h1.ld" else "bda_icon" if path.name == "icon.png" else "compiler_dependency"})
    sdk_records = []
    for path in sorted(sdk_dependencies):
        target = dst / normalized(path)
        raw = path.read_bytes()
        curated = target.read_bytes()
        if lf(raw, path.suffix) != lf(curated, path.suffix):
            raise RuntimeError(f"Pinned SDK header differs from original compiler input: {path.name}")
        sdk_records.append({"path": normalized(path), "original_raw_sha256": sha(raw),
                            "pinned_sdk_raw_sha256": sha(curated), "bytes": len(curated),
                            "normalized_equal": True})
    groups = {}
    for record in records:
        group = record["path"].split("/")[0]
        entry = groups.setdefault(group, {"files": 0, "curated_bytes": 0, "original_bytes": 0})
        entry["files"] += 1
        entry["curated_bytes"] += record["curated_bytes"]
        entry["original_bytes"] += record["original_bytes"]
    report = {
        "schema": 1, "release_version": "0.27", "original_bda_sha256": metadata["bda_sha256"],
        "original_build_metadata_sha256": sha(metadata_raw),
        "dependency_trace": "Regenerated -MM from final frozen sources; original build retained no .d files.",
        "compiler_flags": FLAGS, "compiler_version": subprocess.run([str(args.compiler.resolve()), "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0],
        "newline_policy": "Only CRLF to LF for curated text. Compare original_raw_sha256 with original source map; curated_lf_sha256 with new checkout. Exact BDA rebuilt independently.",
        "translation_units": translation_units, "files": records, "sdk_header_dependencies": sdk_records,
        "summary": {"translation_units": len(translation_units), "curated_files": len(records),
                    "original_bytes": sum(f["original_bytes"] for f in records),
                    "curated_bytes": sum(f["curated_bytes"] for f in records),
                    "source_map_matches": len(records), "groups": groups},
        "excluded_obsolete_names": sorted(FORBIDDEN),
    }
    # Use the actual pinned SDK commit, not an abbreviated assumed identifier.
    git = subprocess.run(["git", "-c", f"safe.directory={(dst / 'sdk').as_posix()}", "-C", str(dst / "sdk"), "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
    report["sdk_submodule_commit"] = git.stdout.strip()
    put_json(out / "source-curation.json", report)
    print(json.dumps(report["summary"], ensure_ascii=False))


if __name__ == "__main__":
    main()
