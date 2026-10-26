# Android P9: the loading screen and the render-scale / UI-scale coupling (2026-10-25)

NOT RETAIL. Session log of two P9 items (`docs/runtime_port/NEXT_SESSION.md` §2.8), lane `p9load`, branch based
on `main` 14415217e. Lasting facts went to `docs/android/README.md`, `docs/extensions/handins/PHONEMENU.md` and
`docs/known_port_issues.md`. Screenshots (not in the tree): the session scratchpad's `android-loading-scale/`.

## 1. Loading screen under ext.phone_menu: DONE

**What was wrong.** On the S25 the loading screen's bottom black border was off the screen and "Loading..."
was drawn over the picture, not over the border as the main menu's logo is.

**How retail draws it.** `Dialog/Loading.rsc` (a `LoaderDialog`, built by every `GameLoadDialogLoopT`: the
boot's Loading splash, a new game, a load, the tutorial transitions; also ext.load_progress's boot screen) is
a 1600x900 (16:9) picture with torn black borders at its top and bottom, drawn as the dialog's backdrop with
`UserInterface::Draw`'s cover fit (0x180121dbe), and one group, `main`, alignment 7 (bottom centre):
`fullContent` (bottom padding 24) holding the spinner (`animatedImage`, `loadingImage`), `loadingText`
("Loading...") and `descText` (the tip). The splash's UI scale is `900 / back-buffer height` (row 1243), so
the layout is always about 900 units high. Measured on the installed picture: the solid part of the bottom
border is the picture's lowest 10.3 %, its torn edge reaching up to about 14 %. On a 16:9 screen the group's
text lies in that border. On a wider screen the width-fitted picture is taller than the screen and centred,
so it overhangs top and bottom: on the S25 (19.5:9) by 22 % of the screen height in all, which is more than
the bottom border; ext.phone_menu's safe area then lifted the group by a further 4.5 %.

