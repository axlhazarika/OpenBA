#pragma once
// ============================================================================
// src/ext/phone_menu/loading_screen.h -- ext.phone_menu: the loading screen (P9)
// ============================================================================
// NOT RETAIL. The look is in loading_math.h; docs/android/P9_LOADING_SCALE.md §1.
// ============================================================================

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace PhoneMenu {

// From phone_menu.cpp's seam (UI::UserInterface::_extDialogPlace), with the row on, before its own
// arms. True when `dialog` is a loading screen (a dialog whose tree holds Dialog/Loading.rsc's
// `loadingImage`) and the point was handled: kExtPlaceLayout keeps retail's rectangle (the group
// bottom-centred, no safe-area inset); kExtPlaceBackdrop puts the picture's top on the screen's top
// (LogoTop). False for every other dialog. Layout on the main thread (which records the
// screen), the backdrop on whichever thread draws.
bool PlaceLoading(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
                  int32_t* rect);

// UI::UserInterface::_extBackdropOver (installed by Sync, null under the faithful profile): over a
// loading screen's top-aligned picture, with the row on, the same picture bottom-aligned at the same
// scale (loading_math.h LowerCopy), clipped to its torn bottom border (BorderStripTop). False for every
// other dialog and when the picture does not overhang the screen's bottom.
bool BackdropOver(const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect,
                  int32_t* clip);

// The loading screen's UI scale (owner's ruling, P9: the loading group follows the UI scale):
// `settingsScale` (Platform::Display::UiScaleRatio of the Options UI scale) with the row on, retail's
// splash scale (`retailScale`, 900 / back-buffer height) otherwise. GameLoadDialogLoopT::Startup and
// ext.load_progress's boot screen.
float LoadingUiScale(float retailScale, float settingsScale);

}  // namespace PhoneMenu
}  // namespace Ext
