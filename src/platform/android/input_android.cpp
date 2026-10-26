// ============================================================================
// src/platform/android/input_android.cpp -- the Android twin of src/platform/posix/input_posix.cpp
// ============================================================================
// It replaces input_posix.cpp whole on Android (src/platform/android/README.md). ShiftHeld and
// the identity hooks are posix's (that file's banner has ShiftHeld's story). One hook differs:
// RotateDragReturnsCursor is true. The camera-rotate drag's relative mode is pointer capture on
// Android (SDL's SDLGenericMotionListener_API26), and Android ends a capture with the pointer
// where the capture began; SDL's own position has moved with the drag's relative motion, so the
// first motion after the release snapped the cursor back across the screen. Measured on the S25
// with a uinput mouse: drag began at x 802, SDL at 1002 at the release, the next motion at 833
// (docs/android/P8_INPUT_OPTIONS.md, round 2; docs/reference/android_mouse.md).
// ============================================================================

#include "platform_input.h"
#include "platform_input_hooks.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>

namespace Platform {
namespace Input {

bool ShiftHeld() {
    SDL_PumpEvents();
    return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
}

// The hooks of platform_input_hooks.h: the identity, but RotateDragReturnsCursor (banner).
const bool* ApplyKeyLayer(const bool* raw, int* /*count*/, bool* /*scratch*/) {
    return raw;
}

uint32_t PolledMouseButtons(uint32_t physical) {
    return physical;
}

void PreprocessPumpEvent(SDL_Event* /*event*/) {
}

bool ConsumePumpEvent(const SDL_Event& /*event*/) {
    return false;
}

bool RotateDragReturnsCursor() {
    return true;
}

}  // namespace Input
}  // namespace Platform