**The change** (row `ext.phone_menu`, class A, its existing switch; `src/ext/phone_menu/loading_screen.cpp`,
`loading_math.h`, a two-line call in `phone_menu.cpp`'s `Place`). The menus' logo rule mirrored: on a screen
wider than the picture its bottom goes on the screen's bottom (`BottomOnBottom`), so the border that carries
the text is whole and the cut moves to the top border; the group keeps retail's bottom-centred rectangle
(no safe-area inset: the bottom centre is clear of the rounded corners and of the camera cut-out on a short
edge). A loading screen is recognised at its first layout by its `loadingImage` element and its description
recorded (the Shining Rock splash is a `LoaderDialog` without one and keeps retail's fit). Row off or
`/profile:faithful`: untouched (the seam returns before, or is null). A 16:9 / 16:10 screen: the picture
does not overhang, nothing moves.

**Verified.**
- Windows (DX11 debug, functional dir, row on, a 2528x1345 window = 1.88:1): before, "Loading..." on the
  snow picture with the bottom border mostly cut (`base_4.png`); after, the whole bottom border on the
  screen with "Loading..." and the tip in it (`fit1_7.png`).
- S25 (2340x1080, debug apk of this branch, the owner's settings: UI 42, render scale 100): the new-game
  loading screen with "Loading..." and the tip line in the black border (`p1_14.png` .. `p1_19.png`,
  `p1_18_half.png`); the boot's Loading splash (no picture) unchanged (`p1_9.png` .. `p1_11.png`).
- Unit: `tests/unit/ext_phone_loading_test.cpp` (2 cases).

**For a ruling.** On the S25 the top border is now entirely off the screen and the picture's top edge meets
the screen's top as a straight cut (`p1_18_half.png`). The alternative that keeps both borders is a height
fit (the whole picture, black side bars of about 9 % of the width each on the S25); it changes retail's
cover look more and was not built.

**Follow-up after the merge onto `android/p9` (72abd6571): the menus' two-draw look does not carry over.**
The menus now draw their 16:9 backdrop twice (`kExtPlaceBackdropLower`, `touch_fit.cpp`
`BackdropLowerDraw`): top-aligned, plus a bottom-aligned copy drawn first, under it. That works for the
menus because their backdrop is an overlay, transparent between the logo and the borders, over the 3D menu
scene. The Loading picture (`Dialog/Loading.rsc:gameLoad%i`, 1600x900) is opaque from edge to edge: top-aligned
at the width-fitted scale it covers the whole screen, and a copy drawn under it never shows. Built and seen
on Windows (functional dir, 2340x1080 client, UI 40, row on): the top border whole, NO bottom border, and
"Loading..." on the picture (the session scratchpad's `android-loading-scale/w6_16.png`; the attempt's diff is
`lower_draw_attempt.diff` there, not committed). `SpriteDraw` carries no source rectangle, so the copy cannot
be cropped to its border either. The committed fit (bottom-aligned, above) stays; on the merged tree at the
same settings the main menu and the loading screen side by side are `win_menu_vs_loading_opt1.png` (same
picture scale, the loading screen's top border cut). The choices left for the owner:
1. keep the bottom-aligned fit (the bottom border whole with the text in it, the top border cut);
2. top-aligned plus the lower copy drawn OVER it, clipped to the bottom strip by the sprite context's scissor
   (an engine-seam change: the extra draw after retail's, with a scissor); the strip's top is either a
   straight black edge or the torn edge with a seam in the picture just above it;
3. a height fit: both borders whole, the picture smaller than the menus' with black side bars.
A different reading of the owner's remark: the loading screen's own UI (spinner, "Loading...") is laid out at
retail's splash scale, 900 / screen height (0.83 on the S25), not at the Options UI scale (0.40 at UI 40), so
its text is drawn at a different scale from the menus'. Making it follow the UI scale is possible
(`GameLoadDialogLoopT::Startup`'s step 5 and ext.load_progress's copy of it); not built pending the ruling.

## 2. Render scale and UI scale coupled: DONE

**What was wrong.** `_uiScale = UIScale / 100` is layout units per BACK-BUFFER pixel; the render scale
(`VideoRenderScale`, Android) makes the back buffer half or a quarter of the screen, so the same UI scale drew
the interface twice as large at Half and four times at Quarter, and ext.phone_menu's floor (computed on the
back buffer) was 70 at Half and 100 (its cap) at Quarter.

**The rule** (`src/platform/common/ui_render_scale.h`, pure, unit-tested):
- **Stored:** `GameUIScale` is the UI scale AT FULL RENDER SCALE, i.e. in screen pixels. It is what the
  Options slider shows and writes. A render scale change never rewrites it.
- **Applied:** `_uiScale = UIScale / renderPercent` (`Platform::Display::UiScaleRatio`, from
  `GameContainer::ApplyKeyMapToUI` and `OptionsDialog::ApplyGameSettings`). The layout space
  (back buffer x `_uiScale`) is then `screen x UIScale / 100` at every render scale: on the S25 at UI 35 it is
  819 x 378 units at Full (2340 x 0.35), Half (1170 x 0.70) and Quarter (585 x 1.40).
- **On a render scale change** (Options > Enhanced > Render scale, Apply): the row re-derives the open
  interface's `_uiScale` from the setting it stands for (`UiPercentFromRatio`, then `UiScaleRatioAt` at the
  new scale), so the Options dialog keeps its size at once; every later `ApplyKeyMapToUI` derives the same
  value. The back buffer is rebuilt at the next frame (P8), so that one frame is laid out at the new ratio on
  the old back buffer.
- **Slider range:** retail's 75..150 (the density rule unchanged) and ext.phone_menu's minimum are in the same
  screen units, so the range does not move with the render scale. The floor (`UiScaleFloorNow`) is taken on
  the screen the back buffer stands for (`ScreenExtent`): 35 on the S25 at Full, Half and Quarter. The
  ceiling stays 150 (a UI at 150 % of the screen's pixels; at Quarter that is `_uiScale` 6.0, legal and tiny).
- **At the floor:** a stored value below it is still raised at every loop's start (`ClampUiScale`), now
  against the screen floor; at Half a stored 35 stays 35 (before P9 it was raised to 70).
- **First run** (no `GameUIScale` saved): the game's default 100 is in back-buffer pixels, so it becomes
  `100 x renderPercent / 100` in screen units (`FirstRunPercentAt`, inside
  `Platform::Display::FirstRunUiScalePercent`): the phone's seeded Half starts at **50**, which draws exactly
  what a fresh phone drew before P9 (1170 x 540 units). The seed itself (`kPhoneSettings`) is unchanged: it
  writes no `GameUIScale`; the floor (35) is below 50, so nothing is raised.
- **Desktops** (render scale 100 always): `_uiScale = UIScale / 100.0f`, the very float retail computes; the
  first run and the slider are unchanged.

Ownership: the coupling belongs to the render scale, a platform setting (P7 / P8: not an extension row), in
`src/platform/common/` beside the density rule; the live re-derive is the Enhanced tab's Render scale row
(`src/ext/enhanced_tab/render_scale_entry.cpp`); the floor in screen units is `ext.phone_menu`'s.

**Migration.** None. A `settings.cfg` written before P9 on a phone at Half or Quarter holds a value that
meant back-buffer pixels; read as screen pixels it draws the UI at half (Half) or a quarter (Quarter) of its
old size, and ext.phone_menu raises anything under 35. The owner's phone was at render scale 100 (UI 42), so
it is unaffected. A one-time conversion would need a marker key; not built (a ruling if any tester's phone
needs it).

**Verified on the S25** (debug apk of this branch, the owner's UI 42):
- boot at render scale 50: `[SharpUI] on scale=0.8400`, `[PhoneMenu] 982x453: 1 row`, the same layout as
  at 100 (`cmp_menu_100_vs_50.png`: the menu at 100 above, at 50 below, same size); the Options slider
  shows 42 at Half (`opt1_half.png`);
- boot at Quarter: `982x453` again; Options > Enhanced > Render scale Quarter -> Full, Apply:
  `[RenderScale] UI scale 42 % kept on the screen: _uiScale 1.680 -> 0.420`, the dialog the same size before
  and after (`live_quarter_to_full.png`). The first try of the live path found no interface (only a
  dialog's root carries its `DialogInstance`; fixed by walking the page's parents, commit d331e9858).
- Windows: DX11 debug build; unit 1504 / 0 / 1 skipped (the kit cross-check, as on main). On a desktop the
  render scale is fixed at 100, so the rule is the identity there.

**Seen on the way (open).** At Quarter with the UI at 42 % `ext.sharp_ui` rasterises the fonts at about
0.6 of their size in a 585 x 270 back buffer: the Options text's letters run together
(`live_quarter_to_full.png`, top). It is the back buffer's resolution, not the layout (the text was readable
at Quarter before P9 only because the whole UI was four times larger). Half reads clean.

## 3. Phone state at the end

This branch's debug apk installed, app stopped; `settings.cfg` pushed back byte-identical to the file found
at the start (UI 42, render scale 100). The phone lock was released.
