#pragma once
// ============================================================================
// src/diag/android/boot_state_background.h -- the Android watchdog's background bracket
// ============================================================================
// NOT RETAIL. Android only (boot_state_watchdog_android.cpp has the body). While the app is in the
// background SDL blocks the main thread, so no frame completes and the watchdog would read HUNG
// after 20 s (and end an unattended run with exit 4). The platform's lifecycle watch
// (src/platform/android/app_android.cpp) brackets the background with these two calls: between
// them the hang budget does not run (the state reads RUNNING / BOOTING with detail=background), and
// it restarts from zero when the app is back.
// ============================================================================

namespace BootState {
namespace Android {

void EnterBackground();   // SDL_EVENT_WILL_ENTER_BACKGROUND
void EnterForeground();   // SDL_EVENT_DID_ENTER_FOREGROUND

}  // namespace Android
}  // namespace BootState
