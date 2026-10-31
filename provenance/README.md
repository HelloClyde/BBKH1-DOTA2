# Curated source provenance

`source-curation.json` binds the minimal production source set to the frozen
v0.27 candidate that produced BDA SHA-256
`608b9fdf3c2751b0c14378663e638819a749e7603b45d231001f9b097f43184d`.
There are 29 translation units and 128 ordinary source, header, link, and icon
inputs, totaling 2,203,392 bytes after newline normalization.

The original build retained no compiler `.d` files. `dependencies/*.d` are newly
generated traces from the final frozen sources, using the production
`DOTA_BAKED` configuration and the same MIPS GCC preprocessor. Paths have been
normalized for this repository. These traces are evidence, not the build entry
point. Use `tools/build.py` to build the application.

Each curated input records both its original raw SHA-256 and its new LF SHA-256.
All 128 original hashes matched the original candidate's source map before
copying. Text changes consist only of CRLF to LF; they are not described as
byte-identical source copies. The standalone build's BDA comparison provides
the independent check that newline normalization did not change the payload.
The nine required SDK headers are owned by the pinned `sdk` submodule and were
also checked for normalized content equality.

`private/generated/` and `private/audio/assets/audio_data.h` are small, tracked
compile inputs. Their historical directory name does not imply a Git ignore
rule. Keeping these relative paths preserves the production include layout.
The old live-3D prototype (`scene3d`, `assets3d`, `dota_3d.h`) and the earlier
truncated-color research kernel (`dota_mxu.h`) are excluded. Production uses
`platform/dota_mxu_exact.h`.

The large pre-rendered map/sprite package, sound package, and optional map cache
are tracked with Git LFS under `assets/runtime/`. Their independent runtime
manifest records bytes and SHA-256. They are runtime inputs, not prerequisites
for extracting new C/C++ dependency traces.

`resource-pipeline-curation.json` catalogs the readable archived offline
recipes under `tools/resource_pipeline/`, including their hashes, local import
closure, and historical absolute-path mentions. The archive is not a working
rebake pipeline for this compact checkout. Original Steam assets, extraction
outputs, and offline intermediates are deliberately not bundled.

The two `curate_*.py` files reproduce this curation/audit only when explicitly
given the original frozen source checkout. They never write into that checkout,
are not called by the application build, and have no network or remote setup.

Code and resource ownership are described by the repository's notices and
license files. An extraction tool's license does not license Valve's assets.
