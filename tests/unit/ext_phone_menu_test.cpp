// ============================================================================
// tests/unit/ext_phone_menu_test.cpp -- ext.phone_menu
// ============================================================================
// src/ext/phone_menu/phone_math.h without the game: the registry row (class A, OFF by default, the
// profile keys), the safe-area margin and inset, the backdrop's top anchor, and the slider minimum.
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

UNIT_TEST(ext_phone_menu_logo_left) {
    // 2340x1080 against the menu's 16:9 overlay: retail cuts 118 off the top and the bottom.
    const Ext::PhoneMenu::Rect retail = RetailCoverWide(2340, 1080, 1600.0f / 900.0f);
    CHECK(retail.y1 == -118);
    CHECK(retail.y2 == 1198);
    // 1.28 screen widths wide, against the right and top edges: the logo centred on 36 %.
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::LogoLeft(retail, 2340);
    CHECK(r.x2 == 2340);
    CHECK(r.x1 == 2340 - 2995);
    CHECK(r.y1 == 0);
    CHECK(r.y2 >= 1080);                                   // still covers the screen
    CHECK(r.x1 + (r.x2 - r.x1) / 2 == 842);                // 0.36 * 2340
    CHECK(Ext::PhoneMenu::LogoRight(r) == -655 + 2052);    // 0.685 of the overlay
    // A screen narrower than the overlay (cut at the sides) or an exact fit: unchanged.
    const Ext::PhoneMenu::Rect sides{-200, 0, 1800, 1200};
    const Ext::PhoneMenu::Rect same = Ext::PhoneMenu::LogoLeft(sides, 1600);
    CHECK(same.x1 == -200);
    CHECK(same.y1 == 0);
    CHECK(same.x2 == 1800);
    CHECK(same.y2 == 1200);
}

UNIT_TEST(ext_phone_menu_column_beside_the_logo) {
    // The S25 at 100 %: the logo ends at 1397, a 240-unit column: centred between the logo and
    // the safe area's right edge.
    Ext::PhoneMenu::Rect r = Ext::PhoneMenu::ColumnBeside(2340, 1080, 1397, 240);
    CHECK(r.x1 == 1397);
    CHECK(r.x2 == 2340 - 49);
    CHECK(r.y1 == 49);
    CHECK(r.y2 == 1080 - 49);
    // At 25 % (585x270, the logo ends at 349): a 250-unit column does not fit right of the
    // logo, so it is right-aligned on the safe margin.
    r = Ext::PhoneMenu::ColumnBeside(585, 270, 349, 250);
    CHECK(r.x2 == 585 - 12);
    CHECK(r.x1 == 585 - 12 - 250);
    // Not laid out yet: the space right of the logo.
    r = Ext::PhoneMenu::ColumnBeside(585, 270, 349, 0);
    CHECK(r.x1 == 349);
    // The version line centred on the logo's centre line.
    r = Ext::PhoneMenu::VersionUnderLogo(2340, 1080, 842);
    CHECK(r.x1 == 0);
    CHECK(r.x2 == 1684);
    CHECK(r.y2 == 1080 - 49);
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

UNIT_TEST(ext_phone_menu_slider_minimum) {
    CHECK(Ext::PhoneMenu::SliderMinimum(75.0f) == 25.0f);   // retail's 75 at density 1
    CHECK(Ext::PhoneMenu::SliderMinimum(38.0f) == 25.0f);   // a Retina window's 38
    CHECK(Ext::PhoneMenu::SliderMinimum(19.0f) == 19.0f);   // a platform answer already lower is kept
}
