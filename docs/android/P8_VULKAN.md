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

## 6. Round 2: the owner's S25 test of the merged tree (`android/p8`, PR #53)

Run on the merged tree with the owner's own `settings.cfg` (backed up first and restored byte for byte after;
see the phone state below):
- `VideoShadowScalar=1` (a 1024 sun map);
- render scale 100, MSAA off, `VideoTextureFilter=16`;
- the extensions on except AO.

### 6.1 AO did nothing on the S25: FIXED (`5d34a3287`, `3c286f6e5`)

**The cause.**
- `vkCreateGraphicsPipelines` returned `VK_ERROR_UNKNOWN` for the AO pass's first pipeline. The log line:
  `[AmbientOcclusion] Vulkan: pipeline PSResolveDepth1x: -13`. logcat had
  `AdrenoVK-0: Shader compilation failed for shaderType: 0`.
- The pass then failed at its first step every frame, so the AO rows changed nothing.
- The validation layer had nothing to say.

**Finding the shader.** The failing stage was the vertex shader, `VSFullscreen`. Compared with every retail
vertex program (a census over the 465 permutations), it is the only one with `OpBitFieldInsert` (dxbc-spirv's
form of SM5 `bfi`). Each experiment was a phone run:

| tried | result |
|---|---|
| signed image coordinates for the pixel shader's fetch | still `-13`; not kept |
| `OpBitFieldInsert` rewritten as shifts and masks | the pipeline builds; `first AO frame 2340x1080 x1` logged, no failure |

**The fix.**
- `spirv_legalize.{h,cpp}` lowers every `OpBitFieldInsert` with a constant offset and count to
  `((insert << offset) & mask) | (base & ~mask)`. It runs for every converted module.
- `kMappingVersion` 8. Versions 6 and 7 were test builds on the phone and are skipped, so their cache entries
  are never read back.
- Unit test `vulkan_ao_bitfield_insert_lowered`. `vulkan-shader-check`: 465/465 still convert and pass
  spirv-val.

**"Not supported" instead of a silent stub (`3c286f6e5`).**
- A pipeline the driver refuses now makes `Assao::Ready()` false, so `AoBackend::Supported` is false.
- The row then goes inactive, logged.
- The Enhanced tab disables the AO combo and the strength slider wherever the backend cannot run the pass.

**Verified:**
- S25 with the final apk: `ASSAO ready`, the AO frame drawn, no pipeline failure, clean exit (`EXITED`).
- Windows Vulkan, non-faithful profile, MSAA 4x: the AO frame drawn.

**What is left.** On the tiler-MSAA path (`VideoMultisample` > 0 on the phone) AO still does not run. That is
the known row "AO on the tiler MSAA path" (`docs/known_port_issues.md`). The pass logs it, but the Enhanced
rows stay enabled on that path. **Ruling:** should they also show disabled when MSAA is on?

### 6.2 Anisotropic filtering does nothing: the setting reaches the samplers (`d38986100`)

**The chain, read and then logged.**
- `VideoTextureFilter` is read into `Configuration::textureFilter`.
- `SamplerFor` (`companions.cpp`) gives the anisotropic kind with that level, clamped to the adapter's
  `maxFilterLevel` (16 on the S25).
- `GetSampler` sets `anisotropyEnable` / `maxAnisotropy`. The device enables `samplerAnisotropy` when the GPU
  has it.

**The new log line.** On the S25: `[Vulkan] sampler: anisotropic 16x requested, enabled (device max 16)`. The
adapter's `maxFilterLevel` is now 0 when the device lacks the feature, so the Options combo never offers a
level the device cannot honour.

**Why it can look like nothing.**
- As on DX11 (`imagebufferdata.cpp`), a texture's sampler is made when the texture is created. A change in
  Options applies at the next start, not live: retail's rule on both backends.
- Banished's camera looks steeply down, so the S25 screenshots at 16x and 0x (pinned on the town at 3 and 80)
  show no difference worth quoting. Their weather also differed between runs.

