# MENUENTRY -- the mod scan and the package remount on every menu entry (issue #13)

Block MENUENTRY (caching round 2, item B3), 2026-10-23, branch `worktree-agent-a78308657a84b3313`, based on
`main` 62384a4df. One class A row, `ext.menu_entry` ("Quick return to the menu"), default ON. Code:
`src/ext/menu_entry/`, seams in `src/engine/io/` (RefreshList, SetupFileSystem, ClosePackages).

## 1. What was slow

Entering a menu loop from any other loop (`Engine::Loader::Load`, loops.cpp:402-411: the boot's
menu, every return from a game or a save) runs `Modification::RefreshList` and sets `_dirty`, so the
menu loop's own `Loader::Load` remounts every package (`Modification::SetupFileSystem`). Retail does
both every time; skipping either is an extension. Measured on the seven-part MegaMod (perf exe):

| step | cost |
|---|---|
| RefreshList loop 1 (ReadPackfile of each .pkm: header, icon, preview, entry table; the path-set inserts) | 450-490 ms |
| RefreshList loop 2 (the collision pass over every pair) | 3 ms |
| remount at the boot's menu entry | 1.05-1.19 s |
| remount at a return from a game | 0.98-1.05 s |
| unmodded remount | 7 ms |

Loop 2 is cheap with MegaMod (the parts share keys early), so loop 1 is the scan's cost.

## 2. As built

