// ============================================================================
// tests/unit/ext_touch_mode_test.cpp -- ext.touch_mode's pure decisions (TOUCHMODE)
// ============================================================================
// docs/android/TOUCH_MODE.md section 7: the cluster's rectangles on the S25 and their invariance, the
// toolbar's fitted scale, the D-pad's sectors, the zoom repeat, the unit-space mappings (the sub-toolbar
// centring re-run with retail's own formula, the tooltip), and the registry row and its value key.
// ============================================================================
#include "unit.h"
#include "ext/touch_mode/touch_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace {

using namespace Ext::TouchMode;
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

bool Same(const Rect& r, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    return r.x1 == x1 && r.y1 == y1 && r.x2 == x2 && r.y2 == y2;
}

bool Inside(const Rect& inner, const Rect& outer) {
    return inner.x1 >= outer.x1 && inner.y1 >= outer.y1 && inner.x2 <= outer.x2 && inner.y2 <= outer.y2;
}

}  // namespace

// The S25 at UI 100 (layout units = device pixels), ext.phone_menu's 4.5 % margin (49 px): the table in
// TOUCH_MODE.md section 3.
UNIT_TEST(ext_touch_mode_cluster_layout_s25) {
    const Cluster c = LayoutCluster(2340, 1080, 49);
    CHECK(c.cell == 119);
    CHECK(c.gap == 22);
    CHECK(Same(c.control[kPad], 49, 674, 406, 1031));
    CHECK(Same(c.control[kRotateLeft], 49, 557, 216, 652));
    CHECK(Same(c.control[kRotateRight], 239, 557, 406, 652));
    CHECK(Same(c.control[kZoomIn], 428, 674, 595, 841));
    CHECK(Same(c.control[kZoomOut], 428, 864, 595, 1031));
    CHECK(Same(c.box, 49, 557, 595, 1031));
    CHECK(c.barRegion.x1 == 639);
    CHECK(c.barRegion.x2 == 2291);
    CHECK(c.barRegion.y2 == 1031);
    // No two controls overlap.
    for (int32_t i = 0; i < kControlCount; ++i) {
        for (int32_t j = i + 1; j < kControlCount; ++j) {
            const Rect& a = c.control[i];
            const Rect& b = c.control[j];
            CHECK(a.x2 <= b.x1 || b.x2 <= a.x1 || a.y2 <= b.y1 || b.y2 <= a.y1);
        }
    }
}

// The same fractions on other screens, the cluster inside the margin; the desktop margin without phone_menu.
UNIT_TEST(ext_touch_mode_cluster_layout_other_screens) {
    const int32_t sizes[][2] = {{1920, 1080}, {3080, 1421}, {936, 432}, {1170, 540}, {585, 270}};
    for (const auto& s : sizes) {
        const int32_t w = s[0];
        const int32_t h = s[1];
        const int32_t m = Margin(w, h, 0);
        CHECK(m == static_cast<int32_t>(std::floor(static_cast<float>(h) * kDesktopMarginFraction + 0.5f)));
        const Cluster c = LayoutCluster(w, h, m);
        const Rect safe{m, m, w - m, h - m};
        CHECK(Inside(c.box, safe));
        CHECK(std::abs(c.cell - static_cast<int32_t>(static_cast<float>(h) * kCellFraction + 0.5f)) <= 1);
        CHECK(c.box.y2 == h - m);
        CHECK(c.barRegion.x1 > c.box.x2);
        CHECK(c.barRegion.x2 == w - m);
    }
    CHECK(Margin(2340, 1080, 49) == 49);   // ext.phone_menu's margin when that row is on
}

