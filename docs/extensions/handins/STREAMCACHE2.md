# STREAMCACHE2: a stream-cache hit touches nothing of the package (2026-10-24)

Lane `t-notouch`, branch `cache-round2/integrate` (base `6fd873764`). The existing row
`ext.stream_cache` (class A, settings key unchanged, no new row): what a hit serves is byte for byte
what it served before. Follows STREAMCACHE.md (sections 3 and 8) and the verification note
`v-streamtouch` (scratchpad `verify/v-streamtouch.md`).

**Ruling (the user, 2026-10-24).** A stream-cache hit must not touch the package file at all. The
cache trusts package identity (file size, header crc field, file count, name, every entry's offset and
size, plus mtime) instead of re-reading each entry's block directory from the package mapping.
Accepted downside: an in-place edit that preserves all of those is served stale until Clear cache
(already true before for an edit that keeps the block sizes).

## 1. The header crc field: a content crc

What the mod kit writes into the package header's crc field (`PackageFile::_crc`, +0xA8, read at
`package_file_read.cpp:505`). The official kit (`ToolKit/bin/x64/Tools-x64.exe` 1.0.7, `/build` then
`/mod`, the `ext_resources.sh` path conventions) built a one-string-table mod three times
(scratchpad `notouch/kitcrc.sh`, headers read by `notouch/pkghdr.py`):

| build | the string | .pkm size | header crc | whole-file CRC-32 |
|---|---|---|---|---|
| a1 | `"Alpha"` | 602 | `4c126ee2` | `b0aa27b6` |
| a2 | `"Alpha"` (rebuilt 2 s later) | 602 | `4c126ee2` | `b0aa27b6` |
| b1 | `"Blpha"` (one byte changed) | 602 | `3b8cbc12` | `5fc10f74` |

**Content crc**: deterministic (a rebuild of the same content is byte-identical, crc included: no
timestamp or random seed in it) and it changes with a one-byte change of a packed resource. It is not
a plain CRC-32 of the file, of the payload span `[4, header)`, of the compressed or decompressed header
blob, nor of the loose compiled files (`crcprobe.py`, `crcprobe2.py`: no match), so the exact formula
(a seeded or chained CRC over the packed entries, most likely) is not identified; what matters for the
ruling is shown: it is a function of the packed content. The shipped packages agree: data0 `3f070e50`,
data1 `5ac8fb0b`, patch0 `9886ff33`, patch1 `cacd090a`, MegaMod1..7 `a2a077ae 85856f6a df769ff1
d5916a45 5dccf820 54c2f019 e6f7763f` (all distinct, no pattern). The engine never recomputes it
(`filesystem.cpp:295` and `modification_refresh.cpp:622` use it as an id).

**Residual risk.** A content edit that keeps the file size, every entry's offset and size, the name and
the version fields AND lands on the same crc: a CRC-32 collision (2^-32 for an arbitrary edit) -- and
with the mtime in the identity, the edit must also restore the last write time. A tool that rewrites
the package in place without updating the crc field (no kit does) is the one way to be served stale.

## 2. Design

**The record carries the directory** (`stream_record.h`, chunk format 3). Per entry: offset (u64),
size, decompressed total, segment count, then the entry's first 4 + 8n bytes exactly as the package
holds them (block count n and the n `{sourceOffset, compressedSize, uncompressedSize}` block headers),
then the segment table and the bytes. A directory adds 4 + 8n bytes per record: the MegaMod set's
362,106 records grow 150.4 -> 154.7 MB. `Bundle::AddChunk` refuses a record whose directory does not
fit its entry or does not describe its recorded total (`DirectoryTotal`).

**The key.** Within a package a record is found by (entry offset, size) -- the per-entry `Hash64` of
the directory and the block count are gone from the key. The package identity (`PackageIdentity`,
XXH64) is { header crc, version, user version, entry count, file size, last write time } + name + every
entry's { offset, size }, formed ONCE per package (per `PackageFile` the FileSystem mounts, as before)
from what the engine already holds plus two queries on the package's open handle
(`Platform::FileSize64`, `Platform::FileLastWrite`). The derived-cache key's `sourceHash` is that
identity, so a replaced or touched package is another key: its records are not read back and are
recorded again in one cold run.

**A hit reads nothing of the package.** `LoadBegin` (stream_cache.cpp): packed-entry origin (handle
arithmetic), `PackageAt` (the bundle), `Find(offset, size)`, the `SegmentStream`. No `PackedMapSpan`.
create_on_demand's range check over a record stream (`AltLocate`) runs `RangeInStream` on the record's
cached 4 + 8n bytes instead of the mapping (the same bytes, the same function); `AltSkip` never read
the package. The mapping handle is still acquired (no page is touched by that); a skipped payload's
bytes are read from the mapping only when the renderer materialises it (`Redecode`), which is the
point of create_on_demand.

