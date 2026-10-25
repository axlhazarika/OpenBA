// ============================================================================
// src/platform/android/window_android.cpp -- Platform::Window hooks (Android)
// ============================================================================
// The Android twin of src/platform/posix/window_posix.cpp (src/platform/android/README.md):
// it replaces that file whole on Android. One body differs: PrepareCreate adds
// SDL_WINDOW_FULLSCREEN to the Vulkan flag. An Android window always covers the activity, but
// only a fullscreen SDL window puts the activity in immersive mode (status and navigation bars
// hidden; a swipe brings them back for a moment), which is how a game owns the screen there. The
// engine's own "fullscreen" (a borderless window placed over the display, SetFramed) has no
// meaning on Android and stays as the shared SDL code does it. Every other hook is the identity.
// ============================================================================

#include "platform_window_hooks.h"

#include <SDL3/SDL_video.h>

namespace Platform {
namespace Window {

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
