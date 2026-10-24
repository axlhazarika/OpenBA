# VISITREPLAY -- ext.visit_replay and ext.find_fastpath (issue #13, caching round 2 items B2 and B4)

Block VISITREPLAY, 2026-10-23, branch `worktree-agent-adc8845874bb61b11` (based on main 62384a4df). Two class A
extensions on `ExternalFactory::_resources`, registered per DESIGN.md §3 (rows `ext.visit_replay` and
`ext.find_fastpath`, class A, default ON, read once at boot, never under the faithful profile; Names-table row
"resource-table shortcuts"). Both follow VISITPASS's (a)-(d) rules: the answer is reused only where it is a function
of state that is checked or watched, and the debug exe checks every reuse against the faithful path.

## 1. Census (taken before building, kept as print-only lines)

Two print-only lines were added and stay: `[VisitCensus] <part> <loop> bit= marked= ptrHash= pathHash=` on stderr per
loader bracket part (rows whose clean bit a first touch set; `ptrHash` is an order-free sum over the row pointers,
equal within one run when two passes mark the same rows; the debug exe adds `pathHash` over the row keys, comparable
across runs), and the exit line `[FindCensus] findOrLoad= find= hit= miss= ptrVisit= ptrRead2= ptrRead3=`.

**B2, rows newly marked per loader pass** (perf exe, seven-part MegaMod, warm derived cache):

| bracket (loop) | part | marked | ptrHash |
|---|---|---|---|
| InnerLoaderLoop (the big load) | resources | 19 | (rows are inserted with the bit: no first touches) |
| GameLoadDialogLoopT #1 | resources / load | 375,278 / 9 | 246122a3ea30a8a8 |
| GameLoadDialogLoopT #2 | resources / load | 375,278 / 13 | 246122a3ea30a8a8 |
| MenuDialogLoopT | resources | 375,261 | dca18bf2074fe531 |
| GameLoadDialogLoopT #3 | outside / resources / load | 54 / 375,278 / 12 | 246122a3ea30a8a8 |
| GameUpdateLoop (new game) | resources | 375,514 (375,523 with the menu scene pinned) | |

Every later pass marks the whole table (376,442 rows) again, and the three GameLoadDialogLoopT passes mark the
identical set; the menu loop's set differs by 17 rows. Vanilla, faithful (mods parked): 18 / 11 / 32 / 41 / 31 rows
per menu pass and 2,503 on the new game's pass (2,507 + 922 on a `Gate.sav` load): near zero, as predicted.

**B4, FindOrLoad and pointer reads** (exit line):

| run | FindOrLoad | Find | hit | miss | pointers visited | read, mode 3 | read, mode 2 |
|---|---|---|---|---|---|---|---|
| MegaMod boot + new game | 1,396,728 | 1,396,728 | 1,020,079 | 376,649 | 5,034,571 | **1,021,056** | 0 |
| vanilla new game | 16,039 | 16,039 | 10,345 | 5,694 | 5,849 | 13,547 | 0 |
| vanilla `Gate.sav` load | 206,407 | 206,407 | 200,713 | 5,694 | 8,290 | 13,547 | **183,724** |

1.02M read-mode pointers under MegaMod, well over the ~200k go/no-go line: B4 was built.

The plan's loader prerequisite (one 2000 Hz loader sample on the MegaMod set after stream_cache) is in §6.

## 2. Design

### ext.visit_replay (B2)

The unit is the ROW, not the pass: each row's first-touch visit is recorded as the ordered list of rows that visit
hands to Add's hit arm (directly, not through the visits nested in it), each tagged with whether it came through
Add's descent (FindOrLoad -> Add) or the VISITPASS own-row shortcut. A root External is a row like any other, so
per-root reuse falls out (the loop's own Resources.rsc and each mod's Resources.rsc have their own records), and so
does retail's "stop at a row already carrying the bit": the replay runs the arm itself (ArmMark: OR the bit, the
arm's counters, "was it clear"), and only on a clear bit does it continue into that row's record (or, without one,
its real visit). A pre-marked row with unmarked descendants is therefore never descended into, a pin bit or an
earlier root's marks are honoured exactly, and the rows transitioned are by construction the ones the visit would
transition (not pointer targets: an edge records the row the arm actually ran on).

