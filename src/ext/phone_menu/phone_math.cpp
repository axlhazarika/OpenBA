// ============================================================================
// src/ext/phone_menu/phone_math.cpp -- ext.phone_menu's pure decisions
// ============================================================================
// The rules are in phone_math.h. Standard C++ only.
// ============================================================================

#include "phone_math.h"

namespace Ext {
namespace PhoneMenu {

int32_t SafeMargin(int32_t height) {
    if (height <= 0) return 0;
    return static_cast<int32_t>(static_cast<float>(height) * kSafeMarginFraction + 0.5f);
}

Rect InsetLayout(int32_t width, int32_t height) {
    int32_t m = SafeMargin(height);
    if (2 * m >= width || 2 * m >= height) m = 0;
    return Rect{m, m, width - m, height - m};
}

bool WiderThanBackdrop(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH) {
    if (width <= 0 || height <= 0 || spriteW <= 0 || spriteH <= 0) return false;
    const float screenAspect = static_cast<float>(width) / static_cast<float>(height);
    const float spriteAspect = static_cast<float>(spriteW) / static_cast<float>(spriteH);
    return !(spriteAspect > screenAspect);
}

int32_t BackdropHeight(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH) {
    if (!WiderThanBackdrop(width, height, spriteW, spriteH)) return height;
    const float spriteAspect = static_cast<float>(spriteW) / static_cast<float>(spriteH);
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / spriteAspect + 0.5f) >> 1;
    return 2 * half;
}

Rect LogoTop(const Rect& retail) {
    if (retail.y1 >= 0) return retail;
    return Rect{retail.x1, 0, retail.x2, retail.y2 - retail.y1};
}

int32_t LogoBottom(int32_t backdropHeight) {
    if (backdropHeight <= 0) return -1;
    return static_cast<int32_t>(static_cast<float>(backdropHeight) * kLogoBottomFraction + 0.5f);
}

int32_t SquareSide(const int32_t* labelWidths, const int32_t* labelHeights, int32_t count) {
    int32_t side = 0;
    for (int32_t i = 0; i < count; ++i) {
        if (labelWidths[i] > side) side = labelWidths[i];
        if (labelHeights[i] > side) side = labelHeights[i];
    }
    return side;
}

bool RowFits(int32_t width, int32_t height, int32_t rowWidth, int32_t rowHeight, int32_t logoBottom,
             int32_t versionTop) {
    const Rect safe = InsetLayout(width, height);
    if (rowWidth <= 0 || rowHeight <= 0) return false;
    if (rowWidth > safe.x2 - safe.x1 || rowHeight > safe.y2 - safe.y1) return false;
    const int32_t top = safe.y1 + ((safe.y2 - safe.y1) - rowHeight) / 2;
    if (logoBottom >= 0 && top < logoBottom) return false;
    if (versionTop >= 0 && top + rowHeight > versionTop - kVersionGap) return false;
    return true;
}

Rect MenuBand(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop,
              int32_t columnHeight) {
    const Rect safe = InsetLayout(width, height);
    if (columnHeight <= 0) return safe;
    int32_t bottom = safe.y2;
    if (versionTop > safe.y1 && versionTop - kVersionGap < bottom) bottom = versionTop - kVersionGap;
    int32_t top = safe.y1;
    if (logoBottom > top) top = logoBottom;
    if (columnHeight <= bottom - top) return Rect{safe.x1, top, safe.x2, bottom};
    // Taller than the safe band above the version line but not than the screen above it:
    // DialogInstance::ComputeLayout keeps the column's bottom at `bottom` and lets its top
    // rise into the margin, so the buttons never run under the version line.
    if (columnHeight <= bottom) return Rect{safe.x1, safe.y1, safe.x2, bottom};
    return safe;
}

bool VersionAside(int32_t width, int32_t height, int32_t versionTop, int32_t versionWidth,
                  int32_t columnWidth, int32_t columnHeight) {
    if (versionTop <= 0 || versionWidth <= 0 || columnWidth <= 0 || columnHeight <= 0) return false;
    if (columnHeight <= versionTop - kVersionGap) return false;
    const int32_t besideColumn = (width - columnWidth) / 2;
    return SafeMargin(height) + versionWidth + kVersionGap <= besideColumn;
}

Rect VersionAsideLayout(int32_t width, int32_t height, int32_t versionWidth) {
    const Rect safe = InsetLayout(width, height);
    return Rect{safe.x1, safe.y1, safe.x1 + versionWidth, safe.y2};
}

int32_t VersionTop(int32_t width, int32_t height, int32_t versionHeight) {
    if (versionHeight <= 0) return -1;
    return InsetLayout(width, height).y2 - versionHeight;
}

namespace {

// One axis of SafeArea: {near, far} for an extent with margin `m`.
void SafeAxis(int32_t extent, int32_t m, int32_t upperLeft, int32_t last, int32_t& nearEdge,
              int32_t& farEdge) {
    nearEdge = 0;
    farEdge = extent;
    if (last > extent - 2 * m) return;
    if (upperLeft < m) nearEdge = m - upperLeft;
    farEdge = extent - m;
}

}  // namespace

Rect SafeArea(int32_t width, int32_t height, int32_t upperLeftX, int32_t upperLeftY, int32_t lastWidth,
              int32_t lastHeight) {
    const Rect safe = InsetLayout(width, height);
    const int32_t m = safe.x1;
    if (m == 0) return safe;
    Rect r{};
    SafeAxis(width, m, upperLeftX, lastWidth, r.x1, r.x2);
    SafeAxis(height, m, upperLeftY, lastHeight, r.y1, r.y2);
    return r;
}

float SliderMinimum(float platformMinPercent) {
    if (platformMinPercent < kUiScaleMinPercent) return platformMinPercent;
    return kUiScaleMinPercent;
}

}  // namespace PhoneMenu
}  // namespace Ext
