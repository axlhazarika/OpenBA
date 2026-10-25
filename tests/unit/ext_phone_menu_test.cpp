// ============================================================================
// tests/unit/ext_phone_menu_test.cpp -- ext.phone_menu
// ============================================================================
// src/ext/phone_menu/phone_math.h without the game: the registry row (class A, OFF by default, the
// profile keys), the safe-area margin and inset, the backdrop's top anchor, every other dialog's safe
// area, and the slider minimum.
// The seams themselves (UserInterface::Update / Draw, the two dialogs) need the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/phone_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

// Retail's cover fit (UserInterface::Draw, 0x180121dbe), on a screen wider than the sprite.
Ext::PhoneMenu::Rect RetailCoverWide(int32_t width, int32_t height, float spriteAspect) {
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / spriteAspect + 0.5f) >> 1;
    const int32_t mid = height >> 1;
    return Ext::PhoneMenu::Rect{0, mid - half, width, mid + half};
}

}  // namespace

UNIT_TEST(ext_phone_menu_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("phone_menu");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.phone_menu");
    CHECK(!ResolveText("").ext.phone_menu);                         // absent key = the default (off)
    CHECK(ResolveText("ext.phone_menu=1\n").ext.phone_menu);        // the Android first run's line
    CHECK(!ResolveText("ext.phone_menu=1\nprofile=faithful\n").ext.phone_menu);
}

UNIT_TEST(ext_phone_menu_safe_margin_is_4_5_percent_of_the_height) {
    CHECK(Ext::PhoneMenu::SafeMargin(1080) == 49);    // the S25 at 100 %: 48.6 rounds up
    CHECK(Ext::PhoneMenu::SafeMargin(270) == 12);     // the S25 at 25 %: 12.15
    CHECK(Ext::PhoneMenu::SafeMargin(0) == 0);
    CHECK(Ext::PhoneMenu::SafeMargin(-5) == 0);
}

UNIT_TEST(ext_phone_menu_inset_layout_takes_edges) {
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::InsetLayout(2340, 1080);
    CHECK(r.x1 == 49);
    CHECK(r.y1 == 49);
    CHECK(r.x2 == 2340 - 49);   // the right EDGE, as DialogInstance::ComputeLayout takes it
    CHECK(r.y2 == 1080 - 49);
    // A screen too thin for the margin keeps retail's whole-screen rectangle.
    const Ext::PhoneMenu::Rect thin = Ext::PhoneMenu::InsetLayout(1, 1000);
    CHECK(thin.x1 == 0);
    CHECK(thin.y1 == 0);
    CHECK(thin.x2 == 1);
    CHECK(thin.y2 == 1000);
}

UNIT_TEST(ext_phone_menu_backdrop_logo_top) {
    // 2340x1080 against the menu's 16:9 overlay: retail cuts 118 off the top and the bottom.
    CHECK(Ext::PhoneMenu::WiderThanBackdrop(2340, 1080, 1600, 900));
    const Ext::PhoneMenu::Rect retail = RetailCoverWide(2340, 1080, 1600.0f / 900.0f);
    CHECK(retail.y1 == -118);
    CHECK(retail.y2 == 1198);
    CHECK(Ext::PhoneMenu::BackdropHeight(2340, 1080, 1600, 900) == 1316);
    // The top on the screen's top: the logo whole and centred, the cut moved to the bottom.
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::LogoTop(retail);
    CHECK(r.x1 == 0);
    CHECK(r.x2 == 2340);
    CHECK(r.y1 == 0);
    CHECK(r.y2 == 1316);
    CHECK(Ext::PhoneMenu::LogoBottom(1316) == 290);   // 0.22 of the drawn picture
    // A 16:9 screen fits exactly (retail's compare sends it to the width fit, no overhang).
    CHECK(Ext::PhoneMenu::WiderThanBackdrop(1920, 1080, 1600, 900));
    CHECK(Ext::PhoneMenu::BackdropHeight(1920, 1080, 1600, 900) == 1080);
    // A 16:10 screen is narrower: retail fits the height, the picture is the screen's height.
    CHECK(!Ext::PhoneMenu::WiderThanBackdrop(1920, 1200, 1600, 900));
    CHECK(Ext::PhoneMenu::BackdropHeight(1920, 1200, 1600, 900) == 1200);
    // A rectangle that does not overhang the top: unchanged.
    const Ext::PhoneMenu::Rect sides{-200, 0, 1800, 1200};
    const Ext::PhoneMenu::Rect same = Ext::PhoneMenu::LogoTop(sides);
    CHECK(same.x1 == -200);
    CHECK(same.y1 == 0);
    CHECK(same.y2 == 1200);
    // No picture: never wider.
    CHECK(!Ext::PhoneMenu::WiderThanBackdrop(2340, 1080, 0, 0));
    CHECK(Ext::PhoneMenu::LogoBottom(0) == -1);
}

