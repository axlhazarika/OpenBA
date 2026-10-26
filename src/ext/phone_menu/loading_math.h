#pragma once
// ============================================================================
// src/ext/phone_menu/loading_math.h -- ext.phone_menu's loading-screen fit (pure)
// ============================================================================
// NOT RETAIL (P9, docs/android/P9_LOADING_SCALE.md §1). Standard C++ only
// (tests/unit/ext_phone_loading_test.cpp). Units: the UI layout space, as phone_math.h.
//
// The loading screen (Dialog/Loading.rsc, the LoaderDialog of every GameLoadDialogLoopT) is a 16:9
// picture with torn black borders at its top and bottom (its backdrop), and one bottom-centred
// group, the spinner and "Loading..." with the tip line under it (`main`, alignment 7). On a 16:9
// screen retail's cover fit shows the whole picture and the group sits in the bottom border. On a
// screen wider than the picture (a 19.5:9 phone) the width-fitted picture overhangs the screen top
// and bottom by the same amount: the bottom border leaves the screen and the group is drawn over the
// picture. The rule is the menus' logo rule mirrored: the picture's bottom goes on the screen's
// bottom (BottomOnBottom), so the border that carries the text is whole and the cut moves to the
// top border; the group keeps retail's bottom-centred place (no safe-area inset: bottom centre is
// clear of the rounded corners and of the camera cut-out on a short edge).
// ============================================================================

#include "ext/phone_menu/phone_math.h"

#include <cstdint>

namespace Ext {
namespace PhoneMenu {

// Retail's backdrop rectangle (`retail`) moved so its bottom is on the screen's bottom (`height`)
// when it overhangs it; a rectangle that does not overhang the bottom is returned unchanged.
inline Rect BottomOnBottom(const Rect& retail, int32_t height) {
    if (retail.y2 <= height) return retail;
    const int32_t shift = retail.y2 - height;
    return Rect{retail.x1, retail.y1 - shift, retail.x2, height};
}

}  // namespace PhoneMenu
}  // namespace Ext
