// src/ext/enhanced_tab/render_scale_entry.cpp -- the Enhanced tab's "Render scale" row (P8)
//
// NOT RETAIL, and not an extension row: the render scale is a platform setting, the key
// `VideoRenderScale` (a percent, 25..100) beside retail's Video keys in settings.cfg, read by
// Platform::Window::RenderScale (src/platform/common/platform_window_hooks.h; the phone's first run
// seeds 50). The combo offers Full / Half / Quarter = 100 / 50 / 25. Where the platform can change it
// (RenderScaleAdjustable: Android) Apply sets it live: SetRenderScale, then Video::Device's
// settingsChanged, which makes the next frame's settings change rebuild the back buffer, every
// dynamic target and the swap chain at the new size (the Vulkan backend's ApplyEngineSettingsChange),
// and records the key for the next start. Elsewhere the combo shows Full and is disabled: a
// desktop's present path does not scale the frame (RenderScale is 1.0 there). A hand-edited value
// that is none of the three (75) shows as the nearest item and is left alone unless the player picks
// another item.

#include "ext/enhanced_tab/enhanced_tab.h"

#include "platform_window_hooks.h"   // Platform::Window::RenderScale / RenderScaleAdjustable / SetRenderScale
#include "io/Persistence.h"
#include "ui/ComboElement.h"
#include "video/Device.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;
int g_openItem = 0;   // the item Open showed: Apply changes nothing while it is still picked

constexpr int kPercents[] = { 100, 50, 25 };   // Full, Half, Quarter
constexpr int kItemCount = static_cast<int>(sizeof(kPercents) / sizeof(kPercents[0]));

int CurrentPercent() {
    return static_cast<int>(Platform::Window::RenderScale() * 100.0f + 0.5f);
}

int NearestItem(int percent) {
    if (percent >= 75) return 0;
    if (percent >= 37) return 1;
    return 2;
}

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboRenderScale");
    const bool adjustable = Platform::Window::RenderScaleAdjustable();
    g_openItem = NearestItem(CurrentPercent());
    if (g_combo != nullptr) {
        g_combo->SetCurrentItem(g_openItem);
        if (!adjustable) g_combo->Enable(false);
    }
    std::fprintf(stderr, "[RenderScale] combo=%p item=%d (%d %%)%s\n", static_cast<void*>(g_combo), g_openItem,
                 CurrentPercent(), adjustable ? "" : ", fixed on this platform");
}

void Apply() {
    if (g_combo == nullptr || !Platform::Window::RenderScaleAdjustable()) return;
    const int item = g_combo->_currentItem;
    if (item < 0 || item >= kItemCount || item == g_openItem) return;
    const int percent = kPercents[item];
    const bool changed = percent != CurrentPercent();
    Platform::Window::SetRenderScale(percent);
    if (changed && Video::Device::_singleton != nullptr) Video::Device::_singleton->settingsChanged = 1;
    if (IO::Persistence* p = IO::Persistence::_singleton) p->Set("Video", "RenderScale", percent);
    g_openItem = item;
    std::fprintf(stderr, "[RenderScale] Apply: item=%d VideoRenderScale=%d%s\n", item, percent,
                 changed ? " (the targets are rebuilt at the next frame)" : "");
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& RenderScaleEntry() {
    static const Contribution c = { "render_scale", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
