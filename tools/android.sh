#!/usr/bin/env bash
# tools/android.sh -- build, install and drive the Android app (docs/android/README.md).
#
#   tools/android.sh build [debug|release] [--jobs N]  # android/app/build/outputs/apk/<type>/app-<type>.apk
#   tools/android.sh install [debug|release]           # adb install -r of that apk
#   tools/android.sh push-data                         # the install's WinData/*.pkg -> the phone's data folder
#   tools/android.sh run [KEY=VALUE ...]               # force-stop, then launch with BANISHED_* flags
#   tools/android.sh stop                              # force-stop the app
#   tools/android.sh pull DIR                          # the standalone_* logs and the boot state -> DIR
#   tools/android.sh logcat                            # follow the app's logcat lines
#   tools/android.sh boot-state [--wait N]             # = tools/boot_state.sh --android [--wait N]
#   tools/android.sh validation on LAYER.so | off       # the Khronos validation layer in the debug app
#
# Environment: ANDROID_HOME (default %LOCALAPPDATA%/Android/Sdk), JAVA_HOME (default Android
# Studio's bundled JBR), ADB (default platform-tools/adb of ANDROID_HOME, then C:/code/platform-tools,
# then PATH), ANDROID_SERIAL (adb's own: picks the device when several are attached), BANISHED_DIR
# (the Windows install whose WinData is pushed; never copied anywhere else), BUILD_JOBS / --jobs
# (native compile jobs, default 8 as tools/build.sh).
#
# The harness flags (src/app/options.h) reach the game as launch-intent string extras, which the
# activity turns into the process environment before the native library loads:
#   tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_AUTOQUIT=600
# A run always force-stops first: SDL's activity ends the process's game once main returns and
# refuses a second start in the same process.
set -euo pipefail
# Git Bash on Windows rewrites arguments that look like POSIX paths ("/sdcard/...") before
# handing them to a Windows program; adb's device paths must pass through untouched.
export MSYS_NO_PATHCONV=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"

prop() {
    sed -n "s/^$1=//p" "$ANDROID_DIR/gradle.properties" | tr -d '\r' | head -1
}
PKG="$(prop openba.applicationId)"
ACTIVITY="org.openba.game.OpenBAActivity"
DATA="/sdcard/Android/data/$PKG/files"

if [ -z "${ANDROID_HOME:-}" ]; then
    if [ -n "${LOCALAPPDATA:-}" ]; then
        ANDROID_HOME="$(cygpath -m "$LOCALAPPDATA" 2>/dev/null || echo "$LOCALAPPDATA")/Android/Sdk"
    else
        ANDROID_HOME="$HOME/Android/Sdk"
    fi
fi
export ANDROID_HOME
if [ -z "${JAVA_HOME:-}" ] && [ -d "C:/Program Files/Android/Android Studio/jbr" ]; then
    export JAVA_HOME="C:/Program Files/Android/Android Studio/jbr"
fi
if [ -z "${ADB:-}" ]; then
    for cand in "$ANDROID_HOME/platform-tools/adb.exe" "$ANDROID_HOME/platform-tools/adb" \
                "C:/code/platform-tools/adb.exe"; do
        if [ -x "$cand" ]; then
            ADB="$cand"
            break
        fi
    done
    ADB="${ADB:-adb}"
fi

usage() {
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

gradlew() {
    if [ "${OS:-}" = "Windows_NT" ]; then
        (cd "$ANDROID_DIR" && ./gradlew.bat "$@")
    else
        (cd "$ANDROID_DIR" && ./gradlew "$@")
    fi
}

apk_path() {
    local p="$ANDROID_DIR/app/build/outputs/apk/$1/app-$1.apk"
    if command -v cygpath >/dev/null 2>&1; then p="$(cygpath -m "$p")"; fi
    echo "$p"
}

cmd="${1:-}"
[ -n "$cmd" ] || usage
shift

case "$cmd" in
build)
    type="debug"
    jobs="${BUILD_JOBS:-8}"
    while [ $# -gt 0 ]; do
        case "$1" in
            debug|release) type="$1" ;;
            --jobs) shift; jobs="$1" ;;
            *) usage ;;
        esac
        shift
    done
    task="assemble$(echo "${type:0:1}" | tr a-z A-Z)${type:1}"
    gradlew --console=plain ":app:$task" "-Popenba.nativeJobs=$jobs"
    ls -la "$(apk_path "$type")"
    ;;
install)
    type="${1:-debug}"
    "$ADB" install -r "$(apk_path "$type")"
    ;;