- Engine side (`external_add.cpp`): the hit arm is split at its branch into `RtObject_ExternalFactory_ArmMark` and
  `RtObject_ExternalFactory_VisitRow`, plus `AddDirect` (Add's test for "the row in hand is the one the descent
  would land on": key == object path character for character, with Add's counters). The seam
  `ExternalFactory::_extVisitReplay` gets `edge` at every arm call, `replay` on a first touch, `visitBegin` /
  `visitEnd` around a real visit, and `event` from the change feed `Object::NotifyResourceEvent`.
- A record is kept only if, from the visit's start to its end: loading enabled and the path stack empty (both
  ends); no FindOrLoad miss and no new row anywhere (a list holds positive answers only, and a positive answer
  survives inserts because the tree never holds two fold-equal keys); no erase and no table drop; every arm call's
  object is the row's own.
- Invalidation, all through `NotifyResourceEvent` (`resource_ext.h`): `Insert` (rtresset::InsertCore, which covers
  FindOrLoad's miss, Add's miss and ext/resources Pin/Publish) and `Miss` spoil records in flight; `Erase`
  (rtresset::Erase: Clean, a remount's sweep) drops the row's own record at once, and the next store access
  settles the sweep: if a surviving record names a freed row the store empties (a sweep frees a row's children
  before or after the row in key order, so this is checked once per sweep, not per erase); `Clear`
  (rtresset::IndexClear) empties it; `Reload` (External::Load(path, existing): OnVideoRestart and the remount's
  act 4) forgets that row's own record (its row, address and key stand, so records naming it stand); `PathWrite`
  (every `_path` write: FindOrLoad's miss, OnVideoRestart, the remount's act 4, ext/resources Pin and SetPath)
  empties the store unless it is a fresh object's first path, the same characters again, or an in-place-rebuilt
  object (path nulled by its in-place constructor) given its own row key back.
- Threads: frames and scratch are per thread; the store is touched under ResLoadGuard. A replay holds the lock
  while it walks records (explicit stack, same depth-first order as the recursion) and lets go of it, after copying
  what is left of the records it is in, around each call that can do real work (a row's real visit, a full Add).
- Store (`edge_store.h`): flat open-addressing table keyed by row pointer, one edge pool, ~8 bytes per edge (1.0M
  edges, 375,561 lists under MegaMod).

### ext.find_fastpath (B4)

Not the stream-record ordinal (unsound, untouched). FindOrLoad's hit arm is answered from a table: request
characters -> the row Find returned, stored by a faithful hit arm (the row holding `found`, from the VISITPASS
pointer index), used only under an empty path stack, retired as a whole by any erase or Clear (a generation).
Under an empty stack `Find(Expand(P))` is a function of P's characters and the key set, and a positive answer
survives inserts, so the cached row is the row Find would return; the hit arm then runs as `if (_loadEnabled)
AddFound(found, row)` (the arm on the row when its key is found's path, else Add itself). Skipped: Expand (memo
hashing, the returned Filename), the private Find (two Filename copies, the identifier split, the descent) and
Add's copies and lookup. The String in a mode-3 read is still read (the stream must advance). Reference mode
(mode 2, save load): a refId -> row table, filled after a faithful hit, retired by SerializeReferenceTable (its own
generation) and by any erase; it skips GetPathForReferenceId's AVL descent and its three Filename copies too. The
visit's own walk does not use the table (`FindOrLoadVisit`: it asks once per row per run, the VISITPASS shortcut
answers every later visit, so remembering its requests only cost; measured +0.2 s on the first menu pass before
this). Table (`request_table.h`): flat, keys copied once into an append-only arena.

## 3. Exactness evidence

**Shadow mode** (debug exe always, `OPENBA_VISITREPLAY_CHECK` / `OPENBA_FINDFAST_CHECK`; for the MegaMod runs forced
on in the perf exe for the run only, as VISITPASS did, and reverted). visit_replay, at every replay not nested in
another: (A) the replay runs and the rows whose bit it set are collected; (B) those bits are cleared again and the
real visit runs, collecting its rows and recording the list afresh; sets and lists compared. find_fastpath: every
answer compared with `Find(Expand(P))` (for an id, `GetPathForReferenceId` first).

| route | replays checked | rows compared | mismatches | lists differing only in the Add tag | find answers checked | mismatches |
|---|---|---|---|---|---|---|
| MegaMod boot + new game (perf, forced) | 224 | 1,500,941 | **0** | 11 | 579,695 | **0** |
| MegaMod + `Gate.sav` load, 3 remounts (perf, forced) | 375 | 758,717 | **0** | 13 | 764,806 (183,386 by id) | **0** |
| vanilla new game (debug) | 41 | 76 | **0** | 0 | 7,033 | **0** |
| vanilla `Gate.sav` load (debug) | 48 | 85 | **0** | 0 | 191,650 | **0** |
| Mods dialog toggle + OK, 2 extra remounts, DebugMenu + betterFields mounted (debug) | 58 | 1,724 | **0** | 0 | 4,271 | **0** |

0 store drops on the MegaMod save load (after the per-row Reload rule; the first version emptied the store 557
times there). The "Add tag only" differences: an edge recorded from the verifying walk (FindOrLoad -> Add) is the
VISITPASS shortcut in a later visit; both run the arm on the same row with the same object; only Add's own
`[ExtAdd] calls/hit` counters see it (the replay counts an Add where a later faithful visit would shortcut). No
`[ExpandMemo]` / `[VisitPass]` MISMATCH line in any run; crash logs 0 bytes, exit 0 everywhere.

**Invariants** (debug exe, menu scene pinned):
- Faithful profile, branch vs base main exe (62384a4df, built from `git archive`), new game: typetable (sorted
  `typeId=` lines) md5 36f80ed8... identical; `[ResSet] Clean` + `[ExtLoad]` lines identical; `[ExtLoad]
  calls=5694 openFileFail=0`.
- Per-entry profile with every fix / extension off, the two rows on vs off (checks compiled out for the run):
  `[VisitCensus]` lines (counts and `pathHash`) identical on the new game and on `Gate.sav`; `[ResSet] Clean`
  visited/evicted and `[ExtLoad]` identical to the digit; `[ExtAdd]` on `Gate.sav` identical to the digit
  (calls=210853 hit=205153 visited=3562 skipped=201591); on the new game visited=2640 identical, skipped varies run
  to run in both arms (off 10697 / 10698 / 10699, on 10692 / 10695 / 10696: the number of gameplay frames before the
  pause-menu quit).
- Perf, MegaMod: the `[VisitCensus]` lines (marked counts) identical on vs off on three new-game pairs and two
  `Gate.sav` pairs.
- Resave gate, functional dir (`C:/GOG Games/Banished-B`): faithful, branch exe PASS twice against its own
  NEW-GOLDEN, and that golden is byte-identical to the base main exe's resave on the same dir (md5
  df13140001e884796643b8e1e1824761; the dir's user data differs from the one 971aaec2... was cut on, base and
  branch agree). Per-entry profile with both rows on (debug exe, shadow checks on): **5/5 PASS** against the base
  exe's per-entry resave (adba6cd57120f971c7d83c3ad22d3521); an earlier no-check build also 5/5.
