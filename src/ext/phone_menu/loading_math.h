#pragma once
// ============================================================================
// src/ext/phone_menu/loading_math.h -- ext.phone_menu's loading-screen fit (pure)
// ============================================================================
// NOT RETAIL (P9, docs/android/P9_LOADING_SCALE.md §1). Standard C++ only
// (tests/unit/ext_phone_loading_test.cpp). Units: the UI layout space, as phone_math.h.
//
// The loading screen (Dialog/Loading.rsc, the LoaderDialog of every GameLoadDialogLoopT) is an OPAQUE
// 16:9 picture with torn black borders at its top and bottom, and one bottom-centred group (`main`:
// the spinner, "Loading...", the tip). On a screen wider than the picture retail's centred cover fit
// cuts both borders. The owner's ruling (P9): look like the pause menu, both torn borders whole at the
// menus' width-fitted picture scale. The picture is drawn top-aligned (phone_math.h LogoTop: its top
// border whole) and then once more, bottom-aligned at the same scale (LowerCopy), OVER it but clipped
// to the bottom strip that holds its torn bottom border (BorderStripTop): the strip shows the border
// whole, the group sits in it. (The menus' see-through overlay, which the menus draw a second time
// instead, is a resource of the menu loop and is freed before a loading screen draws.)
// ============================================================================

#include "ext/phone_menu/phone_math.h"

#include <cstdint>

namespace Ext {
namespace PhoneMenu {

// The part of the loading picture's height the clipped strip takes, from the picture's bottom:
// measured on the installed pictures (the solid black from about 0.897 of the drawn height down, the
// torn edge reaching up to between 0.857 and 0.885 depending on the picture). 0.125 sits inside the
// torn edge: a higher line shows a band of the lower copy's picture above the edge (a visible seam),
// a lower one cuts the highest peaks of the edge flat. A number about the user's own data.
constexpr float kLoadingBorderFraction = 0.125f;

// The picture drawn bottom-aligned at the size it has in `top` (its top-aligned rectangle): its bottom
// on the screen's bottom. Empty ({0, 0, 0, 0}) when `top` does not overhang the screen's bottom (the
// border is already whole).
inline Rect LowerCopy(const Rect& top, int32_t height) {
    const int32_t pictureH = top.y2 - top.y1;
    if (pictureH <= 0 || height <= 0 || top.y2 <= height) return Rect{0, 0, 0, 0};
    return Rect{top.x1, height - pictureH, top.x2, height};
}

// The top of the strip the lower copy is clipped to (layout units from the screen's top): the
// screen's bottom less kLoadingBorderFraction of the picture's height.
inline int32_t BorderStripTop(int32_t height, int32_t pictureH) {
    return height - static_cast<int32_t>(static_cast<float>(pictureH) * kLoadingBorderFraction + 0.5f);
}

}  // namespace PhoneMenu
}  // namespace Ext
