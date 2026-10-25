// ============================================================================
// src/ext/phone_menu/phone_menu.cpp -- ext.phone_menu (class A, live)
// ============================================================================
// The rules are in phone_menu.h; the arithmetic is phone_math.cpp. The one engine seam is
// UI::UserInterface::_extDialogPlace (layout on the main thread, the backdrop on whichever thread
// draws the UI): both read the row and the recorded descriptions, nothing else.
// ============================================================================

#include "ext/phone_menu/phone_menu.h"
#include "ext/phone_menu/phone_math.h"
#include "ext/registry.h"

#include <atomic>
#include <cstdio>

#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"

namespace Ext {
namespace PhoneMenu {

namespace {

// The descriptions of the dialogs the row moves, once each has been opened: the menus
// (Dialog/MainMenu.rsc, Dialog/Pause.rsc) and the version line (Dialog/BuildId.rsc).
// Resources live for the run, so a recorded pointer stays valid; written on the main
// thread, read by the draw as well.
constexpr int kMaxDialogs = 3;
std::atomic<const UI::Dialog*> g_menus[kMaxDialogs] = {};
std::atomic<const UI::Dialog*> g_buildIds[kMaxDialogs] = {};

// Last frame's facts, in layout units (-1 = not known): where the logo ends (the menu
// backdrop's draw) and where the version line starts (its layout).
std::atomic<int32_t> g_logoBottom{-1};
std::atomic<int32_t> g_logoRight{-1};    // the logo-left layout: the logo's right edge
std::atomic<int32_t> g_logoCentre{-1};   //   and its centre line (-1: the centred layout)
std::atomic<int32_t> g_versionTop{-1};
std::atomic<int32_t> g_versionWidth{-1};
std::atomic<int32_t> g_columnWidth{-1};
std::atomic<int32_t> g_columnHeight{-1};

bool Aside(int32_t width, int32_t height) {
    return VersionAside(width, height, g_versionTop.load(std::memory_order_relaxed),
                        g_versionWidth.load(std::memory_order_relaxed),
                        g_columnWidth.load(std::memory_order_relaxed),
                        g_columnHeight.load(std::memory_order_relaxed));
}

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

void Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
           int32_t* rect) {
    if (!Ext().phone_menu || dialog == nullptr) return;
    const bool menu = In(g_menus, dialog->_dialog);
    const bool version = !menu && In(g_buildIds, dialog->_dialog);
    if (!menu && !version) return;
    const UI::Element* const root = dialog->_root.Get();

    if (point == UI::UserInterface::kExtPlaceBackdrop) {
        if (!menu) return;
        const Rect retail{rect[0], rect[1], rect[2], rect[3]};
        const Rect r = LogoLeft(retail, width);
        Store(r, rect);
        const bool logoLeft = r.y1 != retail.y1;
        g_logoRight.store(logoLeft ? LogoRight(r) : -1, std::memory_order_relaxed);
        g_logoCentre.store(logoLeft ? r.x1 + (r.x2 - r.x1) / 2 : -1, std::memory_order_relaxed);
        const float logo = static_cast<float>(r.y2 - r.y1) * kLogoBottomFraction;
        g_logoBottom.store(r.y1 + static_cast<int32_t>(logo + 0.5f), std::memory_order_relaxed);
        return;
    }
    if (point != UI::UserInterface::kExtPlaceLayout) return;
    // Last frame's laid-out sizes: the decisions below run before this frame's layout.
    const int32_t logoRight = g_logoRight.load(std::memory_order_relaxed);
    if (logoRight > 0) {
        // The logo-left layout: the logo on the left, the buttons right of it.
        if (version) {
            Store(VersionUnderLogo(width, height, g_logoCentre.load(std::memory_order_relaxed)), rect);
            return;
        }
        const int32_t column = root != nullptr ? root->_layoutRight - root->_layoutLeft : 0;
        Store(ColumnBeside(width, height, logoRight, column), rect);
        return;
    }
    if (version) {
        if (root != nullptr) {
            g_versionTop.store(root->_layoutTop, std::memory_order_relaxed);
            g_versionWidth.store(root->_layoutRight - root->_layoutLeft, std::memory_order_relaxed);
        }
        const int32_t w = g_versionWidth.load(std::memory_order_relaxed);
        Store(Aside(width, height) ? VersionAsideLayout(width, height, w) : InsetLayout(width, height), rect);
        return;
    }
    if (root != nullptr) {
        g_columnWidth.store(root->_layoutRight - root->_layoutLeft, std::memory_order_relaxed);
        g_columnHeight.store(root->_layoutBottom - root->_layoutTop, std::memory_order_relaxed);
    }
    const int32_t versionTop = Aside(width, height) ? -1 : g_versionTop.load(std::memory_order_relaxed);
    Store(MenuBand(width, height, g_logoBottom.load(std::memory_order_relaxed), versionTop,
                   g_columnHeight.load(std::memory_order_relaxed)),
          rect);
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    UI::UserInterface::_extDialogPlace = faithful ? nullptr : &Place;
}

void MenuOpened(const UI::Dialog* menu, const UI::Dialog* buildId) {
    Record(g_menus, menu);
    Record(g_buildIds, buildId);
    // A menu without a backdrop (the pause menu) must not keep the front end's logo line.
    g_logoBottom.store(-1, std::memory_order_relaxed);
    g_logoRight.store(-1, std::memory_order_relaxed);
    g_logoCentre.store(-1, std::memory_order_relaxed);
    g_versionTop.store(-1, std::memory_order_relaxed);
    g_versionWidth.store(-1, std::memory_order_relaxed);
    g_columnWidth.store(-1, std::memory_order_relaxed);
    g_columnHeight.store(-1, std::memory_order_relaxed);
    std::fprintf(stderr, "[PhoneMenu] menu %p buildId %p recorded (row %s)\n", (const void*)menu,
                 (const void*)buildId, Ext().phone_menu ? "on" : "off");
}

float UiScaleSliderMinimum(float platformMinPercent) {
    if (!Ext().phone_menu) return platformMinPercent;
    return SliderMinimum(platformMinPercent);
}

}  // namespace PhoneMenu
}  // namespace Ext
