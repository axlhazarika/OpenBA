# PHONEMENU hand-in

`ext.phone_menu`: the front end fitted to a phone held in landscape (the Galaxy S25: 2340x1080,
19.5:9, rounded corners, a camera cut-out on one short edge). One class A row, **off by default**,
switched on by the Android first-run `settings.cfg` (`src/platform/android/app_android.cpp`), live,
no serialized state. Android fit P7.

## 1. What retail does on a 19.5:9 screen

The main menu is a 3D scene (`Game/MenuScene/Scene%i.rsc`, a season picked per start) under the menu
dialog. The dialog's backdrop (`Dialog/MainMenu.rsc` `_backgroundSpriteSheet`) is a 16:9 overlay: the
Banished logo near the top and the dark torn borders, transparent between them. `UserInterface::Draw`
(retail 0x180121dbe) cover-fits a backdrop and centres it: on a screen wider than 16:9 it scales the
overlay to the screen width and cuts the same amount off its top and bottom. At 2340x1080 that is 118
pixels each side, and the top of the logo is cut off. The button column (`buttonList`, MidCenter) and
the version line (`Dialog/BuildId.rsc`, bottom centre) are centred on the screen.

## 2. What the row does

Measured on the installed overlay: the logo spans 0.330 .. 0.675 of the drawn width (centred on the
overlay's own centre line, within 0.3 %) and 0.039 .. 0.210 of its height.

* **Logo-left layout** (a screen wider than the overlay). The overlay is drawn 1.28 screen widths wide,
  its right edge on the screen's right edge and its top on the screen's top, so it still covers the
  screen, the logo is whole and centred on 36 % of the width, and the space right of it is free. The
  button column is centred between the logo's right edge (0.685 of the overlay, one percent of air) and
  the safe area's right edge; when it does not fit there (a very large UI) it is right-aligned on the
  safe margin. The version line is centred under the logo. Owner's ruling 2026-10-25: moving the menu
  layout is fine; logo left, buttons right.
* **Centred layout** (any other screen, and the pause menu, which has no backdrop). Retail's cover fit;
  the menu and the version line are laid out inside the safe area; the column is centred between the
  logo's bottom (0.22 of the overlay) and the version line when it fits there, else above the version
  line, and the version line moves to the bottom-left corner when the column cannot sit above it.
* **Safe area**: 4.5 % of the screen height in from every edge. The S25's corner radius is about 3.5 %
  of the 1080-pixel height and the cut-out about 3 % deep; 4.5 % clears both with a little air and
  moves nothing centred on a desktop.
* **Every other dialog** (P8, 2026-10-25: the owner saw the in-game toolbar under the S25's corner). What
  retail anchors to an edge sits inside the same safe area. `DialogInstance::ComputeLayout` (0x180113d90)
  puts a dialog at `_upperLeftX + x` (left / top alignment), against `right` less its width (right /
  bottom), or centred between them, and pulls one that would cross `right` back. The row hands it
  `{max(0, m - upperLeftX), max(0, m - upperLeftY), w - m, h - m}` (`phone_math.h` `SafeArea`): the far
  edges are the safe area's (the toolbar and the event notices, bottom-right, move in by the margin;
  centred dialogs do not move), the near edges are a floor (a panel at its default (0, 0) opens at the
  safe corner; a tooltip at the cursor, a combo box's drop-down or a dragged panel stays where retail puts
  it unless that point is inside the margin). An axis on which the dialog (last frame's size) is larger
  than the safe area keeps retail's extent, and a root sized to the back buffer (`ComputeLayout`'s 0x10 /
  0x20 flags) keeps retail's rectangle. In-game dialogs seen on a new game: `Toolbar.rsc` (bottom-right),
  `EventNotify.rsc` (bottom-right), `AutoSave.rsc` (top-left); front end: `ShiningRock.rsc`,
  `Loading.rsc`, `NewGame.rsc`. Known effect: a panel dragged out of the corner moves once the cursor
  has passed the margin (`docs/known_port_issues.md` row 1336).
* **UI scale slider**: Options > User interface scale goes down to 25 % instead of 75 %.
  `_uiScale = percent / 100` is layout units per device pixel, so 25 % draws the UI four times larger
  than 100 %. The platform hook `Platform::Display::UiScaleSliderRange` stays what it is (a density
  compensation that keeps retail's physical range on a high-density window; the identity on Android,
  whose window is not created high-density): a wider range is a feature retail lacks, so it is this
  row (DESIGN.md §2), on for the phone by its first-run settings and available to a desktop player
  with a small high-resolution screen.

## 3. The seams

* `UI::UserInterface::_extDialogPlace` (engine, null by default and under the faithful profile, set by
  `Ext::PhoneMenu::Sync` at boot): one function pointer, two call points.
  * `kExtPlaceLayout` in `UserInterface::Update` (`userinterface_frame.cpp`): the rectangle each dialog
    is laid out in (`ComputeLayout(x, y, right, bottom)`), retail's `{0, 0, w, h}` unless the row moves
    a recorded menu dialog or puts another dialog in its safe area. With the pointer null the loop is retail's line.
  * `kExtPlaceBackdrop` in `UserInterface::Draw` (`userinterface_draw.cpp`): the backdrop rectangle after
    retail's cover fit.
* `MainMenuDialog::OnOpened` (`mainmenudialog.cpp`): `Ext::PhoneMenu::MenuOpened` records the menu's and
  the version line's dialog descriptions; only those dialogs are ever moved.
* `OptionsDialog::OnOpened` (`optionsdialog.cpp`): `Ext::PhoneMenu::UiScaleSliderMinimum` after the
  platform hook.

The hooks read the row on every call (live) and decide from last frame's laid-out sizes, so a resize or
a scale change settles one frame later.

## 4. ext.sharp_ui below 75 %

At density 1, `Ext::SharpUI::PickFontHeight` drew the base glyphs one texel per device pixel whenever
`PickVariantHeight` answered the base font. Below 75 % (a ratio above 4/3, reachable only with this row)
that answer means "outside the compiled variants", and the text came out at its 1x size, spaced by the
magnified advances (seen at 25 %). It now keeps retail's scaled quads there, as the Retina path already
did (`kRetailRatioMax`); inside 75 .. 150 % the selection is unchanged (the existing unit test sweeps it).

## 5. Verified (Windows, debug, functional game dir)

A 3080x1421 window (2.167:1, the S25's 2340x1080 aspect; this desktop's window manager keeps the
window at the work area's height, so the aspect was reproduced at that height):
* row off: the logo's top cut off, retail's layout;
* row on, UI 75 %: logo whole on the left, column right of it, version line under the logo;
* row on, UI 25 %: the same, nothing overlapping; Options opens with the slider at 25 and the text
  drawn by retail's quads (sharp_ui on).

Not verified: the phone itself (reserved by another agent during this work), and the Options dialog at
25 % is taller than a 1080-pixel screen (its tabs go off the top; Cancel / Apply / Ok stay on screen,
retail's ComputeLayout keeps a too-tall dialog's bottom on screen).

Seen on the phone itself in P8 (2026-10-25, Galaxy S25, debug apk, render scale 50, UI 100 and 83;
`docs/android/P8_UI.md`): the logo-left menu, the toolbar clear of the bottom-right corner, a sub-toolbar
right-aligned on the safe edge, the Town panel opened at the safe top-left corner, tooltips at the cursor.

## 6. Where

| file | change |
|---|---|
| `src/ext/phone_menu/phone_math.{h,cpp}` | the pure decisions (unit-tested) |
| `src/ext/phone_menu/phone_menu.{h,cpp}` | the hook, the recorded dialogs, the slider minimum |
| `src/engine/ui/UserInterface.h`, `userinterface_frame.cpp`, `userinterface_draw.cpp` | the seam |
| `src/game/Dialog/mainmenudialog.cpp`, `optionsdialog.cpp` | the two game seams |
| `src/ext/sharp_ui/sharp_math.{h,cpp}` | §4 |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | the row (class A, default off), `Sync` |
| `src/platform/android/app_android.cpp` | `ext.phone_menu=1` in the first-run settings |
| `tests/unit/ext_phone_menu_test.cpp`, `ext_registry_test.cpp`, `ext_sharp_ui_test.cpp`, `sources.cmake` | tests |
