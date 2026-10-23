# CREATEONDEMAND: static GPU objects created at their first use (issue #13, 2026-10-23)

> Branch `issue13/lazy`, based on `issue13/next` (`2ceb8a3bd`: bc4 cache + compress_ao + share_gpu_data
> + pathindex). Windows only (perf + debug builds, unit, the gate, the resave gate, the MegaMod runs
> below). Not built: Linux (dxvk-native: the same DX11 code), macOS (Metal: a "not supported" body).
> **The row ships ON: the user ruled 2026-10-23.**

## 1. The cost

A MegaMod 7-part boot creates ~9.8 GB of static GPU data from its resource files (vertex buffers 4.46,
index buffers 0.90, textures 4.47 GB); with compress_ao + share_gpu_data on, the process holds ~9.6 GB
private and ~7.4 GB of dedicated GPU memory. Under 1 % of it is bound after the menu and a minute of a
new game. The spike (branch `issue13/redecode-spike`, `ee5628886`) proved that one payload can be
re-produced alone from the mapped package: 12,532 re-decodes, 0 mismatches, ~0.9 ms per decoded MB.

## 2. The change: `ext.create_on_demand` (class A, next start, default ON)

**Locator.** A compiled resource in a package is one compressed stream: block count, a directory of
{sourceOffset, compressedSize, uncompressedSize}, then 32 KB blocks of the decompressed stream. A GPU
payload is a range of that stream. `CreateOnDemand::Locator` = {package mapping, entry offset, entry
size, decompressed pos, len}. `Locate` (`stream_locate.h`) builds it from the loading serializer's
stream at the moment of the read: a `CompressedStream` whose source is the `FileStream` over a packed
entry (`FileSystem::PackedEntryOrigin`, NOT RETAIL, gives the package's handle and the entry offset,
wide packages included), and checks the mapped block directory against the stream's own. `Skip`
advances `_position` past the payload: `CompressedStream::Read` starts every read with
`SetWindow(_position)`, so the next read decodes what it needs. Loose files, nested packages and
memory streams are not locatable: read as retail reads them (0 such static objects on MegaMod).

**Mappings.** The engine's MAPREAD mappings go with the package handle (ClosePackages: two remounts
per boot) while resources stay alive. The extension maps each package itself, read-only, from the
engine's handle on first use (the view outlives the handle on Windows and POSIX), counts its Locators,
and is told by `IO::FileSystem::_extHandleClosing` (EXT SEAM in `PackedMapRelease`, before
CloseHandle) to detach it: never handed out for that handle value again, unmapped with its last
Locator. A MegaMod boot holds 8-10 mappings (8 detached after the remount).

**Buffers.** `GpuBuffer_Serialize`'s load arm: a static buffer (not dynamic, no buffer yet) whose
payload locates is not read; the record keeps the descriptor `GpuBuffer_Create` builds for seeded data;
the owner already holds the size / dynamic Create would store.

**Textures.** `Video::ImageBuffer::_extLevelSource` (EXT SEAM in `PlatformSerialize`'s read arm): for a
usage-2 image (not degenerate) on a locatable stream, each level's size is read as retail reads it and
its bytes skipped; the level arrays are never allocated; one Locator per level is left on the loading
thread (`pending_levels.h`). `ImageBufferData::Create` runs every engine-visible step (registry,
released interfaces, descriptor, pitches) and makes the sampler as retail does; the texture and SRV
are recorded. A level that cannot be located ends the skipping: it and the rest are read the retail
way and the skipped ones re-decoded into their arrays, so Create gets every byte.

**Materialize.** `CreateOnDemand::LazyTable` (backend-neutral). `Ensure(this)` right after
async_gpu_create's `Await` in Bind, Lock, SetData, Update, Read and the save arm, behind the handle's
own null test (a made object pays one compare). The record re-decodes the covering blocks into its own
block, then runs the load's creates through the same helpers: compress_ao's `ConvertForCreate`, then
share_gpu_data's create-or-share (a plain create with that row off), on the device current at that
moment, held by a reference. Materialize runs unlocked; `Publish` writes the handles under the table's
mutex; the record stays, MADE, with its Locator (section 7). A use that meets a record running
elsewhere waits for it; Create,
AttachColor/DepthBuffer and the destructors `Forget` first (drop unrun, wait for running).

**Prefetch.** `Graphics::MeshInstance::_extMeshAssigned` (EXT SEAM at the end of `SetMesh`): an entity
placed, loaded from a save or changing model gets its mesh before it is drawn; the extension queues
every detail level's four buffers and every material's textures for the table's workers (two, one
below eight hardware threads). A use that meets a queued record runs it itself.

