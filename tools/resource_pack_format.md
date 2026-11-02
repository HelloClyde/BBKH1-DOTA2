# DOTA2.H1R version 1

All integers below are little-endian `uint32_t`. Offsets are absolute container
offsets, except virtual stream offsets. CRC is standard CRC-32 as in zlib.
The four section IDs are 0 SCENE, 1 SOUND, 2 VISION, 3 TREE. MAP12 is absent.
SCENE/SOUND/VISION preserve the original file's complete byte stream. TREE is
the losslessly equivalent sparse RAW v2 stream; its independent producer
verifies all original dense canonical tiles. No reverse-RLE layer is required.

The 128-byte header starts with 8 bytes `DOTA2H1R`, then these u32 fields:

| Offset | Field |
|---:|---|
| 8 | version = 1 |
| 12 | header bytes = 128 |
| 16 | physical file bytes, below 0x80000000 |
| 20 | sections = 4 |
| 24 | section table offset = 128 |
| 28 | section entry bytes = 96 |
| 32 | unique chunk count |
| 36 | chunk directory page count |
| 40 | chunk page guide offset |
| 44 | all guide entry bytes = 24 |
| 48 | chunk record bytes = 20 |
| 52 | chunk records per full page = 512 |
| 56 | maximum canonical chunk bytes = 16384 |
| 60 | flags = 0 |
| 64 | data start (RAW SOUND physical offset) |
| 68 | section table CRC |
| 72 | chunk page guide CRC |
| 76..120 | reserved, zero |
| 124 | header CRC, this word zero during calculation |

Each 96-byte section entry is 24 words:

| Word | Field |
|---:|---|
| 0 | section ID |
| 1 | flags: 0 chunked, 1 RAW continuous (SOUND only) |
| 2 | complete logical stream bytes |
| 3 | logical stream CRC |
| 4..11 | exact 32 SHA-256 bytes, no word byte reversal |
| 12 | span count, zero for RAW |
| 13 | span page count, zero for RAW |
| 14 | span page guide offset, zero for RAW |
| 15 | span page guide CRC, zero for RAW |
| 16 | RAW physical offset, zero for chunked |
| 17 | RAW stored bytes = logical bytes, zero for chunked |
| 18..23 | reserved zero |

SOUND is one 4096-aligned RAW section. It can be opened twice using two normal
file descriptors. An audio read needs neither chunk decoding nor directory IO.

A span page guide entry is six words:
`[virtualStart, virtualEndExclusive, physicalPageOffset, pageBytes, records, CRC]`.
Each page contains at most 1024 span records, 12 bytes each:
`[virtualStart, rawBytes, chunkID]`. Sections have exact continuous coverage
without holes or overlaps. A span references the entire canonical chunk:
`span.rawBytes == chunk.rawBytes`. Pages are ordered by virtual start and guide
records cover the stream exactly. Guides/pages are RAW in version 1.

A chunk page guide entry is six words:
`[firstChunkID, count, physicalPageOffset, pageBytes, CRC, reservedZero]`.
Every page except the last contains 512 20-byte chunk records:
`[physicalOffset, storedBytes, rawBytes, canonicalCRC, codec]`.
Chunk IDs are zero-based, global across all chunked sections. Canonical SHA-256
identity is used by the offline producer for global deduplication. On-device
page/chunk CRCs detect corruption; release metadata pins the complete H1R SHA.

Codec 0 is canonical RAW; codecs 1/2 are independent **raw DEFLATE** streams
(no zlib/gzip wrapper or cross-chunk dictionary). Codec 1 expands directly to
canonical bytes. Codec 2 expands to delta16 + zigzag + low/high byte transpose:
for N little-endian u16 values, start previous=0; delta=(value-previous)&65535;
zigzag=((delta<<1)^(-(delta>>15)))&65535. Store N low bytes followed by N high
bytes. Inverse is delta=(zigzag>>1)^(-(zigzag&1)), running sum modulo65536.
CRC is checked **after** inverse. Codec 2 raw length must be even.

Hard bounds are `0 < storedBytes <= rawBytes <= 16384`. Compressed codecs are
used only when `storedBytes < rawBytes`; codec 0 requires equality. Decoders
must reject trailing/unfinished DEFLATE data, output overruns, unknown codecs,
invalid counts, bad CRCs, and out-of-file ranges. Chunk data is four-aligned.
Directory cache, compressed input, decoder state and decoded chunks are bounded;
the complete directory must not be mandatory resident memory. The runtime may
cache guide arrays and a few pages/chunks. Audio pumping occurs after each
successfully decoded chunk, including large virtual reads spanning many chunks.

`tools/repack_resources.py` builds from existing audited packages and sparse
TREE v2; it never reads Steam or rebakes art. `--verify-only DOTA2.H1R` reopens
the physical container and checks every logical stream's full CRC and SHA.
