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

**The change, round 1 (replaced by round 2 below)** (row `ext.phone_menu`, class A, its existing switch; `src/ext/phone_menu/loading_screen.cpp`,
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

**For a ruling (answered by round 2 below).** On the S25 the top border is now entirely off the screen and the picture's top edge meets
the screen's top as a straight cut (`p1_18_half.png`). The alternative that keeps both borders is a height
fit (the whole picture, black side bars of about 9 % of the width each on the S25); it changes retail's
cover look more and was not built.

**Round 2 (after the merge onto `android/p9` 72abd6571; owner's rulings 2026-10-26).** The owner, watching
the runs: "the draws in the loading screen dont have the same scale" as the menus. Ruled: (a) the loading
group (spinner, "Loading...", tip) follows the UI scale, ext.load_progress's screen included; (b) the
loading screen looks like the pause menu, both torn borders whole at the menus' picture scale (the
bottom-aligned fit above stays only if that is impossible).

What did not work, and why:
- The menus' two-draw look (`kExtPlaceBackdropLower`, a bottom-aligned copy drawn first, UNDER the
  top-aligned backdrop; `touch_fit.cpp`) works for the menus because their backdrop is a see-through overlay
  over the 3D menu scene. The Loading picture (`Dialog/Loading.rsc:gameLoad%i`, 1600x900) is opaque: top-aligned
  it covers the screen and the copy under it never shows (`w6_16.png`: top border, no bottom border).
- Drawing the menus' overlay (`Build/MainMenuBackground.png`, its own one-sprite sheet) over the picture:
  the overlay is a resource of the menu loop and is freed before a loading screen draws; a kept pointer to
  it faulted in the draw (ACCESS_VIOLATION reading the freed sheet, `standalone_crash.log`, symbolised to
  `loading_screen.cpp` `MenuOverlay`). Keeping it alive would need a boot-time load outside the loops'
  resource lists; not attempted.

The change (row `ext.phone_menu`; `loading_screen.cpp`, `loading_math.h`; one new engine seam):
- the picture top-aligned at the menus' width-fitted scale (`LogoTop`: its top border whole);
- the same picture drawn once more, bottom-aligned at the same scale (`LowerCopy`), OVER it, clipped by the
  sprite scissor to the strip holding its torn bottom border (`BorderStripTop`, 0.125 of the picture's
  height). Seam: `UI::UserInterface::_extBackdropOver` (`userinterface_draw.cpp`, after retail's backdrop
  draw; null under the faithful profile; contract in `PHONEMENU.md` §3). A scissor change flushes the
  sprite batch and forgets its sheet: the first build of this lost the clipped copy until the sheet was
  bound again on both sides of it;
- the group keeps retail's bottom-centred rectangle; with the row on its UI scale is the Options UI scale
  (`LoadingUiScale`, in `GameLoadDialogLoopT::Startup` step 5 and `Ext::LoadProgress::BootBegin`, which now
  takes the UI scale percent from `game_main.cpp`).

The strip line is a trade-off measured on the installed pictures: the torn edge reaches from about 0.857 to
0.885 of the drawn height depending on the picture, the solid black starts near 0.897. A line at 0.145 left
a band of the lower copy's picture above the edge (a visible seam, `ph9_27_bottomleft.png`); 0.125 leaves
a thin sliver on some pictures and cuts the tallest edge peaks flat (`ph10_26_bottomleft.png`). Open for the
owner's eye.

Verified:
- Windows (DX11 debug, functional dir, 2340x1080 client, UI 40, row on): main menu and loading screen
  side by side `win_menu_vs_loading_final.png` (both borders, the same picture scale, the group at the
  menus' text size; before round 2: `win_menu_vs_loading_opt1.png`).
- S25 (this branch's debug apk, the owner's settings UI 42, render scale 100): the pause menu and the
  loading screen side by side `phone_pause_vs_loading_final.png`; the main menu and loading
  `phone_menu_pause_loading.png` (0.145 strip).
- Unit `ext_phone_loading_test.cpp` (2 cases): the top-aligned picture and its lower copy at the menus'
  scale, the strip; nothing added on a 16:9 screen. Unit total 1511 / 0 / 1 skipped.
- Not run: ext.load_progress's boot screen (needs an enabled mod) and the quit-to-menu loading screen.

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

This branch's debug apk installed (round 2 build), app stopped; `settings.cfg` pushed back byte-identical to
the file found at the start of each round (UI 42, render scale 100). The phone lock was released.
