// ============================================================================
// src/platform/android/app_android.cpp -- Platform::App lifecycle hooks (Android)
// ============================================================================
// The Android twin of src/platform/posix/app_posix.cpp (src/platform/android/README.md):
// it replaces that file whole on Android. NOT RETAIL. The Android-only steps:
//   EnterRunDirectory  the data folder (android_paths.h) becomes the working directory, so
//                      the logs, the crash / hang dumps and the boot state land beside the
//                      pushed WinData/ where `adb pull` and tools/android_boot_state.sh find
//                      them; the per-user Documents/ subfolder is made.
//   PreSdlInitHints    landscape only (the manifest locks it too); the logcat copy of
//                      standalone_error.log starts (the logs are open by now).
//   Shutdown           stop the boot-state watchdog (EXITED, as on macOS), drain the logcat
//                      copy.
// The harness flags reach OptionsInit through the process environment, which the app's
// activity fills from the launch intent's string extras before this library's main runs
// (android/app/src/main/java/org/openba/game/OpenBAActivity.java, src/app/options.h).
// ============================================================================

#include "platform_app.h"
#include "android_paths.h"

#include "boot_state_watchdog.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_system.h>

#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace Platform {

namespace Android {

const std::string& DataDir() {
    static std::string dir;
    static bool asked = false;
    if (!asked) {
        asked = true;
        const char* ext = SDL_GetAndroidExternalStoragePath();
        if (ext != nullptr) dir = ext;
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    }
    return dir;
}

}  // namespace Android

namespace {

const char kLogTag[] = "OpenBA";

// The logcat copy of standalone_error.log: every [AutoStart], [BootState], [Vulkan] and error
// line the run writes to stderr, read back from the file and written to logcat line by line
// (tag OpenBA), so `adb logcat -s OpenBA` follows a run live. A reader thread rather than a
// second write at every fprintf: stderr stays the one sink the rest of the tree knows.
std::atomic<bool> g_mirrorStop{false};
std::thread g_mirror;

void MirrorLines(FILE* f, std::string& pending) {
    char buf[4096];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        pending.append(buf, n);
    }
    std::clearerr(f);
    size_t start = 0;
    for (;;) {
        const size_t nl = pending.find('\n', start);
        if (nl == std::string::npos) break;
        const std::string line = pending.substr(start, nl - start);
        if (!line.empty()) __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
        start = nl + 1;
    }
    pending.erase(0, start);
}

void MirrorThread() {
    FILE* f = nullptr;
    std::string pending;
    while (!g_mirrorStop.load()) {
        if (f == nullptr) f = std::fopen("standalone_error.log", "r");
        if (f != nullptr) MirrorLines(f, pending);
        usleep(100 * 1000);
    }
    if (f != nullptr) {
        MirrorLines(f, pending);
        if (!pending.empty()) __android_log_write(ANDROID_LOG_INFO, kLogTag, pending.c_str());
        std::fclose(f);
    }
}

// The phone defaults (docs/vulkan/PLAN.md §1, ruled 2026-10-24): MSAA off, the smallest shadow map,
// the graphics extensions off. A first run on the phone (no settings.cfg in the user folder yet)
// starts from a settings.cfg holding only those keys; everything else takes the game's own defaults,
// and the player's later choices in Options are kept as on any platform (the file is never written
// again here). Retail has no "shadows off": ShadowScalar 2 is its Low (the smallest sun map); a true
// off is an extension question for P7, as is the half-resolution render scale.
const char kPhoneSettings[] =
    "VideoMultisample=0\n"
    "VideoShadowScalar=2\n"
    "ext.ambient_occlusion=0\n"
    "ext.better_trees=0\n"
    "ext.foliage_aa=0\n"
    "ext.billboard_shadow=0\n"
    "ext.max_shadow_res=0\n";

void SeedPhoneSettings(const std::string& userDir) {
    mkdir(userDir.c_str(), 0770);
    const std::string path = userDir + "/settings.cfg";
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return;
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) return;
    std::fputs(kPhoneSettings, f);
    std::fclose(f);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "first run: phone defaults written to %s", path.c_str());
}

}  // namespace

namespace App {

void EnterRunDirectory() {
    const std::string& dir = Android::DataDir();
    if (dir.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "SDL_GetAndroidExternalStoragePath failed (%s): the logs go to the working directory",
                            SDL_GetError());
        return;
    }
    // The folder exists once Android has made it for the package. WinData/ and Documents/ are made
    // here, by the app: a folder adb makes belongs to the shell user, and an app process cannot list
    // or open anything inside it (EACCES), while files adb pushes into the app's own folder are
    // readable. tools/android.sh push-data therefore launches the app once before the first push.
    mkdir(dir.c_str(), 0770);
    const std::string winData = dir + "/WinData";
    mkdir(winData.c_str(), 0770);
    const std::string docs = dir + "/Documents";
    mkdir(docs.c_str(), 0770);
    SeedPhoneSettings(docs + "/Banished");
    if (chdir(dir.c_str()) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "chdir(%s) failed: the logs go to the working directory",
                            dir.c_str());
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "run directory %s", dir.c_str());
}

void PreSdlInitHints() {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (!g_mirror.joinable()) {
        g_mirrorStop.store(false);
        g_mirror = std::thread(&MirrorThread);
    }
}

void PostSdlInit() {
}

void Shutdown() {
    BootState::Stop();
    std::fflush(stderr);
    if (g_mirror.joinable()) {
        g_mirrorStop.store(true);
        g_mirror.join();
    }
}

}  // namespace App
}  // namespace Platform
