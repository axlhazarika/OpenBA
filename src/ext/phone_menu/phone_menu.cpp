// ============================================================================
// src/ext/phone_menu/phone_menu.cpp -- ext.phone_menu (class A, live)
// ============================================================================
// The rules are in phone_menu.h; the arithmetic is phone_math.cpp. The one engine seam is
// UI::UserInterface::_extDialogPlace (layout on the main thread, the backdrop on whichever thread
// draws the UI). The layout hook decides from THIS frame's sizes: it sizes the menu's tree (and
// the version line's) itself before retail's ComputeLayout does, so a menu's first drawn frame is
// already its final layout. A dialog that is not a menu is laid out in phone_math.h's SafeArea.
//
// THE BUTTON ROW. On a screen wider than the menus' backdrop a menu's button list (MainMenu.rsc
// `buttonList`; Pause.rsc's root; RibbonElements laid out top-to-bottom) is turned into one
// horizontal row of square buttons: the ribbon's `_vertical` cleared, every button's minimum
// size set to one square side (as large as fits across the safe area, phone_math.h RowSide), its
// label centred with the icon above the text, in the small font (Font/FontSmall.rsc) when the
// labels' own does not fit. The values are the menu INSTANCE's (the description is untouched);
// the hook keeps retail's values and puts them back whenever the row does not apply (the row
// switched off, a screen it does not fit). Main thread only.
// ============================================================================

#include "ext/phone_menu/phone_menu.h"
#include "ext/phone_menu/phone_math.h"
#include "ext/registry.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "object/External.h"
#include "system/Filename.h"
#include "ui/DialogInstance.h"
#include "ui/Font.h"
#include "ui/LabelElement.h"
#include "ui/RibbonElement.h"
#include "ui/SpriteSheet.h"
#include "ui/UserInterface.h"

