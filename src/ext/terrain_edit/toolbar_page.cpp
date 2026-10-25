// ============================================================================
// src/ext/terrain_edit/toolbar_page.cpp -- ext.terrain_edit's page in the extension window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md, src/ext/toolbar/toolbar.h. A note, one row of picture buttons
// (the five brushes and Undo), a size slider, a strength slider and a caption. A brush button picks the
// mode and starts the brush tool (brush_tool.cpp), which then runs in the world like any of the game's own
// tools. The sliders are read once a frame while the brush is the active tool (SetFrameHook), so the host
// needs no slider event. The page's objects are ours (src/ext/ui/Dialog/TerrainEditPage.rsc); the texts are
// in ExtensionsStringTable.rsc (the TerrainEdit* keys).
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/SliderElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"

#include "ext/terrain_edit/terrain_edit.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Ext {
namespace TerrainEdit {

namespace {

// Local element ids (the host adds the page's idBase). The tool row's buttons are kModeBase + their place
// in it: the modes, then Undo.
constexpr int32_t kModeBase = 10;
constexpr int32_t kUndoId   = kModeBase + kModeCount;
constexpr int32_t kToolCount = kModeCount + 1;

// The strength slider counts in steps of kStrengthStep, from kMinStrength / kStrengthStep to kMaxStrength /
// kStrengthStep.
constexpr float kStrengthSliderMin = kMinStrength / kStrengthStep;
constexpr float kStrengthSliderMax = kMaxStrength / kStrengthStep;

const char* const kModeNames[kModeCount] = { "Raise", "Lower", "Flatten", "Mountain", "Smooth" };

// The page, kept while it is open so the brush can refresh it (the host has no per-frame call for a page).
// Forgotten in PageClose.
Object::InternalPtr<UI::Element> g_page;
UI::SliderElement* g_size = nullptr;
UI::SliderElement* g_strength = nullptr;
char g_shown[96] = "";   // the caption as last set, so the text is only touched when it changes

// Gives the buttons of the row named `rowName` the ids first, first + 1, ...
void NumberRow(UI::Element* page, const char* rowName, int32_t idBase, int32_t first, int32_t count) {
    UI::Element* row = page->FindElement(System::String(rowName)).Get();
    if (row == nullptr) return;
    for (int32_t i = 0; i < count && i < row->_children.GetSize(); ++i) {
        row->_children.GetData()[i]->SetElementId(idBase + first + i);
    }
}

// "Mountain brush, 3 to undo" into the caption.
void ShowStatus(UI::Element* page) {
    UI::Element* e = page->FindElement(System::String("textTerrainEditStatus")).Get();
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) return;
    char text[96];
    if (UndoRefused()) {
        std::snprintf(text, sizeof text, "Cannot undo: something was built on this stroke");
    } else {
        std::snprintf(text, sizeof text, "%s brush, %d to undo", kModeNames[static_cast<int>(State().mode)],
                      static_cast<int>(UndoDepth()));
    }
    if (std::strcmp(text, g_shown) == 0) return;
    std::snprintf(g_shown, sizeof g_shown, "%s", text);
    static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text));
}

// The brush's size and strength from the sliders (whole steps; the strength slider is in half units).
void ReadSliders() {
    BrushState& st = State();
    if (g_size != nullptr) {
        st.radius = ClampFloat(std::floor(g_size->_value + 0.5f), kMinRadius, kMaxRadius);
    }
    if (g_strength != nullptr) {
        const float steps =
            ClampFloat(std::floor(g_strength->_value + 0.5f), kStrengthSliderMin, kStrengthSliderMax);
        st.strength = steps * kStrengthStep;
    }
}

void RefreshPage() {
    if (g_page.Get() == nullptr) return;
    ReadSliders();
    ShowStatus(g_page.Get());
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_page = Object::InternalPtr<UI::Element>(page);
    g_shown[0] = '\0';
    SetUndoChangedHook(&RefreshPage);
    SetFrameHook(&RefreshPage);
    NumberRow(page, "rowTerrainEditModes", idBase, kModeBase, kToolCount);
    g_size = FindElement<UI::SliderElement>(page, "sliderTerrainEditSize");
    g_strength = FindElement<UI::SliderElement>(page, "sliderTerrainEditStrength");
    const BrushState& st = State();
    if (g_size != nullptr) {
        g_size->_minimum = kMinRadius;
        g_size->_maximum = kMaxRadius;
        g_size->_value = st.radius;
    }
    if (g_strength != nullptr) {
        g_strength->_minimum = kStrengthSliderMin;
        g_strength->_maximum = kStrengthSliderMax;
        g_strength->_value = st.strength / kStrengthStep;
    }
    std::fprintf(stderr, "[TerrainEdit] page: size slider=%p strength slider=%p\n",
                 static_cast<void*>(g_size), static_cast<void*>(g_strength));
    ShowStatus(page);
}

bool PageChanged(UI::Element* page, int32_t id) {
    if (id >= kModeBase && id < kModeBase + kModeCount) {
        ReadSliders();
        State().mode = static_cast<Mode>(id - kModeBase);
        if (!BrushActive()) StartBrush();
    } else if (id == kUndoId) {
        UndoLast();
    } else {
        return false;
    }
    ShowStatus(page);
    return true;
}

void PageClose() {
    SetUndoChangedHook(nullptr);
    SetFrameHook(nullptr);
    g_page = Object::InternalPtr<UI::Element>();
    g_size = nullptr;
    g_strength = nullptr;
}

}  // namespace

}  // namespace TerrainEdit

namespace Toolbar {

const Contribution& TerrainEditPage() {
    static const Contribution c = { "terrain_edit", "TerrainEditTab",
                                    "Dialog/TerrainEditPage.rsc:pageTerrainEdit",
                                    TerrainEdit::PageOpen, TerrainEdit::PageChanged, TerrainEdit::PageClose };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
