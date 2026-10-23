// src/ext/derived_cache/enhanced_entry.cpp -- the derived-data cache's row of the Enhanced tab (block COMPRESSAO)
//
// NOT RETAIL (docs/extensions/handins/COMPRESSAO.md). The text "Data cache on disk: <size>" and the
// button "Clear cache". The size is the shared store's (or the files', when this run did not open
// the cache: the row off, the faithful profile chosen in settings.cfg, another instance holding it).
// The button acts at once and cannot be undone by Cancel: the shared store stops reading and writing
// for the rest of the run, and the files are deleted at the next start (the running store holds them
// open). Element ids: kClearButtonId, far above the Options dialog's own (10..12, input binds from 13).

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/derived_cache/derived_cache.h"

#include "ui/ButtonElement.h"
#include "ui/TextElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

constexpr int32_t kClearButtonId = 0x0DC00001;

UI::TextElement*   g_text   = nullptr;
UI::ButtonElement* g_button = nullptr;

void ShowSize() {
    if (g_text == nullptr) return;
    char text[64];
    if (DerivedCache::SharedClearPending()) {
        std::snprintf(text, sizeof(text), "0 MB (cleared at the next start)");
    } else {
        const unsigned long long mb = (DerivedCache::SharedBytes() + (1u << 19)) >> 20;
        std::snprintf(text, sizeof(text), "%llu MB", mb);
    }
    g_text->SetParameter(0, System::String(text));
}

void Open(UI::Element* page) {
    g_text   = FindElement<UI::TextElement>(page, "textDerivedCache");
    g_button = FindElement<UI::ButtonElement>(page, "buttonDerivedCacheClear");
    if (g_button != nullptr) g_button->SetElementId(kClearButtonId);
    ShowSize();
    std::fprintf(stderr, "[DerivedCache] Enhanced tab: text=%p button=%p\n", static_cast<void*>(g_text),
                 static_cast<void*>(g_button));
}

void Apply() {
}

void Close() {
    g_text   = nullptr;
    g_button = nullptr;
}

bool Button(int32_t elementId) {
    if (elementId != kClearButtonId) return false;
    DerivedCache::ClearShared();
    ShowSize();
    return true;
}

}  // namespace

const Contribution& DerivedCacheEntry() {
    static const Contribution c = { "derived_cache", Open, Apply, Close, Button };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