**Threading.** One table mutex, nothing heavy under it; the loader only locates and skips.

**Device lost / exit.** `DeviceData::Shutdown` (exit, both recoveries, a rebuild) calls `Pause` after
async_gpu_create's drain: no new materialization starts, the running ones finish. A successful
`CreateDevice` calls `Resume`. A materialization that finds no device, or a removed one
(`GetDeviceRemovedReason`), returns Retry: nothing published, the record stays for the next use on the
recovered device. Not exercised by a forced device-lost run (open, section 6).

**Interplay chosen.** compress_ao: the AO maps materialize through the derived cache (decode the R8
levels, hash, lookup; encode + Put on a miss): no load-time hashing is kept. share_gpu_data: hashed at
materialize (the bytes are in hand then); only materialized objects meet in its table, so an unused
copy costs nothing anyway. async_gpu_create: a deferred object never reaches its queue; what is not
deferred keeps its path.

Wiring (DESIGN.md section 3): registry row `create_on_demand` ("Load graphics on demand", class A,
default ON), `Ext::CreateOnDemand::Boot` (registry_boot.cpp), seam `CreateOnDemandBackend::Request /
Supported / Prefetch`, DX11 body `src/render/dx11/create_on_demand/`, Metal "not supported"
(`integration.mm`), `Entry_create_on_demand.png` (`tools/ext_icons/createondemand.py`, baked), the
Names-table row "first-use GPU creates", LIST.MD row. Summary lines `[CreateOnDemand] load end` at each
loader-bracket close and `in game` at most every 30 s after first-use creates.

## 3. Proof

**Memory and boot**, perf build, MegaMod 7-part, default profile (async_gpu_create, compress_ao,
share_gpu_data on), warm derived cache, `scratchpad/lazy/run.sh`, interleaved off / on / off / on.
Machine load **CLEAN** (no compiler at launch or in game in any of the four). Menu memory = the last
sample before the new game; in game = the median from in-game + 30 s.

| Run | Menu (s) | In game (s) | Menu private / WS / GPU (GB) | In game private / WS / GPU (GB) |
|---|---|---|---|---|
| off 1 | 21.0 | 23.4 | 9.55 / 2.12 / 7.10 | 9.59 / 1.98 / 7.42 |
| off 2 | 21.6 | 23.2 | 9.53 / 2.12 / 7.41 | 9.61 / 2.04 / 7.42 |
| on 1 | 18.6 | 21.0 | 2.17 / 1.88 / 0.62 | 2.25 / 1.95 / 0.65 |
| on 2 | 18.8 | 21.1 | 2.19 / 1.89 / 0.62 | 2.31 / 2.00 / 0.65 |

