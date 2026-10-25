// ============================================================================
// src/platform/android/window_android.cpp -- Platform::Window hooks (Android)
// ============================================================================
// The Android twin of src/platform/posix/window_posix.cpp (src/platform/android/README.md):
// it replaces that file whole on Android. One body differs: PrepareCreate adds
// SDL_WINDOW_FULLSCREEN to the Vulkan flag. An Android window always covers the activity, but
// only a fullscreen SDL window puts the activity in immersive mode (status and navigation bars
// hidden; a swipe brings them back for a moment), which is how a game owns the screen there. The
// engine's own "fullscreen" (a borderless window placed over the display, SetFramed) has no
// meaning on Android and stays as the shared SDL code does it. P7: RenderScale reads the phone's
// render scale (below); P8: SetRenderScale changes it in the run. Every other hook is the identity.
// ============================================================================

#include "platform_window_hooks.h"
#include "android_paths.h"

#include <SDL3/SDL_video.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Platform {
namespace Window {

// P7: `VideoRenderScale=<percent>` in the user's settings.cfg (Documents/Banished under the data
// folder), read once at the first call: the window's creation, after EnterRunDirectory has seeded a
// first run's file (app_android.cpp: 50). Absent or out of 25..100: 100, the window's own size. The
// engine keeps the key with the rest (IO::Persistence writes back every key it read). P8: the
// Options dialog's Render scale row changes it in the run (SetRenderScale) and records the key. The
// percent is an atomic: the Vulkan backend reads the scale where it rebuilds the swap chain, which is
// the render thread under ext.render_pipe.
namespace {

std::atomic<int> g_percent{0};   // 0 = not read yet

int ReadPercentFromSettings() {
    int result = 100;
    const std::string path = Android::DataDir() + "/Documents/Banished/settings.cfg";
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return result;
    char line[256];
    const char key[] = "VideoRenderScale=";
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, key, sizeof(key) - 1) != 0) continue;
        const int percent = std::atoi(line + sizeof(key) - 1);
        if (percent >= 25 && percent <= 100) result = percent;
    }
    std::fclose(f);
    std::fprintf(stderr, "[Window] render scale %.2f (VideoRenderScale in %s)\n",
                 static_cast<float>(result) / 100.0f, path.c_str());
    return result;
}

}  // namespace

float RenderScale() {
    int percent = g_percent.load(std::memory_order_acquire);
    if (percent == 0) {
        percent = ReadPercentFromSettings();
        int expected = 0;
        if (!g_percent.compare_exchange_strong(expected, percent, std::memory_order_acq_rel))
            percent = expected;
    }
    return static_cast<float>(percent) / 100.0f;
}

bool RenderScaleAdjustable() {
    return true;
}

bool FixedDisplaySurface() {
    return true;
}

void SetRenderScale(int percent) {
    if (percent < 25 || percent > 100) return;
    RenderScale();   // the boot value is read first, so a late first read cannot overwrite this one
    const int before = g_percent.exchange(percent, std::memory_order_acq_rel);
    if (before != percent) {
        std::fprintf(stderr, "[Window] render scale %.2f -> %.2f (Options)\n",
                     static_cast<float>(before) / 100.0f, static_cast<float>(percent) / 100.0f);
    }
}

uint64_t PrepareCreate(int32_t* width, int32_t* height) {
    (void)width;
    (void)height;
    return SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN;
}

void ConfigureBackgroundWindow(SDL_Window* window) {
    (void)window;
}

void FitToUsableScreen(SDL_Window* window, const FrameBorders& frame, int32_t* clientX,
                       int32_t* clientY, int32_t* clientW, int32_t* clientH) {
    (void)window;
    (void)frame;
    (void)clientX;
    (void)clientY;
    (void)clientW;
    (void)clientH;
}

void ApplyInitialWindowGeometry(SDL_Window* window, int savedX, int savedY, int savedPxW,
                                int savedPxH, int videoW, int videoH) {
    (void)window;
    (void)savedX;
    (void)savedY;
    (void)savedPxW;
    (void)savedPxH;
    (void)videoW;
    (void)videoH;
}

void QueryScreenForModes(int* maxW, int* maxH, int* nativeW, int* nativeH) {
    *maxW = 0;
    *maxH = 0;
    *nativeW = 0;
    *nativeH = 0;
}

}  // namespace Window
}  // namespace Platform
