# Archived offline resource recipes

This directory preserves the readable source recipes used while constructing
the baked DOTA2 port: Source 2 extraction helpers, terrain and navigation
generation, sprite and skill baking, map fixtures, UI headers, audio packing,
and the HBK runtime pack format. `provenance/resource-pipeline-curation.json`
records the original path, original raw SHA-256, curated LF SHA-256, and
historical absolute paths for every archived file.

The normal build does **not** invoke these recipes. Version 0.30 consumes the
tracked generated headers, icon, and one Git LFS runtime container,
`assets/runtime/DOTA2.H1R` (112,861,448 bytes). It exposes four logical streams:
SCENE, SOUND, VISION and sparse TREE. The previous five packages totaled
450,526,944 bytes; lossless packing and deduplication reduce storage by 74.95%.
MAP12's duplicate map cache is no longer a separate runtime stream. Original
pixels remain exact, and SOUND is retained as one continuous RAW section without
recompression. Use `tools/build.py` for the application build.

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

Repacking existing packages is separate from rebaking. The current offline
entry points are `tools/pack_tree_sparse.py` and `tools/repack_resources.py` in
the repository's top-level tools directory. They do not run the archived
recipes or extract Steam assets. Historical packages are preserved separately;
commands, source hashes, logical-stream proofs and the binary format are
recorded in `provenance/resource-v030/` and `tools/resource_pack_format.md`.
The native reader uses the MIT-licensed miniz 3.1.0 low-level inflater; its
upstream identities and license are in `third_party/miniz/`. Offline byte
equality and size reductions do not constitute completed v0.30 H1 functional
or FPS acceptance; those device results remain pending independently.

The v0.30 dedicated TP input, queued walk-to-cast behavior and match-history UI
are ordinary application C modules, not additional baked asset packages. The
history uses separate writable MATCH0/1.H1S or A-drive fallback files and must
not be written into H1R. Existing icon pixels are reused; the base font subset
adds only four glyphs (676 total, prior672 records unchanged), with its original
font ownership retained. These changes do not make the archived extraction
recipes part of the normal build.

The catalog includes the local Python import and quoted helper-script closure;
it does not promise an installed or pinned Python environment for rebaking.
Optional third-party modules include NumPy, Pillow, imageio-ffmpeg, pefile, and
Capstone. The historical live-3D prototype and workspace-specific application
builders are excluded. Ownership and licenses remain as stated in the root
repository notices.
