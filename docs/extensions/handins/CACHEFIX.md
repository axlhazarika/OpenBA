# CACHEFIX: six fixes to the derived-data cache (caching round 2, item A)

> Branch `worktree-agent-a871f49a4bed7584d`, based on `main` at `62384a4df`. Six `fix:` commits: one
> per item A1-A5, and one for the coordinator's ruling 4. Built and tested on Windows only (debug unit build and debug exe). Linux and macOS were not
> built: the POSIX twins were written against their neighbours and read carefully, but no compiler saw
> them. Infrastructure, no extension class: `src/ext/derived_cache/` and its Platform seams.

## 0. Facts this rests on (verified before the change)

- `Store::Open` called `Platform::DirCreate` once; `CreateDirectoryW` and `mkdir` make no parents. The
  settings twins had their own private `CreateDirectoryChain` (three copies: win, posix, darwin).
- The lock, `derived.dat` and `derived.idx` were all opened exclusively (`FileOpenExclusive`: share 0 /
  `flock(LOCK_EX)`); the instance that lost the lock got `InUse` and no cache.
- `derived.used` held only the last session's key ids; `Compact(usedOnly)` kept only those. A missing
  `derived.used` made the keep list empty, so an over-cap rewrite dropped every entry.
- Eviction ran only when `_dataEnd > cap` at Open. `WriteOne` refuses any payload that would cross the
  cap, so `_dataEnd` never exceeds it unless the cap is lowered: a full cache never evicted, and new
  content was never stored again. (Found while doing A3; fixed there.)
- `Compact` ran inside `LoadIndex` inside `Open`, synchronously on the caller of `OpenShared` (before
  the menu), and replaced dat then idx with no way to tell a mixed pair from a good one.
- No thread priority or scan hint anywhere in the cache.

## 1. What changed, per item

