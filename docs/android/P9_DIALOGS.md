# Android P9: the game's dialogs for a finger (2026-10-26)

Session log of four P9 items (`docs/runtime_port/NEXT_SESSION.md` §2.8), all under `ext.phone_menu`
(same row, same switch, same seam as P7 / P8). Facts that outlive the session are in
`docs/extensions/handins/PHONEMENU.md` §2 / §3 and `docs/android/README.md`. Code:
`src/ext/phone_menu/touch_fit.{h,cpp}`, `touch_math.{h,cpp}`, a few lines in `phone_menu.cpp`, one new
call point on the seam in `src/engine/ui/userinterface_draw.cpp` / `UserInterface.h`.

How the look was reproduced on Windows: the functional game dir, the row on, UI 40, render scale Full, the
window's client sized to 2340 x 1080 after boot (a desktop window manager resizes it back at creation, so
the run script sets the size with `SetWindowPos` once the game is up): the S25's 936 x 432 layout exactly.

## 1. Scroll bars about 4x wider: DONE

**How retail sizes them.** `UI::ScrollElement::ComputeSize` (0x18011ad80) makes a bar as thick as its
track sprite: 12 units for the game's `SharedElements.rsc:verticalScrollbar` (dumped from the running
Options dialog: `scroll v1 track 12x7 thumb 12x7 inset 2`). A scrolling list is a
`UI::ContainerElement` (ComputeSize 0x1800cc850): clip region + vertical bar + optional bottom bar; the
bar's gutter is its padded width raised to its `_minWidth`, the clip region gets what is left of the
container's requested width (Options' pages: 405 = 389 + 4 + 12). The bar fills the gutter only with
its stretch flag (`ComputeLayout` 0x18011af20); its thumb and hit test follow its laid-out rectangle
(`GetBarLayout` 0x18011a2d0). No scroll element lives anywhere but in the .rsc layouts, so the change is
made on the instances at run time; nothing of retail's data is copied.

