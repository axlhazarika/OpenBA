#pragma once
// ============================================================================
// src/ext/phone_menu/loading_screen.h -- ext.phone_menu: the loading screen (P9)
// ============================================================================
// NOT RETAIL. The rule is in loading_math.h; docs/android/P9_LOADING_SCALE.md §1.
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
// bottom-centred, no safe-area inset), kExtPlaceBackdrop puts the picture's bottom on the screen's
// bottom (BottomOnBottom). False for every other dialog. Layout on the main thread (which records
// the screen), the backdrop on whichever thread draws.
bool PlaceLoading(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
                  int32_t* rect);

}  // namespace PhoneMenu
}  // namespace Ext