### A1 `8b4d58fef` parents of the cache folder
- `Platform::EnsureDirChain(const PathChar*)` in `platform_file.h`, ONE body in
  `src/platform/common/dir_chain.cpp` over `DirCreate` + `PathAttributes` (both separators; a
  two-character drive prefix skipped; true when the path is a directory afterwards). It needs no twins:
  the OS work and the `\` -> `/` mapping are already in the `DirCreate` twins.
- The three private `CreateDirectoryChain` copies (settings_win / settings_posix / settings_darwin) are
  deleted; their `WriteFileAtomic` calls `EnsureDirChain` (POSIX: through `Posix::Utf8ToUtf16`).
- `Store::Open` calls it instead of the single `DirCreate`. `CacheDir` was NOT moved (out of scope).

### A2 `4447379e8` read-only second instance
- `Platform::FileOpenWriteShared` (platform_file.h; `file_rw_win.cpp` / `file_rw_posix.cpp`): the
  holder's dat / idx. Windows `GENERIC_READ|GENERIC_WRITE`, `FILE_SHARE_READ|FILE_SHARE_DELETE`,
  `OPEN_ALWAYS`. POSIX: `open(O_RDWR|O_CREAT, 0644)` and no flock.
- `OpenStatus::ReadOnly`: the lock loser opens both files with the existing `FileOpenReadShared`
  (Windows share READ|WRITE|DELETE, so it coexists with the holder's write handle), loads the valid
  index prefix as it is at that moment, serves `Find` through the handle (no mapping: the holder may
  truncate a torn tail under it, which would be SIGBUS on a POSIX mapping), never writes, truncates,
  stamps or rewrites, and starts no writer thread. `Put` / `Flush` / `RequestFlush` / `RequestCompact`
  are no-ops. A clear request is left for the holder. `OpenShared` accepts `ReadOnly`; `InUse` now
  means "held and unreadable".
- On LOCK_SH: not used. A shared flock would have nothing to share with: the writers' exclusion is
  `derived.lock`, the data files carry no lock, and a POSIX reader keeps the inode it opened across the
  holder's rename. RULED by the coordinator: no LOCK_SH.
- **FileReplace with a reader open (the question asked).** Windows 10 19045 REFUSES `MoveFileExW(...,
  MOVEFILE_REPLACE_EXISTING)` onto a file another process holds, FILE_SHARE_DELETE or not (measured:
  unit test `ext_derived_cache_rewrite_with_a_reader_open` prints "kept as it was"). What happens: the
  rewrite deletes its `.new` files and keeps the cache exactly as it was (it used to fall into "the
  over-cap rewrite failed: starting empty" and wipe the cache). The rewrite runs again in a later
  session with no reader. `DeleteFileW` with the reader open DOES work (POSIX delete semantics on this
  build): a clear with a reader open goes through, and the reader keeps reading its old bytes (same
  test file, the second-instance test). POSIX: rename over an open file always works.
  A possible follow-up, not done: `SetFileInformationByHandle(FileRenameInfoEx,
  FILE_RENAME_FLAG_POSIX_SEMANTICS | FILE_RENAME_FLAG_REPLACE_IF_EXISTS)` would let the replace go
  through with a reader open on Windows 10 1607+ NTFS.

### A3 `efa922403` least-recently-used eviction, stamps merged across sessions
- Each holder Open is a session (a counter in `derived.used`'s header, `reserved[0]`). A Find hit or a
  store stamps the entry with the session; earlier stamps are kept. `derived.used` is now 16-byte
  records (KeyId, stamp), written for every live entry that has a stamp; the old 8-byte list fails the
  header's recordSize check and reads as "no stamps".
- Eviction: oldest stamp first, then oldest in the file (no stamp = oldest), until derived.dat is at
  90% of the cap; lower by the bytes that were refused (so they fit), never below half the cap.
- Trigger: past the cap, OR a payload was refused at the cap (this session, or the one before:
  `derived.used` header `reserved[1]` = refused bytes) and the cache is above that target. This is the
  "a full cache never evicted" fix.
- A missing `derived.used` therefore evicts the oldest-written entries, not everything.

### A4 `3553d126b` the rewrite on the writer thread, crash-safe between the two replaces
- **Format version 1 -> 2** (`kFormatVersion`): the dat / idx headers' `reserved[0]` is now a
  generation (the same in both files; every rewrite and every reset makes the next one). A version-1
  cache is reset at the first Open ("unknown or damaged cache format: starting empty"), never misread.
- The rewrite writes `derived.dat.new` / `derived.idx.new` with generation+1, **flushes both**, then
  replaces dat, then idx. Open with differing generations: if `derived.idx.new` carries the data file's
  generation, it finishes the rewrite (replaces idx from it, "finished a rewrite that was interrupted
  between its two files": nothing lost). Otherwise (a pair put together by hand) it keeps the index
  prefix whose payloads match their XXH64, deterministically, and rewrites the index header. Leftover
  `.new` files of a rewrite that never reached its replaces (they can be as large as the cache) are
  deleted at Open.
- `Compact` no longer runs in Open. The writer thread runs it when its queue is empty and the files
  need it (superseded / bad copies, or A3's eviction trigger): at once after `Store::RequestCompact()`
  (called by ext.stream_cache at the boot's last bracket, MenuDialogLoopT), and otherwise once per run,
  10 s after the cache's last Find / Put, once a load's end (`RequestFlush`, sent by CompressAo's and
  StreamCache's load ends) was seen. So with compress_ao alone it still runs after the loads.
- Finds go on during the rewrite: `Find` now holds `_fileMutex` across its lookup and copy (it already
  held it for the copy; lock order `_fileMutex` then `_mutex` everywhere), so the swap is atomic to it;
  the hash check stays outside the locks, and a bad-payload erase is skipped if a rewrite moved the
  records since (`_layout`). Every payload is checked against its record on the way (a bad one is
  dropped). The rewrite gives way (to retry after the next quiet spell) when half the 256 MB put queue
  fills, so it never blocks the loader's `Put`. Close aborts a rewrite in progress.
- If the idx replace fails after the dat replace succeeded, the store turns itself off for the run
  (the next Open finishes the rewrite from derived.idx.new).
- Consequence: an over-cap or full cache runs its first load still full (new payloads refused until the
  rewrite); before, eviction happened before the load. Later loads in the same run have room.
- Crash-injection hook: `SetCrashBetweenReplacesForTest(bool)` (derived_cache.h), used by the unit test.

### A5 `0667cdc61` background mode, sequential scan
- `Platform::ThreadBackgroundMode(bool on)` (platform_thread.h; Windows `SetThreadPriority(
  GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN/_END)`, POSIX a no-op returning false).
- **Deviation from the plan, on purpose:** background mode wraps the REWRITE only, not the whole
  writer. The appends are on the loader's critical path once 256 MB are queued (`Put` waits), and a
  very-low I/O priority writer during a cold first launch could stall the loader. RULED by the
  coordinator: background mode stays on the rewrite only.
- `FileOpenWriteShared(path, sequentialScan)`: `FILE_FLAG_SEQUENTIAL_SCAN` on derived.dat (POSIX:
  `posix_fadvise(SEQUENTIAL)` under `#if defined(POSIX_FADV_SEQUENTIAL)`; macOS has none). Note the
  mapping ignores the flag; it affects the handle reads after `ReleaseMapping` and the appends.