**(a) The mod scan.** Two seam pairs on `IO::Modification` (null by default and under the faithful
profile):
- loop 1, per .pkm found: `_extModCached(file, Mod&)` answers `kModRead`, `kModFilled` or `kModSkipped`;
  `_extModStored(file, record)` hands over the record a read built (null for a file retail discards:
  unreadable, or another platform's). The record is the stack `Mod` after the path-set loop and before
  the settings part: enabled 0, order 0, `files` empty. The extension keys it by the .pkm path's
  characters and its stamp (size and last write time, read through a handle opened on the path; stamped
  BEFORE the read, so a file changed during the read is read again next time). A hit copies the record
  into the stack `Mod` with `Mod::operator=` (0x180026ef0) and keeps `file` the FindFiles one. Retail's
  settings part (ModEnabled_ / ModIndex_ from settings.cfg) and the append run unchanged on both paths:
  they were moved verbatim into a lambda (`applySettings`) that the read path calls where the lines stood,
  before `~PackageFile`.
- loop 2: `_extOverridesRestore` rebuilds each mod's `files` when the found mods' stamps, in found order,
  equal the key of the kept answer (indices j per mod i, appended with the loop's own
  `RtColl_ArraySetSizeFill_180026880(&mods[i].files, ..., &mods[j].file)` in the loop's order);
  `_extOverridesBuilt` keeps the answer after the loop ran. Loop 2 runs before the sort and reads neither
  enabled nor order, so a reorder in the Mods dialog keeps the answer.
- Records of files not found by a scan are dropped. The boot's first scan (Engine::Startup) runs before
  the registry; `Sync` primes the records from that scan's mods (enabled and order back to 0, `files`
  freed with `DestroyAndFree`, stamped at Sync), so the boot's second scan reads nothing.
- In process only (no derived-cache entry): with the records kept, the decode of the icons is gone
  entirely, and the remaining cost is not decode (below).

**(b) The remount.** `IO::Modification::_extRemountSkip` runs after the `_dirty` test; true skips acts
1-4 and still runs act 2's order stamp (`_mods[i].order = i`, every mod) and act 5's `_dirty = 0`, so
SaveList, ModDialog::Apply (which tests `_dirty`) and the dialog read the same values.
`_extRemountDone(mod, reloads, unloads)` runs at the end of act 5 of a remount that ran (act 3's queue
size and a new ForceUnload count, both NOT RETAIL counters). `IO::FileSystem::_extPackagesClosed` runs
at the end of every ClosePackages; outside a remount it drops the fingerprint.

Fingerprint (`fingerprint.h`), taken at the end of the last completed remount and compared with the one
computed at the next remount's entry:
- `base`: the pack directory's *.pkg, LoadPackages' own FindFiles in listing order (the pack index is
  the position, a byte of every packed FileInfo), each name + size + write time;
- `user`: the user pack list: at the end of a remount `FileSystem::_userPackFilenames` as mounted; at
  the entry act 2's list from the mods (enabled, last to first: the bubbled order a save load leaves),
  each with its stamp;
- `mounted`: the live `_packfiles`: object address, FileInfo handle word and OS handle, name, entry
  count, priority, CRC, override byte;
- the gates `_existsInPackfileGate` / `_platformExistsGate`;
- salt: `fix.large_packages`, `ext.package_cache`.
Any difference runs today's remount and logs which part changed (`[MenuEntry] remount runs: user
changed`). The expand memo has had no clear since PATHINDEX (`ExpandMemoClear` has no caller) and does
not depend on the packages; the fold index stays populated across a skip, correct only because the
tree it indexes is untouched: the debug exe checks every node (section 3).

Save load: `saveload_update.cpp:685/745` set `_dirty` and re-enable/bubble the save's mods; the loader
loop's `Loader::Load` (loops.cpp:420) goes through the same seam. A save whose list differs from the
current one remounts (shown below); a save whose list matches is skipped (not exercised: no MegaMod save
was at hand). The Mods-dialog Cancel path leaves `_dirty` set (moddialog.cpp:612/645); the next loop
switch goes through the fingerprint.

Class A: nothing is saved, no setting changes a mod, the save's mod list is written from `_mods`,
which a skip does not touch beyond the order stamp retail writes too.

## 3. Evidence

Unit (`tests/unit/ext_menu_entry_test.cpp`, 7 cases; suite 1390 tests, 0 failures, 1 skipped, not
this block's): user pack order; fingerprint equal for the same install; not equal for a reorder, an
enable toggle (on or off), a size change, a write-time change, a rebuilt *.pkg, an added enabled .pkm,
an added *.pkg, a removed mod file, another `_packfiles` object, a gate, either row; EQUAL for an added
.pkm left disabled and a changed disabled .pkm (act 2 never mounts it; exact).

Debug shadow (the debug exe never skips; `OPENBA_PATHINDEX_CHECK`, the existing debug-exe check
constant of PATHINDEX / PKGCACHE, so no new flag): when the fingerprint says skip it runs acts 1-4 and
compares the package state before and after with PKGCACHE's harness (`KeyBytes`: every package's entry
names in order and its override byte; `Encode`: the directory tree node for node, values, balance,
height, shape), the `_packfiles` summary (name, entries, priority, CRC, version, override byte), the
user pack list, act 3's reload queue and ForceUnload count, and checks the fold index answers every node
of the live tree. The scan is shadowed too: a kept record is not served, the file is read and the read's
record compared member by member (strings, ints, icon, preview, file, path set by shape, keys, balance,
height); loop 2 runs and is compared with the kept answer. MegaMod, boot + 2 x (new game, quit to menu),
two runs: 6 shadowed remounts (both runs) and 3 shadowed scans (the second run, after the scan shadow
was added), all:
```
[MenuEntry] shadow: the remount would be skipped; running it (fold index 376953 of 376953 answered, 0 wrong)
[MenuEntry] shadow SAME: 11 packages, 376953 index nodes, entries same, tree same, _packfiles same, user packs same, reloads 0, unloads 0
[MenuEntry] shadow SAME: 7 kept records read again, 7 same, 0 differ
... collisions computed; shadow SAME as the kept answer
```
0 `MISMATCH` lines in the run (PKGCACHE's `[Snapshot]` / `[CompiledReuse]` and PATHINDEX's checks
included). The first scan's records compared SAME are the primed ones (section 2), which proves the
priming.

Routes (perf exe, MegaMod, `ext.menu_entry=1`):
| route | result |
|---|---|
| boot to menu, quit | boot scan 2: 7 kept, 0 read; menu remount skipped |
| 2 x (new game, pause-menu quit to menu) | each return: 7 kept, 0 read, collisions from the last scan; remount skipped |
| Mods dialog, harness toggle (1000) + OK | skipped, correctly: the harness click does not change the checkbox, so `Enable(i, checked)` rewrites the same value (the shadow of this case is the same as any skip) |
| Mods dialog, move to top (4003) + OK | reload: `remount runs: user changed`, then the menu entry after it skipped |
| Mods dialog, move to top (4003) + Cancel | rescan from the records; no loop switch before quit |
| added a disabled .pkm (hard link) between two returns | 8 mods, 7 kept, 1 read; remount skipped (exact: not mounted) |
| removed it and touched MegaMod3.pkm between the next two | 7 mods, 6 kept, 1 read; `remount runs: user changed` |
| load Gate.sav (no mods) with MegaMod on | load remount runs (user changed), the return to the menu remount runs (user changed) |
| unmodded, 2 x (new game, quit to menu) | each remount skipped (7 ms -> 0.3 ms) |

The Gate.sav route then CRASHES inside the return-to-menu remount (act 4 reload, Filename copy under
`SerializeExternal<GraphicsMesh>`), with the row OFF as well (`load_gate_off_1`: same address
OpenBA+0x20A36B, same stack). Pre-existing and independent of this block (the remount there runs in full
either way); for the coordinator, not chased here.

Gate: `tools/resave_gate/resave_gate.sh Gate` (debug exe, faithful profile, mods parked): **PASS**
(resave == golden, 23453453 bytes, md5 971aaec2cf95f5f730bb1cd1a4fa8428, the md5 committed in
`docs/runtime_port/resave_gate/golden/`). `tools/acceptance/acceptance.sh`: 0 lines off target.
`tools/build.sh run` (the faithful by-hand gate) was not run by this block: the seams are null under the
faithful profile.

## 4. Timings

Perf exe, MegaMod 7-part, perf game dir with a private user dir. **Machine load: NOT clean (other
agents building and running throughout). Indicative only; the coordinator re-times.**

| | row off | row on |
|---|---|---|
| scan at a return from a game | 474-478 ms | 151-184 ms (records copied 46-55 ms) |
| remount at a return from a game | 0.98-1.05 s | 0.4-0.6 ms (fingerprint) |
| `loader.mount GameLoadDialogLoopT` -> `menu.opened`, return from a game | 2.32 s | 1.01 s |
| boot: scan 2 + remount 2 | 0.49 s + 1.05-1.19 s | 0.16 s + 0.5 ms |
| unmodded remount | 7 ms | 0.3 ms |

About 1.3 s per return to the menu and 1.2-1.5 s per boot with MegaMod. The scan's remaining ~160 ms is
the kept record's copy into the stack Mod (~50 ms, a clone of ~57,000 path-set nodes per part), retail's
own append (`SetSize`, a second clone), the stack Mod's destruction and the settings part.

## 5. Not done

- The record copy could hand the kept path set to the stack Mod and take it back after the append (one
  clone fewer, ~50-70 ms per scan); it needs a third seam after the append. Not built.
- A matching modded save (skip on save load) was not exercised; only the differing-list case was.
- No 2000 Hz loader profile was taken (the round's prerequisite note is for the loader items; this
  block's costs are main-thread and stamped).
- No derived-cache entry: the icon decode is no longer paid at all after the first scan of a run.
