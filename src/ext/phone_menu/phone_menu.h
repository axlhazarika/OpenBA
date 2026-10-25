#pragma once
// ============================================================================
// src/ext/phone_menu/phone_menu.h -- ext.phone_menu (class A, live)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/PHONEMENU.md). The front end fitted to a phone held in
// landscape (19.5:9, rounded corners, a camera cut-out), and usable on any screen:
//   (a) on a screen wider than the main menu's backdrop overlay (16:9: the Banished logo and
//       the dark torn borders over the 3D menu scene; retail cover-fits it and cuts the logo's
//       top off at 19.5:9) the menu becomes a logo-left layout: the overlay drawn larger
//       against the right and top edges so the logo is whole, centred on 36 % of the width,
//       the button column right of the logo and the version line under it
//       (phone_math.h LogoLeft / ColumnBeside / VersionUnderLogo);
//   (b) on any other screen the picture is retail's and the menu dialogs (the main / pause
//       menu and the version line) are laid out inside a safe area 4.5 % of the screen height
//       in from every edge, the column centred between the logo and the version line when it
//       fits there (MenuBand), the version line moved to the bottom-left when the column
//       cannot sit above it (VersionAside); the logo-left layout uses the same safe area;
//   (c) the Options UI scale slider goes down to 25 % (the UI four times larger than 100 %);
//   (d) every other dialog (the in-game toolbar and event notices bottom-right, the autosave
//       notice, the panels, tooltips and drop-downs, the front end's other dialogs) is kept
//       inside the same safe area: an edge or corner anchor moves in by the margin, a dialog
//       put at a screen point stays there unless the point is inside the margin, a centred
//       dialog does not move (phone_math.h SafeArea).
// Default OFF on a desktop; the Android first run switches it on (src/platform/android/
// app_android.cpp). Live: every hook reads the row each time. Under the faithful profile the
// engine seam stays null.

#include <cstdint>

namespace UI {
class CheckElement;
class ComboElement;
class Dialog;
}

namespace Ext {
namespace PhoneMenu {

// Boot only (registry_boot.cpp): installs UI::UserInterface::_extDialogPlace (null under the
// faithful profile).
void Sync();

// MainMenuDialog::OnOpened (the main menu and the pause menu): the dialogs the safe area and the
// backdrop move apply to, by their description: the menu's own and the version line's
// (Dialog/BuildId.rsc). Either may be null.
void MenuOpened(const UI::Dialog* menu, const UI::Dialog* buildId);

// OptionsDialog::OnOpened, after Platform::Display::UiScaleSliderRange: the slider's minimum
// (percent), lowered to 25 while the row is on; `platformMinPercent` unchanged otherwise.
float UiScaleSliderMinimum(float platformMinPercent);

// P8, retail's Video rows on a fixed surface (video_rows.cpp, docs/android/P8_INPUT_OPTIONS.md
// round 2). True while the row is on and Platform::Window::FixedDisplaySurface (Android): the
// surface is the screen at its own size and refresh, so retail's Resolution, Refresh rate and
// Fullscreen rows change nothing there, and VSync is kept on (the swap chain presents FIFO at the
// panel's rate; the frame rate below it is the Enhanced tab's Frame rate cap, ext.fps_cap, alone).
bool FixedVideoRows();

// Boot (registry_boot.cpp, after Sync): with FixedVideoRows, a VSync=false left in settings.cfg is
// turned on (Video::Configuration::vsync, then Video::Device::settingsChanged rebuilds the swap chain
// at the first frame), so a MAILBOX swap chain never renders frames the panel never shows.
void KeepVSyncOnFixedSurface();

// OptionsDialog::OnOpened, after retail filled the Video tab: with FixedVideoRows, the Resolution and
// Refresh rate combos and the Fullscreen and VSync checks are disabled, VSync shown ticked (Apply then
// reads values that change nothing, VSync on). Any argument may be null. Retail's rows elsewhere.
void FitVideoRows(UI::ComboElement* resolution, UI::ComboElement* refresh, UI::CheckElement* fullscreen,
                  UI::CheckElement* vsync);

}  // namespace PhoneMenu
}  // namespace Ext
