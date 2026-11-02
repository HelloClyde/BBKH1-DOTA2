# v0.30 verification

The installed candidate is BDA SHA-256
`d569b2ae381441ea5af09f61d4b956c97f5077e81a65909734a9d18b43365920`.
Its unchanged unified resource is 112,861,448 bytes, SHA-256
`00ecddaf28bb3716cf9042115498b81e2b01363e66baa3e03b03b914be466153`.

`INDEX.json` binds byte-identical copies of the underlying reports. Earlier
reports retain their original identities and disclosed source differences;
the `*-final` host reports bind the final sources. `candidate-build.json`
contains the complete compiler input inventory and actual ELF ABI/RAM checks.

Final source checks include full resource reconstruction, exact old/new
renderer comparison, sparse-tree canonical reconstruction, actual MIPS resource
and native item-input calls, actual MIPS save persistence, and host gameplay
orders, TP slot, ten-player result/history UI and save failure recovery.

Actual MIPS means the exact compiled BDA payload executed under a bounded CPU
and firmware-service model. These tests do not run the application entry,
physical graphics/audio, H1 scheduler or USB. The match-storage model uses
explicit completed-game fixtures and a modeled writable filesystem. UI images
use deterministic test records and are not played H1 matches or user saves.

Mass-storage receipts prove installation and SHA-256 file readback, including
the final feature candidate. The old runtime files and v0.21 backup were
removed at the user's request. The 112.9 MB resource was retained for the later
feature-program update and was completely rehashed before copying the BDA.

**Physical game runtime acceptance remains pending H1Debug USB.** No v0.30
physical game FPS, uninterrupted audio or successful physical gameplay is
claimed. No release pin, GitHub update or formal installation ZIP has been
created for this candidate yet. The current status is also recorded in
`docs/verification/v0.30-h1.json`.
