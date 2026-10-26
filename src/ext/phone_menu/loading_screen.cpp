// ============================================================================
// src/ext/phone_menu/loading_screen.cpp -- ext.phone_menu: the loading screen (P9)
// ============================================================================
// NOT RETAIL. loading_screen.h has the contract, loading_math.h the rule.
//
// Which dialogs: Dialog/Loading.rsc is opened by every GameLoadDialogLoopT (the boot's Loading
// splash, a new game, a load, the tutorial transitions) and by ext.load_progress's boot screen, as
// a LoaderDialog whose OnOpened looks up `loadingImage` (loaderdialog.cpp). The Shining Rock splash
// is a LoaderDialog too but has no such element, and keeps retail's fit. The descriptions are
// recognised at their first layout by that element's name and recorded (resources live for the
// run); the draw reads the record.
// ============================================================================

#include "ext/phone_menu/loading_screen.h"
#include "ext/phone_menu/loading_math.h"

#include <atomic>
#include <cstring>

#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/UserInterface.h"

namespace Ext {
namespace PhoneMenu {

namespace {

constexpr int kMaxLoading = 4;
std::atomic<const UI::Dialog*> g_loading[kMaxLoading] = {};

// Descriptions seen at a layout and found not to be a loading screen (main thread only): the
// tree walk runs once per description, not per frame. A full table only costs the walk again.
constexpr int kMaxOther = 32;
const UI::Dialog* g_other[kMaxOther] = {};
int g_otherCount = 0;

bool Has(const UI::Element* e, const char* name, int depth) {
    if (e == nullptr || depth > 6) return false;
    if (e->_name != nullptr && std::strcmp(e->_name, name) == 0) return true;
    for (int32_t i = 0; i < e->_children._size; ++i) {
        if (Has(e->_children._data[i], name, depth + 1)) return true;
    }
    return false;
}

bool Recorded(const UI::Dialog* d) {
    for (int i = 0; i < kMaxLoading; ++i) {
        if (g_loading[i].load(std::memory_order_relaxed) == d) return true;
    }
    return false;
}

void Record(const UI::Dialog* d) {
    for (int i = 0; i < kMaxLoading; ++i) {
        const UI::Dialog* cur = g_loading[i].load(std::memory_order_relaxed);
        if (cur == d) return;
        if (cur == nullptr) {
            g_loading[i].store(d, std::memory_order_relaxed);
            return;
        }
    }
}

// Main thread, at a layout: is `dialog` a loading screen? Recorded the first time it is.
bool Recognise(const UI::DialogInstance* dialog) {
    const UI::Dialog* d = dialog->_dialog;
    if (d == nullptr) return false;
    if (Recorded(d)) return true;
    for (int i = 0; i < g_otherCount; ++i) {
        if (g_other[i] == d) return false;
    }
    const UI::Element* root = dialog->_root.Get();
    if (root == nullptr) return false;   // not built yet: asked again at the next layout
    if (Has(root, "loadingImage", 0)) {
        Record(d);
        return true;
    }
    if (g_otherCount < kMaxOther) g_other[g_otherCount++] = d;
    return false;
}

}  // namespace

bool PlaceLoading(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
                  int32_t* rect) {
    (void)width;
    if (dialog == nullptr) return false;
    if (point == UI::UserInterface::kExtPlaceLayout) {
        // Retail's rectangle: `main` bottom-centred on the screen's bottom, in the bottom border.
        return Recognise(dialog);
    }
    if (point == UI::UserInterface::kExtPlaceBackdrop) {
        if (!Recorded(dialog->_dialog)) return false;
        const Rect r = BottomOnBottom(Rect{rect[0], rect[1], rect[2], rect[3]}, height);
        rect[0] = r.x1;
        rect[1] = r.y1;
        rect[2] = r.x2;
        rect[3] = r.y2;
        return true;
    }
    if (point == UI::UserInterface::kExtPlaceAfterLayout) return Recorded(dialog->_dialog);
    return false;
}

}  // namespace PhoneMenu
}  // namespace Ext
