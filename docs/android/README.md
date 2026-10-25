# OpenBA on Android

NOT RETAIL. The Android build of OpenBA: the same game, engine and Vulkan backend as Linux, packaged as
an app whose native library is the repository's root `CMakeLists.txt`. Plan and rulings:
`docs/vulkan/PLAN.md` (§1, §3.6, §4.5, §4.6, phases P6 and P7). Reference device: Galaxy S25 (Adreno 830).
Only new Adreno parts are in scope; a mouse and a keyboard are how the game is played (touches arrive as
mouse clicks).

No game data is in the tree, the build directory or the apk. The player copies the `WinData` packages of
their own Banished install into the app's data folder (below).

## Layout

| Path | What |
|---|---|
| `android/` | The Gradle project: `settings.gradle`, `build.gradle` (AGP pin), `gradle.properties` (the package id and app name, each set once), the Gradle wrapper. |
| `android/app/build.gradle` | The app module: SDK levels, NDK and CMake pins, arm64-v8a only, the native build over `../../CMakeLists.txt` (targets `OpenBA` = `libmain.so` and `SDL3-shared` = `libSDL3.so`), SDL's Java glue compiled in place from `third_party/SDL/android-project`. |
| `android/app/src/main/AndroidManifest.xml` | One activity, landscape only, Vulkan 1.3 required. |
| `android/app/src/main/java/org/openba/game/OpenBAActivity.java` | `SDLActivity` subclass: runs the game's own `main`, turns the launch intent's `BANISHED_*` extras into the environment. |
| `src/platform/android/` | The platform twins (`README.md` there has the rule). |
| `src/diag/android/` | The crash handler and boot-state watchdog twins. |
| `tools/android.sh` | Build, install, push the data, run, pull the logs. |
| `tools/boot_state.sh --android` | The boot state of the app on the attached device. |

## Versions (pinned)

| Item | Version |
|---|---|
| Android Gradle Plugin | 8.8.1 (`android/build.gradle`) |
| Gradle | 8.12.1 (wrapper, distribution SHA-256 pinned) |
| JDK running Gradle | Android Studio's bundled JBR 21 (AGP 8.8 needs 17 or newer) |
| NDK | r30, `30.0.16248370` (clang 21) |
| CMake (SDK) | 3.31.6, with its ninja |
| compileSdk / targetSdk / minSdk | 35 / 35 / 29 |
| ABI | arm64-v8a only |
| C++ runtime | `c++_shared` (`libc++_shared.so` in the apk) |

## Build

