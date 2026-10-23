# STREAMCACHE: the loader's resource streams from the derived cache (issue #13, 2026-10-23)

> Branch `issue13/streamcache`, based on `issue13/lazy` (`6892deb79`). Windows only (perf + debug builds,
> unit, the gate, the resave gate, the MegaMod runs below). Not built: Linux, macOS (the code is
> backend-neutral standard C++ over the engine's IO classes; the Metal backend's Locate goes through the
> same seam). Three parts, three commits; the user ruled the shape (item 1 as option b: a bundle of the
> SMALL resources only; geometry and images stay compressed and gain the head / tail records).

## 1. What was measured (branch `issue13/bootprof`, TEMP, never merged)

A MegaMod 7-part boot with ext.create_on_demand on, perf exe, 2000 Hz sampler, two runs: menu at 18.9 /
18.6 s. Load 1 (the root Resources list + 7 mod lists) is 10.0 s on the loader thread for 376,107
`External::Load`s: 482,948 block decodes, 2.9 GB compressed in, 4.7 GB out, with 0 static GPU payloads
read. Per resource TYPE (exclusive ms of the 10.0 s): GraphicsMesh 2,093, ImageBuffer 1,646, PickingMesh
1,442, EdgeMesh 1,285 (98k loads, 4.63 GB decoded: 2.3 blocks per object, the header block and the
block each skipped payload ends in), ~40 *Description / UIConfig / Toolbar types 2,962 (247k loads of
0.2..2 KB, ~12 us each, 50 MB decoded in all: pure per-stream overhead). Of the loader thread's samples:
the block decode 32-34% (two thirds of it the FIRST block of every stream, read by the BinaryReader's
constructor; the rest the tail blocks after skipped textures and buffers), `CompressedStream::Open`
15% (`SetWindowCount`: a 32 KB window allocated and zeroed per stream), the allocator 18%, path /
string work 16.5%, ext.decode_ahead's hook 12.4% inclusive of which 9.3% WAITING for its own block
(19% of the blocks served). The sample files and the per-phase tables are listed in the report to the
lead (scratchpad `bootprof/`).

## 2. Part A: ext.decode_ahead stays off while ext.create_on_demand is on (`1c3f6efc7`)

With the payloads skipped a stream is read in its header block and in the block each skipped payload
ends in; the kAhead blocks queued at the header are mostly never wanted, and the one that is wanted
waits in the queue behind them. `DecodeAhead::Sync` leaves the seam null when both rows are on and says
so in one `[DecodeAhead]` boot line. Exact: decode_ahead never changes a byte, only where a block is
decoded. 3x3 interleaved A/B of menu time (section 6).

## 3. Parts B + C: ext.stream_cache (class A, next start, default ON)

One mechanism for both: a RECORD of what a load consumed. `CompressedStream::_extReadTap` sees every
read of the retail compressed stream (Read, and ReadArray's batched copy as the m reads it stands for);
the extension notes, per load, the ranges `[pos, pos + n)` of the DECOMPRESSED stream in read order with
their bytes, adjacent reads merged. A load whose reads all succeeded, never went backwards and consumed
at most `kRecordMax` = 8 KB is stored as a record keyed by the entry's identity {offset in the package,
size, block count, Hash64 of the block directory}. A small description is recorded whole (one segment
covering the total: part C); a mesh or texture whose payloads create_on_demand skipped is recorded as
its header and the size fields between the payloads (part B); CPU geometry (picking / edge meshes,
30..50 KB fully read) and anything else large exceeds the limit and stays on the compressed path. The
limit is a constant with its rationale in `stream_record.h`, not a setting.