// Render scale Full / Half / Quarter with the UI scale re-derived (the interface keeps its on-screen size): the
// layout screen is the same, so the cluster is too. And at any UI scale its device size is the same fraction of
// the short edge.
UNIT_TEST(ext_touch_mode_cluster_layout_render_scale_invariant) {
    // UI 40 at Full on the S25: 936 x 432 layout units; at Half with UI re-derived to 80: 1170*0.8 x 540*0.8.
    const Cluster full = LayoutCluster(936, 432, 19);
    const Cluster half = LayoutCluster(static_cast<int32_t>(1170 * 0.8f), static_cast<int32_t>(540 * 0.8f), 19);
    CHECK(Same(full.box, half.box.x1, half.box.y1, half.box.x2, half.box.y2));
    // Device pixels per layout unit at UI 40 = 2.5: the pad is 0.33 of the 1080-pixel short edge either way.
    const float padPx = static_cast<float>(full.control[kPad].Width()) * 2.5f;
    CHECK(std::fabs(padPx / 1080.0f - 3.0f * kCellFraction) < 0.01f);
}

UNIT_TEST(ext_touch_mode_toolbar_fit) {
    // The ruled case: 447 own units at UI 40 = 1118 px, the bar region 1652 px: k = 1.48 (shrunk from 2.0).
    const float k = ToolbarScale(2.0f, 1652, 1118);
    CHECK(std::fabs(k - 1.4776f) < 0.001f);
    CHECK(ToolbarScale(1.25f, 1652, 1118) == 1.25f);   // the request fits: kept
    CHECK(ToolbarScale(2.0f, 800, 1118) == 1.0f);      // never below 1
    CHECK(ToolbarScale(2.0f, 1652, 0) == 2.0f);        // not laid out yet: the request
    // Stable: the toolbar's width in its own units does not depend on k, so the answer is the same each frame.
    CHECK(ToolbarScale(2.0f, 1652, 1118) == k);
    CHECK(ScaleOfPercent(200) == 2.0f);
    CHECK(ScaleOfPercent(125) == 1.25f);
}

UNIT_TEST(ext_touch_mode_hit_control) {
    const Cluster c = LayoutCluster(2340, 1080, 49);
    CHECK(HitControl(c, 200, 850) == kPad);
    CHECK(HitControl(c, 100, 600) == kRotateLeft);
    CHECK(HitControl(c, 300, 600) == kRotateRight);
    CHECK(HitControl(c, 500, 700) == kZoomIn);
    CHECK(HitControl(c, 500, 1000) == kZoomOut);
    CHECK(HitControl(c, 220, 600) == kNone);   // the gap between the rotate buttons
    CHECK(HitControl(c, 1200, 500) == kNone);  // the game view
}

UNIT_TEST(ext_touch_mode_pad_direction) {
    const Rect pad{0, 0, 300, 300};   // centre 150, 150; dead zone 45
    int32_t dx = 9;
    int32_t dy = 9;
    PadDirection(pad, 150, 150, dx, dy);
    CHECK(dx == 0 && dy == 0);
    PadDirection(pad, 180, 170, dx, dy);   // inside the dead zone
    CHECK(dx == 0 && dy == 0);
    PadDirection(pad, 290, 150, dx, dy);
    CHECK(dx == 1 && dy == 0);
    PadDirection(pad, 10, 150, dx, dy);
    CHECK(dx == -1 && dy == 0);
    PadDirection(pad, 150, 10, dx, dy);
    CHECK(dx == 0 && dy == -1);
    PadDirection(pad, 150, 290, dx, dy);
    CHECK(dx == 0 && dy == 1);
    PadDirection(pad, 280, 280, dx, dy);   // a corner: the diagonal
    CHECK(dx == 1 && dy == 1);
    PadDirection(pad, 20, 20, dx, dy);
    CHECK(dx == -1 && dy == -1);
    // The sector boundary: 22.5 degrees off the axis. Just inside the right sector, just past it.
    PadDirection(pad, 150 + 100, 150 + 40, dx, dy);   // 21.8 deg
    CHECK(dx == 1 && dy == 0);
    PadDirection(pad, 150 + 100, 150 + 43, dx, dy);   // 23.3 deg
    CHECK(dx == 1 && dy == 1);
    PadDirection(pad, 400, 150, dx, dy);   // outside the pad
    CHECK(dx == 0 && dy == 0);
}

