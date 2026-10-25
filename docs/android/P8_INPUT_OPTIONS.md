# Android P8: middle-mouse release, render scale and frame rate cap in Options, rotation

NOT RETAIL. One of the four P8 passes (`docs/runtime_port/NEXT_SESSION.md` §2.7). Device: Galaxy S25
(SM-S931U, Adreno 830, 120 Hz FIFO). Branch based on `main` 8e94db750.

## 1. Middle-mouse release lost (real mouse over a KVM)

**Cause: SDL's Java glue, not our pump.** The middle press arrives uncaptured (`SDLSurface.onTouch`),
the game's `ReadMouse` sees it and turns on relative mode for the camera drag
(`input_sdl.cpp` `SetRotateDrag`), which SDL implements on Android as `requestPointerCapture()`. From
then on Android delivers the mouse to `onCapturedPointerEvent`, which the pinned SDL (3.4.18) forwards to
a listener that keeps only scroll and hover motion: the drag's `ACTION_MOVE` and the release
(`ACTION_BUTTON_RELEASE` / `ACTION_UP`) are dropped. SDL's button state keeps the middle button down,
the game keeps rotating and never ends the capture. Facts: `docs/reference/android_mouse.md`.

**Fix, on our side:** `android/app/src/main/java/org/openba/game/OpenBASurface.java` (an `SDLSurface`
subclass, made by `OpenBAActivity.createSDLSurface`) passes the captured mouse's motion and button
changes to `SDLActivity.onNativeMouse` as SDL's `onTouch` would. No change in `third_party/SDL` (the
worktree has no submodule of its own, and SDL's contribution rules exclude generated code); an SDL bump
that fixes `SDLSurface.onCapturedPointerEvent` would make the override redundant.

**Verified on the S25 with a uinput mouse** (`adb shell uinput`, a three-button evdev mouse; the script
is in the reference):
- before (the apk installed at the start, without the fix): middle press -> `Pointer Capture: RELATIVE`;
  after the release the window still held the capture 6 s later (`Current Window with Pointer Capture:
  ...OpenBAActivity`);
- after: `RELATIVE` while held, `UNCAPTURED` / `None` right after the release
  (`PointerCaptureChangedEvent(mode=UNCAPTURED)` at the release's time);
- in game, a middle drag of 8 x 60 counts turned the camera (screenshots before / after) and the
  capture ended with the release.

**Unverified:** a real mouse over the owner's KVM (a KVM may send its own report shapes; the uinput
mouse goes through the same `CursorInputMapper` path).

## 2. Options entries: render scale and frame rate cap

Both are rows of the Options dialog's Enhanced tab (`src/ext/ui/Dialog/EnhancedOptions.rsc`, now ten
rows), after Texture quality.

**Render scale** (`src/ext/enhanced_tab/render_scale_entry.cpp`): Full / Half / Quarter =
`VideoRenderScale` 100 / 50 / 25. Not an extension row: the key is the platform setting P7 made, read by
`Platform::Window::RenderScale`. Two new hooks in `platform_window_hooks.h`: `RenderScaleAdjustable()`
(true on Android only) and `SetRenderScale(percent)` (Android: an atomic percent, since the Vulkan
backend reads it on the render thread under `ext.render_pipe`; identity twins on Windows, POSIX,
macOS). Apply sets the scale, raises `Video::Device::settingsChanged` and records `VideoRenderScale`;
the Vulkan backend's existing `ApplyEngineSettingsChange` then republishes the window's (scaled) size as
the back buffer and rebuilds every dynamic target and the swap chain. On a desktop the combo shows Full,
disabled (`RenderScale` is 1.0; the DX11 / desktop present path does not scale).

