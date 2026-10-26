# Android P8: UI fit and the font rasteriser (2026-10-25)

Session log of the P8 UI items (`docs/runtime_port/NEXT_SESSION.md` §2.7): the in-game buttons under the
S25's edge mask, ext.phone_menu seen on the phone itself, and the font rasteriser on Android (ruled ADD).
Facts that outlive the session are in `docs/android/README.md` and `docs/extensions/handins/PHONEMENU.md`.

## 1. In-game buttons under the edge mask: DONE

**What was wrong.** ext.phone_menu moved only the menus it records (the main / pause menu and the version
line). Every other dialog was laid out in retail's `{0, 0, w, h}`, so the toolbar (`Dialog/Toolbar.rsc`,
bottom-right) and the event notices (`EventNotify.rsc`, bottom-right) sat in the S25's rounded corner, and a
panel opened at its default upper-left (0, 0) sat in the opposite one.

**How retail places a dialog.** `DialogInstance::ComputeLayout` (0x180113d90, `src/engine/ui/dialog.cpp`):
`posX = _upperLeftX + x` (left alignment), `right - width` (right), or centred between them; one that would
cross `right` is pulled back to it. Dialogs put at a screen point use `SetUpperLeft`: the tooltip at the
cursor (`userinterface_hover.cpp`), a combo box's drop-down under its box (`combodialog.cpp`), panels at
their saved / dragged position (`toolbardialog.cpp`, `toolui.cpp`, `entityui.cpp`), the tool bubbles
(`edittool.cpp`, `tool_base_createbar.cpp`). A plain inset of `x` would have shifted all of those by the
margin.

**The change** (same row, same switch, same 4.5 % margin; `src/ext/phone_menu/`). Every dialog that is not
a recorded menu is laid out in `phone_math.h` `SafeArea`: `{max(0, m - upperLeftX), max(0, m - upperLeftY),
w - m, h - m}`. Far edges are the safe area's, near edges are a floor on the upper-left; an axis on which
the dialog is larger than the safe area keeps retail's extent; a root sized to the back buffer (flags
0x10 / 0x20) keeps retail's rectangle. Row off or `/profile:faithful`: the seam is null or returns retail's
rectangle untouched. Unit tests: `tests/unit/ext_phone_menu_test.cpp` (three new cases). The row's
description in the Extensions dialog now names the game's interface too.

**Known effect** (`docs/known_port_issues.md` row 1336): a panel dragged out of the corner starts moving
once the cursor has passed the margin; retail has the same dead zone at its right and bottom edges.

## 2. ext.phone_menu on the phone: VERIFIED

Galaxy S25 (2340x1080), this branch's debug apk, the phone's seeded settings (`ext.phone_menu=1`,
`VideoRenderScale=50`, `GameEdgeScroll=false`, UI 100). `adb exec-out screencap`:

| Look | Seen |
|---|---|
| Main menu | Logo-left layout: the logo whole on the left, the button column right of it inside the safe area, the version line under the logo. Nothing to fix. |
| In game, UI 100 | Toolbar clear of the bottom-right corner (about 49 device pixels in from the right and the bottom). |
| In game, UI 83 | The same, smaller. |
| Tools sub-toolbar | Right-aligned on the safe edge above the toolbar. |
| Town panel | Opened at the safe top-left corner (its default upper-left). |
| Tooltips | At the touch point, drawn with the rasterised font at UI 83. |

Screenshots (not in the tree): the session scratchpad's `android-ui-fit/` folder
(`phone_menu.png`, `phone_game.png`, `phone_game83.png`, `phone_panel3.png`, `phone_panel4.png`).

Windows (DX11 debug, functional dir, 2528x1345 window, UI 83, row on): the toolbar inset from the
bottom-right corner (`win_on_br.png` in the same folder); unit 1480 tests, 0 failures, 1 skipped (the kit
cross-check, as on main).

## 3. Font rasteriser on Android: DONE

