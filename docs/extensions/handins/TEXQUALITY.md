# TEXQUALITY: the "Texture quality" setting, a top-mip drop (round 2 B1, 2026-10-23)

> Branch `worktree-agent-aeee4a041fcf6199c`, based on `main` `62384a4df`. Windows only (debug build,
> unit, the runs below on vanilla data, mods parked). Not built: Linux (dxvk-native, the same DX11
> code) and macOS (Metal answers "not supported", section 5). Retail has no such setting.

## 1. What it does

`ext.texture_quality` (class A, default OFF, next start) with the valued key
`ext.texture_quality_drop` 0..2 (default 0 = Full: ruled by the user, a `profile=all` run or the row
alone loads textures at Full). On the Enhanced tab: the combo **Texture quality**
Full / Half (next start) / Quarter (next start); Full is the row off (or drop 0), Half / Quarter the
row on with drop 1 / 2 (`SetTextureQualityAndRecord`, `TextureQualityChoice` in `registry_boot.cpp`). The drop is
read once at boot (`Ext::TextureQuality::Boot`, after create_on_demand's Boot and before
stream_cache's) into `TextureQuality::SetDrop`. Off under the faithful profile (the row is off).

A texture that **qualifies** is created without its `drop` largest mip levels: descriptor at
`(width >> drop) x (height >> drop)`, subresources from the first kept level, sampler MaxLOD
`levels - 1 - drop`. The `Video::ImageBuffer` itself (width, height, flags, serialized at
`imagebuffer_vtable.cpp`) is never changed, so game code and saves see retail's values.

**The rule** (`src/render/common/texture_quality/texture_quality.h`, pure, unit-tested; no usage
flag exists): usage 2 (seeded from a resource file), 2D, mipmapped, the file holds exactly
`GetLevelCount()` levels, at least 5 levels, both sides >= 512, both sides a multiple of
`4 << drop` (each kept level is then exactly the D3D level of the smaller texture, BC rows stay whole
blocks, compress_ao's BC4 still gets `% 4`).

## 2. Where it runs

| Piece | File |
|---|---|
| Rule, run state, counters, the `[TextureQuality]` line | `src/render/common/texture_quality/texture_quality.{h,cpp}` |
| Backend answer | `texture_quality_backend.h`; DX11 `src/render/dx11/texture_quality/` (true), Metal `src/render/metal/texture_quality/` (false) |
| Lazy reader (create_on_demand on) | `src/ext/create_on_demand/level_source.cpp`: dropped levels sized and skipped, **no Locator kept**; the record holds the kept levels only, so Materialize never re-decodes the dropped ones |
| Eager reader (create_on_demand off, or a stream create_on_demand cannot locate) | `src/ext/texture_quality/texture_quality.cpp` `ReadDropped`: each dropped level's size read as retail reads it, its bytes skipped by `CreateOnDemand::SkipIfWithin` (new, `stream_locate.h`: a reading CompressedStream whose block directory says the bytes lie ahead, or ext.stream_cache's record stream) or, where that cannot skip, read and freed; the kept levels by retail's `RtVideo_SerializeBlobArray` |
| Texture create | `src/render/dx11/imagebufferdata.cpp` `Create` (EXT SEAM lines): `drop = DropFor(*ib, levels->GetSize())`; desc halved; after the faithful pitch loop `DropLeading` removes the dropped subresources (the loop's pitches for the kept levels are the smaller texture's own: unit test); `CreateSamplerState(drop)`; the deferred (async_gpu_create) copy and share_gpu_data sizes index level `i + drop` |
| Point sampler (sharp_ui) | `AcquirePointSampler` copies MaxLOD from the creation-time sampler |
| Summary | `DeviceData::Slot4(false)` (`devicedata_stubs.cpp`) prints it at each loader bracket close when it changed |

Level-count couplings: `bc4Candidate` tests the kept top size; the restart counter in the faithful
loop is untouched (2D only: it never fires before the last level, and the dropped entries are removed
after the loop); cube SRVs untouched (cubes never qualify).

**share_gpu_data:** no drop count in its key. The key hashes the full descriptor plus every level's
pitch, size and XXH64 (`share_create.cpp:136-170`), so a dropped texture differs from the full one in
descriptor and payload already; two equal dropped textures still share.

**compress_ao:** the BC4 cache key hashes every source level it is given plus the shape
(`bc4_chain.h`), so a dropped R8 texture is encoded from its kept levels at the halved size and
stored as a new entry under the same producer (`AOB4`); nothing is sliced. Seen in the runs:
`[CompressAo] textures=100 cache_hits=94 encoded=6` at drop 1 (the 6 = the dropped AO chains).

**stream_cache:** its record key `shape[1]` now carries the drop (`(payloadsSkipped) | drop << 1`):
with create_on_demand off what a load consumes depends on it. Verified: E2 below replayed records
recorded at drop 1 (2,945 hits) with the eager reader skipping inside the record stream.

## 3. Exclusions seen in the log (vanilla, new game, seed 12345)

Per boot: **59 textures dropped, 337 kept full** (not mipmapped 17, cube 2, few levels 3, small 315,
level count 0, odd size 0). The first 12 of each verdict are logged by name; `_path` is not yet set
when the level reader runs, so they print `(no path)` with size / levels / format / flags:

- dropped: 1024x1024 BC1/BC3 (11 levels), 2048x2048 BC3 (12 levels), 512x512 BC1 (10 levels),
  all `flags 0x3` (mipmapped);
- not mipmapped (sprite sheets, fonts, UI): 1600x900 / 1024x1024 / 256x256 / 128x128 BGRA,
  512x512 format 1, 32x32 cursors, `levels 1/1 flags 0x0`;
- small: 64x64 / 128x128 R8 AO maps and BGRA icons, 256x128;
- cube: 256x256 and 1024x1024 (54 / 66 level blobs = 6 faces);
- few levels: 4x4 / 8x8 R8.

No sprite sheet or font reached the drop (none is mipmapped).

## 4. Numbers per level (vanilla; perf dir with a private user dir, NOT a timing run)

`[CreateOnDemand] load end` after the new game (deferred = every texture/buffer recorded, made = the
ones the world prefetched):

| Level | deferred | made | `[TextureQuality]` | GPU dedicated (process, ~25 s in) |
|---|---|---|---|---|
| 0 Full (A, F) | 111.5 MB | 86.4 MB | off | 673.2 MB (F) |
| 1 Half (B) | 61.0 MB | 41.6 MB | 59 textures, skipped_bytes 50.5 MB never decoded, kept 16.8 MB | 632.2 MB |
| 2 Quarter (C) | 48.3 MB | 30.3 MB | 59 textures, 63.1 MB never decoded, kept 4.2 MB | 617.2 MB |
| 1, create_on_demand off (E) | n/a | n/a | 59 textures (eager), 50.5 MB skipped in the stream, 0.0 MB decoded and discarded | 649.2 MB |

GPU dedicated is `\GPU Process Memory(pid_*)\Dedicated Usage` sampled once per run (no DXGI memory
stamp exists in the exe); one sample, different pins between rows, so read it as direction only.
On a MegaMod set the qualifying share is larger; not measured here (vanilla only).

## 5. Screenshots (in the session scratchpad, `t-texq/runs/<run>/Screenshot<N>.png`)

All `BANISHED_SEED=12345`, `BANISHED_CAMERA_PIN=town,<dist>`; the retail screenshot holds the world
only (no UI).

- `A_drop0/Screenshot0.png` (Full, dist 45) and `B_drop1/Screenshot1.png` (Half, dist 45): at this
  distance indistinguishable; terrain, roofs, trees, water, rocks all normal, no seams or wrong mips.
- `F_drop0_close/Screenshot6.png` (Full, dist 20): sharp roof shingles and grass.
- `E2_drop1_eager_cached/Screenshot5.png` (Half, dist 20, eager reader from stream-cache records):
  roofs visibly softer, grass a little softer, otherwise correct.
- `C_drop2/Screenshot2.png` (Quarter, dist 20): roofs clearly blurry (shingle lines gone), terrain
  grass smooth; no corruption, colours and alpha right, trees and citizens unchanged (their textures
  are small and keep every level).
- `D_drop0_loadTQ1/Screenshot3.png`: the save made at Half, loaded at Full: full-detail terrain.

**The look at Quarter is the coordinator's / user's call** (close up it is obviously low-res).

## 6. Verification

- Unit: `tests/unit/ext_texture_quality_test.cpp` (row, value key, the rule incl. sprite / cube /
  small / odd sizes, `DropLeading`, pitch rebasing over BC1 / BC3 / R8 / BGRA and six sizes: the
  faithful loop's pitches minus the dropped front equal the loop's pitches for the smaller texture,
  and D3D11's own row pitches for power-of-two sizes); registry / values / rsc tests updated.
  `tools/build.sh unit debug`: 1388 tests, 0 failures, 1 skipped (the kit cross-check).
- Runs A-F + E2: all exit 0, empty crash log. Save made at drop 1 (`[SaveTest]` Auto.sav, 9.38 MB)
  loaded at drop 0 (run D) and played on.
- trace_oracle (RenderDoc, frame 150, pin town,30): captures at drop 1 (x2), 0 and 2. **S4 for file
  textures: 0 at every level** (414 / 414 / 335 maxLOD checks against the bound texture's mip
  count). The tool's S4 total reads 60 on EVERY trace, drop 0 included (identity baseline 60): all
  are ext.ambient_occlusion's ASSAO render targets (`R16_FLOAT m4` viewed with 1 mip, `R8G8`,
  `R32_FLOAT`, MaxLOD = FLT_MAX), pre-existing and unrelated (`t-texq/trace/s4_detail.txt`). drop 1
  vs drop 0, control drop1/drop1: S2 0, S3 0, S6 0; S1 32 and S5 26 are texture keys (desc +
  mip-0 hash differ by design). Traces: `t-texq/trace/*.jsonl`.

## 7. Not done / not tested

- **Metal**: answers `TextureQualityBackend::Supported() == false`; the extension then keeps the drop
  at 0, so `PlanUploads` (`backend.mm:1498`) and `shape.levelCount` (`:1616`) never see a dropped
  array and were left untouched. Supporting it = plan uploads from the first kept level at the
  halved size with `levelCount - drop` levels, and `lodMaxClamp` lowered the same way. Not compiled
  here (no Mac); the new Metal file is plain C++.
- Linux not built.
- MegaMod set not run (vanilla only); the faithful gate and resave gate not run (coordinator).
- A UI-visible check of the Enhanced tab combo was not done (the rsc parse test covers the row).
- The eager path's "decoded and discarded" branch (a stream that cannot skip) did not fire in any
  run (every resource stream is a CompressedStream); it is retail's read followed by a free.