- Unit (`tools/build.sh unit debug`): 1386 tests, 0 failures, 1 skipped (the kit cross-check, as on main). New:
  `ext_visit_replay_replay_equals_visit_under_premarked_rows` (a model of the arm: replay vs always-visit under
  random bits, pre-marked rows and roots, masks and the call sequence equal), `ext_visit_replay_store_erase_and_settle`,
  `ext_find_fastpath_request_table_matches_a_map`.

## 4. Timings (indicative: machine load: other agents building)

Perf exe, seven-part MegaMod, per-entry profile (the user's settings), warm derived cache, menu scene pinned, new
game autostarted, three interleaved pairs; per-bracket `[LoadTime] loader.run .. loader.done`, ms:

| bracket | off (3 runs) | on (3 runs) |
|---|---|---|
| InnerLoaderLoop (big load) | 7768 / 7745 / 7711 | 7540 / 7417 / 7893 |
| GameLoadDialogLoopT #1 (records) | 526 / 524 / 514 | 593 / 631 / 633 |
| GameLoadDialogLoopT #2 | 199 / 203 / 196 | 35 / 38 / 43 |
| MenuDialogLoopT | 207 / 202 / 199 | 39 / 41 / 44 |
| GameLoadDialogLoopT #3 | 228 / 220 / 233 | 36 / 36 / 37 |
| GameUpdateLoop (new game) | 781 / 792 / 790 | 644 / 656 / 678 |
| **after the big load, summed** | **1941 / 1941 / 1932** | **1347 / 1402 / 1435** (-0.54 s) |
| `game.update.first` | 15795 / 15790 / 15772 | 15256 / 15255 / 15787 |

Save load, `ser.entities` stage: MegaMod `Gate.sav` 243 / 236 -> 126 / 125 ms; vanilla `Gate.sav` 228 / 205 -> 80 / 78
ms (its GameUpdateLoop bracket 361 / 326 -> 199 / 201 ms). The first menu pass pays +70-110 ms for recording 375k
lists; each later pass saves ~160-190 ms. B4's share of the big load (580k answered reads) is inside the run-to-run
noise here (-0.12 s mean, spread 0.5 s): the coordinator's perf-dir A/B decides it. Rows default ON (the registry
test requires class A rows ON unless exempted); if the perf-dir A/B does not show find_fastpath's gain on the boot,
it still carries the save-load gain above.