**Ruling:** should the Options row say "applies at the next start"? That would be an `ext.*` UI change, so it
needs a ruling.

### 6.3 Citizen shadows flicker: NOT FOUND, measured further

What was measured on the owner's settings:

1. **Frame-to-frame differences.** A pinned camera at distances 7, 12 and 18, and the free autostart camera,
   recorded at 120 fps (`screenrecord` up to 40 Mbit/s). The frame-to-frame difference of a 400x250 crop
   around walking citizens shows:
   - their silhouettes;
   - their shadows moving in sun-map texel steps as they walk (the shadow follows its caster);
   - nothing on standing citizens or static shadows.

   On a still camera nothing flickers.
2. **The sun camera's fit, logged per call** (a temporary log, not committed).
   - It runs twice a frame with identical results.
   - The texel snap (`sun_camera.cpp`, retail's `fmod` onto the texel grid) holds: texel 0.0837 world units
     at 1024 (dim, radius 56.3, skew 0.761), constant while the camera is still.
   - The texel size changes only while the camera zooms or tilts (radius and skew change). Retail's shadows
     crawl during that too.
   - The fit is engine code, the same on DX11 and Vulkan.
3. **Input.** `adb shell input keyevent` and `swipe` did not move the camera in a way that could be measured
   (the swipe dragged a window). An unattended moving-camera recording was not possible.
4. **RenderDoc on Android.** Not done: it needs the qrenderdoc GUI driven by hand (`PERF_P7.md`).

Ruled out on the backend side (round 1, §1): sync hazards (synchronisation validation), the comparison
sampler, depth bias and slope, the sun map's format, store op and resolve (not msrtss). The combined-sampler
conversion is ruled out too: the pair census shows t5+s5 (the sun map's slot) combined like every texture.

**Candidates that are NOT backend code, for the owner to toggle** (Options > Extensions, live):
- **`ext.render_culling`** (class A, on by default).
  - Its `animCull` / `geomCull` skip the pose update and `SetTransform` of a citizen off-screen of the VIEW
    camera.
  - Such a citizen still casts its shadow INTO the view (the sun map covers more than the view).
  - Its shadow can then freeze and jump as it crosses the view's edge. The phone's 19.5:9 view puts more
    citizens near that edge.
- **`ext.render_pipe`** (class A, on): the render thread draws the frame the main thread handed over.
  `RENDERPIPE0.md` §6's hash names any writer between the gather and the emit.

**What to ask the owner to record:**
1. The same spot twice, one minute each: once with `ext.render_culling` on, once off. Settings as played,
   camera moving as in play.
2. A screen recording on the S25 of the flicker as seen. Use the phone's own screen recorder, or
   `adb shell screenrecord --bit-rate 40000000 --time-limit 30 /sdcard/flick.mp4`.
3. Whether the same scene on the PC flickers:
   - DX11, the same shadow quality (Options > Video);
   - the same extensions;
   - camera moved the same way.
4. The time of day and weather (the sun direction changes the snap grid's orientation through the day).

## 7. Round 3: two owner rulings

### 7.1 AO on the tiler MSAA path: BUILT (`359c1c379`)

**The change.**
- `assao_pass.cpp`: the msrtss case is no longer skipped. The depth resolve picks `PSResolveDepth1x`
  when `t.msrtss` (the main depth image is single-sampled there), `PSResolveDepthMs` only for a real
  multisampled depth. The apply's pipeline is made at the MSAA count, as every main pass on that path.
- `draw.cpp` `DrawAoBegin`: on msrtss a memory barrier from the colour-output stage (where an implicit
  resolve writes) to the AO pass's fragment reads, before the depth's move to `SHADER_READ_ONLY`.

**Checked on the way.**
- (a) Stored with a resolve mode: `BeginPass` already gives every msrtss pass's depth and stencil
  `SAMPLE_ZERO` with `STORE`. Nothing was `DONT_CARE` except through the depth discard.
- (b) The depth discard: `MainDepthPass(loads, allowed = !g_readableRequested)`. With AO on, the frame
  lines read `depth 0/3 discarded` (none).
- (c) The depth view: `DrawAoBegin` hands the `depthSampled` view (depth aspect) of the single-sampled
  image, in `SHADER_READ_ONLY`. The next main pass loads it back.

**Verified on the S25** (debug apk of `359c1c379`, seeded autostart, camera pinned on the town).
- Validation on, MSAA 2x + AO Low, 90 s: no validation error. The only message is the known shadow-pass
  warning (a depth-only pass whose pixel shader writes `SV_Target`), present since P7.
- The log: `the main target uses the tiler's MSAA ...: the AO pass reads its single-sampled depth
  (sample 0) as the non-MSAA path`, then `first AO frame 2340x1080 x2`.
- Screenshots, AO High, render scale 100, the same pin: MSAA 0 off / on and MSAA 2x off / on. AO
  darkens the building bases and the ground under the stockpile the same way at 0x and 2x. The mean
  luminance of the town crop drops 2.6 (0x) and 3.8 (2x) levels with AO on. The screenshots are not
  in the tree.

**GPU cost** (render scale 50, MSAA 2x, town pinned, the frame cap at 60, two alternated runs of each,
40 one-second kgsl samples after 50 s; machine load: other agents building on the PC):

| | GPU busy | GPU clock |
|---|---|---|
| MSAA 2x | 65.3 %, 65.0 % | 160, 162 MHz |
| MSAA 2x + AO Low | 78.0 %, 77.7 % | 166, 165 MHz |

The governor stays at its lowest step in both. At 60 fps, 12.7 points of busy are about 2.1 ms of GPU time
a frame at that clock.

**The Enhanced AO rows** stay enabled on this path. They show disabled only when the driver refuses an
AO pipeline (§6.1).

### 7.2 Texture filter: "(next start)" on the choices (`6728bebee`)

- Retail's Options > Video "Texture Filtering" combo writes `Video::Configuration::textureFilter`. Both
  backends read it when a texture is created, so a new choice reaches the game's textures at the next
  start (§6.2).
- `src/ext/enhanced_tab/texture_filter_note.cpp`, called at the end of `OptionsDialog::PopulateFilterCombo`
  (an `EXT SEAM`). Every choice other than the filter the textures were made with gets the suffix
  `(next start)`, as the Enhanced tab's own `On (next start)` / `Half (next start)` items.
- The suffix is ours: `NextStartSuffix` in `Dialog/ExtensionsStringTable.rsc`. Retail's string table is
  not edited.
- **The owner.** No extension changed retail's Options text before this. The note rides on the Enhanced
  tab's host (`Ext::EnhancedTab::Offered()`: everywhere except `/profile:faithful`), the extension
  that owns the Options dialog's additions.
- The filter "in effect" is the configuration's value at the first Options open. Only this dialog's Apply
  changes it, and a later Apply does not change the textures already made.
- Trace: `[EnhancedTab] Texture Filter: N of M choices marked "(next start)" (textures made with filter F)`.

**Verified on Windows DX11** (debug build, functional game dir, Options > Video):
- The closed combo reads `Anisotropic 4x`, the filter in effect, with no suffix.
- The trace reads `10 of 11 choices marked`.
- A throwaway build that showed the last item reads `Anisotropic 16x (next start)`. The widest item
  fits the 180-wide combo, its last letter at the drop arrow's edge (UI scale 100, 2560x1440).
- Under `/profile:faithful` the combo is retail's and there is no trace.

## Phone state at the end

- Installed: the DEBUG apk of `359c1c379` (this branch, round 3; `adb install -r -d`).
- Validation off.
- The app stopped.
- `settings.cfg`: the owner's own file, restored byte for byte from the copy taken before round 2 (`cmp`
  clean). The copy is still at `/sdcard/p8_settings_backup.cfg`.
- One round-2 run read no settings (`ReadSettings: 0 entries ... defaults`) after an in-place `sed -i` over adb,
  and wrote a defaults file at its exit. Restored the same way.
- The test videos and screenshots under `/sdcard` are deleted.