**Storage.** Records are grouped per package into CHUNKS (16-byte header + records, 8-byte aligned) and
put in the derived cache (`src/ext/derived_cache/`, content-addressed, class A, never opened under the
faithful profile) at each load bracket's end (`Engine::LoaderLoop::_extLoadDone`, after the
`loader.done` stamp) by the cache's writer thread; one chunk per bracket that recorded something, chunk
ordinals 0, 1, ... under the package's key. The key: producer `STRM`, format version, shape {package
CRC, create_on_demand on/off (what a load consumes depends on it), the build's git hash (a record is
only as good as the serializer that made it), chunk ordinal}, sourceSize = the package's entry count,
sourceHash = the package IDENTITY: Hash64 over {CRC, version, user version, entry count}, the package
name and its whole entry table (offset + size of every entry), so a rebuilt package at the same path is
another identity. A package's chunks are loaded at its first load of the run (ordinals from 0 until the
first miss) into a `Bundle` indexed by entry offset; identities survive the two remounts (the pack
index -> PackageFile pointer is re-derived when the FileSystem's table changes).

**Serving.** `Object::ExternalFactory::_extLoadBegin` runs in `External::Load` with the packed entry's
FileStream open and before `CompressedStream::Open`: for a packed entry on a mapped package it reads
the block directory from the mapping (`PackedMapSpan`), forms the identity and asks the bundle. A hit
returns a `SegmentStream` (an IO::Stream in the decompressed stream's coordinates, backed by the
record) that the Serializer and the object's Serialize read instead of the compressed stream: no
window, no directory read into the stream, no decode. A read outside the record fails (false, nothing
copied) and marks the stream; `_extLoadEnd` counts and reports it ("stale") -- it cannot happen for a
record made by the same build (the key), and the counter is the canary. A miss returns null: retail's
`CompressedStream::Open` and read follow unchanged, tapped. Loose files, nested packages, memory
streams: null, nothing recorded.

**create_on_demand over a record stream.** `CreateOnDemand::Locate` asks `g_altLocate` when the stream
is not a CompressedStream, `Skip` asks `g_altSkip` first: the extension answers with the same Locator
the compressed stream gives (the package mapping from `Maps().Acquire(handle)`, the entry offset / size
the stream carries, the decompressed position, the length), checked with `RangeInStream` against the
mapped entry's own block directory, and advances the record stream's position past the payload. The
coordinate system is the original decompressed stream's throughout.

**Threads.** A thread keeps a stack of the loads it is inside (loads nest through ExternalPtr); the tap
attributes a read to the top. One mutex over the package table, the bundles and the pending records.

**Exactness.** The record is the bytes the serializer itself consumed in order; the record stream
answers the same reads with the same bytes at the same positions. WHICH resources load and in what
order is the serializer's business and unchanged (the save's reference table cannot move). Proof:
section 5.

## 4. Files

`src/ext/stream_cache/{stream_record,segment_stream,stream_cache}.{h,cpp}`; seams in
`object/External.h` + `external_load.cpp`, `io/Stream.h` + `compressed_stream_read.cpp`,
`engine/UpdateLoop.h` + `loops.cpp`, `render/common/create_on_demand/stream_locate.{h,cpp}`; the
registry row (`registry.{h,cpp}`, `registry_boot.cpp`), `tests/unit/ext_stream_cache_test.cpp` (+
`sources.cmake`), DESIGN.md's Names table, LIST.MD.

## 5. Proof

- **Shadow run** (TEMP, not committed: on every hit the retail CompressedStream over the same entry was
  opened too and read in step -- every Read of the record stream compared byte for byte with retail's
  Read of the same count, Skip and SetCurrentPos mirrored, the end positions compared). A MegaMod boot
  to a new game, perf exe, warm records: **83,806,898 reads, 0 mismatches, 0 end-position differences**,
  0 stale records, crash log 0. 362,106 hits (288,722 whole-stream, 73,384 elided), 14,315 misses (all
  streams over the 8 KB limit).
- **Unit** (`tools/build.sh unit perf`): 1359 tests, 0 failures, 1 skipped (the kit cross-check); 4
  `ext_stream_cache_*`: a recording merges adjacent reads, keeps gaps as segments and answers exactly the
  recorded ranges (a read across a gap or past a segment fails); a backwards read, a failed read, an
  empty recording or more than kRecordMax bytes drop the record; a chunk round-trips through a Bundle,
  is found by its full identity only (offset AND size AND block count AND directory hash), a second
  chunk adds to it; a bad magic / count / truncation / segments out of order refuse the whole chunk.
