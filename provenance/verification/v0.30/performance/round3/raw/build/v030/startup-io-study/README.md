# Reader cache study

The trace runs the exact C7 BDA's normal init/tick/render APIs, with 320 menu
frames followed by 160 ideal 20 Hz match frames. It does not run USB, LCD,
audio playback, MXU, application entry or physical scheduling. All cache
replays use the actual C reader and compare every returned byte with original
SCENE/SOUND/VISION/sparse-TREE logical streams.

| Phase | 2 index / 2 decoded reads | 8 index / 4 decoded reads | Seeks before/after | Decoded chunks before/after |
|---|---:|---:|---:|---:|
| Resource startup | 1451 | 845 | 912 / 300 | 2255 / 1735 |
| Main menu | 345 | 282 | 271 / 209 | 520 / 520 |
| First modeled 8 seconds | 287 | 178 | 226 / 117 | 347 / 347 |

8/8 reduces a further 44 reads and 29 seeks during resource startup, but has
the same menu/play counts as 8/4 and requires another 64 KiB. Selected 8/4
uses 168336 additional heap bytes on H1 (168416 on the 64-bit host). The
original small mandatory 2/2 buffers remain usable if this optional allocation
fails. Enlarged descriptors are also in that optional allocation. RAW audio
continues to use independent file cursors and bypass all visual caches.

Evidence:
- `c7-trace/verification.json`: frozen MIPS trace identity and limitations.
- `cache-matrix/measurement.json`: all nine 2/4/8 combinations and byte checks.
- `selected-replay/verification.json`: selected production optional cache has
  exactly the fixed 8/4 IO/decode counters and all bytes match.
- `reader-selected-tests/verification.json`: all four full logical streams,
  16 malformed containers, unpadded compressed EOF, nested RAW audio callback,
  mandatory allocation failures, payload allocation failure, optional reader
  allocation failure, successful remount and late reservation migration.

Object-only MIPS size comparison with exact C7 flags: reader +556 text, +20
data and +4 BSS; native reader and row-cache hooks together +20 text. These
counts are not FPS results.

## Targeted animation prewarm trial

`prewarm-trial2/verification.json` replays the same real menu/play sprite IDs
through frozen C7 `get_sprite` and its existing 14 MiB pool. The trial adds
53 selected initial actor frames (360583 raw bytes) between menu and play;
every returned sprite is compared with its full canonical frame bytes.

| Work | Extra prewarm | Play without prewarm | Play after prewarm | Combined prewarm + play |
|---|---:|---:|---:|---:|
| Logical reads | 45 | 77 | 32 | 77 |
| Physical reads | 47 | 137 | 48 | 95 |
| Seeks | 28 | 106 | 41 | 69 |
| Decoded chunks | 180 | 312 | 132 | 312 |
| Decoded bytes | 274090 | 553271 | 279181 | 553271 |

This shows potential to group cold IO, while decoding work merely moves to a
loading wait. It omits unrelated map/item/audio contention and assumes known
initial roster directions; native loading latency and net FPS are unmeasured.
No production start-game warmup is enabled in this round. Native seed, tick,
history and clock accounting remain unchanged. The accepted native hooks are
the reader reservation after mount and 128 sprite row slots after required
scene/vision/tree/audio/history allocations.