**A miss** (first boot, or an entry not recorded): the count and the directory are read from the
mapping as before (one span of 4 + 8n bytes now, after the package lock is released) and handed to the
recorder, which stores them with the record.

**Stale caches.** Version 2 chunks have another derived-cache key version (`kVersion` =
`kChunkVersion`): they are never asked for, so the first start after this change records once (the USB
prime below: 0 hits, 362,106 records, 154.7 MB in 11 chunks) and the old chunks age out through the cap.
A chunk of the wrong version that did reach `AddChunk` is refused whole (unit test).

**Not changed.** The 14.3k misses' payload reads (the streams over 8 KB) and the fallback path (a read
outside a record opens retail's compressed stream, which reads the directory: counted "extended", 0 in
every run below). A tool that resets the mtime after editing in place is covered only by the crc and
the entry table (section 1).

**Debug shadow.** `BANISHED_STREAMCACHE_SHADOW=1` (harness section of `src/app/options.h`, read once
at boot; any build): on every hit, also read the entry's 4 + 8n bytes from the package mapping and
compare them with the record's copy; the load-end line gains `[StreamCache] shadow (run total): N hits
checked against the package, M mismatches`, and the first 16 mismatches are named.

## 3. Proof

Perf dir (`C:/GOG Games/Banished`, lane lock `t-notouch`), user dir `C:/GOG Games/notouch_user`
(seeded with the real settings.cfg, plus `Save/Gate.sav` for the load route), perf exes; scripts
scratchpad `notouch/ssd_run.sh`, `blobcmp.py`.

- **Debug shadow** (after exe, `BANISHED_STREAMCACHE_SHADOW=1`, warm cache, MegaMod 7-part): boot +
  new game **361,837 hits checked against the package, 0 mismatches**; boot + `Gate.sav` load (world
  built, `[GUL::Load] EXIT`) **362,115 checked, 0 mismatches**. Extended 0, crash log 0 bytes.
- **Per-blob hash** (DECODE's TEMP `blobhash.patch` via `apply_blobhash.py`, applied to before and to
  after, never committed; `BANISHED_MENU_SCENE=0`, new game): MegaMod 7-part, both warm (361,837 hits
  each): **376,649 lines, 919,961,255 bytes, identical in order**. Vanilla (MegaMod off): the
  recording run (2,370 hits, 297 recorded) and the warm run (2,667 hits) of each exe: **5,702 lines,
  15,525,747 bytes, identical in order**, before = after, and recording run = warm run.
- **Resave gate** (`tools/resave_gate/resave_gate.sh Gate`, the after perf exe, mods parked, real user
  dir; faithful profile, so the row is off there): the worktree had no golden, so NEW-GOLDEN with md5
  `971aaec2cf95f5f730bb1cd1a4fa8428` (the known golden), then **PASS** (resave == golden, 23,453,453
  bytes), crash log 0.

- **Unit** (`tools/build.sh unit perf`): 1407 tests, 0 failures, 1 skipped (the kit cross-check). New:
  `ext_stream_cache_record_carries_the_directory` (a real three-block entry's head round-trips byte
  for byte, through widening and consolidation; a directory that disagrees with the total or the
  entry, and a version 2 chunk, are refused), `ext_stream_cache_package_identity` (each of size, mtime,
  crc, version, user version, name, one entry's offset, one entry's size and the entry count changes
  the identity; an identical copy with a new mtime finds nothing, records under its own identity and
  hits at the next start). The 11 existing `ext_stream_cache_*` tests pass on the new format
  (`..._bundle_round_trip_and_identity` now checks the (offset, size) key).

## 4. USB bed (the headline)

`F:/Banished-USB` (exFAT stick), user dir `C:/GOG Games/usb_user`, MegaMod 7-part mounted by move
(`mm.sh`), perf exes before = `6fd873764`, after = `3728df54f`, both with warm derived caches (the
after exe primed once: its cache was in the old format). Cold = `purge.py` (every file on the stick
opened unbuffered); warm = the next run, the stick's pages cached. Autostart new game, Quit from the
pause menu. `[LoadTime]` clock; stick reads from the PhysicalDisk raw counters (`extract.py`, the t-usb
lane's), to menu.opened including the pre-monitor launch. Three interleaved pairs, each pair cold then
warm of one exe then the other, alternating which exe goes first (B A, A B, B A). Scripts: scratchpad
`notouch/usb/` (`series.sh`, `run.sh`, `table.py`).

| run | boot to menu | load 1 | page faults to menu | stick read to menu | busy at launch |
|---|---|---|---|---|---|
| before cold 1 | 136.36 s | 127.98 s | 1009.1k | 2452.0 MB / 68,716 reads | 3 |
| before cold 2 | 120.98 s | 115.84 s | 1039.1k | 2467.2 MB / 69,508 reads | 0 |
| before cold 3 | 120.00 s | 115.03 s | 1039.8k | 2445.4 MB / 69,260 reads | 0 |
| **after cold 1** | **57.76 s** | **52.94 s** | 854.8k | **1123.8 MB / 25,897 reads** | 1 |
| **after cold 2** | **58.73 s** | **52.90 s** | 882.1k | **1122.1 MB / 26,148 reads** | 3 |
| **after cold 3** | **57.37 s** | **52.64 s** | 882.4k | **1122.5 MB / 26,045 reads** | 0 |
| before warm 1 | 12.03 s | 7.22 s | 990.9k | 0.0 MB / 0 | 2 |
| before warm 2 | 13.05 s | 8.70 s | 1015.9k | 4.3 MB / 19 | 0 |
| before warm 3 | 12.29 s | 7.81 s | 1016.3k | 4.0 MB / 10 | 0 |
| after warm 1 | 11.23 s | 6.95 s | 683.0k | 4.4 MB / 74 | 0 |
| after warm 2 | 11.77 s | 7.10 s | 873.0k | 4.8 MB / 30 | 0 |
| after warm 3 | 11.16 s | 6.94 s | 876.6k | 4.4 MB / 54 | 0 |

Means: **cold boot to menu 125.8 s -> 58.0 s (-67.8 s, -54%)**, load 1 119.6 -> 52.8 s; the stick
reads 2,450 MB / 69,000 reads -> 1,117 MB / 25,900 reads to the menu (-1.33 GB, -62% of the reads).
Warm: 12.46 -> 11.39 s (-1.07 s), load 1 7.91 -> 7.00 s, 200k fewer page faults to the menu (the
362k directory touches and the 73k elided entries' re-touches). The new game after the menu is
unchanged (1.51 / 1.56 s cold, 1.44 / 1.38 s warm). Every run: hits 361,837, misses 14,309, extended 0,
read back 90-97 ms, crash log 0 bytes. "Busy at launch" counts compilers and OpenBA processes when the
run started (t-thrash was timing on the SSD perf dir between and sometimes during these runs, so the
machine was NOT quiet; the cold numbers are I/O-bound, the spread of the after-cold runs is 1.4 s).

What is left of a cold boot (58 s, 1.1 GB from the stick): the 14.3k misses' compressed streams
(picking and edge meshes, sounds, large descriptions: their payload pages), the package directories
at mount, the exe and the remaining loose data.

## 5. SSD (perf dir, warm, must not regress)

MegaMod 7-part, warm (OS cache and both derived caches; each exe primed once in the fresh user dir:
prime runs 16.70 / 16.62 s to the menu, 361,837 records each), autostart new game, Quit; three
interleaved pairs B A, A B, B A. **Machine load: CLEAN** (0 compilers, 0 other OpenBA at every launch;
t-thrash had released the perf dir, the functional dir was free).

| run | menu.opened | load 1 |
|---|---|---|
| before 1 | 10.97 s | 7.05 s |
| after 1 | 11.01 s | 6.86 s |
| after 2 | 10.61 s | 6.77 s |
| before 2 | 10.97 s | 7.00 s |
| before 3 | 10.85 s | 6.93 s |
| after 3 | 10.61 s | 6.78 s |

Means: menu.opened 10.93 -> 10.74 s (**-0.19 s**), load 1 6.99 -> 6.80 s (-0.19 s; every after run
below every before run on load 1). No regression; inside the expected -0.1 to -0.4 s.

## 6. Known limitation

- An in-place edit of a package that keeps its file size, every entry's offset and size, its name and
  version fields and its header crc, and restores its last write time, is served stale until the user
  clears the cache (Enhanced tab: Clear cache). The crc is the kit's content crc (section 1), so this
  takes a non-kit tool that leaves the crc field alone, or a CRC-32 collision, plus an mtime reset.
- A package touched without a content change (copied, re-downloaded, `touch`) is one cold run: its
  records are recorded again under the new identity (the old ones age out).

## 7. CHANGELOG line (ready to lift)

- `ext.stream_cache` (on): a boot no longer reads anything of a package for the resources it serves
  from the cache; the cache trusts the package's size, date, crc and file table instead. MegaMod from a
  USB stick, cold: boot to the menu 126 s down to 58 s (warm 12.5 s down to 11.4 s). The cache rebuilds
  once after this update.