namespace Ext {
namespace PhoneMenu {

namespace {

// The descriptions of the dialogs the row moves, once each has been opened: the main menu
// (Dialog/MainMenu.rsc), the pause menu (Dialog/Pause.rsc) and the version line
// (Dialog/BuildId.rsc). Resources live for the run, so a recorded pointer stays valid; written
// on the main thread, read by the draw as well.
constexpr int kMaxDialogs = 3;
std::atomic<const UI::Dialog*> g_menus[kMaxDialogs] = {};
std::atomic<const UI::Dialog*> g_pauses[kMaxDialogs] = {};
std::atomic<const UI::Dialog*> g_buildIds[kMaxDialogs] = {};

// This frame's facts, written by the menu's layout and read by the version line's, which
// UserInterface::Update lays out after it (the version line is the menu's child dialog, added
// after it). Main thread only.
bool g_rowActive = false;
bool g_versionAside = false;
int32_t g_versionWidth = -1;

void Record(std::atomic<const UI::Dialog*>* set, const UI::Dialog* dialog) {
    if (dialog == nullptr) return;
    for (int i = 0; i < kMaxDialogs; ++i) {
        const UI::Dialog* cur = set[i].load(std::memory_order_relaxed);
        if (cur == dialog) return;
        if (cur == nullptr) {
            set[i].store(dialog, std::memory_order_relaxed);
            return;
        }
    }
}

bool In(const std::atomic<const UI::Dialog*>* set, const UI::Dialog* dialog) {
    if (dialog == nullptr) return false;
    for (int i = 0; i < kMaxDialogs; ++i) {
        if (set[i].load(std::memory_order_relaxed) == dialog) return true;
    }
    return false;
}

void Store(const Rect& r, int32_t* rect) {
    rect[0] = r.x1;
    rect[1] = r.y1;
    rect[2] = r.x2;
    rect[3] = r.y2;
}

// ---- the menu instance's retail values and the row ------------------------

constexpr int kMaxButtons = 16;

struct RowState {
    const UI::DialogInstance* dialog = nullptr;   // the menu instance the values below belong to
    UI::RibbonElement* ribbon = nullptr;          // its `buttonList`
    uint8_t vertical = 1;                         // retail's values, as captured
    int32_t count = 0;
    UI::Element* buttons[kMaxButtons] = {};
    UI::LabelElement* labels[kMaxButtons] = {};
    int32_t minWidth[kMaxButtons] = {};
    int32_t minHeight[kMaxButtons] = {};
    int32_t alignment[kMaxButtons] = {};          // the label's place in its button
    int32_t inner[kMaxButtons] = {};              // the icon / text pair's alignment (_align)
    uint32_t mode[kMaxButtons] = {};              // the icon's side of the text (_mode)
    UI::Font* font[kMaxButtons] = {};
    int32_t pads[kMaxButtons][4] = {};            // the label's left / top / right / bottom padding
    UI::Font* smallFont = nullptr;                // Font/FontSmall.rsc, when the row needs it
    bool applied = false;
};
RowState g_row;

UI::Element* FindNamed(UI::Element* e, const char* name, int depth) {
    if (e == nullptr || depth > 6) return nullptr;
    if (e->_name != nullptr && std::strcmp(e->_name, name) == 0) return e;
    for (int32_t i = 0; i < e->_children._size; ++i) {
        UI::Element* hit = FindNamed(e->_children._data[i], name, depth + 1);
        if (hit != nullptr) return hit;
    }
    return nullptr;
}

UI::LabelElement* LabelOf(UI::Element* button) {
    for (int32_t i = 0; i < button->_children._size; ++i) {
        UI::Element* child = button->_children._data[i];
        if (::Object::IsDerivedFromNullSafe(child, UI::LabelElement::TypeId()))
            return static_cast<UI::LabelElement*>(child);
    }
    return nullptr;
}

UI::Font* LoadSmallFont() {
    Object::ExternalFactory* const factory = Object::ExternalFactory::_singleton;
    if (factory == nullptr) return nullptr;
    UI::Font* font = factory->FindOrLoadAs<UI::Font>(System::Filename("Font/FontSmall.rsc"));
    return ::Object::IsDerivedFromNullSafe(font, UI::Font::TypeId()) ? font : nullptr;
}

// The first layout of a menu instance: keep its retail values.
void Capture(const UI::DialogInstance* dialog, UI::Element* root) {
    g_row = RowState();
    g_row.dialog = dialog;
    // The main menu's buttons are its `buttonList`; the pause menu's root is the list itself.
    UI::Element* list = FindNamed(root, "buttonList", 0);
    if (list == nullptr) list = root;
    if (!::Object::IsDerivedFromNullSafe(list, UI::RibbonElement::TypeId())) return;
    UI::RibbonElement* ribbon = static_cast<UI::RibbonElement*>(list);
    for (int32_t i = 0; i < ribbon->_children._size && g_row.count < kMaxButtons; ++i) {
        UI::Element* button = ribbon->_children._data[i];
        if (button == nullptr) continue;
        UI::LabelElement* label = LabelOf(button);
        if (label == nullptr) continue;
        const int32_t n = g_row.count++;
        g_row.buttons[n] = button;
        g_row.labels[n] = label;
        g_row.minWidth[n] = button->_minWidth;
        g_row.minHeight[n] = button->_minHeight;
        g_row.alignment[n] = label->_alignment;
        g_row.inner[n] = label->_align;
        g_row.mode[n] = label->_mode;
        g_row.font[n] = label->_font;
        g_row.pads[n][0] = label->_leftPad;
        g_row.pads[n][1] = label->_topPad;
        g_row.pads[n][2] = label->_rightPad;
        g_row.pads[n][3] = label->_bottomPad;
    }
    g_row.ribbon = ribbon;
    g_row.vertical = ribbon->_vertical;
}

void Restore() {
    if (!g_row.applied || g_row.ribbon == nullptr) return;
    g_row.ribbon->_vertical = g_row.vertical;
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        g_row.buttons[i]->_minWidth = g_row.minWidth[i];
        g_row.buttons[i]->_minHeight = g_row.minHeight[i];
        label->_alignment = g_row.alignment[i];
        label->_align = g_row.inner[i];
        label->_mode = g_row.mode[i];
        label->_font = g_row.font[i];
        label->_leftPad = g_row.pads[i][0];
        label->_topPad = g_row.pads[i][1];
        label->_rightPad = g_row.pads[i][2];
        label->_bottomPad = g_row.pads[i][3];
        label->SetNeedsSizeCalc();
    }
    g_row.ribbon->SetNeedsSizeCalc();
    g_row.applied = false;
}

// The row's shape: the ribbon across, every label centred in its button with its icon above
// its text (in `font`, or each label's own when null), every button `side` square.
void Shape(UI::Font* font, int32_t side) {
    g_row.ribbon->_vertical = 0;
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        g_row.buttons[i]->_minWidth = side;
        g_row.buttons[i]->_minHeight = side;
        label->_alignment = 4;   // MidCenter: the label in the middle of its square
        label->_align = 4;       //   and the icon / text pair centred inside the label
        label->_mode = 3;        //   with the icon above the text
        label->_font = font != nullptr ? font : g_row.font[i];
        label->_leftPad = 0;     //   with no padding of its own: the square's frame is the margin
        label->_topPad = 0;
        label->_rightPad = 0;
        label->_bottomPad = 0;
        label->SetNeedsSizeCalc();
    }
    g_row.ribbon->SetNeedsSizeCalc();
    g_row.applied = true;
}

// The visible buttons' labels, sized in their row shape: the square side they need.
int32_t ContentSide(int32_t& visible) {
    int32_t widths[kMaxButtons];
    int32_t heights[kMaxButtons];
    visible = 0;
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        if ((g_row.buttons[i]->_flags & UI::kElementFlagHidden) != 0) continue;
        label->ComputeSize();
        widths[visible] = label->_layoutWidth + label->_leftPad + label->_rightPad;
        heights[visible] = label->_layoutHeight + label->_topPad + label->_bottomPad;
        ++visible;
    }
    return SquareSide(widths, heights, visible);
}