push-data)
    src="${BANISHED_DIR:-C:/GOG Games/Banished}/WinData"
    [ -d "$src" ] || { echo "no WinData under $src (set BANISHED_DIR)" >&2; exit 1; }
    # WinData/ must belong to the app: a folder adb makes belongs to the shell user and the app
    # cannot read inside it (src/platform/android/app_android.cpp, EnterRunDirectory). The app
    # makes it at its first start, so start it once when the folder is missing or adb-made.
    owner="$("$ADB" shell stat -c %U "$DATA/WinData" 2>/dev/null | tr -d '\r' || true)"
    if [ "$owner" = "shell" ]; then
        echo "WinData on the device was made by adb (unreadable by the app): removing it"
        "$ADB" shell rm -rf "$DATA/WinData"
        owner=""
    fi
    if [ -z "$owner" ]; then
        echo "starting the app once so that it makes $DATA/WinData"
        "$ADB" shell am start -n "$PKG/$ACTIVITY" >/dev/null
        for _ in $(seq 1 30); do
            [ -n "$("$ADB" shell stat -c %U "$DATA/WinData" 2>/dev/null | tr -d '\r' || true)" ] && break
            sleep 1
        done
        "$ADB" shell am force-stop "$PKG"
    fi
    # The retail packages only: parked mods (_mods_off and every other subfolder) stay home.
    for pkg in "$src"/*.pkg; do
        [ -f "$pkg" ] || continue
        if command -v cygpath >/dev/null 2>&1; then pkg="$(cygpath -m "$pkg")"; fi
        "$ADB" push "$pkg" "$DATA/WinData/"
    done
    "$ADB" shell ls -l "$DATA/WinData"
    ;;
run)
    "$ADB" shell am force-stop "$PKG"
    extras=()
    for kv in "$@"; do
        case "$kv" in
            BANISHED_*=*) extras+=(-e "${kv%%=*}" "${kv#*=}") ;;
            *) echo "not a BANISHED_* flag: $kv" >&2; exit 2 ;;
        esac
    done
    "$ADB" shell rm -f "$DATA/standalone_boot_state.txt"
    "$ADB" shell am start -W -n "$PKG/$ACTIVITY" "${extras[@]}"
    ;;
stop)
    "$ADB" shell am force-stop "$PKG"
    ;;
pull)
    dir="${1:?pull DIR}"
    mkdir -p "$dir"
    if command -v cygpath >/dev/null 2>&1; then dir="$(cygpath -m "$dir")"; fi
    for f in standalone_debug.log standalone_error.log standalone_crash.log standalone_hang.log \
             standalone_boot_state.txt; do
        "$ADB" pull "$DATA/$f" "$dir/$f" >/dev/null 2>&1 || true
    done
    ls -la "$dir"
    ;;
logcat)
    "$ADB" logcat -v time -s OpenBA:V SDL:V SDL/APP:V DEBUG:V AndroidRuntime:E libc:V vulkan:V
    ;;
boot-state)
    exec "$ROOT/tools/boot_state.sh" --android "$@"
    ;;
validation)
    # The Khronos validation layer in the debuggable (debug) app (docs/android/README.md,
    # "Validation layers"). `on` takes the arm64-v8a libVkLayer_khronos_validation.so of the
    # Vulkan-ValidationLayers release "android-binaries-<ver>.zip"; nothing of it is kept in the
    # tree. The Android loader injects the layers named by the gpu_debug_* settings into the one
    # app named there; its messages are logcat tag VALIDATION and, through the backend's
    # messenger, `[Vulkan] validation` lines in standalone_error.log.
    mode="${1:-}"
    case "$mode" in
    on)
        layer="${2:?validation on LAYER.so}"
        [ -f "$layer" ] || { echo "no layer at $layer" >&2; exit 1; }
        if command -v cygpath >/dev/null 2>&1; then layer="$(cygpath -m "$layer")"; fi
        "$ADB" push "$layer" /data/local/tmp/libVkLayer_khronos_validation.so
        "$ADB" shell run-as "$PKG" cp /data/local/tmp/libVkLayer_khronos_validation.so .
        "$ADB" shell rm -f /data/local/tmp/libVkLayer_khronos_validation.so
        "$ADB" shell settings put global enable_gpu_debug_layers 1
        "$ADB" shell settings put global gpu_debug_app "$PKG"
        "$ADB" shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation
        "$ADB" shell settings put global gpu_debug_layer_app "$PKG"
        "$ADB" shell run-as "$PKG" ls -l libVkLayer_khronos_validation.so
        ;;
    off)
        "$ADB" shell settings delete global enable_gpu_debug_layers
        "$ADB" shell settings delete global gpu_debug_app
        "$ADB" shell settings delete global gpu_debug_layers
        "$ADB" shell settings delete global gpu_debug_layer_app
        "$ADB" shell run-as "$PKG" rm -f libVkLayer_khronos_validation.so || true
        ;;
    *)
        usage
        ;;
    esac
    "$ADB" shell settings get global enable_gpu_debug_layers
    ;;
*)
    usage
    ;;
esac