- **-7.3 GB private, -6.8 GB GPU dedicated** in game; boot to the menu -2.6 s, to the game -2.3 s.
- Per boot: 178,206 objects deferred (10.06 GB), 0 static objects read at load; ~580 (77 MB) made in
  the first minute of a new game (~460 by the prefetch, ~120 on use: the menu's UI and scene).
- Debug smokes (buffers only: private 5.51 GB, GPU 3.81 GB; buffers + textures: 2.30 / 0.65 GB).

**Hitches**, the showroom harness (`issue13/showroom` merged into the TEMP branch `issue13/lazy-sr-tmp`
with a TEMP frame-period probe and a TEMP switch for the prefetch, never merged): 48 MegaMod buildings,
seed 12345, the camera pinned on each from two headings, perf build, 60 fps limiter. Frame periods of
the 5,760 frames right after each camera move:

| Run | p50 / p99 (ms) | worst frame (ms) | frames > 33 ms | per-shot worst, median / p90 (ms) |
|---|---|---|---|---|
| off | 16.67 / 18.52 | 221.95 (shot 58) | 1 | 18.74 / 20.20 |
| on | 16.66 / 18.59 | 48.30 (shot 0) | 1 | 18.74 / 21.06 |
| on, prefetch OFF (TEMP) | 16.67 / 18.67 | 68.11 (shot 0) | 1 | 18.84 / 20.80 |

The worst single first-use create was 7.7-8.2 ms (a large texture through the derived cache); the
worst frame with the row on is the first framing. The off run's 222 ms frame is not the extension's.

**Visual.** 96 shot pairs off vs on, and off vs on-without-prefetch: no pair has more than 0.001 % of
its pixels over 24 / 255 (the rest is animated water and the "?" icons). Looked at side by side:
BoKaapRowhouse, EssaouiraHouse, MDOldCourthouse, NotreDameCathedral, EBWindmill, Metalsmith: the same
meshes, textures and AO. Nothing can pop in late: a use waits for its create (the bind is the create).

**Save + reload.** The showroom town (48 MegaMod buildings) autosaved at frame 900 (row on), reloaded
with `BANISHED_AUTOLOAD` (row on, camera pinned on the cathedral): in town 70 s, crash log empty, 629
objects prefetched by the load's SetMesh calls; the capture draws every building, the AO and the
terrain.

**Gates.** `tools/build.sh unit`: 1349 tests, 0 failures, 1 skipped (the kit cross-check); 11
`ext_create_on_demand_*` (the row; RedecodeRange over the real encoder's entries with compressed,
stored and short blocks, and bad input; the mappings shared, detached, outliving the handle; the
Locator through the process maps; the table: once, forget, a use waiting for a worker, pause / resume /
retry / failed, four threads over 2,000 records; and 4,000 MegaMod payloads read by the engine's own
`CompressedStream::Read` then re-produced through Maps / Locator / Redecode, 0 mismatches). Gate
(`tools/build.sh run`, faithful, mods parked): in town, crash log 0, `[CreateOnDemand] off`. Resave gate
`Gate`: NEW-GOLDEN (fresh worktree, md5 971aaec2cf95f5f730bb1cd1a4fa8428), then PASS.

## 4. Classification

Class A: the same objects with the same contents and the same creates, made later (at first use, or
earlier by the prefetch), never seen half-made. Off under the faithful profile; one null test per site
otherwise. Ships ON: the user ruled 2026-10-23. It changes when every static object exists, and a
first-use create costs up to ~8 ms on the using thread when the prefetch has not run.

## 5. Files

`src/render/common/create_on_demand/{payload,lazy_table,stream_locate}.{h,cpp}`, `pending_levels.h`,
`create_on_demand_backend.h`; `src/render/dx11/create_on_demand/lazy_create.{h,cpp}`; `src/ext/
create_on_demand/{create_on_demand,level_source,prefetch}.{h,cpp}`; seams in `io/FileSystem.h` /
`filesystem_openfile.cpp`, `video/ImageBuffer.h` / `imagebuffer_platform_serialize.cpp`,
`graphics/Mesh.h` / `mesh.cpp`; `src/render/dx11/{buffers,imagebufferdata,devicedata,devicedata_stubs}.cpp`;
`src/render/metal/integration.mm`; registry, sprite, tests, LIST.MD, DESIGN.md.

## 6. Open

- Not built: Linux / macOS. (The forced device-lost round trip ran: section 8.)
- The prefetch knows only `MeshInstance::SetMesh`; edge meshes, particles and UI images are made at
  first use (the menu's ~120 objects, up to ~8 ms each).
- An object given back (section 7) is re-made at its next use on the using thread: no prefetch tells
  it is about to be seen again (SetMesh is not called again), so a building framed after 5 idle
  minutes costs its first-use creates in that frame.
- A cold derived cache encodes an AO map at its first use (~5 ms each), not at load.
- Mapped package pages count in the working set while touched (WS within the noise of off here).

## 7. Objects given back after an idle time (2026-10-23)

A made object unused for more than `ext.create_on_demand_idle_min` minutes (value key, owner
create_on_demand, 0..120, default 5, 0 = never; read at boot) gives its GPU memory back and is re-made
from its Locator at its next use. Closes the former open item "objects never return to deferred".

- **Records kept.** A materialized record stays in the table (state MADE) with its Locator instead of
  being dropped; Forget drops it as before.
- **Use stamps** (`use_tracker.h`, `UseStamps`): a fixed open-addressing table (2^18 slots) of the made
  objects and the epoch (minute) of their last use. Every slot that reads a companion's handles (Bind,
  Lock, SetData, Update, Read, the save arm) holds a `UseScope`: it stamps the object, lock-free (a
  probe for an object never made stops at the first empty slot; the epoch is stored only when it
  changed). An object that does not fit in 64 probes is never tracked, so never given back.
- **Use gate** (`UseGate`): the same `UseScope` keeps the gate entered for the slot's body. A sweep
  closes it (waiting for the uses in progress, holding new ones back) only to give objects back, so
  no use can read a handle while it is released.
- **Sweep.** Worker 0 sweeps once a minute: the epoch advances; the made objects are scanned under the
  table's mutex alone (no use waits on the scan); an object last used more than the limit's whole
  epochs ago is a candidate; then the gate closes, the table's mutex is taken, and each candidate still
  made and still idle is given back: `Record::Evict` drops share_gpu_data's user first, releases the
  handles in ReleaseInterfaces' order (the view, then the texture; a buffer) and nulls the fields; the
  sampler (made at load, as retail makes it) stays. The record is idle again: the next use, inside its
  gate, re-decodes and re-creates it.
- **Device.** A sweep during a pause (the device going away) gives nothing back.
- Summary: `[CreateOnDemand] given back (run total): given back N (MB) over S sweeps (uses held X ms),
  re-made R; made and held MB` after each sweep that gave something back.

**Proof** (perf, MegaMod 7-part, `ext.create_on_demand_idle_min=1`; the showroom of section 3 with a
TEMP away pass on `issue13/lazy-sr-tmp`, never merged: the 96 shots, then 200 s with the camera on an
empty corner, then the 96 shots again; machine load **BUSY**: 2-7 compilers of other agents ran through
these runs, so frame times are indicative only).

| Moment (run EV_on2) | Made and held | Process private / GPU dedicated (GB) |
|---|---|---|
| after the first pass | 211 MB | 2.46 / 0.79 |
| after 200 s away | 15 MB (948 objects, 239 MB given back) | 2.17 / 0.59 |
| after the second pass | 211 MB (545 re-made) | 2.42 / 0.75 |

- **The buildings redraw identically when framed again**: 92 of the second pass's 96 shots match the
  first-pass reference run (`SR_on`) within 0.01 % of pixels over 24 / 255. The other 4 are the
  first shots after the camera comes back, which miss the same way in the row-off run (5 there): the
  harness's camera is still travelling, not the extension.
- The first eviction rule gave back every made object at every sweep (a use late in the last epoch
  counted as idle); fixed (`c33c33575`, "more than the limit's whole epochs"), and the scan moved off
  the gate (`d62fab971`): the gate is now closed 0.7-1.1 ms per evicting sweep (was ~10 ms per sweep).
- Frame times (busy machine): second pass worst frame 89 ms (on) vs 51 ms (off); the re-makes happen
  on use, without a prefetch (section 6).
- Unit: `ext_create_on_demand_use_stamps`, `..._table_evicts_idle_and_remakes`,
  `..._table_idle_zero_never_evicts`, `..._table_eviction_waits_for_a_use_in_progress`,
  `..._table_timed_sweep_on_worker`, `..._idle_key`; 1355 tests, 0 failures, 1 skipped.

## 8. Forced device-lost round trip (2026-10-23)

Perf, MegaMod 7-part, the showroom town's save autoloaded, `BANISHED_FORCE_DEVICE_LOST=30000` (in game).
`[CreateOnDemand] device going away` (the pause), the share table cleared, `recreate OK`, `round trip
COMPLETE`, crash log empty, alive to the end. OnVideoError destroys every deferrable object (ImageBuffer,
GraphicsMesh, EdgeMesh are in its type list), so all pending records were dropped unmade and
OnVideoRestart's reload deferred all 178,206 again on the new device; the capture after recovery draws
every building. Memory across the loss: row on 2.42 -> 3.24 GB private, GPU 0.79 -> 1.49 GB; row off
9.62 -> 17.60 GB, GPU 7.43 -> 13.66 GB (the growth after a loss is not this extension's).
A forced reset does not remove the device; for a real removal a record now checks
`GetDeviceRemovedReason` after its creates and, when lost, releases what it made (share table first),
publishes nothing and stays recorded (Retry) for the recovered device (`6d5bb9f43`). Injecting at
Present #3000 lands inside the boot load and crashes in `MaterialInstance::Serialize` on the loader
while OnVideoError runs on the main thread: the known retail window (deferred), not this extension.
