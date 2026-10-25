// ============================================================================
// src/ext/phone_menu/video_rows.cpp -- ext.phone_menu: retail's Video rows on a fixed surface (P8)
// ============================================================================
// NOT RETAIL (docs/android/P8_INPUT_OPTIONS.md, round 2; phone_menu.h has the contract).
//
// What retail's Video rows do on Android (the Vulkan backend, src/render/vulkan/device.cpp):
//   Resolution    the list is BuildResolutionList's standard sizes, unfiltered (no screen size is
//                 known there), so 3840x2160 can be picked; the choice is copied into the back
//                 buffer by ApplyChangedSettings, then ApplyEngineSettingsChange republishes the
//                 window's own size (times the render scale): it changes nothing.
//   Refresh rate  one entry, 60 Hz, for every size; no display mode is ever set (the panel stays
//                 at its own 120 Hz): it changes nothing, and it reads wrong.
//   Fullscreen    the activity's window is always the whole screen: it changes nothing.
//   VSync         the one live row: on = FIFO (the panel's 120 Hz paces the frames), off = MAILBOX
//                 (frames rendered as fast as the GPU goes, most never shown).
// The owner's ruling (2026-10-25) offered two ways: drop the meaningless rows, or let the
// Enhanced rows override them. Dropped (disabled) here: the rows that change nothing take no input (the
// combos grey; retail's check sprite has no disabled look, a tap does nothing),
// and VSync is kept on, so FIFO holds the panel's rate and the Frame rate cap (ext.fps_cap) is the
// one control below it; the two can never both be choices. Render scale is the Enhanced tab's.

#include "ext/phone_menu/phone_menu.h"

#include "ext/registry.h"
#include "io/Persistence.h"
#include "platform_window_hooks.h"   // Platform::Window::FixedDisplaySurface
#include "ui/CheckElement.h"
#include "ui/ComboElement.h"
#include "video/Device.h"

#include <cstdio>

namespace Ext {
namespace PhoneMenu {

bool FixedVideoRows() {
    return Ext().phone_menu && Platform::Window::FixedDisplaySurface();
}

void KeepVSyncOnFixedSurface() {
    if (!FixedVideoRows()) return;
    Video::Configuration* const cfg = Video::Configuration::_singleton;
    if (cfg == nullptr || cfg->vsync != 0) return;
    cfg->vsync = 1;
    if (Video::Device::_singleton != nullptr) Video::Device::_singleton->settingsChanged = 1;
    // Recorded too (written with the next settings write), so the next start opens FIFO at once.
    if (IO::Persistence* p = IO::Persistence::_singleton) p->Set("Video", "VSync", true);
    std::fprintf(stderr, "[PhoneMenu] VSync was off on a fixed surface: turned on (FIFO; the frame rate "
                         "below the panel's is the Frame rate cap)\n");
}

void FitVideoRows(UI::ComboElement* resolution, UI::ComboElement* refresh, UI::CheckElement* fullscreen,
                  UI::CheckElement* vsync) {
    if (!FixedVideoRows()) return;
    if (resolution != nullptr) resolution->Enable(false);
    if (refresh != nullptr) refresh->Enable(false);
    if (fullscreen != nullptr) fullscreen->Enable(false);
    if (vsync != nullptr) {
        vsync->_checked = 1;
        vsync->Enable(false);
    }
    std::fprintf(stderr, "[PhoneMenu] fixed surface: Resolution, Refresh rate, Fullscreen and VSync (on) "
                         "disabled in Options > Video\n");
}

}  // namespace PhoneMenu
}  // namespace Ext