void PaddedSize(UI::Element* root, int32_t& w, int32_t& h);

// The last row decision's numbers, for the one [PhoneMenu] line printed when they change.
struct RowNumbers {
    int32_t visible = 0;
    int32_t content = 0;
    int32_t fit = 0;
    int32_t side = 0;
    bool small = false;
};
RowNumbers g_numbers;

// The row in one font: the squares as large as fits across the safe area (RowSide). False when
// the labels do not fit in a square that small.
bool ShapeRow(UI::Element* root, UI::Font* font, int32_t width, int32_t height) {
    Shape(font, 0);
    int32_t visible = 0;
    const int32_t content = ContentSide(visible);
    g_numbers = RowNumbers();
    g_numbers.visible = visible;
    g_numbers.content = content;
    g_numbers.small = font != nullptr;
    if (visible == 0 || content <= 0) return false;
    Shape(font, content);
    int32_t w = 0;
    int32_t h = 0;
    PaddedSize(root, w, h);
    g_numbers.fit = FitSide(width, height, w - visible * content, visible);
    const int32_t side = RowSide(content, g_numbers.fit, height);
    g_numbers.side = side;
    if (side <= 0) return false;
    Shape(font, side);
    return true;
}

// The row: in the labels' own font, else in the small font; false when neither fits.
bool ApplyRow(UI::Element* root, int32_t width, int32_t height) {
    if (g_row.ribbon == nullptr || g_row.count == 0) return false;
    if (ShapeRow(root, nullptr, width, height)) return true;
    if (g_row.smallFont == nullptr) g_row.smallFont = LoadSmallFont();
    return g_row.smallFont != nullptr && ShapeRow(root, g_row.smallFont, width, height);
}

// The padded size the dialog's root will be laid out at (DialogInstance::ComputeLayout's
// arithmetic, sized now).
void PaddedSize(UI::Element* root, int32_t& w, int32_t& h) {
    root->ComputeSize();
    w = root->_layoutWidth + root->_leftPad + root->_rightPad;
    h = root->_layoutHeight + root->_topPad + root->_bottomPad;
}

// The version line's dialog in the same UserInterface (the menu's child, laid out after it),
// sized now; false when it is not open.
bool VersionSize(const UI::DialogInstance* menu, int32_t& w, int32_t& h) {
    w = -1;
    h = -1;
    UI::UserInterface* ui = menu->_ui;
    if (ui == nullptr) return false;
    for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
        UI::DialogInstance* d = node->element;
        if (d == nullptr || !In(g_buildIds, d->_dialog)) continue;
        UI::Element* root = d->_root.Get();
        if (root == nullptr) return false;
        PaddedSize(root, w, h);
        return true;
    }
    return false;
}

// ---- the hook ---------------------------------------------------------------

// One stderr line each time the menu's layout decision changes (a new menu, a new screen size
// or UI scale): row or column, and the numbers that decided it.
void LogDecision(bool wide, int32_t width, int32_t height) {
    static bool lastRow = false;
    static int32_t lastWidth = -1;
    static int32_t lastSide = -1;
    static const UI::DialogInstance* lastDialog = nullptr;
    if (lastDialog == g_row.dialog && lastRow == g_rowActive && lastWidth == width && lastSide == g_numbers.side)
        return;
    lastDialog = g_row.dialog;
    lastRow = g_rowActive;
    lastWidth = width;
    lastSide = g_numbers.side;
    std::fprintf(stderr, "[PhoneMenu] %dx%d: %s (wide %d, %d buttons, labels need %d, %d fit across, square %d%s)\n",
                 width, height, g_rowActive ? "button row" : "column", wide ? 1 : 0, g_numbers.visible,
                 g_numbers.content, g_numbers.fit, g_numbers.side, g_numbers.small ? ", small font" : "");
}

