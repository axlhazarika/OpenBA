# Android P8: shadows off, the signed release apk, the trim-memory save (2026-10-25)

Session log of one P8 agent (`docs/runtime_port/NEXT_SESSION.md` §2.7). Branch from `main` 8e94db750. Three items.

## 1. Shadows off: retail already has it (no ext row)

The P7 line "retail has no shadows-off" (`P7_FIRST_PASS.md` Left, `PERF_P7.md` rank 6 / row 8,
`app_android.cpp`, `README.md`) looked at the **Shadow Resolution** combo only. Retail has two shadow combos:

| Combo | Key | Items | Effect |
|---|---|---|---|
| Shadow Resolution | `VideoShadowScalar` 0..2 | High / Medium / Low | sun map 2048 / 1024 / 512 (`0x800 >> scalar`) |
| Shadow Quality | `VideoShaderDetail` 0..3 | **None** / Low / Medium / High | the pixel-program variant (DETAIL0..3, the PCF kernel) and the shadow gate |

`Video::Configuration::Startup` clamps `shaderDetail` to 0..3 (`device_startup.cpp`). At 0 (None):
- `GameUpdateLoop::Draw` step 6 (and `ext.render_pipe`'s copy) stores 0 in `Renderer::_shaderDetail`;
- `SunLight::AddDrawItems` (`wave28_lane_partsys.cpp`) queues **no casters** (`AddGlobalDrawItems` /
  `AddCulledDrawItems` sit behind `_shaderDetail != 0`);
- `SunLight::DrawShadowMap` (`lights_vtable.cpp`) returns before it touches the context: no render buffer
  bound, no clear, no draw, so **no shadow pass** on either backend;
- the main pass binds the DETAIL0 pixel variant, the one retail's reflection pass always uses: unshadowed.

What None leaves running: the sun map is still created at the Shadow Resolution size (`[ShadowRes] sun map
512x512` on the phone at None), and `AddDrawItems` still runs `AddLight`, `GetSunCamera` and an empty
`BeginDrawGroup` / `EndDrawGroup`; `DrawLight` still binds the (never written) map to slot 5, which the DETAIL0
variant does not sample (the look below). Cost: 1 MB of device memory at the phone's 512 and one camera fit a
frame; not measured, not worth a row unless a phone profile shows it (possible follow-up).

**Verified 2026-10-25:**
- **DX11, Windows** (functional dir, debug build of this branch, `/profile:faithful`, seed 12345,
  `BANISHED_CAMERA_PIN=town,60`, `BANISHED_SCREENSHOT_AT=900`): `VideoShaderDetail=0` renders the town fully lit,
  no cast shadow anywhere; `VideoShaderDetail=1` (Low) on the same pin shows the trees' and houses' shadows.
  Both runs quit cleanly through the pause menu, empty crash log. The Options dialog (Video tab) lists
  **Shadow Quality: None** (retail's own string table; window capture). The screenshots are kept off-tree.
- **S25, Vulkan** (the perf apk of this branch, the phone's own settings, the trim-memory town loaded with
  `BANISHED_AUTOLOAD=1`): at `VideoShaderDetail=0` the town is fully lit; at the phone's 2 (Medium) the same view
  has the cast shadows. Not measured: the render-thread time at None (PERF_P7 rank 6 put the shadow pass at
  33-36 % of the render thread at the 512 map).
- Windows Vulkan: not run (the phone covers the backend).

**Ruling (owner, through the coordinator, 2026-10-25):** no ext row; retail's None is the off. The phone seed
keeps shadows on (no `VideoShaderDetail` key: the game's default 2). Nothing in game code changed, so the save
format and the faithful path are untouched. The docs that said "retail has no off" are corrected
(`app_android.cpp` comment, `README.md`, `PERF_P7.md`, `P7_FIRST_PASS.md`, `NEXT_SESSION.md` §2.7).

## 2. The signed apk in `release.yml`

- `tools/android.sh sign [IN OUT]`: `zipalign -f -P 16 4`, `apksigner sign` (passwords via `env:`; v2 / v3 by
  minSdk 29), `zipalign -c -P 16 4`, `apksigner verify --verbose --print-certs`. Key from `OPENBA_KEYSTORE`,
  `OPENBA_KEY_ALIAS`, `OPENBA_KEYSTORE_PASSWORD`, `OPENBA_KEY_PASSWORD`. `build release` now prints the real
  output (`app-release-unsigned.apk`; it used to `ls` a file Gradle never writes and fail); `build --ccache`
  passes `-Popenba.ccache=true` (ccache on C and C++ for the native part); `install` takes a file and passes
  `-r -d` (see the version below).
- `android/app/build.gradle`: versionName / versionCode from the root `CMakeLists.txt` `project(... VERSION x.y.z)`
  (1.0.1 -> versionCode 10001; was a hard-coded `1.0.1` / 1), so the tag check and the apk agree and a bump is
  one edit. A dev phone holding an older tree's apk (code 1) installs a newer one normally; the other way needs
  `-d`, which Android honours over a debuggable install only.
- `release.yml` job `android` (Ubuntu 24.04, Temurin 21, the pinned SDK packages added to the runner's SDK, ccache
  and Gradle caches): build, then a separate signing step that decodes `ANDROID_KEYSTORE_BASE64` to
  `$RUNNER_TEMP` (umask 077, removed by a trap), signs to `dist/OpenBA-v<ver>-android-arm64.apk` + `.sha256`.
  The `release` job needs it and attaches `package-android/*`; a dispatch keeps it as the run artifact.
  apksigner in a step rather than Gradle `signingConfig`: the secrets never reach Gradle, its daemon or the
  native build, and the build stays the developer's command.
- `ci.yml` job `Android (arm64, debug apk)` builds the debug apk on every PR. **Not a required check**: branch
  protection names the four; a fifth is the owner's call.
- Validation: `actionlint` 1.7 (pip `actionlint-py`) clean on both files. The signing commands ran locally on
  the perf apk with the debug keystore: zipalign check passed, `Verified using v3 scheme: true`, signer
  `CN=Android Debug`; the result installed on the S25 and ran. The workflow itself has not run (no GitHub
  Actions from here): the first tag or dispatch is its first run.

## 3. In-game trim-memory save after the P7 latch fix

On the S25 (the signed perf apk above): `tools/android.sh run BANISHED_AUTOSTART=1`, the town up, then
`adb shell am send-trim-memory org.openba.game RUNNING_CRITICAL`:
```
[Lifecycle] SDL_EVENT_LOW_MEMORY: a save is requested
[Lifecycle] the platform's save request started the autosave (frame 259)
[GUL::Serialize] SAVE DONE
```
`Save/Auto.sav` (9,296,811 bytes) written at 16:31:03.6, the second the trim was sent. The next launch with
`BANISHED_AUTOLOAD=1` loaded it (`[GUL::Serialize] LOAD DONE` on each of the four later launches, the shadow runs among them). **Verified.**

A trap met on the way: `sed -i` on the phone's `settings.cfg` through `adb shell` leaves a shell-owned 0660 file
the app cannot read (`ReadSettings: 0 entries ... (defaults)`), and the game then rewrites it from defaults. Edit
it on the PC and `adb push` it after removing the old one (a pushed file is 0666).

## Phone state at the end

The phone settings were restored from a copy taken first (the game's own boot rewrite sets
`VideoSettings=true`; nothing else differs). `Save/Auto.sav` is the trim-memory save of this session (a fresh
town, seed from the phone's settings). When the phone lock was taken again later, another agent had already
replaced this session's release apk (versionCode 10001, debug key) with a debuggable debug apk (code 1); it was
left as found, app stopped.