UNIT_TEST(ext_phone_menu_button_row) {
    // The labels need the largest padded label ("Achievements", 109 + 16).
    const int32_t widths[] = {65, 67, 92, 125, 85, 71, 104, 60};
    const int32_t heights[] = {30, 30, 30, 30, 30, 30, 30, 30};
    CHECK(Ext::PhoneMenu::SquareSide(widths, heights, 8) == 125);
    const int32_t tallW[] = {20};
    const int32_t tallH[] = {40};
    CHECK(Ext::PhoneMenu::SquareSide(tallW, tallH, 1) == 40);
    CHECK(Ext::PhoneMenu::SquareSide(widths, heights, 0) == 0);
    // The S25 at UI 100, render scale 50: 1170x540 units, margin 24. Eight squares of 125 with
    // the buttons' and the ribbon's padding: 1062 x 145. The logo ends at 145 (0.22 of a
    // 658-unit picture), the version line (56 padded) starts at 460.
    CHECK(Ext::PhoneMenu::BackdropHeight(1170, 540, 1600, 900) == 658);
    CHECK(Ext::PhoneMenu::LogoBottom(658) == 145);
    CHECK(Ext::PhoneMenu::VersionTop(1170, 540, 56) == 460);
    CHECK(Ext::PhoneMenu::VersionTop(1170, 540, 0) == -1);
    CHECK(Ext::PhoneMenu::RowFits(1170, 540, 1062, 145, 145, 460));   // rows 197 .. 342
    CHECK(Ext::PhoneMenu::RowFits(1170, 540, 1062, 145, -1, -1));     // nothing else known
    // Too wide for the safe area (UI 50: 585x270 units): back to the column.
    CHECK(!Ext::PhoneMenu::RowFits(585, 270, 1062, 145, 72, 230));
    CHECK(!Ext::PhoneMenu::RowFits(1170, 540, 1123, 145, -1, -1));
    CHECK(Ext::PhoneMenu::RowFits(1170, 540, 1122, 145, -1, -1));
    // Over the logo, or into the version line's gap.
    CHECK(!Ext::PhoneMenu::RowFits(1170, 540, 1062, 145, 220, 460));
    CHECK(!Ext::PhoneMenu::RowFits(1170, 540, 1062, 145, 145, 340));
    // Not sized.
    CHECK(!Ext::PhoneMenu::RowFits(1170, 540, 0, 0, -1, -1));
}

UNIT_TEST(ext_phone_menu_row_square_side) {
    // The S25 at UI 100, render scale 50 (1170x540 units, safe width 1122): eight squares with
    // 62 units of padding and gaps around them fill the row at 132 ...
    CHECK(Ext::PhoneMenu::FitSide(1170, 540, 62, 8) == 132);
    // ... capped at 0.22 of the safe height (492): 108, more than the labels' 95.
    CHECK(Ext::PhoneMenu::RowSide(95, 132, 540) == 108);
    // The owner's UI 35 at render scale 100 (819x378 units, safe width 785, safe height 344):
    // 90 across; the labels in their own font need 107 (no row), in the small font 71.
    CHECK(Ext::PhoneMenu::FitSide(819, 378, 62, 8) == 90);
    CHECK(Ext::PhoneMenu::RowSide(107, 90, 378) == 0);
    CHECK(Ext::PhoneMenu::RowSide(71, 90, 378) == 75);   // the cap: 0.22 * 344
    CHECK(Ext::PhoneMenu::RowSide(80, 90, 378) == 80);   // never smaller than the labels
    CHECK(Ext::PhoneMenu::RowSide(90, 90, 378) == 90);
    // Nothing fits / nothing to fit.
    CHECK(Ext::PhoneMenu::FitSide(100, 378, 200, 8) == 0);
    CHECK(Ext::PhoneMenu::FitSide(1170, 540, 62, 0) == 0);
    CHECK(Ext::PhoneMenu::RowSide(0, 90, 378) == 0);
}

UNIT_TEST(ext_phone_menu_menu_band) {
    // Nothing known (the first frame): the safe area.
    Ext::PhoneMenu::Rect r = Ext::PhoneMenu::MenuBand(2340, 1080, -1, -1, 0);
    CHECK(r.y1 == 49);
    CHECK(r.y2 == 1031);
    // The S25 at 50 %: 1170x540 units, logo ends at 0.22 of a 658-unit picture (145), version
    // line from 497, a 300-unit column: centred between the two.
    r = Ext::PhoneMenu::MenuBand(1170, 540, 145, 497, 300);
    CHECK(r.x1 == 24);
    CHECK(r.x2 == 1170 - 24);
    CHECK(r.y1 == 145);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
    // Too tall for the band under the logo but not for the space above the version line.
    r = Ext::PhoneMenu::MenuBand(1170, 540, 145, 497, 400);
    CHECK(r.y1 == 24);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
    // Taller than the safe band but not than the space above the version line: the band
    // (ComputeLayout lets its top rise into the margin, its bottom stays above the line).
    r = Ext::PhoneMenu::MenuBand(1170, 540, 145, 497, 480);
    CHECK(r.y1 == 24);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
    // Taller than the screen allows: the safe area.
    r = Ext::PhoneMenu::MenuBand(585, 270, 72, 230, 300);
    CHECK(r.y1 == 12);
    CHECK(r.y2 == 270 - 12);
    // No logo (the pause menu has no backdrop): above the version line.
    r = Ext::PhoneMenu::MenuBand(1170, 540, -1, 497, 300);
    CHECK(r.y1 == 24);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
}