From Git Bash at the repository root, no Android Studio needed:
```bash
tools/android.sh build debug --jobs 8      # android/app/build/outputs/apk/debug/app-debug.apk
```
The script sets `ANDROID_HOME` (default `%LOCALAPPDATA%/Android/Sdk`) and `JAVA_HOME` (default Android
Studio's `jbr`) when they are unset, and passes the job count to the native build. A clean debug build
takes about 4.5 minutes with 2 jobs; a one-file change rebuilds in well under a minute. Release builds
(`build release`, the perf flavour) are unsigned until P7.

The NDK configures the root `CMakeLists.txt` with `ANDROID` set. That branch takes `src/platform/posix`
plus `src/platform/sdl` and `common`, with every `src/platform/android/<name>_android.cpp` replacing its
`posix/<name>_posix.cpp` twin (the darwin rule), the same for `src/diag/android`, builds `OpenBA` as the
shared library `libmain.so`, keeps SDL shared (`libSDL3.so`), and turns on SDL's joystick and HIDAPI
modules because SDL's Java glue calls into them at startup. The renderer is the Vulkan backend.

## Install, data, run

```bash
tools/android.sh install debug            # adb install -r
tools/android.sh push-data                # BANISHED_DIR's WinData/*.pkg + the FrancophilSans files -> the phone (once)
tools/android.sh run                      # launch to the main menu
tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=3000 BANISHED_PAUSEMENU_QUIT=120 \
                     BANISHED_PAUSEMENU_BUTTON=6   # new game, then Quit Game from the pause menu
tools/android.sh boot-state --wait 300    # = tools/boot_state.sh --android --wait 300
tools/android.sh pull <dir>               # the standalone_* logs and the boot state
tools/android.sh stop
```

**The data folder** is the app-specific external folder, `/sdcard/Android/data/<package>/files`
(`SDL_GetAndroidExternalStoragePath()`): no permission, readable over adb and MTP, removed with the app.

| On the phone | What |
|---|---|
| `files/WinData/*.pkg` | The retail packages, pushed by the player. Only the top-level `*.pkg` are pushed; parked mods and other subfolders stay home. |
| `files/FrancophilSans.ttf`, `FrancophilSans-Bold.ttf`, `FrancophilSans_README.txt` | The typeface `ext.sharp_ui` rasterises its fonts from at a UI scale other than 100 % (below). Pushed by `push-data`. |
| `files/OpenBA` | Does not exist: it is the "exe path" the engine is told (`file_android.cpp`), so the game directory is `files/`. |
| `files/standalone_debug.log`, `standalone_error.log`, `standalone_crash.log`, `standalone_hang.log`, `standalone_boot_state.txt` | The same files as beside `OpenBA.exe` on Windows. |
| `files/Documents/Banished/` | The per-user folder: `settings.cfg`, `Save/`, `Cache/` (the derived-data cache: SPIR-V, `vulkan_pipelines.bin` / `.txt`). |
| `files/tmp/` | Temporary files (a save is written there, then moved). |

`WinData/` must be made by the app: a folder adb makes belongs to the shell user, and the app process can
neither list nor open anything inside it (EACCES). The app makes `WinData/` and `Documents/` at every
start, and `push-data` starts the app once when the folder is missing (or removes an adb-made one first).

**The font rasteriser** (P8). `ext.sharp_ui` draws the UI's text crisp at any UI scale by rasterising the
game's typeface, FrancophilSans, at the pixel height the screen needs (FreeType,
`src/platform/common/font_raster_freetype.cpp`). Windows, Linux and macOS find `FrancophilSans.ttf`
installed or beside the exe, where the release packages put it. On Android the one place is the data folder
(`src/platform/android/font_raster_android.cpp`): the app looks for `files/FrancophilSans.ttf` and nowhere
else. The typeface is Manfred Klein's, kept unchanged with his notice in `tools/release/fonts/` and shipped
beside the exe in the desktop packages (`THIRD_PARTY_NOTICES.md`); `tools/android.sh push-data` pushes the
same three files. Without the file the text keeps the game's own glyph sheets (`[ExtRes] font variants off:
typeface FrancophilSans absent` in `standalone_error.log`); with it, a UI scale other than 100 shows
`[ExtRes] font <Kit> at <px> px: ... glyphs rasterised`. The apk carries no font.

**Harness flags.** The game reads its `BANISHED_*` flags once at boot from the process environment
(`src/app/options.cpp`; `src/app/options.h` is the inventory). On Android the launch intent is the
source: every string extra whose key starts with `BANISHED_` becomes that environment variable before the
native library loads (`OpenBAActivity.java`); leftovers from an earlier launch in the same process are
cleared first, so a launch from the icon runs with none. By hand:
```bash
adb shell am start -n org.openba.game/org.openba.game.OpenBAActivity -e BANISHED_AUTOSTART 1
```
SDL ends the game's process state once `main` returns and refuses a second start in the same process, so
`tools/android.sh run` force-stops the app first.

**Phone defaults** (PLAN.md §1). The first start (no `settings.cfg` yet) writes a `settings.cfg` holding
only `GameEdgeScroll=false` (P7: a touch held at the edge is a parked cursor), `VideoRenderScale=50`,
`VideoMultisample=0`, `VideoShadowScalar=2` (retail's Low; retail has no shadows-off), and
`ext.ambient_occlusion`, `ext.better_trees`, `ext.foliage_aa`, `ext.billboard_shadow`,
`ext.max_shadow_res` off, and `ext.phone_menu` on (`app_android.cpp`): on the phone's 19.5:9 screen the
main menu's logo sits whole on the left and the buttons on the right, inside a 4.5 % safe area clear of the
rounded corners and the camera cut-out, the in-game interface (the toolbar and event notices bottom-right,
the panels at their default top-left, tooltips, drop-downs) is kept inside the same safe area, and Options >
User interface scale goes down to 25 % (the UI four times larger than 100 %;
`docs/extensions/handins/PHONEMENU.md`). The first-run UI scale is the game's own
100. Options changes are kept as anywhere else.

**Render scale** (P7). `VideoRenderScale=<percent>` (25..100) sizes the engine's back buffer against the
window: the phone's 50 renders 1170x540 and the present pass scales it up (linear) while turning it into the
1080x2340 swap-chain images. `Platform::Window::RenderScale` reads the key once at window creation (no
Options control; edit the file and restart); desktops answer 1.0. The SDL layer hands the engine the scaled
size and pixel density, so taps land where they are drawn. The MSAA default stays off and the 60 fps cap
is unruled: the numbers are in `P7_FIRST_PASS.md`.

## Logs

- `adb logcat -s OpenBA` follows `standalone_error.log` live (a thread in `app_android.cpp` copies every
  line it gains to logcat), plus the crash dump lines. `tools/android.sh logcat` adds SDL's, the Vulkan
  loader's and the system crash reporter's tags.
- `standalone_debug.log` (stdout) is only in the file.
- `tools/boot_state.sh --android` reads the state file over adb with the Windows script's verdicts and
  exit codes: the pid is checked with `pidof <package>`, the freshness with the file's mtime against the
  phone's clock.

## Crashes: tombstone and ndk-stack

The crash handler (`src/diag/android/crash_handler_android.cpp`) writes `standalone_crash.log` in the
usual shape (arm64 registers, `Fault:` from the ESR, the stack walked from the signal frame's pc / lr /
frame-pointer chain) and copies it to logcat, then hands the signal to Android's crash reporter, which
writes a tombstone. To symbolise either:
```bash
# the unstripped library Gradle built (the apk carries a stripped copy)
SO=$(ls android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libmain.so)
NDK="$ANDROID_HOME/ndk/30.0.16248370"
# one frame of standalone_crash.log ("libmain.so + 0xA74D14"):
"$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-addr2line.exe" -f -C -i -e "$SO" 0xA74D14
# the whole tombstone from logcat:
adb logcat -d | "$NDK/ndk-stack.cmd" -sym "$(dirname "$SO")"
```
`adb bugreport` collects the tombstones (`/data/tombstones` is not readable without root).

## Lifecycle

Going to the background tears the window down without waiting for a Vulkan app. The Vulkan backend
installs an SDL event watch (`src/render/vulkan/device.cpp`, THE MOBILE LIFECYCLE): on
`SDL_EVENT_WILL_ENTER_BACKGROUND` it waits for a frame still being presented, idles the device and
destroys the swap chain and the surface; SDL then blocks the main thread until the app returns. On
`SDL_EVENT_DID_ENTER_FOREGROUND` the next frame makes the surface and the swap chain again. Nothing calls
`exit()`. Since P7 the same watch writes the pipeline cache and the warm list when the app goes to the
background (Android may kill it there without a quit).

A second watch, the platform's (`src/platform/android/app_android.cpp`, THE LIFECYCLE WATCH), pauses
the boot-state watchdog's hang budget while the app is in the background (the state file says
`detail=background`; `src/diag/android/boot_state_background.h`) and turns `SDL_EVENT_LOW_MEMORY` (in the
foreground) and `SDL_EVENT_TERMINATING` into a save request: the next game update starts the game's own
autosave (`Platform::App::TakeSaveRequest`, `GameUpdateLoop::Update`). Every `onTrimMemory` reaches SDL as
LOW_MEMORY, including the one each trip home brings; in the background the main thread is blocked and
such a request is dropped. To check by hand: `adb shell input keyevent KEYCODE_HOME`, then
`adb shell am start -n org.openba.game/org.openba.game.OpenBAActivity` brings the running game back;
`adb shell am send-trim-memory org.openba.game RUNNING_CRITICAL` in a running town writes `Save/Auto.sav`.

## Pre-rotation

The S25's panel is portrait, so a landscape app sees `currentTransform = ROTATE_90`. The swap chain is
made with that transform and portrait images; the game renders its landscape frame into an image of the
backend's own and the frame's end draws it, turned, into the swap-chain image (`src/render/vulkan/present.h`;
shaders `present/present.vert`, `.frag`, compiled by `tools/vulkan/gen_present_spirv.sh`). Without that the
compositor rotates every frame and every present answers `VK_SUBOPTIMAL_KHR`.

## Validation layers

The Khronos validation layer runs in the **debug** apk only (it is debuggable; `run-as` and the GPU
debug-layer settings need that). The layer is not in the tree or the apk: take the arm64-v8a
`libVkLayer_khronos_validation.so` from the Vulkan-ValidationLayers release asset
`android-binaries-<ver>.zip` (the Windows Vulkan SDK has no Android binary; P7 used 1.4.363.0), then:
```bash
tools/android.sh install debug
tools/android.sh validation on <dir>/android-binaries-<ver>/arm64-v8a/libVkLayer_khronos_validation.so
tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=3000 BANISHED_PAUSEMENU_QUIT=120 \
                     BANISHED_PAUSEMENU_BUTTON=6
