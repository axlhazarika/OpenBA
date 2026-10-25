// ============================================================================
// src/platform/posix/app_posix.cpp -- Platform::App lifecycle hooks (POSIX / Linux)
// ============================================================================
// The POSIX half of src/platform/common/platform_app.h. Linux has one step of its own:
// Shutdown stops the boot-state watchdog before SDL_Quit, so a clean exit leaves
// STATE=EXITED (as the Darwin and Android twins do). main()'s SDL hints stay inline in
// src/app/main.cpp, in their order. A Darwin twin (src/platform/darwin/app_darwin.cpp)
// replaces this file whole on Apple.
// ============================================================================

#include "platform_app.h"

#include "boot_state_watchdog.h"

namespace Platform {
namespace App {

void EnterRunDirectory() {
}

void PreSdlInitHints() {
}

void PostSdlInit() {
}

void Shutdown() {
    BootState::Stop();
}

bool TakeSaveRequest() {
    return false;
}

}  // namespace App
}  // namespace Platform
