#pragma once
// ============================================================================
// src/platform/android/android_paths.h -- the Android data folder (NOT RETAIL)
// ============================================================================
// Only src/platform/android/*.cpp include this. The app-specific external folder,
// SDL_GetAndroidExternalStoragePath() (`/sdcard/Android/data/<package>/files`), is the
// game directory on Android: WinData/ is pushed there (docs/android/README.md), the logs
// and the boot state are written there, and the per-user folder is its Documents/
// subfolder (Documents/Banished: settings.cfg, Save/, Cache/), as PLAN.md §1 proposes.
// No permission is needed for it, it is readable over adb and MTP, and it goes with an
// uninstall.
//
// DataDir() is asked by the twins that would read /proc/self/exe or the password
// database on Linux (file_android.cpp's ExecutablePath, settings_android.cpp's
// DocumentsDir). It answers from the first call on (EnterRunDirectory makes that call
// before the logs open); empty when SDL could not answer, in which case the twins fail
// as their Linux twins do when /proc or the password database cannot answer.
// ============================================================================

#include <string>

namespace Platform {
namespace Android {

// The data folder, no trailing '/'. Allocator-free (std::string), like the settings seam.
const std::string& DataDir();

}  // namespace Android
}  // namespace Platform