**The change.** Each frame (once per UserInterface, at its first dialog's layout call) the open dialogs'
trees are walked; a scroll bar gets `_minWidth` (a bottom bar `_minHeight`) = 4 x its sprite and the
stretch flag, and its container's requested width grows by what the bar gained, so the list's rows keep
their width (the Mods list's rows are `_minWidth = 580`, exactly the clip). Row off: the values come back
from the element's own description (`Element::_description`).

**Verified.** Windows: Options (`opt_after0.png`), Fixes and Extensions (`ext_after0.png`, the rows the
same width as with the row off, `ext_rowoff0.png`), Mods (`mods_after0.png`), the Storage Barn panel
(`panel_after30.png`). S25: Options (`ph_options.png`), the Storage Barn panel (`ph_panel.png`): a 48-unit
bar, 120 device pixels (about 7 mm) at UI 40.

## 2. Close [x] buttons larger: DONE

**How retail sizes them.** `StandardDialog.rsc`'s title bar holds `buttonClose`: a 20 x 20 ButtonElement
around a 16 x 16 `imageClose` ImageElement padded 2. An ImageElement is drawn over its `_width` x
`_height` when set (`ImageElement::ComputeSize` 0x180118650; `ImageDescription` +0x78 / +0x7c, -1 = the
sprite's size), and the button is sized by its content.

**The change.** The same walk: an element named `buttonClose` gets its icon's `_width` / `_height` set to
twice the sprite (32); the button (36), its frame and the title bar grow around it. Twice, a whole
multiple, so the cross's pixels stay even (the same rule as the menu icons in P8 round 4). Row off: the
description's -1 back.

**Verified.** Windows `panel_after30.png` (before: `panel_before40.png`), S25 `ph_panel.png`.

## 3. Options / Fixes and Extensions cut off at the top: DONE (the dialog scaled down to fit)

**What was wrong.** At UI 40 render scale Full the S25's layout is 936 x 432; the safe area is 394 high.
The Options dialog is 437 x 427 (dumped), Fixes and Extensions taller. Retail's
`DialogInstance::ComputeLayout` (0x180113d90) keeps a too-tall dialog's bottom on screen, so the tab row
went off the top (`opt_before0.png`; with the row off, `ext_rowoff0.png`).

**The two options, and the choice.** (a) Cap the dialog to the safe height by shortening its scrolling
lists. The Options tab element sizes itself to its TALLEST page (`TabElement::ComputeSize` 0x1800d9ea0,
max over all pages, shown or not), and the pages are: Game, Input, Enhanced = scrolling containers 318
high; Audio = a 120-unit table; **Video = a plain ribbon of two tables, 327 high, no scroll** (dumped
`graphicsOptions` 413 x 327). Shortening the lists cannot move the dialog below Video's 327 units, and
Video cannot scroll without building new elements into retail's layout. (b) Scale the dialog down. The
engine has one scale per UserInterface (`_uiScale`, layout units per device pixel: Update's layout size,
Draw's sprite scale, ext.sharp_ui's font choice all read it, and the Options slider writes it), no
per-dialog scale. **Chosen: (b), at the UserInterface level**: while a window larger than the safe area is
open, its UserInterface is laid out and drawn at the lowest whole percent at which every open window fits
(`FitScale`), and the player's scale comes back when it closes. Options at UI 40 shows at 44 %, Fixes and
Extensions at 52 %. Everything in that UI shrinks with it while the dialog is open (in the front end, the
menu around it; the dialog covers most of it).

**How it is applied without a jump.** The decision is made at the first dialog's `kExtPlaceLayout` call
of the frame, before any dialog of that UI is laid out; Update computed its layout size before the loop, so
`TouchFrame` hands every handler the fitted size (and `{0, 0, w, h}` as the rectangle) for the rest of the
frame; Draw reads `_uiScale` itself. A scale another writer put in `_uiScale` while the fit is in force
(the Options slider's Apply) becomes the player's scale. A one-percent hysteresis keeps the fit from
hunting when a font change moves a dialog by a unit. Windows (combo drop-downs, tooltips, the menus, the
loading screen) are excluded: no description, a full-screen backdrop, or a root sized to the screen.

**Verified.** Windows `opt_after0.png` (whole, inside the safe area, `[PhoneMenu] UI ...: ... laid out at UI
scale 0.44 (the player's 0.40)`), `ext_after0.png` (0.52), `mods_after0.png`. S25 `ph_options.png`
(Options whole at UI 40, the tab row and Cancel / Apply / Ok on screen).

## 4. The version line over the pause menu's rows, the missing black fringe: DONE

**What it was.** On Windows at the S25's layout the pause menu's two rows ended just above the version
line (`pause_before15.png`); the owner saw them overlap on the phone. Both menus lacked the backdrop's
bottom border (`menu_before0.png`): `LogoTop` (P7) puts the 16:9 picture's top on the screen's top to keep
the logo whole, so at 19.5:9 its bottom border (the last tenth or so) fell 94 units below the screen.

**The change.**
* The picture is drawn twice for the two menus: a copy with its bottom on the screen's bottom first, then
  retail's (moved) draw over it, whose opaque top border covers the copy's upper part (the lower edge of a
  second logo) and whose transparent middle lets the copy's bottom border through
  (`kExtPlaceBackdropLower`, a fourth call point on the seam; PHONEMENU.md §3 has its contract for the
  loading lane). The first build scissored the copy to the screen's lower half instead: it drew in the
  front end once the sprite sheet was re-selected after the scissor change (a scissor change flushes the
  batch and clears `SpriteContext::_currentSheet`), but not over the game; the draw order needs no
  scissor and was kept.
* The owner, watching the Windows runs: "the version text could be farther down to be all in the black
  area". The version line now sits on the screen's bottom edge (the safe area's sides and top, the
  screen's bottom), inside the restored border, and the rows' band ends above it, so the rows can no
  longer reach the line (`LowVersionLayout` / `LowVersionTop`).

**Verified.** Windows `menu_after0.png`, `pause_after30.png`; S25 `ph_pause.png` (UI 40, render scale
Full: two rows of five, both borders, the version text inside the bottom border).

## 5. Where the screenshots are

The session scratchpad's `android-dialogs/windows/` (opt_before0, opt_after0, ext_rowoff0, ext_after0,
mods_after0, menu_before0, menu_after0, pause_before15, pause_after30, panel_before40, panel_after30) and
`android-dialogs/phone/` (ph_options, ph_pause, ph_panel). Not in the tree.

Unit tests: `tests/unit/ext_phone_touch_test.cpp` (six cases); 1502 tests, 0 failures, 1 skipped (the kit
cross-check, as on main). The phone was left with this branch's debug apk, stopped, and its
`settings.cfg` as found (UI 42; the runs used a temporary copy at UI 40).

## 6. Left / for a ruling

* The loading screen (another lane, ruled out of this one) does not yet match the menus' two-border look;
  the owner noticed it during these runs. The loading lane can answer `kExtPlaceBackdropLower` for
  `Loading.rsc` the same way.
* While a fitted scale is in force, moving the Options UI scale slider and pressing Apply changes the
  player's scale, but the dialog stays at the fitted size until it closes (the slider's effect shows
  then). A ruling if the owner wants the fit dropped while Options is open.
* The fitted scale shrinks the whole UserInterface (the menu around Options in the front end, the HUD
  behind a large panel in game), not only the dialog: the engine has no per-dialog scale.
* The 4x bar draws the game's 12-pixel bar art stretched across 48 units (the thumb reads as a wide
  rounded block); no wider art exists in the game's data.