**What the other platforms do.** `ext.sharp_ui`'s font variants come from `src/ext/resources/font_provider.cpp`,
which calls `FaceAvailable` / `RasterizeFont` (`font_raster.h`). Windows: GDI over the installed face or the
TTF beside the exe (`font_raster_win.cpp`). Linux / macOS: FreeType (`font_raster_freetype.cpp`) over
`<face>.ttf` found beside the exe, then in the user's and the system's font folders (`font_raster_posix.cpp`,
`font_raster_darwin.cpp`). The face is FrancophilSans.

**What Android lacked.** It compiled the posix twin: the exe directory is `/system/bin` (app_process64), and
neither a home font folder nor `/system/fonts` holds FrancophilSans, so the face was always absent.

**The change.** `src/platform/android/font_raster_android.cpp` replaces the posix twin (the darwin rule):
the face is `<data>/FrancophilSans.ttf`, the data folder being the game directory (`android_paths.h`).
`tools/android.sh push-data` pushes the three files the desktop packages ship.

**Which licence case applies.** The font is the one the repository already ships for the desktop packages:
`tools/release/fonts/FrancophilSans.ttf` and `-Bold.ttf`, Manfred Klein's, unchanged, with his notice
(`FrancophilSans_README.txt`; `THIRD_PARTY_NOTICES.md` "Not third-party code in the tree"). Nothing new
enters the tree. The apk carries no font (the ruling: the TTF from the data folder); bundling it in a
release apk would be allowed on the same terms as the desktop packages, notice included, and is left for
a ruling with the release-apk work.

**Verified on the S25** with the files pushed: UI 100 draws the base fonts natively (no rasterising, as on a
desktop at 100); UI 83 logs `[ExtRes] font Font at 29 px: 96 glyphs rasterised in 4.7 ms`, the same for
FontSmall 17 / FontMedium 22 / FontLarge 58 px, and `[SharpUI] on scale=0.8300 ... Font->29 FontSmall->17
FontMedium->22 FontLarge->58`. The panel and tooltip text read clean.

## 4. What the phone was left with

This branch's debug apk installed and stopped; `settings.cfg` as found (UI 100; the UI 83 run was
temporary); the three FrancophilSans files in `files/` (they are the point of item 3).

## 5. Left

- Render scale 50 halves the back buffer, so sharp_ui rasterises at back-buffer pixels and the present
  pass scales them up; text is not as crisp as the screen allows. Out of scope here (render scale is
  another P8 item).
- A release apk with the font inside (and the extraction to `files/`) is a ruling, not done.
- Dialogs taller than the safe area keep retail's vertical extent (the Options dialog at 25 %).

## 6. Round 2 (the owner's own S25 test of the merged tree, PR #53)

The owner's phone settings at the time: UI 35, render scale 100 (an 819 x 378 layout, 2.86 device
pixels per layout unit). Screenshots: the session scratchpad's `android-ui-fit/round2/`.

**Pause menu jump: FIXED.** Esc showed the pause menu centred for one frame, then moved right. Cause:
`Pause.rsc` has the same 16:9 backdrop as the main menu, and the first layout code learned the logo's
place from the backdrop's DRAW (`kExtPlaceBackdrop`), after the frame's layout had run: the first frame
was laid out without it, the second moved the column beside the logo. The main menu had the same
one-frame jump. The layout hook now decides from this frame's sizes only: it sizes the menu's tree and
the version line's itself (`ComputeSize`, which retail's `ComputeLayout` runs anyway) and takes the
backdrop's place from the dialog's own picture. Checked on Windows with a temporary per-frame print:
the main and pause menus' rectangles are the same from their first frame on.

