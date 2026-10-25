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
