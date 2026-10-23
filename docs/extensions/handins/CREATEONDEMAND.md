# CREATEONDEMAND: static GPU objects created at their first use (issue #13, 2026-10-23)

> Branch `issue13/lazy`, based on `issue13/next` (`2ceb8a3bd`: bc4 cache + compress_ao + share_gpu_data
> + pathindex). Windows only (perf + debug builds, unit, the gate, the resave gate, the MegaMod runs
> below). Not built: Linux (dxvk-native: the same DX11 code), macOS (Metal: a "not supported" body
> at the time; the Metal body followed on `issue13/lazy-metal`, section 6).
> **The row ships OFF until the user rules.**

## 1. The cost

A MegaMod 7-part boot creates ~9.8 GB of static GPU data from its resource files (vertex buffers 4.46,
index buffers 0.90, textures 4.47 GB); with compress_ao + share_gpu_data on, the process holds ~9.6 GB
private and ~7.4 GB of dedicated GPU memory. Under 1 % of it is bound after the menu and a minute of a
new game. The spike (branch `issue13/redecode-spike`, `ee5628886`) proved that one payload can be
re-produced alone from the mapped package: 12,532 re-decodes, 0 mismatches, ~0.9 ms per decoded MB.

## 2. The change: `ext.create_on_demand` (class A, next start, default OFF)

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
mutex, then the record goes. A use that meets a record running elsewhere waits for it; Create,
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
default OFF), `Ext::CreateOnDemand::Boot` (registry_boot.cpp), seam `CreateOnDemandBackend::Request /
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
otherwise. Default OFF until the user rules: it changes when every static object exists, and a
first-use create costs up to ~8 ms on the using thread when the prefetch has not run.

## 5. Files

`src/render/common/create_on_demand/{payload,lazy_table,stream_locate}.{h,cpp}`, `pending_levels.h`,
`create_on_demand_backend.h`; `src/render/dx11/create_on_demand/lazy_create.{h,cpp}`;
`src/render/metal/create_on_demand/lazy_create.{h,mm}`, `src/render/metal/seeded_create.h`; `src/ext/
create_on_demand/{create_on_demand,level_source,prefetch}.{h,cpp}`; seams in `io/FileSystem.h` /
`filesystem_openfile.cpp`, `video/ImageBuffer.h` / `imagebuffer_platform_serialize.cpp`,
`graphics/Mesh.h` / `mesh.cpp`; `src/render/dx11/{buffers,imagebufferdata,devicedata,devicedata_stubs}.cpp`;
`src/render/metal/integration.mm`; registry, sprite, tests, LIST.MD, DESIGN.md.

## 6. Metal

Branch `issue13/lazy-metal` (this branch + `issue13/metal`, the Metal compress_ao / share_gpu_data
bodies). Built by CI on macOS arm64 (and the three other checks); **never run on a Mac**.

- **Body** `src/render/metal/create_on_demand/lazy_create.{h,mm}`: the same table, Locator, Redecode
  and pending levels; a `BufferRecord` and a `TextureRecord` over `MTLBuffer` / `MTLTexture`.
  `CreateOnDemandBackend::Supported()` is true (the "not supported" stub left `integration.mm`).
- **One create chain.** `src/render/metal/seeded_create.h` (bodies in `backend.mm`): `PlanUploads`
  (ext.compress_ao's BC4 conversion and the level uploads), `MakeTexture` / `MakeBuffer`
  (ext.share_gpu_data's create-or-share, else the plain create). The load and the materialization
  both call them, in that order.
- **Buffers.** The buffer `Serialize` load arm (both kinds, one `SerializeBuffer` template): a static
  buffer (not dynamic, size > 0, no buffer yet) whose payload locates is recorded and skipped. The
  eager arm now reads the payload and hands it to `Create` as its seed (DX11's order). Before, Metal
  created an unseeded buffer and read the payload into it: the same bytes, but ext.share_gpu_data
  never saw a loaded buffer, so on Metal it shared nothing from resource files until this commit.
- **Textures.** `ImageBufferData::Create` builds the descriptor and, when the level reader left
  pending Locators for the image, records the texture and makes the sampler. The sampler now takes
  a mip count instead of the texture (`MakeSampler`), so a deferred texture's sampler equals the
  eager one's.
- **Ensure / Forget** at the DX11 slots: `Ensure` in Bind (before the state lock), Lock, SetData,
  Update, Read (before the device lock) and the save arm; `Forget` in Create, AttachColor/
  DepthBuffer and the destructors. Publish writes the companion's strong member (ARC retains it)
  and its `shared` flag, under the table's mutex.
- **Device.** `DeviceData::Shutdown` (exit, a device rebuild) calls `Pause` first; a successful
  `CreateDevice` calls `Resume`. A materialization with no live device returns Retry.
- **Threads.** The prefetch workers are plain threads: each `Materialize` runs in an
  `@autoreleasepool`. The device's creates are thread-safe; the allocation bookkeeping takes the
  backend's state lock.
- **Not tested at run time:** everything above. In particular a deferred texture drawn on first
  bind, the prefetch on Apple silicon, Publish racing a using thread's nil test (the same unlocked
  read as DX11's), a device rebuild with records pending, and the memory figures.

## 7. Open

- Not run: a forced device-lost round trip with the row on (`BANISHED_FORCE_DEVICE_LOST`); the game
  on Linux or on a Mac (both build in CI; the Metal body is section 6).
- The prefetch knows only `MeshInstance::SetMesh`; edge meshes, particles and UI images are made at
  first use (the menu's ~120 objects, up to ~8 ms each).
- Objects are never released back to "deferred": a long session that views everything grows toward
  the eager footprint.
- A cold derived cache encodes an AO map at its first use (~5 ms each), not at load.
- Mapped package pages count in the working set while touched (WS within the noise of off here).