- **Gate** (`tools/build.sh run`, debug exe, `/profile:faithful`, mods parked): the world built
  (`[GUL::Load] EXIT`), 99 s in the town, crash log 0 bytes, the seams null (no `[StreamCache]` line). **Resave gate**
  `Gate`: NEW-GOLDEN with md5 `971aaec2cf95f5f730bb1cd1a4fa8428` -- the golden the brief names -- then
  **PASS**: the save and its reference table are byte-identical.
- A stale record (a different serializer reading the same bytes) is keyed out by the build hash; a read
  outside a record fails loudly and is counted ("stale") in the load-end line.

## 6. Timings

Perf exe, MegaMod 7-part, create_on_demand on, the research branch's warm BC4 cache, `[LoadTime]`
clock (menu.opened). **Machine load: NOT clean** -- four other agents were building and running on this
machine; the compiler count at launch is given per run. Script: scratchpad `streamcache/run.sh`.

**Part A**, 3x3 interleaved, A = decode_ahead hook on (lazy HEAD), B = the hook null (this branch):

| pair | A menu.opened / load 1 | B menu.opened / load 1 | compilers at launch (A / B) |
|---|---|---|---|
| 1 | 17.93 / 9.15 | 17.37 / 8.91 | 5 / 3 |
| 2 | 17.22 / 8.99 | 17.08 / 8.68 | 0 / 0 |
| 3 | 17.57 / 9.14 | 17.02 / 8.72 | 0 / 0 |

B first in every pair: menu.opened mean 17.57 -> 17.16 s (-0.41), load 1 9.09 -> 8.77 s. (The bootprof
session's single A/B, 2000 Hz sampler on, saw 18.64-18.92 -> 18.29.)

**Parts B + C**, exe D (A + the stream cache), two cold / warm pairs; cold = no records (the recording
run), warm = the records that run wrote:

| run | menu.opened | load 1 | loads 2-4 | remount 2 | compilers |
|---|---|---|---|---|---|
| cold 1 | 21.83 | 12.76 | 0.84 / 0.80 / 0.78 | 1.94 | 6 |
| warm 1 | 16.38 | 7.78 | 0.87 / 0.79 / 0.78 | 1.90 | 4 |
| cold 2 | 21.68 | 12.87 | 0.86 / 0.80 / 0.81 | 1.99 | 8 |
| warm 2 | 17.19 | 8.28 | 0.89 / 0.82 / 0.80 | 1.92 | 6 |

Against B (same session, pairs 2-3 clean): load 1 8.7-8.9 -> 7.8-8.3 s, menu.opened 17.0-17.4 -> 16.4-17.2
s with compilers running in the warm runs. The cold run pays ~+2.9 s on load 1 for the recording (a
copy of every read, 132.7 MB in all) and the chunk writes (the cache's writer thread, after the bracket).
**Cache on disk:** 14 chunks, 150.6 MB for 362,316 records (derived.dat 927.1 -> 1077.6 MB); the same
150 MB stays in memory for the run (the bundles). Loads 2-4 and remount 2 are untouched (they are the
clean-generation visit pass and the package directory work; bootprof report items 3-4).

## 7. Open

- The bundles (150 MB) are held for the run; they could be dropped after the boot's last bracket and
  re-read from the cache on demand (a later load that misses records as today).
- A record is keyed on the build's git hash: every new build records again (a cache churn of 150 MB
  per build under the 4 GB cap). A release build could key on the serializer's version instead.
- kRecordMax = 8 KB leaves 14.3k streams (picking / edge meshes, sounds, a few large descriptions) on the
  compressed path; the per-type table says they are the CPU geometry the game needs whole.
- Not built: Linux, macOS. The Metal backend's Locate reaches the same `g_altLocate` seam.
- The record stream takes BinaryReader's per-element array loop (ReadArrayFromWindows is exact-type on
  the compressed streams); a batched copy for it would be the same proof again.
