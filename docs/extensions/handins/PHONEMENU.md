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

* **Logo at the top, the buttons as rows of squares** (a screen wider than the overlay; owner's rulings
  2026-10-25: P8 round 2 replaced the first "logo left, buttons right" layout, round 3 made the row wrap
  to two rows). The overlay keeps retail's width fit but its top goes on the screen's top (`LogoTop`),
  so the logo is whole and centred at the top and the cut moves to the bottom border. The pause menu has
  the same backdrop and gets the same treatment. A menu's button list (MainMenu.rsc `buttonList`;
  Pause.rsc's root; `RibbonElement`s laid out top-to-bottom, `_vertical` 1) is reshaped on the menu
  INSTANCE (the description is not touched): the ribbon's `_vertical` cleared, every button's
  `_minWidth` / `_minHeight` set to one side, each label centred (alignment 4) with its icon above its
  text (`LabelElement::_mode` 3, `_align` 4) and no padding of its own. `PlanRows` picks, in order:
  1. one row in the labels' own font;
  2. two rows in the labels' own font: the first ceil(n / 2) squares, the rest centred under them (an
     even count splits in halves), so a button added to a menu wraps instead of breaking the layout
     (unit-tested for n = 8, 9, 10, 11);
  3. one row, then two rows, in `Font/FontSmall.rsc` (the game's own small font);
  4. else retail's column (retail's values restored).
  The small-font step stays (round 3 asked to pick): on the phone the logo takes the top quarter of the
  screen, so two own-font rows need more height than the S25 has at its smaller UI scales (at UI 35,
  two rows need 87 units down, 99 per square); without the step the pause menu would fall to the
  column there.
  The square side fills the safe width for the longer row, is capped at 0.22 of the safe height and by
  the band between the logo and the version line (`RowBand` / `BlockHeight`; the rows are centred in
  that band), and is never smaller than the labels need: the widest label ("Achievements") plus
  `kSquareInset` (5 units) on each side, so the text keeps 2 units of air inside the button frame (the
  owner saw it cross the frame in round 2). The buttons keep retail's art (the bordered button frames
  are nine-sliced, so they draw at any size).
  Two rows are made without new elements: the ribbon still lays its squares out left to right, but a
  first-row square's bottom padding reserves the second row's height and the ribbon's right padding
  takes the second row's width back, so the dialog (and the panel behind the squares) is sized for two
  rows; after retail's `ComputeLayout` the hook moves every square into its row
  (`UI::Element::AdjustUpperLeft`) and puts each button's frame back around its square (the seam's third
  call point, `kExtPlaceAfterLayout`, right after `DialogInstance::ComputeLayout` in
  `UserInterface::Update`).
  `standalone_error.log` gets one `[PhoneMenu] <w>x<h>: <n> row(s) of square buttons (...)` or
  `... column (...)` line per decision.
  Touch targets on the S25: at the owner's UI 35 (render scale 100, an 819 x 378 layout, 2.86 device
  pixels per unit) both menus use the small font, 78-unit squares = 223 device pixels (about 14 mm): the
  main menu one row of 8, the pause menu two rows of 5. At the seeded UI 100 / render scale 50 (1170 x
  540, 2 px per unit) both are one own-font row: main menu 108 units = 216 px (the 0.22 cap), pause menu
  104 units = 208 px. The column comes back on the S25 (render scale 100) below about UI 33 % for the
  pause menu and below about UI 31 % for the main menu: there even two rows of small-font squares
  (2 x 82 + 18 units) are taller than the band between the logo and the version line (126 units at
  UI 25), and the column then runs off the screen as retail's does at that scale.
* **Centred layout** (any other screen and any screen where the rows do not fit). Retail's cover fit;
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
  `Ext::PhoneMenu::Sync` at boot): one function pointer, three call points.
  * `kExtPlaceLayout` in `UserInterface::Update` (`userinterface_frame.cpp`): the rectangle each dialog
    is laid out in (`ComputeLayout(x, y, right, bottom)`), retail's `{0, 0, w, h}` unless the row moves
    a recorded menu dialog or puts another dialog in its safe area. With the pointer null the loop is retail's line.
  * `kExtPlaceAfterLayout` in the same loop, right after that `ComputeLayout` (P8 round 3): the menus'
    second row of squares is moved into place.
  * `kExtPlaceBackdrop` in `UserInterface::Draw` (`userinterface_draw.cpp`): the backdrop rectangle after
    retail's cover fit.