**Frame rate cap** (`src/ext/fps_cap/`): the class A row `ext.fps_cap` with the discrete key
`ext.fps_cap_rate` (30 / 60 / 90 / 120, default 60); the combo is Off / 30 / 60 / 90 / 120 fps.
Class A because it changes frames only: `MainLoop::Frame`'s fixed-step accumulator runs every simulation
step the elapsed time owes, nothing is saved, and it is live. Default OFF (an exception to "class A ships
ON", like `phone_menu`: on a desktop the game stays uncapped as retail); the Android first run seeds
`ext.fps_cap=1`, `ext.fps_cap_rate=60` (ruled 2026-10-25). It is not an env flag.

**The in-game cap** (`src/app/frame_limiter.{h,cpp}`, `frame_limiter_ceiling.h`): `FrameLimiter::Tick`
paced the menu only. Now `TickInGame` is called at the start of `GameUpdateLoop::Draw` and of
`ext.render_pipe`'s main half (its copy of that Draw). Which ceiling a loop gets
(`ResolveCeiling`, unit-tested): `BANISHED_FPS_CAP` (off = nothing in either loop, a number = that in
both) over the settings cap (both loops; the menu takes the lower of it and its spin guard) over the
menu's refresh x 1.5 spin guard (menu only). With neither a flag nor a settings cap the game is not paced
(one test per frame): the desktop default is unchanged (menu: refresh x 1.5, game: uncapped). The
`BANISHED_FPS_CAP` meaning grows from "menu" to "menu and game" (`options.h` comment); the perf scripts set
`off`, which they already meant for both.

**Verified on the S25** (debug apk of this branch, a first run seeding the new keys):
- menu `ceiling = 60.0 FPS [settings cap]`, game `fps=60.0 (cap=60.0) in game` (1,800 frames in 30 s;
  before the render_pipe call was added the game ran at 119 fps: `ext.render_pipe` replaces
  `GameUpdateLoop::Draw`);
- Options > Enhanced shows Render scale Half and Frame rate cap 60 fps; Half -> Quarter: `[Vulkan]
  resize: wanted 1170x540 -> window 585x270`, `main targets 585x270`, taps land on the combo at Quarter;
  Quarter -> Full: `main targets 2340x1080`; `VideoRenderScale=100` written; no crash, boot state
  RUNNING throughout;
- Frame rate cap 60 -> 30: 600 frames in 20 s; -> Off: `game: not paced`, 2,401 frames in 20 s (120 Hz).
- Windows DX11 debug, the gate route (`tools/build.sh run`, `/profile:faithful`, functional dir): new game,
  `game: not paced`, menu `ceiling = 216.0 FPS [refresh+headroom]` as before; Windows Vulkan debug builds;
  unit 1481 / 0 (1 skipped); acceptance 0 off target.

**Unverified:** the desktop's disabled Render scale combo (the row's code path is two lines; no
desktop run opened the Enhanced tab); power / thermal numbers at the 60 cap (P7's 60 Hz column was the
panel's refresh, not this CPU cap; a CPU cap leaves the panel at 120 Hz, so the savings may be smaller.
`ANativeWindow_setFrameRate` would let the panel drop to 60 too; not built).

## 3. Rotation / display change at render scale 50

`settings put system accelerometer_rotation 0`, `user_rotation 3` then `1` with the game running at 50:
the swap chain is rebuilt each time (`transform 0x2 -> 0x8 -> 0x2`, images 1080x2340, `main targets
1170x540`), the picture is upright in both (screenshot at rotation 3), no crash. Nothing needed fixing.
The manifest is `sensorLandscape`, so portrait (0 / 2) is never offered. Settings restored
(`user_rotation 0`, `accelerometer_rotation 1`).

## Found on the way

- At Quarter the UI (laid out in engine pixels at User interface scale 100) covers four times the
  screen share of Full; the Options dialog's tab row goes above the screen (`known_port_issues.md`).
  Whether the UI scale should follow the render scale is a ruling.
- The phone carried an apk with versionCode 10001 (another P8 pass); `tools/android.sh install debug`
  now uses `adb install -r -d`, which replaces any debuggable build (it cannot replace a non-debuggable
  one: the first install this session had to go through a local versionCode bump, not committed).
- `adb shell input tap` is too short for the polled mouse; `input swipe X Y X Y 200` clicks.

## Phone state at the end

- This branch's debug apk installed (versionCode 1), app stopped.
- `Documents/Banished/settings.cfg` restored to the file found at the start (pushed back over adb).
- Rotation settings as found (`user_rotation 0`, `accelerometer_rotation 1`).
- `/data/local/tmp/mouse_mid.json`, `mouse_rot.json` and screenshots left in `/data/local/tmp`.
