// src/ext/derived_cache/enhanced_entry.cpp -- the derived-data cache's row of the Enhanced tab (block COMPRESSAO)
//
// NOT RETAIL (docs/extensions/handins/COMPRESSAO.md). The label "Data cache on disk", the size beside
// it in the control column and, under the size, the button "Clear cache". The size is the shared store's (or the files', when this run did not open
// the cache: the row off, the faithful profile chosen in settings.cfg, another instance holding it).
// The button acts at once and cannot be undone by Cancel: the shared store stops reading and writing
// for the rest of the run, and the files are deleted at the next start (the running store holds them
// open). Element ids: kClearButtonId, far above the Options dialog's own (10..12, input binds from 13).
//
// THE WIDTH. The page is the Game tab's fixed-size scrolling container (EnhancedOptions.rsc), and its
// control column is 180 wide like the retail tabs'. The size text lives in that column, so every value
// it can show must fit there: "88888 MB" and the short cleared form "Cleared (next start)". Open
// measures the text with each of them and makes the widest its minimum width, so the column keeps one
// width while the dialog is open; a value wider than the column would cut it at the viewport edge.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/derived_cache/derived_cache.h"

#include "ui/ButtonElement.h"
#include "ui/TextElement.h"

#include <cstdint>
#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

constexpr int32_t kClearButtonId = 0x0DC00001;

UI::TextElement*   g_text   = nullptr;
UI::ButtonElement* g_button = nullptr;

void FormatSize(char (&text)[64], bool cleared, unsigned long long mb) {
    if (cleared)
        std::snprintf(text, sizeof(text), "Cleared (next start)");
    else
        std::snprintf(text, sizeof(text), "%llu MB", mb);
}

void ShowSize() {
    if (g_text == nullptr) return;
    char text[64];
    FormatSize(text, DerivedCache::SharedClearPending(), (DerivedCache::SharedBytes() + (1u << 19)) >> 20);
    g_text->SetParameter(0, System::String(text));
}

// The text's width with every value ShowSize can give it; the widest becomes its minimum width.
void FixWidth() {
    if (g_text == nullptr) return;
    // 88888 MB: as many digits as the largest cap (ext.derived_cache_mb 65536), in the widest digit.
    const bool cleared[2]             = { false, true };
    const unsigned long long mb[2]    = { 88888ull, 0ull };
    int32_t widest = g_text->_minWidth;
    for (int i = 0; i < 2; ++i) {
        char text[64];
        FormatSize(text, cleared[i], mb[i]);
        g_text->SetParameter(0, System::String(text));
        g_text->ComputeSize();
        if (g_text->GetLayoutWidth() > widest) widest = g_text->GetLayoutWidth();
    }
    g_text->_minWidth = widest;
}

void Open(UI::Element* page) {
    g_text   = FindElement<UI::TextElement>(page, "textDerivedCacheSize");
    g_button = FindElement<UI::ButtonElement>(page, "buttonDerivedCacheClear");
    if (g_button != nullptr) g_button->SetElementId(kClearButtonId);
    FixWidth();
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