### Stamps for a read-only consumer `3031f5e8a` (coordinator's ruling 4)
- The gap: with only ext.package_cache using the cache, nothing sent `RequestFlush` and the shared
  store is never closed, so derived.used was never written and that user had no LRU order.
- The writer now also writes dirty stamps after the quiet spell (10 s after the cache's last Find /
  Put, the same end-of-load signal the rewrite's fallback uses); a Find that dirties them wakes the
  writer. `Store::WriteStampsNow()` writes them on the calling thread, and `OpenShared` registers it
  with `std::atexit` (the holder only) for a run that ends inside the quiet spell. There is no ext
  shutdown hook: compress_ao / stream_cache only hook load ends (DeviceData::Slot4 and
  LoaderLoop::_extLoadDone), and the shared store is never destroyed; `std::atexit` is what
  `src/app/runtime_init.cpp` already uses for exit-time work. derived.used writes are serialised
  (`_usedFileMutex`), since the writer and the exit hook may both write it.

## 2. Test evidence

Unit (`tools/build.sh unit debug --jobs 2`, Windows): **1390 tests, 0 failures, 1 skipped**
(`ext_sheet_kit_crosscheck`, needs ToolKit, unrelated). Before the branch: 1384. 17 derived-cache tests,
of them new or changed:

| test | what it pins |
|---|---|
| `open_creates_missing_parents` (new) | Open on `<tmp>/a/b/Cache`, a and b missing: Opened, a store lands |
| `second_instance_and_clear` (rewritten) | the loser is ReadOnly and hits the holder's entry; Put/Flush write nothing; a live append is not in an open reader's prefix but is in a new reader's; holder closes and a new holder takes over while the reader reads; a clear with the reader open |
| `rewrite_with_a_reader_open` (new) | Windows: "kept as it was"; holder serves the newest copy, no .new left; the rewrite goes through once the reader closes |
| `cap_refuses_then_keeps_the_used` (updated) | eviction by stamps, on the writer (4 entries after Open, 3 after the rewrite) |
| `eviction_merges_sessions_and_survives_a_lost_used_list` (new) | sets A, B, A over a lowered cap: B's older entry goes, both of A stay (the old code dropped both of B); B again: refused, next session evicts A's least recent, the refused one then fits; deleted derived.used: 2 of 3 kept (old code: 0) |
| `crash_between_the_replaces_is_finished` (new) | crash hook after the dat replace; generations differ by 1 on disk; next Open finishes from idx.new: both kept entries survive with moved offsets, 0 bad payloads, no idx.new left |
| `new_data_old_index_keeps_the_matching_prefix` (new) | same pair without idx.new: 2 of 3 survive (payload-verified prefix), stable over a second Open |
| `rewrite_runs_beside_finds` (new) | 64 entries, half superseded, 3 reader threads during the rewrite: 944 finds, 0 wrong |
| `stamps_of_a_reader_only_session_reach_the_disk` (new) | Find-only sessions: stamps on disk after a Flush; after the quiet spell alone (shortened to 100 ms by `SetQuietSpellForTest`), the earlier session's stamp kept; from `WriteStampsNow` with the store still open |
| `newest_record_wins...`, `restored_payload_survives_reopen` (updated) | the superseded copy is still served correctly before the rewrite, and gone from the files after it |

Debug exe build (`tools/build.sh build debug --jobs 2`): links; no warning in a touched file.

**Two processes, scratch directory** (a harness built from the same sources,
`scratchpad/t-cachefix/harness/second_instance.cpp` + `two_proc.sh`, not in the tree). Process 1 holds
the cache on `<tmp>/live/x/Cache` (x missing: A1), stores 200 x 1 MB, then appends one every 0.5 s.
Process 2, started during the appends: `status=ReadOnly entries=201`, `hits=201 misses=0 bad=0`, 201 MB
read in 31 ms, its Put wrote nothing (bytes unchanged, stored=0). A second reader 5 s later saw 211.
After the holder exited: `Opened entries=260`, 260 hits.

**Game runs, functional dir** (`C:/GOG Games/Banished-B`, own user dir; no `*.pkm` mounted; vanilla;
the extensions in its settings.cfg, which include compress_ao / stream_cache / package_cache). The
cache was last written by `main`'s exe (format 1, 484 MB, MegaMod).
1. `run-menu`: `unknown or damaged cache format: starting empty` (the version bump),
   `0 entries`; the boot stored 80 entries (3.0 MB). With the game at the menu, the harness on the live
   folder: `ReadOnly entries=80`, `hits=80 misses=0 bad=0`, nothing written.
2. Autostart new game (no `/profile:faithful`): `80 entries, 3.0 MB`; CompressAo `cache_hits=82` of 84
   textures; in game at frame 14404. Harness during the session: `ReadOnly entries=91 hits=91 bad=0`
   (the 11 encodes this session stored).
3. One payload byte flipped in derived.dat, then `run-menu`: `an entry's bytes do not match its record:
   re-deriving it`; after `[StreamCache] boot over` (RequestCompact) the writer logged `rewritten without
   the old copies: 92 entries, 3.1 MB`: the rewrite ran on the writer at the boot's end, not in Open.
4. `run-menu` again: `92 entries`, no bad payload, no rewrite pending, CompressAo `cache_hits=84/84`.
5. `tools/build.sh run` (the gate, faithful): new game reached, running at frame 40931 after 60 s,
   `standalone_crash.log` empty; no `[DerivedCache]` line (the faithful profile opens no cache).

6. Ruling 4 check: a private user dir (a copy of the functional settings.cfg with
   `ext.compress_ao=0`, `ext.stream_cache=0`, `ext.package_cache=1`; removed afterwards), `run-menu`
   twice. Boot 1: only `[PackageCache]` lines (stored, then restored on the remount); 8 s after the menu
   no derived.used, at 28 s derived.used is there (48 bytes: header with session 1, one record). Boot 2
   (Find only, nothing stored): derived.used rewritten 10 s after the last use, session 2, the entry's
   stamp 2.

Not run: the resave gate (nothing here touches the serializer), a MegaMod run (no over-cap eviction in
game: the vanilla cache is 3 MB against a 4096 MB cap; eviction is covered by the unit tests only), and
two OpenBA.exe processes on one folder (the harness stood in for the second; overlapping game runs
splice the logs).

## 3. Not tested

- Linux and macOS: not built or run (CI will build and run `unit` there). The POSIX pieces:
  `EnsureDirChain` (common, over the posix `DirCreate`), `FileOpenWriteShared` + `posix_fadvise`,
  `ThreadBackgroundMode` (no-op), the settings twins' `Utf8ToUtf16` call. The unit tests that encode
  Windows-only behaviour accept both outcomes (`rewrite_with_a_reader_open`).
- The quiet-spell trigger of the REWRITE has no unit test (the stamps' quiet-spell write has one); the
  explicit `RequestCompact` path is tested.
- Fresh-profile run on Linux with an empty HOME (plan's A1 check): not done (no Linux here).
- No perf measurement (not asked; the machine was shared with other lanes).

## 4. For the coordinator

- RULED accepted: the format bump resets every existing cache once, including the user's real
  `<Documents>\Banished\Cache` at the first run of this exe. A functional dir shared with lanes still
  on `main`'s exe flips between format 1 and 2 (each side resets the other's cache), so cold-cache
  timings until everyone is on the same exe.
- CHANGELOG line, ready to lift: "The derived cache is rebuilt once after this update (its file format
  changed); a second running copy of the game now reads the cache instead of running without it."
- RULED: no LOCK_SH (A2); background mode on the rewrite only (A5).
- Fixed per ruling 4 (`3031f5e8a`): the package_cache-only gap described above.
