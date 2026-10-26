// src/ext/fps_cap/enhanced_entry.cpp -- ext.fps_cap's row of the Enhanced tab (P8)
//
// NOT RETAIL. The combo "Frame rate cap" Off / 30 / 60 / 90 / 120 fps: Off = the bool row ext.fps_cap off
// (the stored ext.fps_cap_rate is kept), a rate = the row on with ext.fps_cap_rate at that rate.
// SetFpsCapAndRecord records the keys and the frame limiter follows from the next frame (live).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

constexpr int32_t kRates[] = { 0, 30, 60, 90, 120 };   // the combo's items, in order
constexpr int kItemCount = static_cast<int>(sizeof(kRates) / sizeof(kRates[0]));

int ItemOf(int32_t fps) {
    for (int i = 0; i < kItemCount; ++i) {
        if (kRates[i] == fps) return i;
    }
    return 0;
}

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboFpsCap");
    const int item = ItemOf(FpsCapChoice());
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[FpsCap] combo=%p items=%d item=%d\n", static_cast<void*>(g_combo),
                 g_combo != nullptr ? g_combo->GetItemCount() : 0, item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const int item = g_combo->_currentItem;
    const int32_t fps = SetFpsCapAndRecord(item >= 0 && item < kItemCount ? kRates[item] : 0);
    std::fprintf(stderr, "[FpsCap] Apply: item=%d cap=%d fps (0 = off)\n", item, fps);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& FpsCapEntry() {
    static const Contribution c = { "fps_cap", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