**The "changes to mods require a restart" notice not scaled: FIXED (retail bug, under
`fix.ui_scale_layout`).** It is `Dialog/Reload.rsc`, shown modally through `UserInterface::ShowDialog`
(0x18011fd10), which builds a nested UserInterface whose `_uiScale` is its ctor's 1.0: every modal shown
that way ignores the Options UI scale (invisible at retail's 100). The same path shows the Extensions
restart notice, the save / load warnings and the Options key-capture confirmation; all are fixed by the
one line (the nested UI copies the scale; `docs/retail_bugs.md` section 5). The modal also lands in the
safe area (the layout seam is static, the nested UI uses it). Seen on Windows at UI 50 (the notice
the size of the Mods dialog under it); not reproduced on the phone (no mod is installed there).

**Main menu relayout: DONE.** The owner's sketch: the logo centred at the top, the buttons as one
horizontal row of square buttons across the middle, the version line centred at the bottom. Both the
main menu and the pause menu, on a screen wider than their 16:9 backdrop; the logo-left layout is
retired (its math and tests removed). Details and numbers: `docs/extensions/handins/PHONEMENU.md` §2.
Retail button art (nine-sliced frames) at any size, the label's icon above its text; the game's small
font when the labels' own does not fit. Touch targets on the S25 at the owner's UI 35: main menu 254 x
254 device pixels (about 15 mm), pause menu 203 x 203 (about 12 mm). No new art, setting or flag.

Verified: on the S25 the main menu row (8), the pause menu row (10, small font), Options opened from
each (the final build, `p2_pause4.png`, logged `819x378: button row (... square 89)` and `(... square
71, small font)`); on Windows the row at UI 83 and 35, the pause row, the ext off = retail's centred column, unit
1485 / 0 / 1 skipped (the kit cross-check). The phone was left with this branch's debug apk, stopped,
and its settings as found.

Left / for a ruling:
- Below about UI 33 % (render scale 100) the pause menu's ten labels do not fit across even in the small
  font, and below about 27 % the main menu's eight; both fall back to the column, which then overlaps
  the logo. Options if wanted: two rows of squares, or icon-only squares at the smallest scales.
- The Options dialog is taller than the screen at UI 35: its tabs are off the top (retail keeps a
  too-tall dialog's bottom on screen). A phone layout for Options is its own item.
- The "Achievements" label in the small font leaves about one unit to its square's frame.

## 7. Round 3 (owner ruling: the row wraps to two rows)

**Ruled:** when one row of squares does not fit, wrap to two rows (ceil(n / 2) on the first, the rest
centred under it) instead of falling back to the retail column, so an added menu button never breaks
the layout; the column only where even two rows cannot fit. Owner's look at round 2, same session:
"Achievements" crossed its square's frame slightly.

**Done** (`src/ext/phone_menu/`, details in `docs/extensions/handins/PHONEMENU.md` §2):
- `PlanRows`: one row, two rows, then both in the game's small font, else the column. The small-font
  step stays: the logo takes the top quarter of the phone's screen, and at UI 35 two own-font rows need
  more height than the band between the logo and the version line has (without the step the pause menu
  would fall to the column at the owner's scale). Unit tests for n = 8, 9, 10, 11 at the owner's UI 35,
  on a wide window where both menus wrap, at the phone's UI 25 (the column) and the edge cases.
- Two rows from padding plus a move after retail's `ComputeLayout` (a third call point on the existing
  `_extDialogPlace` seam, `kExtPlaceAfterLayout`); no element is created or reparented.
- The rows are centred in the band between the logo and the version line (round 2 centred the row in
  the safe area; the band leaves more room for two rows).
- Each square keeps 5 units between its label and its edge (`kSquareInset`): "Achievements" stays
  inside its frame. At the owner's UI 35 this moves both menus to the small font (the own font with the
  margin needs 99 units, 90 fit across): the main menu is still one row of 8, the pause menu becomes
  two rows of 5, all squares 78 units = 223 device pixels (about 14 mm).

**Verified:** the S25 at UI 35 (main menu one row, pause menu two rows, a second-row square opens its
dialog), at UI 25 (both menus the column); Windows at UI 32 (both menus two rows in their own font);
unit 1490 / 0 / 1 skipped. Screenshots: the session scratchpad's `android-ui-fit/round3/`. The phone was
left with this branch's debug apk, stopped, and the owner's settings (UI 35).

**Threshold:** on the S25 at render scale 100 the column comes back below about UI 33 % (pause menu)
and UI 31 % (main menu): two rows of small-font squares no longer fit between the logo and the version
line, and the column runs off the screen as retail's does at that scale. A third row or a smaller logo
would be the next step if those scales matter (a ruling).
