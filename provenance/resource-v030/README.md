# v0.30 resource conversion receipts

This directory records the offline lossless resource conversion. Native reader
and H1 acceptance are separate evidence; the storage measurements here do not
claim a frame-rate improvement or completed device testing.

The five v0.29 runtime packages total **450,526,944 bytes**. The new
`DOTA2.H1R` is **112,861,448 bytes**, SHA-256
`00ecddaf28bb3716cf9042115498b81e2b01363e66baa3e03b03b914be466153`.
That is 337,665,496 bytes (74.95%) less. It contains four logical streams:
SCENE, SOUND, VISION, and sparse RAW v2 TREE.

* SCENE's entire 406,028,212-byte virtual stream is preserved, including original
  offsets, frame directory, map, water, fountain tracks, item catalog and footer.
* SOUND's entire original 10,226,944 bytes form one continuous, 4096-aligned RAW
  section. Music/effects are not recompressed. Two file handles can read it
  independently without invoking the resource decompressor.
* VISION's complete original stream is preserved.
* TREE's v2 logical representation is 50,343,158 bytes. Its sparse producer
  reconstructed and compared all 364 original dense canonical tile byte arrays.
  Owner tokens, living trees, stumps, constant decor, color, alpha and depth keep
  their original meaning. The outer container preserves this v2 stream exactly.
* MAP12's 16,593,196-byte sidecar is removed from runtime delivery. Its 562
  decoded map tiles were byte-identical to SCENE's tiles. The new provider reads
  the compressed SCENE map representation directly, with no second map copy.

There were **4,349 identical full-frame payload repetitions**, totaling
39,935,693 repeated bytes. Per-plane deduplication detects 50,670,193 repeated
sprite bytes; that figure includes the full-frame duplicates and must not be
added to it. Each animation retains its own metadata/anchor. All frames remain
available. The 169,144 bytes of old, unreferenced frame-refresh payload are also
preserved in SCENE; removing unobserved gameplay animations was not used to
obtain these savings.

Global chunk identities use canonical-byte SHA-256. Chunks contain at most
16 KiB after decoding. RAW, independent raw DEFLATE, and reversible delta16
DEFLATE are selected per chunk. Directory pages have fixed records and CRCs;
the whole index need not occupy device heap. TREE is split by tile into
contiguous bounded chunks, avoiding thousands of tiny frame-plane reads.
See [the exact format](../../tools/resource_pack_format.md).

Receipts:

* `build-manifest.json`: original producer output, including input hashes and
  successful complete virtual-stream CRC/SHA reconstruction.
* `source-packages.json`: unchanged historical raw packages in
  `build/v030/legacy`, plus original Source 2 provenance pointers.
* `tree-sparse-manifest.json`: all 364 old canonical tile comparisons and sparse
  v2 bounds; the old 17.5 MB TREE remains a historical input.
* `deduplication.json`: measured duplicate counts, storage and conservative
  reachability audit. No fallback models were silently deleted.
* `index-summary.json`: exact final section identities and bounded index shape.
* `producer-contract-verification.json`: 4 offline contract cases, 10,504 checks;
  this is Python producer/reader evidence, not native MIPS or H1 execution.
* `reproduction.json`: exact offline conversion commands and Python/zlib versions.
* `copy-receipts.json`: byte-preserving copies of the original small receipts.

Historical recipe archive provenance remains unchanged at
`provenance/resource-pipeline-curation.json` (86 recipe files, current hashes
matched). Its old paths describe a separate extraction/bake environment.
Repacking existing resources does not need Steam or rerendering; rebuilding the
original art from Source 2 still needs the separately recorded original assets
and bake environment. Valve assets are not relicensed by this container format.