void PlaceMenu(const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect) {
    // The layout hook runs where retail's ComputeLayout sizes and lays out this same tree; the
    // seam hands the dialog const because it only reads it on every other path.
    UI::Element* const root = const_cast<UI::Element*>(dialog->_root.Get());
    if (root == nullptr) return;
    if (g_row.dialog != dialog) Capture(dialog, root);

    // The backdrop as Draw will place it (this frame, from the dialog's own picture).
    int32_t logoBottom = -1;
    bool wide = false;
    if (dialog->_spriteSheet != nullptr && dialog->_spriteIndex >= 0) {
        const auto& sprite = dialog->_spriteSheet->m_sprites._data[dialog->_spriteIndex];
        wide = WiderThanBackdrop(width, height, sprite.width, sprite.height);
        logoBottom = LogoBottom(BackdropHeight(width, height, sprite.width, sprite.height));
    }
    int32_t versionW = -1;
    int32_t versionH = -1;
    VersionSize(dialog, versionW, versionH);
    const int32_t versionTop = VersionTop(width, height, versionH);

    // The row on a screen wider than the backdrop, when it fits; retail's column otherwise.
    g_rowActive = false;
    if (wide && ApplyRow(root, width, height)) {
        int32_t w = 0;
        int32_t h = 0;
        PaddedSize(root, w, h);
        g_rowActive = RowFits(width, height, w, h, logoBottom, versionTop);
    }
    if (!g_rowActive) Restore();
    LogDecision(wide, width, height);

    int32_t columnW = 0;
    int32_t columnH = 0;
    PaddedSize(root, columnW, columnH);
    g_versionWidth = versionW;
    g_versionAside = !g_rowActive && VersionAside(width, height, versionTop, versionW, columnW, columnH);
    if (g_rowActive) {
        Store(InsetLayout(width, height), rect);
        return;
    }
    Store(MenuBand(width, height, logoBottom, g_versionAside ? -1 : versionTop, columnH), rect);
}

void Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
           int32_t* rect) {
    if (dialog == nullptr) return;
    if (!Ext().phone_menu) {
        // Switched off while a menu is open: its retail values back before retail lays it out.
        // (A recorded menu at that address went through MenuOpened, which forgets the old one.)
        const bool menuNow = In(g_menus, dialog->_dialog) || In(g_pauses, dialog->_dialog);
        if (point == UI::UserInterface::kExtPlaceLayout && menuNow && dialog == g_row.dialog) Restore();
        return;
    }
    const bool menu = In(g_menus, dialog->_dialog);
    const bool pause = !menu && In(g_pauses, dialog->_dialog);
    const bool version = !menu && !pause && In(g_buildIds, dialog->_dialog);
    const UI::Element* const root = dialog->_root.Get();

    if (point == UI::UserInterface::kExtPlaceBackdrop) {
        // The menus' backdrop on a wider screen: its top on the screen's top, the logo whole.
        // Retail's fit for every other backdrop.
        if (!menu && !pause) return;
        Store(LogoTop(Rect{rect[0], rect[1], rect[2], rect[3]}), rect);
        return;
    }
    if (point != UI::UserInterface::kExtPlaceLayout) return;

    if (menu || pause) {
        PlaceMenu(dialog, width, height, rect);
        return;
    }
    if (version) {
        Store(g_versionAside ? VersionAsideLayout(width, height, g_versionWidth) : InsetLayout(width, height),
              rect);
        return;
    }
    // Every other dialog: the in-game HUD, its panels, tooltips, drop-downs and the front
    // end's other dialogs (phone_math.h SafeArea). Its backdrop keeps retail's fit.
    if (root == nullptr) return;
    // A root sized to the back buffer (ComputeLayout's 0x10 / 0x20 flags) is meant to cover
    // the screen: retail's rectangle.
    if ((root->_flags & 0x30) != 0) return;
    Store(SafeArea(width, height, dialog->_upperLeftX, dialog->_upperLeftY,
                   root->_layoutRight - root->_layoutLeft, root->_layoutBottom - root->_layoutTop),
          rect);
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    UI::UserInterface::_extDialogPlace = faithful ? nullptr : &Place;
}

void MenuOpened(const UI::Dialog* menu, const UI::Dialog* buildId, bool pause) {
    Record(pause ? g_pauses : g_menus, menu);
    Record(g_buildIds, buildId);
    // A new menu instance: its retail values are captured at its first layout.
    g_row = RowState();
    g_rowActive = false;
    g_versionAside = false;
    g_versionWidth = -1;
    std::fprintf(stderr, "[PhoneMenu] %s %p buildId %p recorded (row %s)\n", pause ? "pause menu" : "menu",
                 (const void*)menu, (const void*)buildId, Ext().phone_menu ? "on" : "off");
}

float UiScaleSliderMinimum(float platformMinPercent) {
    if (!Ext().phone_menu) return platformMinPercent;
    return SliderMinimum(platformMinPercent);
}

}  // namespace PhoneMenu
}  // namespace Ext
