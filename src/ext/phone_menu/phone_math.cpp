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

Rect LogoLeft(const Rect& retail, int32_t width) {
    const int32_t drawnW = retail.x2 - retail.x1;
    const int32_t drawnH = retail.y2 - retail.y1;
    if (retail.y1 >= 0 || drawnW <= 0 || drawnH <= 0 || width <= 0) return retail;
    const float w = 2.0f * (1.0f - kLogoCentreFraction) * static_cast<float>(width);
    const float h = w * static_cast<float>(drawnH) / static_cast<float>(drawnW);
    const int32_t overlayW = static_cast<int32_t>(w + 0.5f);
    return Rect{width - overlayW, 0, width, static_cast<int32_t>(h + 0.5f)};
}

int32_t LogoRight(const Rect& overlay) {
    const float w = static_cast<float>(overlay.x2 - overlay.x1);
    return overlay.x1 + static_cast<int32_t>(w * kLogoRightFraction + 0.5f);
}

Rect ColumnBeside(int32_t width, int32_t height, int32_t logoRight, int32_t columnWidth) {
    const Rect safe = InsetLayout(width, height);
    int32_t left = logoRight;
    if (left < safe.x1) left = safe.x1;
    if (columnWidth <= 0 || columnWidth <= safe.x2 - left) return Rect{left, safe.y1, safe.x2, safe.y2};
    return Rect{safe.x2 - columnWidth, safe.y1, safe.x2, safe.y2};
}

Rect VersionUnderLogo(int32_t width, int32_t height, int32_t logoCentre) {
    const Rect safe = InsetLayout(width, height);
    if (logoCentre <= safe.x1 || logoCentre >= safe.x2) return safe;
    // A rectangle symmetric about the logo's centre line, as wide as fits on screen.
    int32_t half = logoCentre;
    if (width - logoCentre < half) half = width - logoCentre;
    return Rect{logoCentre - half, safe.y1, logoCentre + half, safe.y2};
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

float SliderMinimum(float platformMinPercent) {
    if (platformMinPercent < kUiScaleMinPercent) return platformMinPercent;
    return kUiScaleMinPercent;
}

}  // namespace PhoneMenu
}  // namespace Ext
