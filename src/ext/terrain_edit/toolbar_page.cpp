// ============================================================================
// src/ext/terrain_edit/toolbar_page.cpp -- ext.terrain_edit's page in the extension window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md, src/ext/toolbar/toolbar.h. Three sections: the terrain brushes
// (five picture buttons and a strength slider), the resource tool (a Plant button, a resource drop down and
// a density slider) and what both share (the size slider, Undo and the caption). A tool button picks the
// mode and starts the brush tool (brush_tool.cpp), which then runs in the world like any of the game's own
// tools; the button of the tool in use stays lit. The sliders are read once a frame while the brush is the
// active tool (SetFrameHook), so the host needs no slider event. The page's objects are ours
// (src/ext/ui/Dialog/TerrainEditPage.rsc); the texts are in ExtensionsStringTable.rsc (the TerrainEdit*
// keys).
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/SliderElement.h"
#include "ui/ComboElement.h"
#include "ui/CheckElement.h"
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
constexpr int32_t kPlantComboId = kUndoId + 1;   // the Resource drop down

// The strength slider counts in steps of kStrengthStep, from kMinStrength / kStrengthStep to kMaxStrength /
// kStrengthStep.
constexpr float kStrengthSliderMin = kMinStrength / kStrengthStep;
constexpr float kStrengthSliderMax = kMaxStrength / kStrengthStep;

const char* const kModeNames[kModeCount] = { "Raise", "Lower", "Flatten", "Mountain", "Smooth", "Plant" };

// The page, kept while it is open so the brush can refresh it (the host has no per-frame call for a page).
// Forgotten in PageClose.
Object::InternalPtr<UI::Element> g_page;
UI::SliderElement* g_size = nullptr;
UI::SliderElement* g_strength = nullptr;
UI::SliderElement* g_density = nullptr;
UI::CheckElement* g_toolChecks[kModeCount] = {};   // the six tools' buttons, in Mode order
UI::ComboElement* g_plantCombo = nullptr;   // the resource the Plant tool places
char g_shown[96] = "";   // the caption as last set, so the text is only touched when it changes

// The button of the tool in use stays lit, as the game's own toolbar does: a check box is lit while it is
// checked, so exactly the one of the running brush's mode is checked (a click toggled the clicked one; this
// makes it right again, and puts them all out when the brush stops).
void ShowTools() {
    const bool running = BrushRunning();
    for (int i = 0; i < kModeCount; ++i) {
        if (g_toolChecks[i] != nullptr) {
            g_toolChecks[i]->_checked = (running && static_cast<int>(State().mode) == i) ? 1 : 0;
        }
    }
}

// "Mountain brush, 3 to undo" into the caption.
void ShowStatus(UI::Element* page) {
    ShowTools();
    UI::Element* e = page->FindElement(System::String("textTerrainEditStatus")).Get();
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) return;
    char text[96];
    if (State().mode == Mode::Plant) {
        std::snprintf(text, sizeof text, "Plant: %s, density %d%%, %d to undo", PlantName(),
                      static_cast<int>(State().density * 100.0f + 0.5f), static_cast<int>(UndoDepth()));
    } else if (UndoRefused()) {
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
    if (g_density != nullptr) {
        const float steps = ClampFloat(std::floor(g_density->_value + 0.5f), kMinDensity / kDensityStep,
                                       kMaxDensity / kDensityStep);
        st.density = steps * kDensityStep;
    }
    if (g_plantCombo != nullptr) {   // choosing a resource in the drop down selects the Plant tool
        const int chosen = g_plantCombo->_currentItem;
        if (chosen >= 0 && chosen != PlantIndex()) {
            PlantSet(chosen);
            st.mode = Mode::Plant;
            if (!BrushActive()) StartBrush();
        }
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
    static const struct {
        const char* name;
        int32_t id;
    } kButtons[] = {
        { "buttonTerrainEditRaise", kModeBase + 0 },    { "buttonTerrainEditLower", kModeBase + 1 },
        { "buttonTerrainEditFlatten", kModeBase + 2 },  { "buttonTerrainEditMountain", kModeBase + 3 },
        { "buttonTerrainEditSmooth", kModeBase + 4 },   { "buttonTerrainEditPlant", kModeBase + 5 },
        { "buttonTerrainEditUndo", kUndoId },
    };
    for (const auto& b : kButtons) {
        UI::ButtonElement* button = FindElement<UI::ButtonElement>(page, b.name);
        if (button != nullptr) button->SetElementId(idBase + b.id);
    }
    for (int i = 0; i < kModeCount; ++i) {
        g_toolChecks[i] = FindElement<UI::CheckElement>(page, kButtons[i].name);
    }
    g_size = FindElement<UI::SliderElement>(page, "sliderTerrainEditSize");
    g_strength = FindElement<UI::SliderElement>(page, "sliderTerrainEditStrength");
    g_density = FindElement<UI::SliderElement>(page, "sliderTerrainEditDensity");
    g_plantCombo = FindElement<UI::ComboElement>(page, "comboTerrainEditPlant");
    if (g_plantCombo != nullptr) {
        g_plantCombo->SetElementId(idBase + kPlantComboId);
        g_plantCombo->RemoveAllItems();
        const int count = PlantCount();
        for (int i = 0; i < count; ++i) {
            g_plantCombo->AddItem(UI::Sprite(), System::String(PlantNameAt(i)), nullptr);
        }
        g_plantCombo->SetCurrentItem(count > 0 ? PlantIndex() : -1);
    }
    const BrushState& st = State();
    if (g_size != nullptr) {
        g_size->_minimum = kMinRadius;
        g_size->_maximum = kMaxRadius;
        g_size->_value = st.radius;
    }
    if (g_density != nullptr) {
        g_density->_minimum = kMinDensity / kDensityStep;
        g_density->_maximum = kMaxDensity / kDensityStep;
        g_density->_value = st.density / kDensityStep;
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
    } else if (id == kPlantComboId) {
        ReadSliders();

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
    g_density = nullptr;
    g_plantCombo = nullptr;
    for (UI::CheckElement*& check : g_toolChecks) check = nullptr;
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
