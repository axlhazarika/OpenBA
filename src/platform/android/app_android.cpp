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
//   PostSdlInit        P7: the lifecycle watch (below): the boot-state watchdog's background
//                      bracket, and the save request of SDL_EVENT_LOW_MEMORY / TERMINATING.
//   Shutdown           stop the boot-state watchdog (EXITED, as on macOS), drain the logcat
//                      copy.
// The harness flags reach OptionsInit through the process environment, which the app's
// activity fills from the launch intent's string extras before this library's main runs
// (android/app/src/main/java/org/openba/game/OpenBAActivity.java, src/app/options.h).
// ============================================================================

#include "platform_app.h"
#include "android_paths.h"

#include "boot_state_watchdog.h"
#include "android/boot_state_background.h"   // src/diag/android: the watchdog's background bracket

#include <SDL3/SDL_events.h>
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
// the graphics extensions off, ext.phone_menu on (the menu fitted to a landscape phone, the UI scale
// slider down to 25 %; docs/extensions/handins/PHONEMENU.md). A first run on the phone (no settings.cfg in the user folder yet)
// starts from a settings.cfg holding only those keys; everything else takes the game's own defaults,
// and the player's later choices in Options are kept as on any platform (the file is never written
// again here). Retail has no "shadows off": ShadowScalar 2 is its Low (the smallest sun map); a true
// off is an extension question. P7 (ruled 2026-10-25): edge scrolling off, because a touch held at
// the screen's edge is a cursor parked there (an unattended game scrolled to the map's corner); the
// render scale at half the screen (ruled 2026-10-24; window_android.cpp RenderScale reads it).
const char kPhoneSettings[] =
    "GameEdgeScroll=false\n"
    "VideoRenderScale=50\n"
    "VideoMultisample=0\n"
    "VideoShadowScalar=2\n"
    "ext.ambient_occlusion=0\n"
    "ext.better_trees=0\n"
    "ext.foliage_aa=0\n"
    "ext.billboard_shadow=0\n"
    "ext.max_shadow_res=0\n"
    "ext.phone_menu=1\n";

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

// THE LIFECYCLE WATCH (P7, docs/android/README.md "Lifecycle"). SDL delivers the Android lifecycle
// events synchronously to event watches on the thread that pumps events, which is the game's main
// thread (SDL_androidevents.c: the lifecycle queue is drained inside SDL_PumpEvents); after
// WILL_ENTER_BACKGROUND that thread runs one more pump and then blocks in it until the app is back.
//  * WILL_ENTER_BACKGROUND / DID_ENTER_FOREGROUND bracket the watchdog's hang budget.
//  * LOW_MEMORY and TERMINATING ask for a save. The save is the game's own autosave body, a state
//    machine that runs over several frames of the update loop, so the watch only sets a flag and
//    GameUpdateLoop::Update starts the save at its next tick (TakeSaveRequest): in the foreground,
//    the next frame. SDL sends LOW_MEMORY for EVERY onTrimMemory, including the TRIM_MEMORY_UI_HIDDEN
//    each trip to the background brings; one that arrives in the background is logged and dropped
//    (the main thread is blocked there: nothing can save, and a save on the way back would only
//    write the town as it was). TERMINATING comes after SDL has queued SDL_EVENT_QUIT, so the quit
//    usually wins; it is logged.
// The Vulkan backend's own watch (src/render/vulkan/device.cpp, THE MOBILE LIFECYCLE) tears the
// surface down and writes the pipeline cache on the same event.
std::atomic<bool> g_saveRequested{false};
bool g_lifecycleWatch = false;
bool g_inBackground = false;   // main thread only (the watch)

bool LifecycleWatch(void*, SDL_Event* event) {
    switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        g_inBackground = true;
        BootState::Android::EnterBackground();
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        g_inBackground = false;
        BootState::Android::EnterForeground();
        break;
    case SDL_EVENT_LOW_MEMORY:
        if (g_inBackground) {
            std::fprintf(stderr, "[Lifecycle] SDL_EVENT_LOW_MEMORY in the background: no save (the game is blocked)\n");
            break;
        }
        g_saveRequested.store(true);
        std::fprintf(stderr, "[Lifecycle] SDL_EVENT_LOW_MEMORY: a save is requested\n");
        break;
    case SDL_EVENT_TERMINATING:
        g_saveRequested.store(true);
        std::fprintf(stderr, "[Lifecycle] SDL_EVENT_TERMINATING: a save is requested (the quit is queued first)\n");
        break;
    default:
        break;
    }
    return true;
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
    if (!g_lifecycleWatch) g_lifecycleWatch = SDL_AddEventWatch(&LifecycleWatch, nullptr);
}

bool TakeSaveRequest() {
    return g_saveRequested.exchange(false);
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