UNIT_TEST(ext_phone_menu_version_aside) {
    // 25 % on a 2.17:1 screen 1421 pixels tall: 770x355 units, a 100x290 column, the version
    // line 250 wide from 287: the column cannot sit above it, the line fits to its left.
    CHECK(Ext::PhoneMenu::VersionAside(770, 355, 287, 250, 100, 290));
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::VersionAsideLayout(770, 355, 250);
    CHECK(r.x1 == 16);
    CHECK(r.x2 == 16 + 250);
    CHECK(r.y1 == 16);
    CHECK(r.y2 == 355 - 16);
    // The column fits above the line: the line stays centred.
    CHECK(!Ext::PhoneMenu::VersionAside(770, 355, 287, 250, 100, 200));
    // No room beside the column (a 16:9 screen at 25 %): stays.
    CHECK(!Ext::PhoneMenu::VersionAside(450, 253, 190, 250, 100, 290));
    // Not laid out yet.
    CHECK(!Ext::PhoneMenu::VersionAside(770, 355, -1, 250, 100, 290));
    CHECK(!Ext::PhoneMenu::VersionAside(770, 355, 287, 250, 0, 0));
}

UNIT_TEST(ext_phone_menu_safe_area_anchors_move_in) {
    // The S25 at 100 %: margin 49. A dialog at the default upper-left (0, 0), laid out or not.
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 400, 120);
    CHECK(r.x1 == 49);
    CHECK(r.y1 == 49);
    CHECK(r.x2 == 2340 - 49);   // a right / bottom anchor (the toolbar) moves in by the margin
    CHECK(r.y2 == 1080 - 49);
    const Ext::PhoneMenu::Rect first = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 0, 0);
    CHECK(first.x1 == 49);
    CHECK(first.y2 == 1080 - 49);
}

UNIT_TEST(ext_phone_menu_safe_area_is_a_floor_for_placed_dialogs) {
    // ComputeLayout puts a left / top aligned dialog at upperLeft + x1: the margin is a floor.
    const Ext::PhoneMenu::Rect inside = Ext::PhoneMenu::SafeArea(2340, 1080, 20, 30, 300, 100);
    CHECK(20 + inside.x1 == 49);   // a point inside the margin is pulled to its edge
    CHECK(30 + inside.y1 == 49);
    const Ext::PhoneMenu::Rect placed = Ext::PhoneMenu::SafeArea(2340, 1080, 700, 400, 300, 100);
    CHECK(placed.x1 == 0);         // a tooltip / drop-down / dragged panel stays where retail puts it
    CHECK(placed.y1 == 0);
    CHECK(placed.x2 == 2340 - 49); // and is still kept off the far edges
    CHECK(placed.y2 == 1080 - 49);
    const Ext::PhoneMenu::Rect above = Ext::PhoneMenu::SafeArea(2340, 1080, 100, -40, 300, 100);
    CHECK(above.y1 == 89);         // a tooltip above the screen's top: retail's clamp would leave it
                                   // off screen; the floor brings it to the margin
}

UNIT_TEST(ext_phone_menu_safe_area_keeps_retail_for_big_dialogs) {
    // Taller than the safe band (1080 - 98): the vertical axis is retail's, the other still inset.
    const Ext::PhoneMenu::Rect tall = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 900, 1000);
    CHECK(tall.y1 == 0);
    CHECK(tall.y2 == 1080);
    CHECK(tall.x1 == 49);
    CHECK(tall.x2 == 2340 - 49);
    const Ext::PhoneMenu::Rect exact = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 2340 - 98, 1080 - 98);
    CHECK(exact.x1 == 49);         // exactly the safe size still fits
    CHECK(exact.y2 == 1080 - 49);
    // A screen too small for the margin: retail's whole screen.
    const Ext::PhoneMenu::Rect thin = Ext::PhoneMenu::SafeArea(1, 1000, 0, 0, 0, 0);
    CHECK(thin.x1 == 0);
    CHECK(thin.y1 == 0);
    CHECK(thin.x2 == 1);
    CHECK(thin.y2 == 1000);
}

UNIT_TEST(ext_phone_menu_slider_minimum) {
    CHECK(Ext::PhoneMenu::SliderMinimum(75.0f) == 25.0f);   // retail's 75 at density 1
    CHECK(Ext::PhoneMenu::SliderMinimum(38.0f) == 25.0f);   // a Retina window's 38
    CHECK(Ext::PhoneMenu::SliderMinimum(19.0f) == 19.0f);   // a platform answer already lower is kept
}
