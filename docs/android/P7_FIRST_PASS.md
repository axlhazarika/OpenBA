# P7 first pass: Android fit on the Galaxy S25

Phase P7 of `docs/vulkan/PLAN.md` §5, first pass (2026-10-25). The work list came from the survey
`docs/android/PERF_P7.md`. Device: Galaxy S25 (SM-S931U, Adreno 830, driver 1.3.284, FIFO at 120 Hz,
panel 1080x2340 portrait, pre-rotated). One commit per item. Each commit title carries the number the
item moved.

## How it was measured

- **Builds:** the perf flavour (`-O2`, release apk signed with the debug key, `profileable` added
  locally for simpleperf and never committed). Validation runs use the debug apk.
- **Runs:** two kinds.
  - **Stress:** `BANISHED_STRESS=1`, the 800-citizen town at speed 10. `[Stress]` lines at t=100 s.
  - **Town:** a plain new game, `BANISHED_SEED=12345 BANISHED_CAMERA_PIN=town`. The first 30 s are
    left to settle, then 25 samples at 4 s intervals (60 samples for the cap runs).
- **Sampled values:** the frame counter from the boot-state file, GPU busy %, GPU clock and GPU
  temperature from kgsl sysfs, the thermal status (`dumpsys thermalservice`), and SurfaceFlinger
  timestats.
- **Cooling:** before every run the phone cooled to thermal status 0 and a battery temperature of
  36.0 C or less.
- **Fixed clocks:** `cmd power set-fixed-performance-mode-enabled true` pins the GPU at 443 MHz.
  It was on for the A/B runs, so throttling does not mix into the comparison. It was off for the
  final cap runs.
- **Unseeded runs:** town runs before C2 used no seed, so the towns differ. Their numbers can only
  be compared within a row, never across rows.
- **CPU profiles:** simpleperf `cpu-clock` over 30 s of the seeded town.

## A. Validation layer on the device (`e5bc12f82`)

`tools/android.sh validation on <libVkLayer_khronos_validation.so> | off` (README "Validation layers").
The layer is 1.4.363.0 from Vulkan-ValidationLayers' `android-binaries` asset. It is not in the tree.
The run: debug apk, menu, new game, pause-menu quit, 7,220 frames.

| | errors (distinct) | warnings |
|---|---|---|
| before | 4: the present source sampled in UNDEFINED on the first frame; a byte-4 vertex attribute (`R8G8B8A8_UINT`) fed to an `int4` input, three pipelines | the depth-only pass's `SV_Target` |
| after | 0 | the depth-only pass's `SV_Target` (as D3D11 with a null render target) |

The fixes:
- The present source starts in UNDEFINED until the first present pass has read it
  (`PresentSourceLayout`).
- The shader reflection records each vertex input's numeric type (`kMappingVersion` 4). An `int4`
  input fed by the byte-4 element reads it as `R8G8B8A8_SINT`. That is exact for bytes below 128; the
  divergence is in `docs/known_port_issues.md`.

C2 found and fixed two more errors on the tiler MSAA path, which the 1x run never reached:
- a single-sampled attachment needs `resolveMode` under msrtss;
- the msrtss struct is not valid in a pipeline's `pNext`.

On the final merged tree, the debug run (8,114 frames) has 0 errors and only the `SV_Target` warning.

## B. Lifecycle (`2fbb8e6ac`)

