# HEAPSLACK hand-in: ext.heap_slack (cache round 2, lane t-thrash, 2026-10-24)

Branch based on `cache-round2/integrate` `6fd873764`. Class A, next start, **default ON**.

| Commit | What |
|---|---|
| `ext.heap_slack: keep decommitted top-of-range pages ...` | the seam, the extension, the unit test |
| `ext.heap_slack: hand-in, LIST.MD row` | this file, the LIST.MD row, the registry row's default-on comment |

## 1. What the time was

Retail's allocator decommits the top of a range every time a free coalesces to the range's end
(`ReleaseToPage`, 0x1800c7c80, called from `Free`) and commits it again at the next carve past the
mark (`CommitToPage`, 0x1800c7bd0, and `Allocate`'s inlined copy). A stream open / close is a 32 KB
window allocated and freed at the top of the heap, so a load that opens hundreds of thousands of
streams does hundreds of thousands of VirtualFree / VirtualAlloc pairs over the same addresses, and
every recommitted page is then soft-faulted and zeroed by the kernel. The census (scratchpad
`census/REPORT.md`, faithful profile, debug) saw 325k decommits / 15.4 GB on a MegaMod boot.

## 2. Design

`MemoryRange::committed` stays retail's mark, stored exactly as retail stores it: the heap's own
bytes and bookkeeping are unchanged. The extension keeps a second, *real* mark per range (a side
table keyed by the range base, 256 entries; a range past it is passed through as retail's).

- **decommit** (retail lowers the mark from `have` to `need`): the pages stay committed while the
  total slack over all ranges, `sum(real - faithful)`, fits `kBudgetBytes` (64 MB); past it the range
  keeps what still fits and the rest is decommitted.
- **commit** (retail raises the mark from `have` to `need`): the part of `[have, need)` that is really
  still committed is **zeroed with memset**, exactly the pages retail receives fresh (zero) from the
  kernel; the part past the real mark is committed by the platform, as retail does.
- Nothing below retail's mark is ever touched, so a freed block reused without a decommit keeps its
  stale bytes exactly as in retail (XFILEAHEAD.md section 3: retail reads uninitialised heap, so a
  lazy release that handed back stale pages would NOT have been neutral).

Seam: `System::MemoryAllocator::_extCommitSeam` (static, null by default, DESIGN.md hook rule 5),
consulted only inside the three `if (have <= need ...)` / `if (need < committed)` arms, i.e. only
when retail calls the platform. Set once at boot by `Ext::HeapSlack::Sync` through
`RtSystemAllocatorSetCommitSeam`, which stores it under the live allocator's lock. Every seam call
runs under the allocator lock (the raw members' callers hold it). A range committed before the seam
is installed is taken over at its next mark move (its real mark is its faithful one until then).

POSIX twins: unchanged. The seam sits above `g_plat`, and both `allocator_posix.cpp` (MADV_DONTNEED +
PROT_NONE) and `allocator_darwin.cpp` (MAP_FIXED remap) already give zero pages on recommit, so the
memset reproduces the same contract on all three. Built and run: Windows only. Linux / macOS were
not built (the new code is standard C++ in `src/ext` plus three null tests in `allocator.cpp`).

**Is memset cheaper than the kernel?** Measured, not assumed: on the cold MegaMod boot the kernel
path spends 139 ms in commit + 271 ms in decommit syscalls plus a soft fault per recommitted page
(5.3 GB = 1.3 M pages); the extension zeroes the same 5.3 GB in 21 ms (the slack peaks at 3 MB, so
the pages are cache-hot) and spends 40 ms in the commits that remain. The 64 MB budget was never
reached in any run (peak slack 3.2 MB MegaMod, 7.1 MB vanilla / stress).

## 3. Exactness

| check | result |
|---|---|
| unit `ext_heap_slack_every_handed_byte_matches_retail` | the real allocator over the real platform, a 6,000-step seeded alloc / free script (sizes 1 B .. 4 MB, aligns 4/8/16, frees biased to the top, two 8 MB ranges): every block hashed when handed out, before the script writes it, then filled with a pattern. Trace (range, offset, size, hash) and every range's final retail mark identical to the seam-null run at budgets 64 MB, 1 MB, 3 pages and 0; budget 0 reaches the platform on every call. **Mutation check**: with the memset removed the test fails at all three non-zero budgets |
| unit `ext_heap_slack_kept_pages_read_zero_when_retail_recommits` | write, free, reallocate a 512 KB block at a range top: no decommit reached the platform, and every byte past the range's first page reads zero |
| per-blob hash (`blobhash.patch` of DECODE, re-applied), MegaMod 7-part boot to menu, off vs on | 376,439 lines each, **files byte-identical in order** |
| resave gate, faithful (standard) | **PASS** (resave == golden, 23,453,453 bytes; the row is off under /profile:faithful) |
| resave gate, per-entry profile, every row 0 except `ext.heap_slack=1` | resave image **byte-identical** to the same run with `ext.heap_slack=0` (hash b9fe1e948b186c3c both). Both differ from the faithful golden by a 296-byte tail: that is the per-entry profile itself (heap_slack=0 gives the same image), not this row |
| unit total | 1,407 tests, 0 failures, 1 skipped (the kit cross-check, no kit output) |

## 4. Counters (perf exe, the user's settings + mods parked / mounted, `newgame` route, one `[HeapSlack]` line at exit)

OFF = the same exe with the budget forced to 0 (scratch build: every call reaches the platform, counted
and timed). Vanilla workloads vary per new game (the world is random).

| run | retail commits / bytes | retail decommits / bytes | platform commits | platform decommits | zeroed | memset ms | commit ms | decommit ms |
|---|---|---|---|---|---|---|---|---|
| vanilla OFF | 9,588 / 280 MB | 2,522 / 150 MB | 9,588 | 2,522 | 0 | 0 | 12.3 | 12.1 |
| vanilla ON | 7,406 / 155 MB | 142 / 24.7 MB | 7,216 | 0 | 24.2 MB | 0.5 | 5.1 | 0 |
| MegaMod warm OFF | 95,625 / 1.34 GB | 13,228 / 157 MB | 95,625 | 13,228 | 0 | 0 | 78.9 | 26.0 |
| MegaMod warm ON | 95,617 / 1.35 GB | 12,633 / 155 MB | 80,106 | 0 | 154 MB | 1.3 | 57.2 | 0 |
| MegaMod cold OFF | 226,021 / 6.50 GB | 163,140 / 5.31 GB | 226,021 | 163,140 | 0 | 0 | 138.8 | 270.6 |
| MegaMod cold ON | 224,426 / 6.48 GB | 162,142 / 5.29 GB | 58,474 | 0 | 5.29 GB | 21.0 | 39.6 | 0 |
| stress town ON | 42,398 / 3.78 GB | 32,442 / 3.66 GB | 7,924 | 0 | 3.66 GB | 28.1 | 5.8 | 0 |

"cold" = `<user dir>/Cache` removed before the run (the first start after a mod change); "warm" = the
derived cache present. The thrash of the census lives in the cold start.

## 5. Timings

Perf exe, MegaMod 7-part, user dir `C:/GOG Games/thrash_user` (the real settings.cfg). `[LoadTime]` clock.
**Machine load: clean at every launch** (0 other OpenBA.exe, 0 compilers, checked before each run; the
batch waited up to 8 min for other lanes' runs to finish before some launches; another lane may have
started a run during one, see on2 below). Scripts: scratchpad `t-thrash/batch_ab_mm.sh`,
`batch_cold.sh`, `stress1.sh`.

MegaMod warm, order off1 on1 on2 off2 off3 on3:

| | menu.opened s | menu -> game.update.first s |
|---|---|---|
| off | 11.42 / 11.42 / 10.72 (mean 11.19) | 2.51 / 2.43 / 2.49 (2.48) |
| on | 11.30 / 10.84 / 11.30 (mean 11.15) | 2.57 / 3.30 / 2.50 (2.79; 2.54 without the on2 outlier, wall 41.9 s vs ~32) |

Neutral (within noise).

MegaMod cold, order off1 on1 on2 off2 off3 on3:

| | menu.opened s |
|---|---|
| off | 15.79 / 15.76 / 15.71 (mean **15.75**) |
| on | 14.36 / 14.26 / 14.43 (mean **14.35**) |

**-1.40 s (-8.9%)**, every on run below every off run. New-game load unchanged (2.39-2.43 vs 2.41-2.44).

Stress town (`STRESS_EXT=heap_slack` vs `STRESS_EXT=none` + `heap_slack=0`, per-entry profile), order off1
on1 on2 off2 off3 on3: sim ms / iteration 0.15 in all six (vs baseline: off -0.7 / +1.7 / -1.2 %, on -0.3 /
-0.5 / -0.1 %); update ms p95 off 1.95 / 2.01 / 1.92, on 1.86 / 2.00 / 1.92. Neutral (the stress town's
thrash is in its world build, outside the sim measurement).

## 6. Default

ON: every exactness check passes, the cold MegaMod start gains 1.4 s, warm starts and the stress town
are neutral, and the slack costs at most 64 MB of commit (observed peak 7 MB).

## 7. Open

- The budget is a constant (64 MB global); with the observed peak at 7 MB it never binds. Not a setting.
- Not built here: Linux, macOS (CI builds both; the change touches no platform twin).
- The per-entry profile's 296-byte resave tail with every row off (section 3) predates this row; it is
  the route HITCH_SAVE used for its own extension leg.
