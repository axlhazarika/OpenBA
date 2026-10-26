// ============================================================================
// tests/unit/ext_phone_loading_test.cpp -- ext.phone_menu: the loading screen's backdrop (P9)
// ============================================================================
// src/ext/phone_menu/loading_math.h: the picture top-aligned (LogoTop), its lower copy at the same
// scale (LowerCopy) and the strip that copy is clipped to (BorderStripTop).
// docs/android/P9_LOADING_SCALE.md §1. The recognition and the seams need the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/loading_math.h"

namespace {

using Ext::PhoneMenu::BorderStripTop;
using Ext::PhoneMenu::LowerCopy;
using Ext::PhoneMenu::Rect;

// Retail's cover fit on a screen wider than a 16:9 picture (UserInterface::Draw, 0x180121dbe).
Rect CoverWide(int32_t width, int32_t height) {
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / (1600.0f / 900.0f) + 0.5f) >> 1;
    const int32_t mid = height >> 1;
    return Rect{0, mid - half, width, mid + half};
}

}  // namespace

UNIT_TEST(ext_phone_loading_both_borders_at_the_menus_scale) {
    // The S25 at UI 40: a 936 x 432 layout; the width-fitted picture is 526 tall.
    const Rect top = Ext::PhoneMenu::LogoTop(CoverWide(936, 432));
    CHECK(top.y1 == 0);                                                          // the top border whole
    CHECK(top.y2 - top.y1 == Ext::PhoneMenu::BackdropHeight(936, 432, 1600, 900));   // the menus' scale
    const Rect lower = LowerCopy(top, 432);
    CHECK(lower.y2 == 432);                                  // the bottom border on the screen's bottom
    CHECK(lower.y2 - lower.y1 == top.y2 - top.y1);           // at the same scale
    CHECK(lower.x1 == top.x1 && lower.x2 == top.x2);
    // The strip the lower copy is clipped to: kLoadingBorderFraction of the picture, at the bottom.
    const int32_t strip = BorderStripTop(432, lower.y2 - lower.y1);
    CHECK(strip == 432 - 76);   // 526 * 0.145 = 76.3
    CHECK(strip > top.y1 && strip < 432);
}

UNIT_TEST(ext_phone_loading_no_lower_copy_when_the_picture_fits) {
    // 16:9 (the picture fills the screen) and a picture that ends above the bottom: nothing to add.
    const Rect full{0, 0, 1600, 900};
    const Rect a = LowerCopy(full, 900);
    CHECK(a.x2 <= a.x1);
    const Rect b = LowerCopy(Rect{0, 0, 1400, 800}, 1000);
    CHECK(b.x2 <= b.x1);
}