| check | before | after |
|---|---|---|
| pipeline cache at HOME | written only at a clean quit | `vulkan_pipelines.bin` (355,893 bytes) mtime = the second of HOME, warm list with it |
| watchdog over a 40 s HOME | stale grows, HUNG past the budget | `RUNNING stale=0 detail=background` throughout |
| `am send-trim-memory ... RUNNING_CRITICAL` in a town | nothing | `[Lifecycle] SDL_EVENT_LOW_MEMORY: a save is requested`, `Save/Auto.sav` written the same second |
| LOW_MEMORY that arrives in the background (the HOME trip's TRIM_MEMORY_UI_HIDDEN) | n/a | logged and dropped (the main thread is blocked there) |

`TERMINATING` also asks for a save, but SDL queues `SDL_EVENT_QUIT` first, so the quit usually wins.
After a trip to the background with the pause UI up, the autosave is refused ("UI PausesGame").
That is the game's own rule. The four Android rows in `docs/known_port_issues.md` are updated:

| row | status |
|---|---|
| watchdog | FIXED |
| pipeline cache | FIXED |
| save on low memory | narrowed |
| font rasteriser | unchanged |

## C1. Push descriptors (`b902738b2`)

Adreno allows one push set per pipeline layout (`maxPushDescriptors` 32). The new layout:
- each shader pair gets a pair pipeline layout;
- set 1 (the pixel set) is pushed with `vkCmdPushDescriptorSetKHR`;
- set 0 (the vertex set) is pooled, its constant buffers dynamic, and cached per bound state, so a
  draw that changes only offsets rebinds nothing.

The old single-layout path stays for devices without the extension.

| | before | after |
|---|---|---|
| render thread `StageSet` + descriptor bind (simpleperf, seeded town) | 37.2 % of the thread | under 11 % (`PushPixelSet` 7.3 %) |
| render thread CPU, 30 s | 7.32 s | 5.46 s (-25 %) |
| stress draw ms (fixed 443 MHz) | 5.74 | 4.31 |
| stress fps (fixed 443 MHz, GPU-bound at 90 % busy) | 100.1 | 97.8 (noise; the GPU is the limit) |

## C2. Store ops and the MSAA end copy (`7d0291242`)

- **THE DEPTH DISCARD (`draw.cpp`):**
  - The last main-target pass that uses depth is predicted from the previous frame's count, and its
    depth store is `DONT_CARE`.
  - A misprediction (a later pass loads the depth) clears it instead. That pass's geometry then
    draws against an empty depth for one frame and shows through the scene.
  - Review of PR #50 (finding 1): the first version paused the prediction for 60 frames after a miss
    and resumed it, and a session counted 2 misses. Now:
    - the prediction starts only after the count of main-depth passes has been the same for 600
      frames in a row;
    - one miss turns the discard off for the rest of the session, with a log line.
  - The frame log shows both: `depth 1/3 discarded (0 mispredicted)`. The main target is split by
    the frame copy, so only the last of the 3 passes discards.
- **Tiler MSAA (`caps.msrtss`):** the frame renders into the present source, which is made with the
  msrtss flag. The end copy into a separate image is gone.

| | before | after |
|---|---|---|
| stress fps, fixed 443 MHz | 98-100 | 98-101 (noise) |
| validation, MSAA 2x | 2 errors (above) | 0 |

The seeded town at fixed 443 MHz after C2 shows 71.5 % GPU busy at 120.0 fps. No seeded baseline
exists before C2 (the unseeded base town was 81.8 % at 119.0 fps).

Where the discard runs after the review fix (final tree, half resolution):

| run | depth passes per frame | discard | misses |
|---|---|---|---|
| plain new game, debug, validation on, HOME and back (18,900 frames) | 1 at the menu start and the load, then 3 | on (1 of 3) from about frame 1,500 | 0 |
| menu held 1,500 frames (`BANISHED_AUTOSTART=1500`), then the game (7,200 frames) | 3 in the menu and the game, 1 during the load | on (1 of 3) | 0 |
| Windows RTX 5080, new game (debug, validation on) | 3 | on (1 of 3) | 0 |
| stress town, perf, fixed 443 MHz | 1 for the first ~1,800 frames, then 3 | off from frame ~2,000 for the session | 1 |

- **The stress town turns it off.** The harness's first ~1,800 frames hold one main-depth pass, long
  enough to arm the prediction. The first frame with 3 passes discards after pass 0 and pass 1 loads
  it. That miss is the off-after-one rule working: it is the frame that drew through the scene.
- **Stress numbers with the discard off** (fixed 443 MHz): 163.1 fps, frame 6.13 ms, update 4.14 ms
  (0.414 ms/iter), draw 3.79 ms. The C6 run with the old resume-after-60 rule had 162.3 fps, frame
  6.16 ms, draw 3.82 ms. In this town the discard's saving is inside the noise.
- **A plain session keeps it on**, menu included: the menu draws 3 depth passes like the game.

## C3. Gather helper spin (`2e76704d4`)

In `ext.gather_threads`, an idle helper spins for 50 µs and then sleeps on a condition variable. It
wakes on a new ticket, Wake, or quit. `Publish` notifies only when a helper is asleep.

| | before | after |
|---|---|---|
| process CPU, seeded town, 30 s (simpleperf, fixed clocks) | 11.01 s | 10.66 s (-3.2 %) |
| `HelperLoop` spin, 30 s | 0.92 s | 0.29 s |
| stress update ms / sim ms per iteration (fixed) | 4.99 / 0.499 | 5.02 / 0.502 (unchanged) |

Review of PR #50 fixed two flaws in the sleep:
- **Finding 2:** a helper asleep through a `Park` woke on the next `Wake` with a stale epoch, and
  then spun on the lock every 50 µs for the whole frame. It now takes the new epoch as seen.
- **Finding 3:** a `Publish` landing between the failed claim and the sleep was slept through. The
  helper now re-checks the ticket and claims instead of sleeping.

After the fixes (seeded town, half resolution, fixed 443 MHz, simpleperf 30 s):
- process CPU: 10.55 s;
- `HelperLoop` self time: 0.26 s, as low as the 0.29 s above, so no lock spin;
- stress update 4.14 ms (0.414 ms/iter), unchanged against C6 (4.15).

Windows A/B, perf flavour, stress town, `STRESS_EXT=draw_gather,gather_threads`, machine load CLEAN:

| | sim ms/iter | update ms | draw ms | fps |
|---|---|---|---|---|
| without C3 | 0.15 | 1.50 | 0.98 | 140 |
| with C3 | 0.15 | 1.50 | 1.00 | 140 |

## C4. Hygiene (`e019db346`)

- `VK_EXT_vertex_input_dynamic_state` is still probed but no longer enabled. No path used it.
- Three swap-chain images is not possible on the S25: FIFO's surface minimum is 4. The swap chain
  line now says so: `4 images (asked 4, surface min 4)`.
- Seeded town GPU busy (fixed): 71.4 → 70.2 % (noise).

## C5. Phone defaults (`ef60dad1d`)

- `GameEdgeScroll=false` is seeded in the first-run `settings.cfg` (ruled 2026-10-25). A touch held
  at the screen's edge is a cursor parked there.
- The 60 fps cap is not seeded (ruling pending; data below).
- Verified on the phone: settings.cfg deleted, app launched, and the file holds
  `GameEdgeScroll=false`, `VideoRenderScale=50`, `VideoMultisample=0`. Options > Game shows edge
  scrolling unticked.

## C6. Half-resolution render scale (`3a17dd6c2`)

`VideoRenderScale=<percent>` (25..100, default 100) in settings.cfg. It is read through a new hook,
`Platform::Window::RenderScale()`: 1.0 on Windows, Linux and macOS, and read once from the user's
settings.cfg on Android.

How the scale reaches the engine:
- The SDL layer multiplies the window's pixel size by the scale (`Sdl::WindowSizeInEnginePixels`).
- The places that take it: `GetClientRect`, `GetWindowRect`, the input's client size, `SIZE_CHANGED`,
  and the pixel density. The pixel density scales the mouse, so the back buffer, the cursor and the
  UI agree.
- The Vulkan swap chain keeps the panel's size.
- The present pass now runs when the frame and the images differ, turned or not (`PresentActive`).
  It scales the 1170x540 frame up with linear filtering.

The phone's first-run file seeds 50. Checked on the phone:
- `[Window] render scale 0.50`, `present pass: frame 1170x540 drawn into 1080x2340`,
  `main targets 1170x540`;
- the menu, the Options dialog and the game screen look right;
- taps on Options and Cancel land on the buttons;
- validation is clean.

| (fixed 443 MHz) | full res 2340x1080 (C4) | half res 1170x540 |
|---|---|---|
| seeded town fps | 119.0 | 120.1 |
| seeded town GPU busy | 70.2 % | 40.0 % (32.3 % in the C7 repeat) |
| seeded town GPU temp max | 48.4 C | 43.4 C |
| stress fps | 97.8 (C3) | 162.3 |
| stress frame ms (p95) | 10.23 (19.12) | 6.16 (13.59) |
| stress draw ms | 4.34 | 3.82 |
| stress GPU busy | 90.0 % | 88.5 % (still GPU-bound, at 1.66x the frames) |

## C7. MSAA 2x (tiler msrtss) against off, at half resolution

| (fixed 443 MHz, 1170x540) | MSAA off | MSAA 2x msrtss | cost |
|---|---|---|---|
| seeded town fps | 120.1 | 120.1 | none |
| seeded town GPU busy | 32.3 % (40.0 % in the C6 run) | 36.2 % | +3.9 points (+12 % relative to 32.3) |
| stress fps | 162.3 | 146.8 | -9.6 % |
| stress frame ms | 6.16 | 6.81 | +0.65 ms |

The rule was to change the phone default only below ~10 % cost. Stress fps comes in just under
(-9.6 %), but the town's GPU busy goes up 12 %, so the result is borderline. **The phone default
stays `VideoMultisample=0`.** A switch to 2x is a ruling for the owner, made on these numbers. With
msrtss, `ext.ambient_occlusion` does not run (known_port_issues "AO on the tiler MSAA path"), but AO
is off on the phone anyway.

## The 60 fps question (for the ruling)

These runs use the final tree (C1-C6), normal clocks (fixed mode off), the seeded town and 5 minutes
of samples each.

| | 120 Hz FIFO | `BANISHED_FPS_CAP=60` | panel at 60 Hz (Samsung "Standard" motion smoothness) |
|---|---|---|---|
| fps held | 120.1 | 119.7 (the cap did nothing, below) | 59.8 |
| GPU busy mean | 67.7 % | 68.8 % | 43.5 % |
| GPU clock | 210 MHz mean (160 first 40 s, then 222) | 208 mean (160-342) | 160 (the floor) throughout |
| GPU temp max | 44.2 C | 43.4 C | 39.9 C |
| thermal status | 0 throughout | 0 throughout | 0 throughout |
| CPU, simpleperf 30 s (cpu-clock) | 24.4 s | (not profiled) | 18.7 s (-23 %) |

- **The cap does not work in game.** `BANISHED_FPS_CAP=60` logs `[FrameLimiter] ceiling = 60.0 FPS`,
  but the run held 120. Two reasons:
  - `FrameLimiter::Tick` has one call site, the menu loop's Draw (`menudialogloopt_stubs.cpp`);
  - the limiter stands aside by design when an external limiter (here FIFO vsync) already holds the
    rate at or below the refresh rate.
- **The 60 fps numbers come from the panel.** `settings put secure refresh_rate_mode 0` (Samsung's
  Standard mode, a 60 Hz panel; restored to 1 afterwards) gave the 60 fps column. FIFO then paces
  the game to 60.
- **What a phone default would need:** an in-game cap, either a settings key or `SetFrameRate` on the
  surface. Not built: the cap is unruled, and P7 added no cap key.
- **120 fps at the downclocked GPU: yes.**
  - With the clock fixed at 443 MHz (below the 525 MHz the survey saw after throttling), the seeded
    town holds 120.1 fps at 40 % busy (32 % in the repeat). Before half resolution it held 119-120 at
    70 %.
  - At normal clocks the governor picks 160-222 MHz and still holds 120.1 at 68 % busy, thermal 0,
    over 5 minutes.
  - The GPU never needed more than 222 MHz for 120 fps in the plain town.
- **What 60 Hz still saves:**
  - GPU busy falls from 68 % to 44 % at the minimum clock;
  - CPU falls by about 23 %;
  - the GPU runs about 4 C cooler.
  - Neither rate throttled in 5 minutes.

## Left (not done in this pass)

- **Shadows off:** (corrected in P8) retail has one, Options > Shadow Quality = None (`VideoShaderDetail=0`);
  the line here looked at Shadow Resolution only. `P8_SHADOWS_RELEASE.md`.
- **Combined image samplers:** the converter's separate texture / sampler bindings stay.
- **Frame-copy pass split redesign:** the frame copy splits the main target, which is why 2 of 3
  depth passes still store (the discard counter shows it).
- **Release signing:** the perf apk is signed with the debug key; no release key, and no apk in
  `release.yml`. (Done in P8: `release.yml` job `android`, `tools/android.sh sign`.)
- **In-game fps cap:** see above.
- **HWASan and a thermal soak** longer than 5 minutes.
- **The depth discard after a pass-count change:** a session whose frame structure changes after the
  prediction armed (the stress town) loses the discard for good. A smarter arming rule (for example,
  re-arming at a known frame-structure change) is left for later; the rule now never draws a second
  wrong frame.
- **A pair too large to push** (review finding 4) draws through the pool path, logged as
  `[Vulkan] pair layout: pixel stage with N bindings`. No retail shader pair reaches 32 bindings in a
  stage, so neither the S25 nor the RTX 5080 has run that path. It is unverified in a run, and no
  test fakes a 33-binding pair.
- **In-game trim-memory save after the review fix:** the menu-time request is dropped at Startup
  (seen on the phone: `a save request made while no game ran is dropped`). The second in-game
  `send-trim-memory` in that run was refused by Android ("Unable to set a higher trim level than
  current level"), so the in-game save was not re-run after the fix. The first pass verified it.

## Phone state at the end

- DEBUG apk of the merged tree installed; app stopped.
- Validation off (settings deleted, layer removed).
- Fixed performance mode off; `refresh_rate_mode` back to 1 (adaptive 120 Hz).
- `Documents/Banished/settings.cfg` removed, so the next start is a first run that writes the full
  phone seed (including `ext.phone_menu=1`).
- The pre-P7 settings file is kept off-device by the agent, not in the tree.
