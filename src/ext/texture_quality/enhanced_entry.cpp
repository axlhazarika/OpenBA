// src/ext/texture_quality/enhanced_entry.cpp -- ext.texture_quality's row of the Enhanced tab (block TEXQUALITY)
//
// NOT RETAIL. The combo "Texture quality" Full / Half: Full = the bool row `ext.texture_quality` off
// (the stored `ext.texture_quality_drop` is kept), or the key at its default 0 (the row or profile=all
// alone drops nothing); Half = the row on and the valued key `ext.texture_quality_drop` 1. Quarter (2)
// was removed by the user's ruling (too blurry); the key's range is 0..1, so an older settings.cfg's 2
// reads as 1 (Half). SetTextureQualityAndRecord records the keys; the drop was fixed at boot from the
// value the game started with (Ext::TextureQuality::Boot), so the change is seen after the next start.
// The combo shows the REQUESTED value: what the next start uses.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

constexpr int kLastItem = 1;   // Full, Half

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboTextureQuality");
    const int32_t choice = TextureQualityChoice();
    const int item = choice > kLastItem ? kLastItem : choice;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[TextureQuality] combo=%p items=%d item=%d\n", static_cast<void*>(g_combo),
                 g_combo != nullptr ? g_combo->GetItemCount() : 0, item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const int item = g_combo->_currentItem;
    const int32_t choice = SetTextureQualityAndRecord(item >= 0 && item <= kLastItem ? item : 0);
    std::fprintf(stderr, "[TextureQuality] Apply: item=%d requested drop=%d (takes effect at the next start)\n",
                 item, choice);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& TextureQualityEntry() {
    static const Contribution c = { "texture_quality", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
