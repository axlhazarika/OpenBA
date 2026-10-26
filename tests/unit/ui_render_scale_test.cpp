// ============================================================================
// tests/unit/ui_render_scale_test.cpp -- the UI scale measured on the screen (P9)
// ============================================================================
// src/platform/common/ui_render_scale.h: the setting is the UI scale at Full render scale; the
// applied _uiScale is uiPercent / renderPercent, so the layout space (back buffer * _uiScale) is the
// same at every render scale; the phone floor is computed on the screen; a first run keeps the
// pre-P9 look. docs/android/P9_LOADING_SCALE.md §2.
// ============================================================================
#include "unit.h"
#include "platform/common/ui_render_scale.h"
#include "ext/phone_menu/phone_math.h"

#include <cstdint>

namespace {

using Platform::Display::FirstRunPercentAt;
using Platform::Display::ScreenExtent;
using Platform::Display::UiPercentFromRatio;
using Platform::Display::UiScaleRatioAt;

// UserInterface::Update's layout extent: (int)(back buffer * _uiScale).
int32_t Layout(int32_t backBuffer, float uiScale) {
    return static_cast<int32_t>(static_cast<float>(backBuffer) * uiScale);
}

}  // namespace

UNIT_TEST(ui_render_scale_full_is_retail_division) {
    for (int32_t p = 25; p <= 150; ++p) {
        CHECK(UiScaleRatioAt(p, 100) == static_cast<float>(p) / 100.0f);
    }
    CHECK(ScreenExtent(2340, 100) == 2340);
    CHECK(FirstRunPercentAt(100, 100) == 100);
}

UNIT_TEST(ui_render_scale_layout_space_is_the_same_at_every_render_scale) {
    // The S25: 2340x1080 at Full, 1170x540 at Half, 585x270 at Quarter.
    for (int32_t p = 25; p <= 150; ++p) {
        const int32_t w100 = Layout(2340, UiScaleRatioAt(p, 100));
        const int32_t h100 = Layout(1080, UiScaleRatioAt(p, 100));
        const int32_t w50 = Layout(1170, UiScaleRatioAt(p, 50));
        const int32_t h50 = Layout(540, UiScaleRatioAt(p, 50));
        const int32_t w25 = Layout(585, UiScaleRatioAt(p, 25));
        const int32_t h25 = Layout(270, UiScaleRatioAt(p, 25));
        // Float truncation may land one unit apart; never more.
        CHECK(w50 - w100 <= 1 && w100 - w50 <= 1);
        CHECK(h50 - h100 <= 1 && h100 - h50 <= 1);
        CHECK(w25 - w100 <= 1 && w100 - w25 <= 1);
        CHECK(h25 - h100 <= 1 && h100 - h25 <= 1);
    }
    // The owner's UI 35: 819 x 378 layout units at every scale.
    CHECK(Layout(2340, UiScaleRatioAt(35, 100)) == 819);
    CHECK(Layout(1170, UiScaleRatioAt(35, 50)) == 819);
    CHECK(Layout(585, UiScaleRatioAt(35, 25)) == 819);
    CHECK(UiScaleRatioAt(35, 50) == 35.0f / 50.0f);   // 0.7: P8's "70 at Half"
}

UNIT_TEST(ui_render_scale_screen_extent) {
    CHECK(ScreenExtent(1170, 50) == 2340);
    CHECK(ScreenExtent(540, 50) == 1080);
    CHECK(ScreenExtent(585, 25) == 2340);
    CHECK(ScreenExtent(270, 25) == 1080);
    CHECK(ScreenExtent(1345, 0) == 1345);     // insane render percent: no scaling
    CHECK(ScreenExtent(1345, 250) == 1345);
}

UNIT_TEST(ui_render_scale_phone_floor_does_not_move_with_the_render_scale) {
    const Ext::PhoneMenu::RowMetrics pause = Ext::PhoneMenu::PauseMenuMetrics();
    const int32_t v = Ext::PhoneMenu::kVersionHeight;
    // The floor is computed on the screen the back buffer stands for: 35 on the S25 at all three.
    using Ext::PhoneMenu::UiScaleFloor;
    CHECK(UiScaleFloor(ScreenExtent(2340, 100), ScreenExtent(1080, 100), pause, 16, 9, v) == 35);
    CHECK(UiScaleFloor(ScreenExtent(1170, 50), ScreenExtent(540, 50), pause, 16, 9, v) == 35);
    CHECK(UiScaleFloor(ScreenExtent(585, 25), ScreenExtent(270, 25), pause, 16, 9, v) == 35);
    // Before P9 the floor was taken on the back buffer: 70 at Half, and at Quarter none under 100.
    CHECK(UiScaleFloor(585, 270, pause, 16, 9, v) == 100);
}

UNIT_TEST(ui_render_scale_first_run_keeps_the_pre_p9_look) {
    CHECK(FirstRunPercentAt(100, 50) == 50);    // the phone's seeded Half: UI 100 in back-buffer pixels
    CHECK(FirstRunPercentAt(100, 25) == 25);
    CHECK(FirstRunPercentAt(50, 50) == 25);     // a density-2 default, then the render scale
    CHECK(FirstRunPercentAt(1, 25) == 1);       // never 0
    // Its layout is the one UI 100 at Half drew before P9 (1170 x 540 units).
    CHECK(Layout(1170, UiScaleRatioAt(FirstRunPercentAt(100, 50), 50)) == 1170);
}

UNIT_TEST(ui_render_scale_render_change_rederives_the_ratio) {
    // The Render scale row: the setting recovered from the applied ratio, then applied at the new
    // scale; every percent and every pair of Full / Half / Quarter round-trips exactly.
    const int32_t scales[] = { 100, 50, 25 };
    for (int32_t p = 25; p <= 150; ++p) {
        for (int32_t from : scales) {
            CHECK(UiPercentFromRatio(UiScaleRatioAt(p, from), from) == p);
            for (int32_t to : scales) {
                const float after = UiScaleRatioAt(UiPercentFromRatio(UiScaleRatioAt(p, from), from), to);
                CHECK(after == UiScaleRatioAt(p, to));
            }
        }
    }
    CHECK(UiScaleRatioAt(UiPercentFromRatio(0.35f, 100), 50) == 0.7f);
}