UNIT_TEST(ext_touch_mode_zoom_repeat) {
    ZoomRepeat z;
    CHECK(z.Step(false, 0.016f) == 0);
    CHECK(z.Step(true, 0.016f) == 1);    // the press
    int32_t steps = 0;
    float t = 0.0f;
    // Irregular frames up to 0.34 s: nothing more.
    const float frames[] = {0.016f, 0.033f, 0.008f, 0.050f, 0.016f, 0.1f, 0.05f, 0.05f};
    for (float dt : frames) {
        t += dt;
        steps += z.Step(true, dt);
    }
    CHECK(t < kZoomDelay);
    CHECK(steps == 0);
    // Past the delay: one, then one per kZoomRepeat.
    steps = 0;
    for (int i = 0; i < 100; ++i) steps += z.Step(true, 0.01f);   // 1.0 s more, held 1.323 s
    // Repeats at 0.35, 0.50, ..., 1.25: 7.
    CHECK(steps == 7);
    CHECK(z.Step(false, 0.01f) == 0);   // released
    CHECK(z.Step(false, 1.0f) == 0);
    CHECK(z.Step(true, 0.01f) == 1);    // a new press
    // A long frame gives one step, not a burst.
    ZoomRepeat y;
    y.Step(true, 0.0f);
    CHECK(y.Step(true, 2.0f) == 1);
    CHECK(y.Step(true, 0.01f) == 0);
}

UNIT_TEST(ext_touch_mode_unit_mapping) {
    const float k = 1.48f;
    for (int32_t v : {0, 1, 17, 447, 936}) {
        CHECK(std::abs(ToOwn(ToLayout(v, k), k) - v) <= 1);
    }
    // Retail's sub-toolbar centring (ToolbarDialog::OnElementEvent): xPos = W - centre - subW / 2 with the GLOBAL
    // width W, as the right padding of a bottom-right dialog laid out against `right`. With `right` =
    // SubToolbarRight(W, k) in own units the child's left lands at centre - subW / 2.
    const int32_t W = 936;
    const int32_t centre = 400;   // the button's centre, own units
    const int32_t subW = 150;
    const int32_t xPos = W - centre - subW / 2;
    const int32_t right = ToOwn(SubToolbarRight(W, k), k);
    const int32_t left = right - (subW + xPos);
    CHECK(std::abs(left - (centre - subW / 2)) <= 1);
    // The tooltip: retail put it at (x, y - h) in own units; the offset lands it at (x * k, y * k - h).
    const int32_t x = 300;
    const int32_t y = 380;
    const int32_t h = 30;
    int32_t dx = 0;
    int32_t dy = 0;
    TooltipOffset(x, y - h, h, k, dx, dy);
    CHECK(std::abs((x + dx) - static_cast<int32_t>(x * k + 0.5f)) <= 1);
    CHECK(std::abs((y - h + dy) - (static_cast<int32_t>(y * k + 0.5f) - h)) <= 1);
    TooltipOffset(x, y - h, h, 1.0f, dx, dy);
    CHECK(dx == 0 && dy == 0);
}

UNIT_TEST(ext_touch_mode_registry_row_and_scale_key) {
    const Ext::Entry* e = Ext::FindEntry("touch_mode");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.touch_mode");
    CHECK(!ResolveText("").ext.touch_mode);
    CHECK(ResolveText("ext.touch_mode=1\next.touch_mode_toolbar_scale=200\n").ext.touch_mode);
    CHECK(!ResolveText("ext.touch_mode=1\nprofile=faithful\n").ext.touch_mode);

    const Ext::ValueKey* k = Ext::FindValueKey("touch_mode_toolbar_scale");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK(std::string(k->owner) == "touch_mode");
    CHECK(k->defaultValue == 200);
    CHECK(Ext::Values().touch_mode_toolbar_scale == 200);
    CHECK(ResolveText("ext.touch_mode_toolbar_scale=150\n").val.touch_mode_toolbar_scale == 150);
    CHECK(Ext::ClampValue(*k, 175) == 175);
    CHECK(Ext::ClampValue(*k, 160) == 200);   // not one of the combo's sizes: the default
    CHECK(Ext::ClampValue(*k, 50) == 200);
}
