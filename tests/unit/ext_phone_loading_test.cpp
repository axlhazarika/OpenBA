// ============================================================================
// tests/unit/ext_phone_loading_test.cpp -- ext.phone_menu: the loading screen's backdrop (P9)
// ============================================================================
// src/ext/phone_menu/loading_math.h: BottomOnBottom, the menus' LogoTop mirrored.
// docs/android/P9_LOADING_SCALE.md §1. The recognition and the seam need the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/loading_math.h"

namespace {

using Ext::PhoneMenu::BottomOnBottom;
using Ext::PhoneMenu::Rect;

// Retail's cover fit on a screen wider than a 16:9 picture (UserInterface::Draw, 0x180121dbe).
Rect CoverWide(int32_t width, int32_t height) {
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / (1600.0f / 900.0f) + 0.5f) >> 1;
    const int32_t mid = height >> 1;
    return Rect{0, mid - half, width, mid + half};
}

}  // namespace

UNIT_TEST(ext_phone_loading_bottom_on_bottom_on_a_phone) {
    // The S25's loading-screen layout at render scale 100: 900 / 1080 -> _uiScale 0.83, 1942 x 896.
    const Rect retail = CoverWide(1942, 896);
    CHECK(retail.y2 > 896);   // retail's bottom border hangs off the screen
    const Rect r = BottomOnBottom(retail, 896);
    CHECK(r.y2 == 896);                         // the bottom border whole, on the screen's bottom
    CHECK(r.y2 - r.y1 == retail.y2 - retail.y1);   // the picture keeps its size
    CHECK(r.x1 == retail.x1 && r.x2 == retail.x2);
    CHECK(r.y1 < 0);                            // the cut moves to the top border
}

UNIT_TEST(ext_phone_loading_bottom_on_bottom_leaves_a_fitting_picture_alone) {
    // 16:9 (the picture fills the screen) and 16:10 (height-fitted, bars at the sides): unchanged.
    const Rect full{0, 0, 1600, 900};
    const Rect r = BottomOnBottom(full, 900);
    CHECK(r.x1 == 0 && r.y1 == 0 && r.x2 == 1600 && r.y2 == 900);
    const Rect narrow{40, 0, 1400, 1000};
    const Rect n = BottomOnBottom(narrow, 1000);
    CHECK(n.x1 == 40 && n.y1 == 0 && n.x2 == 1400 && n.y2 == 1000);
}