## 5. What was not done / for the coordinator

- Clean A/B on the perf dir (both rows, and find_fastpath alone on the boot's big load).
- The first pass after each start records; nothing is persisted across runs (in-memory only, as specified).
- `[ExtAdd] calls/hit` under MegaMod with the replay on count an Add where a later faithful visit takes the
  shortcut (the tag-only differences above); visited/skipped, the census and the table are exact.
- MegaMod copies: the functional dir held real copies of the seven parts in `WinData` (not hard links, so
  `megamod.sh off` refused them); they were moved to `WinData/_megamod_parked/` and left there (unmounted).

## 6. Loader profile (the plan's prerequisite)

`BANISHED_SAMPLE=2000`, perf exe of this branch (both rows on), seven-part MegaMod, a fresh user dir whose derived
cache one unsampled boot had just filled (`[StreamCache] hits 361837 ... misses 143`: warm), new game autostarted;
1,184,516 samples, loader thread only, `tools/sample_report.py --thread loader`. The sampled boot ran close to the
unsampled numbers (big load 7.66 s, menu 13.2 s, first game frame 15.65 s). Two of the five sampled attempts hung at
boot (`engine.videomodes`, main thread in an ntdll loader-lock wait: the 2000 Hz suspension, not this change; the
watchdog's exit could not complete either, the harness timeout killed them). Samples and log: session scratchpad
`t-visitpass/logs/samples_mm_warm.txt` (180 MB, not committed) and `samples_mm_warm_err.log`.

Big load (InnerLoaderLoop, 9,152 loader samples), self time: unsymbolized static-CRT copy code at exe+0x420e88
17.8 %, ntdll 11.7 %, ucrtbase 8.8 %, KERNEL32 4.6 %, `Compression::Decompress` 4.1 %, `CompressedStream::ReadArray`
3.2 %, the allocator (`GetFreeTagForSize` + `Allocate` + `UnlinkFreeTag`) 5.4 %, `StreamCache::RecordView::Read` 1.6 %,
`StriCmpAscii` 1.6 %, `FindFastpath::RequestTable::Find` 0.7 %. Inclusive: PickingMesh serialization 61.7 %
(`SerializeUShortArray` 35 %, `SerializeTrianglesBody` 25.5 %), `CompressedStream::ReadArray` 48 %, `MemAlloc` 8.7 %,
`DecompressFromSource` 8.7 %, `String::Release` 6.5 %, `ExternalFactory::Add` 4.7 %. The big load is array decode
and copy (picking meshes first), not path or table work. First menu pass (774 samples): the verifying walk
(`VisitExternal` -> FindOrLoad, ExpandRelativePath 18.5 %, Filename copies and AdjustDelimiters) plus the
recording. New-game bracket (791 samples): `WorldGeometry::BuildTerrain` 77 %, of which erosion 52 %; the
resource pass no longer shows.
