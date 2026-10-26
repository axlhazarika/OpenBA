// src/ext/enhanced_tab/texture_filter_note.cpp -- "(next start)" on retail's Texture Filter choices (P8)
//
// NOT RETAIL (owner ruling, docs/android/P8_VULKAN.md §7.2). Retail's Options > Video "Texture Filter"
// combo (OptionsDialog::PopulateFilterCombo: None, Bilinear, Anisotropic Nx) writes
// Video::Configuration::textureFilter, which both backends read when a texture is CREATED
// (dx11 imagebufferdata.cpp, vulkan companions.cpp SamplerFor): a new choice reaches the game's textures
// at the next start, retail's own rule. The combo never said so. Here every choice other than the one
// the textures were made with gets the Enhanced tab's own suffix, as its "On (next start)" /
// "Half (next start)" items (Dialog/EnhancedOptions.rsc); the suffix text is ours
// (Dialog/ExtensionsStringTable.rsc "NextStartSuffix"), never an edit of retail's string table.
//
// The value the textures were made with is the configuration's at the first Options open: nothing
// changes textureFilter before that (only this dialog's Apply writes it), and a later Apply in the same
// session does not change the textures already made.
//
// Offered() gates it, as the tab: under the faithful profile the combo is retail's.
#include "ext/enhanced_tab/enhanced_tab.h"

#include "object/External.h"
#include "system/Filename.h"
#include "ui/ComboElement.h"
#include "ui/StringTable.h"
#include "video/Device.h"   // Video::Configuration

#include <cstdint>
#include <cstdio>

namespace Ext {
namespace EnhancedTab {
namespace {

constexpr const char* kStringTableRsc = "Dialog/ExtensionsStringTable.rsc";
constexpr const char* kKeySuffix = "NextStartSuffix";   // "(next start)"

bool g_haveBootFilter = false;
int32_t g_bootFilter = 0;

}  // namespace

void NoteTextureFilterNextStart(UI::ComboElement* filter) {
    if (!Offered() || filter == nullptr) return;
    const Video::Configuration* cfg = Video::Configuration::_singleton;
    if (cfg == nullptr) return;
    if (!g_haveBootFilter) {
        g_haveBootFilter = true;
        g_bootFilter = cfg->textureFilter;
    }
    const UI::StringTable* table = Object::ExternalFactory::_singleton->FindOrLoadAs<UI::StringTable>(
        System::Filename(kStringTableRsc));
    if (!Object::IsDerivedFromNullSafe(table, UI::StringTable::TypeId())) {
        std::fprintf(stderr, "[EnhancedTab] %s missing: the Texture Filter combo stays retail's\n", kStringTableRsc);
        return;
    }
    const System::String suffix = table->GetText(System::String(kKeySuffix));
    const int32_t count = filter->GetItemCount();
    int32_t marked = 0;
    for (int32_t i = 0; i < count; ++i) {
        const intptr_t data = reinterpret_cast<intptr_t>(filter->GetItemData(i));   // CAST-OK: ptr_int
        if (static_cast<int32_t>(data) == g_bootFilter) continue;
        System::String& text = filter->_strings._data[i];
        text = System::String::Format("%s %s", text._data, suffix._data);
        ++marked;
    }
    std::fprintf(stderr,
                 "[EnhancedTab] Texture Filter: %d of %d choices marked \"%s\" (textures made with filter %d)\n",
                 marked, count, suffix._data, g_bootFilter);
    // The closed combo shows its current item's text: refresh it from the edited list (SetCurrentItem
    // returns early on the same index).
    filter->UpdateText();}

}  // namespace EnhancedTab
}  // namespace Ext