* `MainMenuDialog::OnOpened` (`mainmenudialog.cpp`): `Ext::PhoneMenu::MenuOpened` records the menu's and
  the version line's dialog descriptions, and whether the menu is the pause menu (`game` set).
* `OptionsDialog::OnOpened` (`optionsdialog.cpp`): `Ext::PhoneMenu::UiScaleSliderMinimum` after the
  platform hook.

The hooks read the row on every call (live). The menus' layout is decided from THIS frame's sizes: the
hook sizes the menu's tree and the version line's (`ComputeSize`, which retail's `ComputeLayout` runs
anyway) before retail lays the menu out, and the backdrop's place comes from the dialog's own picture.
P8 round 2 fixed the jump this replaced: the first logo-left code learned the logo's place from the
backdrop's DRAW, so a menu's first frame was laid out without it (the pause menu showed centred for one
frame, then moved right; the main menu likewise). Every other dialog's `SafeArea` reads its last laid-out
size only to decide whether it is larger than the safe area.

## 4. ext.sharp_ui below 75 %

At density 1, `Ext::SharpUI::PickFontHeight` drew the base glyphs one texel per device pixel whenever
`PickVariantHeight` answered the base font. Below 75 % (a ratio above 4/3, reachable only with this row)
that answer means "outside the compiled variants", and the text came out at its 1x size, spaced by the
magnified advances (seen at 25 %). It now keeps retail's scaled quads there, as the Retina path already
did (`kRetailRatioMax`); inside 75 .. 150 % the selection is unchanged (the existing unit test sweeps it).

## 5. Verified (Windows, debug, functional game dir)

P8 round 3 (2026-10-26; screenshots in the session scratchpad `android-ui-fit/round3/`):
* S25, the owner's UI 35 at render scale 100: the main menu one row of 8 small-font squares with
  "Achievements" inside its frame (`r3_p_menu35.png`); the pause menu two rows of 5
  (`r3_p_pause35.png`); a tap on a second-row square (Extensions) opens its dialog
  (`r3_p_pause35_ext.png`). At UI 25 both menus are the column (`r3_p_pause25.png`, the log's
  `585x270: column` lines).
* Windows, 2528 x 1345 window at UI 32 (an 808 x 430 layout): the main menu two rows of 4 and the pause
  menu two rows of 5, own font, 99-unit squares (`r3_win_menu32.png`, `r3_win_pause32.png`; the
  hovered square shows its highlight and tooltip).

P8 round 2 (2026-10-25; screenshots in the session scratchpad `android-ui-fit/round2/`):
* S25, owner's settings (UI 35, render scale 100): the main menu as a row of 8 squares under the whole
  logo, the version line at the bottom (`p2_menu3.png`); the pause menu as a row of 10 small-font
  squares (`p2_pause3.png`); Options opened from both rows (`p2_options.png`, `p2_pause_options.png`).
  The first round-2 build (a row only when the labels fit in their own font, the pause menu left a
  column) fell back to the column at UI 35, which ran over the logo and under the version line
  (`p2_menu.png`, `p2_pause.png`): what the owner's sketch was about.
* Windows, 2528 x 1345 window (1.88:1), row on: UI 83 a row of 8 (`win_row.png`), UI 35 a row of 8 in
  the small font (`win_row35.png`), the pause menu a row of 10; row off: retail's centred column and
  cover fit (`win_off.png`). First-frame check: the menu's and the pause menu's laid-out rectangle is
  the same from their first frame on (a temporary per-frame print, removed).
* The mods' restart notice at UI 50 follows the UI scale under `fix.ui_scale_layout`
  (`win_modal_fix.png`; `docs/retail_bugs.md` section 5).

Earlier (the first logo-left pass):

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
