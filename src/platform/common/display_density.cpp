// ============================================================================
// src/platform/common/display_density.cpp -- UI-scale defaults for a high-density window
// ============================================================================
// The rules are in display_density.h. Both answers derive from the game window's pixel
// density (Platform::Window::GamePixelDensity), which is 1.0 for every window not created
// with the high-density flag (Windows, Linux, and Apple without the Retina mode), so
// there both are the game's own values, unchanged.
// ============================================================================

#include "display_density.h"
#include "platform_window_hooks.h"
#include "ui_render_scale.h"   // P9: the UI scale in screen units (pure)

namespace Platform {
namespace Display {

namespace {
float Density() {
    const float d = Window::GamePixelDensity();
    return d >= 1.0f ? d : 1.0f;
}
}  // namespace

int FirstRunUiScalePercent(int defaultPercent) {
    const float d = Density();
    int percent = defaultPercent;
    if (d > 1.0f) percent = static_cast<int>(static_cast<float>(defaultPercent) / d + 0.5f);
    // P9: the setting is in screen units; the game's default is in back-buffer units.
    return FirstRunPercentAt(percent, RenderPercent());
}

int RenderPercent() {
    return static_cast<int>(Window::RenderScale() * 100.0f + 0.5f);
}

float UiScaleRatio(int uiPercent) {
    return UiScaleRatioAt(uiPercent, RenderPercent());
}

void UiScaleSliderRange(float* minPercent, float* maxPercent) {
    const float d = Density();
    if (d <= 1.0f) return;
    *minPercent = static_cast<float>(static_cast<int>(*minPercent / d + 0.5f));
    *maxPercent = static_cast<float>(static_cast<int>(*maxPercent / d + 0.5f));
}

}  // namespace Display
}  // namespace Platform
