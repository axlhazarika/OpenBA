# P8 Vulkan: shadows, the depth discard, combined samplers, the frame-copy split

Android P8, the Vulkan items (`docs/runtime_port/NEXT_SESSION.md` §2.7, ruled 2026-10-25). Device: Galaxy S25
(Adreno 830, driver 1.3.284). Windows bed: RTX 5080, `BANISHED_RENDER_BACKEND=vulkan`, debug flavour with the
validation layer. Branch base `8e94db750`.

Machine load for every Windows run below: other agents building. No timing in this file is quotable; the
results are counters, validation counts and pictures.

## 1. Citizen shadows flicker on the phone: NOT REPRODUCED

What was checked:

- **The phone at its own settings** (`VideoShadowScalar=2`, a 512x512 sun map, render scale 50, MSAA off;
  debug apk of this branch's base). Seeded new game (`BANISHED_SEED=12345`), the camera pinned on the town at
  distance 18 and 45, `screenrecord` for 4 s (about 120 frames per second recorded, all frames extracted).
  - A zoomed montage of 48 consecutive frames around the citizens and the cows shows no frame-to-frame
    flicker of their shadows.
  - The mean luminance of three shadow patches (a cow's, a citizen's, open grass) over 80 consecutive frames
    changes smoothly (at most 0.5 per frame on a 0-255 scale). A shadow that popped in and out would show as
    a dip of tens.
- **Windows, Vulkan against DX11, same settings as the phone** (`VideoShadowScalar=2`, MSAA off, same seed,
  same pin, one `BANISHED_SCREENSHOT_AT` shot each, looked at side by side, not diffed). The citizens' and
  the cows' shadows are the same faint blobs on both backends; the cows' shadows match in shape and strength.
- **Synchronisation.** `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1` over a new game on Windows Vulkan (the frame
  records the same passes and barriers as on the phone, apart from the present pass): no hazard. So the sun
  map is not read while the next frame's shadow pass writes it. A race like that would show only on moving
  casters such as citizens.
- **The state the converter and the backend give the sun map**, read against DX11:
  - D24S8 on both, and the S25 has `d24s8=1`;
  - the comparison sampler is LESS with linear filtering, as D3D11's `COMPARISON_MIN_MAG_MIP_LINEAR`;
  - depth bias `bias * 16777215` and slope `slopeBias * 4096`, clamp 0, depth clip on, as
    `renderstatedata.cpp`;
  - the store op is STORE: the depth discard only touches the main depth.

  Nothing in the shadow path differs from DX11.

What is left: the owner's conditions. At 512 texels a citizen covers less than one texel of the sun map. Any
shimmer would be retail's own look at retail's "Low", and it would grow when the camera moves (the sun
frustum follows the camera). A pinned camera cannot move unattended.

To settle it, one of these is needed from the owner:
- a screen recording of the flicker;
- the settings in force at the time (MSAA 2x msrtss, render scale, shadow quality);
- whether the camera was moving.

With any of these, the next step is a RenderDoc Android capture of two consecutive frames: compare the sun
map contents and the citizen draws in it.

## 2. Depth-discard re-arm: DONE (`7e23be243`)

**The cause.** The P7 rule predicted the last main-depth pass by its index: the last frame's count, less one.
At the first game frame after the menu or the load, that prediction missed, and the miss turned the discard
off for the rest of the session. The pass log of that frame shows why:

1. The game's first frame begins with a colour-only clear of the main target (`Clear(1)`).
2. `FlushPendingClear` ran that clear as an empty pass with the main depth attached, loading and storing it.
3. That pass was main-depth pass 0, and it LOADed the depth the last loading frame had discarded.

The P7 doc saw the same in the stress town. In this run, the plain autostart session on the phone lost the
discard too: menu frames hold 1 main-depth pass, so the prediction armed there.

**The fix:**
- `depth_discard.h` predicts by **frame-structure signature**. Every structural event of the frame is folded
  into a running hash: a pass begun (with its colour and depth attachments, and whether they are the main
  ones), a frame copy, a read-back.
- A main-depth pass discards only when its signature has ended the frame 120 times. A signature that is seen
  with a later main-depth pass after it loses that evidence. The signatures sit in a small table, so a frame
  that alternates (a minimap capture now and then) keeps the usual one armed.
- A structure change BEFORE the pass changes its signature. The game's shadow and reflection passes ahead of
  the main pass are such a change, so the new structure starts unarmed and arms on its own evidence, with no
  wrong frame.
- The one case a signature cannot see is a main-depth pass appended AFTER an unchanged prefix. It keeps P7's
  guard: the pass that loads discarded contents clears instead, and the discard is off for the session. So
  there is never a second wrong frame.
- `DrawTargetsChanged` (the main targets remade) and `DrawStartup` clear the table.
- **The empty clear pass attaches only what it clears.** D3D11's `ClearRenderTargetView` touches only the
  colour view. Before this, the colour-only clear loaded and stored the main depth for nothing, and that is
  what tripped P7's rule.

**Verified:**
- `tests/unit/vulkan_depth_discard_test.cpp`, 7 cases:
  - menu to game: no miss, and the game structure re-arms;
  - an occasional prefix change keeps it armed;
  - an appended pass: one miss, then off;
  - a non-terminal sighting resets the evidence;
  - `StructureChanged` disarms;
  - not allowed: no discard.
- Windows Vulkan, new game, debug, validation on (MSAA 4x resolve path):

  | | passes | main-depth passes | discarded | misses |
  |---|---|---|---|---|
  | base (`8e94db750`) | 5 | 3 | 0 (off from the game's first frame) | 1 |
  | this branch | 4 | 2 | 1 | 0 (3,600 frames) |

- S25, debug apk, validation layer on, autostart new game, pause-menu quit after 9,627 frames:
  - the menu frames: `1/1` discarded;
  - the game from frame ~1,800: `1/2` discarded, 0 mispredicted, 0 validation errors;
  - the base apk in the same session shape: `0/3 (1 mispredicted)` from the game's first frame on.

Not run: the stress town on the phone (the phone was shared; the plain session was the case that failed).

## 3. Combined image samplers: DONE (`bb82f4506`)

**The pairs the retail programs use.** `vulkan-shader-check` now prints the census, over 465 permutations:

| pair | permutations |
|---|---|
| t0+s0 | 226 |
| t1+s1 | 141 |
| t2+s2 | 72 |
| t3+s3 | 128 |
| t5+s5 | 132 |
| t6+s6 | 16 |

- No texture is sampled with two samplers.
- No texture is read without one.

**The mapping** (`shader_convert.h` COMBINED IMAGE SAMPLERS, `spirv_combine.{h,cpp}`). A word-level SPIR-V pass
after dxbc-spirv:
- Texture t's primary pair (t, its lowest sampler register) is one `COMBINED_IMAGE_SAMPLER` at
  `BindingOfTexture(t)`.
- A further pair of the same texture gets its own combined descriptor in the sampler range,
  `BindingOfSampler(k)`, which is free once no binding is a separate sampler.
- A texture read without a sampler (`ld`, a size query) is combined with no sampler register (the descriptor
  carries the default sampler), and its image is taken with `OpImage`.
- The descriptor's sampler is the one the engine binds at the pair's sampler register: the sampler of the
  image bound at that slot, as `ImageBufferData::Bind` sets both on DX11.
- Any other form fails the conversion with a message, never silently:
  - a descriptor array;
  - a buffer SRV;
  - a sampler value read outside `OpSampledImage`.

**Scope:**
- The game's programs are combined: the companions, the ext rewrites, the linked pixel modules and the shader
  check.
- The AO pass keeps its own separate layout (`ConvertOptions::combineSamplers` off).
- Set layouts (pair and game-wide), pools, `StageSet`, `PushPixelSet` and `BindVertexSet` write one combined
  descriptor per image binding.

**Cache invalidation:**
- `kMappingVersion` 4 -> 5;
- the combined flag in the SPIR-V cache key (bit 31 of the stage word);
- the pipeline-cache file magic `OBAVKPC1` -> `OBAVKPC2`.

The first boot after the change converted every program ("0 from the cache") on both Windows and the phone.

**Verified:**
- `vulkan-shader-check` over the install: 465/465 convert, pass `spirv-val --target-env vulkan1.3` (SDK
  1.4.363) and round-trip the cache entry.
- Unit tests `vulkan_combine_*`:
  - one pair: no separate sampler binding left, the cache key differs, the entry round-trips;
  - a texture sampled with two samplers plus a second texture: a further pair at `BindingOfSampler(0)`;
  - a texture read only with `ld`: `OpImage`, no sampler.

  The two-sampler module was also run through spirv-val once: valid.
- Windows Vulkan: 0 validation errors over a new game. A screenshot of the town looks as before: textures,
  shadows, water.
- S25 with the validation layer: 0 errors over 9,627 frames.

## 4. Frame-copy pass split: INVESTIGATED; the avoidable split REMOVED (`7e23be243`)

**What split the main target** (the pass log of a game frame on the base tree):

1. reflection (own target);
2. sun map (own target);
3. main pass A: ended at the water's first draw. The reflection image was still in COLOR_ATTACHMENT, so
   `PrepareTextures` ended the pass to move it to SHADER_READ_ONLY.
4. main pass B: ended by `DrawResolve`, the frame copy.
5. main pass C: to the frame's end. The frame copy's image was left in TRANSFER_DST; in this order no draw
   sampled it inside C, but one that did would have split it again.

**Fixed (small and safe):**
- At the start of a main pass, every render-buffer image drawn earlier in the frame and still in an
  attachment layout moves to SHADER_READ_ONLY, between passes. One that is rendered again moves back at its
  own pass.
- `DrawResolve` moves the copy's destination to SHADER_READ_ONLY right after the copy.
- Result: 3 main passes become 2 (5 -> 4 passes a frame on Windows and the S25), and the discard covers 1 of
  the 2.

**Saving, from the formats (not measured):**
- One colour store + load and one depth store + load fewer per frame.
- At the phone's 1170x540, BGRA8 colour and D24S8 depth are 2.53 MB each: about 10 MB a frame, or about
  1.2 GB/s at 120 fps.
- At full resolution, four times that.

**What is left, and why it stays:** the split at the frame copy itself (main pass A, copy, main pass B):
- A copy cannot run inside a render pass.
- The water samples the copy at offset coordinates, so tile-local reads (input attachments, framebuffer fetch)
  cannot replace it.
- Pass B depth-tests against pass A's depth, so A must store it. The remaining store is the one retail's
  structure requires.

**A redesign would need one of these:**
- (a) the copy taken from a previous frame: a one-frame lag of the refraction, not faithful, an `ext.*` row
  at best;
- (b) a second colour output written by the opaque draws themselves (MRT): a shader rewrite of every opaque
  program, against "the DX11 look is the reference";
- (c) moving the water before the copy: changes the draw order, not faithful.

None is small or safe. The plan is to leave it.

## 5. Unverified P7 items

**(a) The oversized push-descriptor pair: VERIFIED by a forced exercise (not committed).**
- `PairLayoutFor`'s limit was set to 3 bindings in a local build, so every pair with 4 or more pixel bindings
  took the pool path: 10 pairs, logged as `pixel stage with N bindings ... takes the pool path`.
- Pool and push pairs mixed in one frame: 47 sets and 48 pushes in the frame line.
- Windows Vulkan, debug, validation on, MSAA 4x: the town renders as normal and validation has 0 errors.
- The temporary limit was reverted before any commit.

**(b) The Windows Vulkan boot state left RUNNING after a clean exit: REAL, FIXED (`ea06201ea`).**
- The cause: `BootState::Stop()` (which writes `STATE=EXITED`) had callers on Darwin and Android only. On
  Windows (both backends) and Linux, the last one-second tick stayed in the file: RUNNING, or QUITTING when
  the tick landed after the quit.
- The fix: `Platform::App::Shutdown` calls it on Windows and Linux too.
- `tools/boot_state.sh` reads an EXITED line whose pid is gone as `NOT_RUNNING (clean shutdown)` instead of
  STALE. `tools/boot_state_controls.sh` gains that case (N4b); 17/17 PASS.
- Verified:
  - Windows Vulkan pause-menu quit: `STATE=EXITED ... detail=clean-shutdown`;
  - Windows DX11: the same;
  - the S25: EXITED, as before.

## Phone state at the end

- Installed: the DEBUG apk of `bb82f4506` (this branch; `adb install -r -d` over versionCode 10001).
- Validation off: the settings deleted, the layer removed.
- The app stopped.
- `settings.cfg` untouched: the phone defaults, with `VideoShadowScalar=2`, render scale 50 and MSAA 0.
