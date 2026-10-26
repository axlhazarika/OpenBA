#pragma once
// ============================================================================
// src/platform/common/ui_render_scale.h -- the UI scale measured on the screen, not the back buffer
// ============================================================================
// NOT RETAIL (P9, docs/android/P9_LOADING_SCALE.md §2). Pure: no platform call, unit-tested in
// tests/unit/ui_render_scale_test.cpp.
//
// Retail's UI scale (GameSettings::uiScale, "GameUIScale", percent) becomes
// UserInterface::_uiScale = percent / 100, LAYOUT UNITS PER BACK-BUFFER PIXEL. On a platform whose
// render scale (Platform::Window::RenderScale, `VideoRenderScale`, Android only) makes the back
// buffer smaller than the screen, a back-buffer pixel is 100 / renderPercent screen pixels, so the
// same percent drew the interface twice as large at Half and four times at Quarter.
//
// The rule: the setting is the UI scale AT FULL RENDER SCALE, i.e. layout units per SCREEN pixel.
//   * stored    GameUIScale = the percent the slider shows; a render scale change never rewrites it;
//   * applied   _uiScale = uiPercent / renderPercent (layout units per back-buffer pixel), so the
//               layout space (back buffer * _uiScale) is screen * uiPercent / 100 at every render
//               scale and the interface keeps its size on the screen;
//   * the slider's range (retail 75..150, the density rule, ext.phone_menu's floor) is in the same
//               screen units, so it does not move with the render scale; the phone floor is
//               computed on the screen's size (ScreenExtent), 35 on the S25 at every render scale;
//   * a first run (no GameUIScale saved) gets retail's 100 back-buffer percent expressed in screen
//               units, defaultPercent * renderPercent / 100: the phone's seeded Half starts at 50,
//               the look a fresh phone had before P9.
// At renderPercent 100 (every desktop) all three are the identity: _uiScale = uiPercent / 100.0f,
// the very float retail computes.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Display {

// A render scale as the integer percent Options offers (100 / 50 / 25); anything outside 1..100
// is treated as 100 (no scaling).
inline int32_t SaneRenderPercent(int32_t renderPercent) {
    return (renderPercent >= 1 && renderPercent <= 100) ? renderPercent : 100;
}

// UserInterface::_uiScale for the setting `uiPercent` on a back buffer at `renderPercent` of the
// screen. 100 -> uiPercent / 100.0f exactly (retail's division).
inline float UiScaleRatioAt(int32_t uiPercent, int32_t renderPercent) {
    const int32_t r = SaneRenderPercent(renderPercent);
    if (r == 100) return static_cast<float>(uiPercent) / 100.0f;
    return static_cast<float>(uiPercent) / static_cast<float>(r);
}

// The setting (percent, screen units) an applied _uiScale stands for at `renderPercent`, rounded:
// the inverse of UiScaleRatioAt. The Render scale row re-derives _uiScale with it when the render
// scale changes (UiScaleRatioAt(UiPercentFromRatio(ui, before), after)).
inline int32_t UiPercentFromRatio(float uiScale, int32_t renderPercent) {
    const float p = uiScale * static_cast<float>(SaneRenderPercent(renderPercent));
    return static_cast<int32_t>(p + 0.5f);
}

// The screen extent (pixels) a back-buffer extent stands for at `renderPercent`, rounded to the
// nearest pixel (the back buffer is the window's size times the scale, truncated).
inline int32_t ScreenExtent(int32_t backBufferExtent, int32_t renderPercent) {
    const int32_t r = SaneRenderPercent(renderPercent);
    if (r == 100) return backBufferExtent;
    return static_cast<int32_t>((static_cast<int64_t>(backBufferExtent) * 100 + r / 2) / r);
}

// A first run's UI scale (screen units) for the game's default `defaultPercent` (back-buffer units,
// retail's 100) at `renderPercent`, rounded to the nearest percent; never below 1.
inline int32_t FirstRunPercentAt(int32_t defaultPercent, int32_t renderPercent) {
    const int32_t r = SaneRenderPercent(renderPercent);
    if (r == 100) return defaultPercent;
    const int32_t p = static_cast<int32_t>((static_cast<int64_t>(defaultPercent) * r + 50) / 100);
    return p >= 1 ? p : 1;
}

}  // namespace Display
}  // namespace Platform
