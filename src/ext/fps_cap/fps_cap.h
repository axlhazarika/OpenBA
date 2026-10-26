#pragma once
// ============================================================================
// src/ext/fps_cap/fps_cap.h -- ext.fps_cap (class A, live, default OFF)
// ============================================================================
// NOT RETAIL (docs/android/P8_INPUT_OPTIONS.md). A frame-rate ceiling for the menu and the game:
// with the row on, src/app/frame_limiter.h paces both loops to ext.fps_cap_rate (30 / 60 / 90 / 120).
// Retail's game loop is uncapped (its menu only gets the limiter's spin guard). The sim is not
// touched: MainLoop::Frame's fixed-step accumulator runs every step the elapsed time owes, so a
// capped run draws fewer frames of the same simulation. Class A: nothing is saved, live.
// OFF by default on a desktop; the Android first run turns it on at 60 (ruled 2026-10-25,
// src/platform/android/app_android.cpp). Under the faithful profile the row is off: no cap.
// The Options dialog's Enhanced tab sets it (enhanced_entry.cpp, Ext::SetFpsCapAndRecord).
// BANISHED_FPS_CAP (a harness flag) still overrides it.

namespace Ext {
namespace FpsCap {

// The cap the frame limiter uses now: ext.fps_cap_rate while the row is on, else none. Boot
// (registry_boot.cpp, after InitFromSettings resolved the rows) and every toggle (SetAndRecord,
// SetValueAndRecord). Main thread.
void Sync();

}  // namespace FpsCap
}  // namespace Ext
