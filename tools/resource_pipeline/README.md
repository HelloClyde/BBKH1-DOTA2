# Archived offline resource recipes

This directory preserves the readable source recipes used while constructing
the baked DOTA2 port: Source 2 extraction helpers, terrain and navigation
generation, sprite and skill baking, map fixtures, UI headers, audio packing,
and the HBK runtime pack format. `provenance/resource-pipeline-curation.json`
records the original path, original raw SHA-256, curated LF SHA-256, and
historical absolute paths for every archived file.

The normal build does **not** invoke these recipes. It consumes the tracked
generated headers, icon, and the three runtime packages in `assets/runtime/`.
Use `tools/build.py` for the application build.

These recipes are an archive, not an executable build entry. They were kept
unchanged except CRLF to LF, so `ROOT = Path(__file__).resolve().parents[1]`
still describes their original `tools/` location. Some recipes name the
original developer's Steam installation, host GCC, Source 2 extraction tools,
or intermediate `private/` and `build/` directories. Those paths are historical
evidence; they are not configuration requirements for this repository's build.
Do not run the archive in place.

A new resource bake needs a separately configured offline workspace, the
user's original Steam Dota 2 assets, extracted/converted models and materials,
intermediate manifests, and suitable host tools. Those inputs, original VPKs,
firmware, DLLs, and the compiler toolchain are not bundled here. The compact
checkout alone cannot reproduce the pre-rendered packages from source assets.
The existing runtime package hashes preserve the exact adopted resources.

The catalog includes the local Python import and quoted helper-script closure;
it does not promise an installed or pinned Python environment for rebaking.
Optional third-party modules include NumPy, Pillow, imageio-ffmpeg, pefile, and
Capstone. The historical live-3D prototype and workspace-specific application
builders are excluded. Ownership and licenses remain as stated in the root
repository notices.