tools/android.sh pull <dir>                # grep '\[Vulkan\] validation' <dir>/standalone_error.log
tools/android.sh validation off            # the settings deleted, the layer removed from the app
```
`validation on` copies the layer into the app's private folder (`run-as <pkg> cp`) and sets
`enable_gpu_debug_layers`, `gpu_debug_app`, `gpu_debug_layers`, `gpu_debug_layer_app`; the Android loader
then lists the layer to that one app, and the debug flavour enables it (`[Vulkan] instance: ...,
validation on`, `validation=1` on the device line). Messages land in `standalone_error.log` as
`[Vulkan] validation ERROR|warning: ...` through the backend's debug messenger (logcat tag OpenBA; the
layer's own VALIDATION tag stays empty while a messenger is installed). The layer stops repeating one
message after 10 (`duplicate_message_limit`), so a count is per distinct message, capped. Layer settings
are system properties: `adb shell setprop debug.vulkan.khronos_validation.<setting> <value>`.

P7 results (2026-10-25, menu + new game + pause-menu quit, 7,220 frames): before, 4 errors (the present
source's first-frame layout; the byte-4 vertex attribute against an `int4` input, three pipelines) and the
depth-only pass's unused `SV_Target` warning; after, 0 errors and the warning
(`docs/android/P7_FIRST_PASS.md` §A).
