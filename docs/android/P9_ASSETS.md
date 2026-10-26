# Android P9: the Texture Filter note removed, the P8 row icons, the font in the apk (2026-10-26)

Session log of the `p9assets` agent (`docs/runtime_port/NEXT_SESSION.md` §2.8). Branch from `main` 14415217e.
Three items, all owner rulings. Lasting facts are in `docs/android/README.md`.

## 1. Texture Filter: "(next start)" removed again

P8 (`P8_VULKAN.md` §7.2, `6728bebee`) marked every Options > Video "Texture Filter" choice other than the
filter the textures were made with "(next start)". The owner ruled it out. Removed whole, not just the text:

- `src/ext/enhanced_tab/texture_filter_note.cpp` deleted (the only body of `Ext::EnhancedTab::NoteTextureFilterNextStart`).
- `src/ext/enhanced_tab/enhanced_tab.h`: the declaration and the `UI::ComboElement` forward declaration it alone used.
- `src/game/Dialog/optionsdialog.cpp`: the `EXT SEAM` call at the end of `OptionsDialog::PopulateFilterCombo`; the
  function is retail's again. The file still includes `enhanced_tab.h` for the Enhanced tab's other seams.
- `src/ext/ui/Dialog/ExtensionsStringTable.rsc`: the `NextStartSuffix` string; `tests/unit/ext_rsc_text_test.cpp`
  counts 70 strings again.
- No Names-table or registry row existed for it (it rode on the Enhanced tab host). `P8_VULKAN.md` §7.2 now opens
  with a "Removed in P9" line; nothing else in the tree named it.

**Verified:** the unit tests pass; a Windows debug run on the functional dir with `BANISHED_AUTOMENU_BUTTON=4`
(Options) opened the dialog (`[EnhancedTab] Enhanced tab added: tabs=5 entries=8`) and wrote no
`[EnhancedTab] Texture Filter: ...` line, which P8 wrote at every `PopulateFilterCombo`.

## 2. Row icons for the P8 rows

Every row of `src/ext/registry.cpp` was checked against `src/ext/ui/Dialog/ExtensionsSprites.rsc`. Two had no
`ExtensionsEntry_<id>` sprite, both added in P8: `phone_menu` and `fps_cap` (class A, blue tile). All 51 other rows
had one; `unit_a` / `unit_b` / `unit_c` are test-only rows, not in the exe.

- `tools/ext_icons/phone.py` (new): `phone_menu` = a phone in landscape with a dark camera cut-out and the interface
  inside a smaller safe area; `fps_cap` = a gauge whose dark needle stops short of the top. Our own 12 x 12
  pictograms on the 16 px tile, saved 48 x 48 by `tools/ext_sprites.py` like every other row icon.
- `src/ext/ui/Dialog/Sprite/Extensions/Entry_phone_menu.png`, `Entry_fps_cap.png` (generated), two rows in
  `ExtensionsSprites.rsc`, `src/ext/resources/ext_sprites_data.h` re-baked (`tools/ext_sprites_bake.py`, 68 sprites;
  the sheet still fits 512 x 512).
- New unit test `ext_sheet_every_entry_has_an_icon` (`tests/unit/ext_sheet_builder_test.cpp`): every registry row
  except the `unit_*` rows has a baked `ExtensionsEntry_<id>`. A row added without an icon now fails `unit`.
- `tools/ext_sprites.py` regenerates every PNG; it rewrote `Entry_draw_batch.png` with different file bytes but
  the same pixels (a PNG encoding difference only). That file was restored, not committed.
- `tools/ext_resources.sh` was not run: since KITFREE the exe builds the sheet from the baked header and the kit
  output is the cross-check oracle only (`ext_sheet_kit_crosscheck` skips without it, as on every worktree).

**Verified:** `tools/build.sh unit --jobs 2`: 1497 tests, 0 failures, 1 skipped (the kit cross-check). A Windows
debug run (functional dir, `BANISHED_AUTOMENU_BUTTON=13`) built the sheet (`[ExtRes] sprites: built sheet +
material + texture (68 sprites, 512 x 512, pad 1)`) and opened the Extensions dialog. The two new rows sit at the
end of the Optional list; scrolling there unattended failed (synthesised wheel and drag input do not reach the
window), so the in-dialog look of the two icons is open for a look by hand.

## 3. The font in the apk

The FrancophilSans files (`tools/release/fonts/FrancophilSans.ttf`, `FrancophilSans-Bold.ttf`,
`FrancophilSans_README.txt`) are apk assets and are copied to the data folder at start when absent.

- `android/app/build.gradle`: `assets.srcDirs += file('../../tools/release/fonts')`, the folder used in place;
  nothing copied into `android/`. The header comment says the apk carries the typeface.
- `OpenBAActivity.onCreate` copies each of the three files absent from `getExternalFilesDir(null)` (the folder
  `SDL_GetAndroidExternalStoragePath` names, `Platform::Android::DataDir`) before `super.onCreate`, so before the
  native side runs. Written to `<name>.part`, then renamed; a file already there is left alone. Logcat (tag
  `OpenBA`): `font: copied <name> to <dir>`.
- **Why Java:** assets are a Java API (`getAssets().open`). The native side would need the activity's
  `AssetManager` through JNI (`AAssetManager_fromJava`) for the same three copies; SDL offers no asset-to-file
  helper. The rasteriser (`font_raster_android.cpp`) is unchanged apart from its header comment.
- `tools/android.sh push-data` no longer pushes the font (header comment and the step's comment updated).
- `THIRD_PARTY_NOTICES.md` says the apk carries the three files unchanged; `docs/android/README.md`'s data-folder
  table, install block and font paragraph say the same.

**Apk size:** debug apk 19,862,830 bytes. The three assets are 123,643 bytes raw, 72,197 deflated in the apk
(`assets/FrancophilSans.ttf` 35,094, `-Bold.ttf` 36,075, `_README.txt` 1,028). The P8 debug apk of the main
checkout was 19,716,263 bytes (+146,567, the rest is the code of this branch and build-path differences).

**On device:** verified, §4.

## 4. Phone check

Galaxy S25, this branch's debug apk (`tools/android.sh install debug`).

1. `adb shell rm` of the three font files in `files/` (they were adb-pushed copies, owner `shell`, from P8).
2. Launch to the menu: logcat `font: copied FrancophilSans.ttf / FrancophilSans-Bold.ttf /
   FrancophilSans_README.txt to /storage/emulated/0/Android/data/org.openba.game/files`; the three files are back,
   now owned by the app (`u0_a29`), same sizes (60,564 / 61,020 / 2,059).
3. Rasterising. The owner's settings are at UI 42, where ext.sharp_ui keeps retail's scaled quads for every font
   (`[SharpUI] on scale=0.4200 ... Font->retail(range)`: the ratio is outside the variant range, with or without
   the face), so UI 42 proves nothing about the face. The owner's `settings.cfg` was not touched: the run used
   `BANISHED_USER_DIR=<files>/p9font_user` (a launch-intent extra; the app made the folder, then a copy of the
   owner's settings with `GameUIScale=83` was pushed into it). With the fonts deleted again before that launch,
   the same run copied them and logged `[ExtRes] font Font at 29 px: 96 glyphs rasterised in 4.1 ms` (FontSmall
   17, FontMedium 22, FontLarge 58 likewise) and `[SharpUI] on scale=0.8300 ... Font->29 FontSmall->17
   FontMedium->22 FontLarge->58`. A tap on the Extensions button showed its tooltip in the rasterised font.
   Screenshots (not in the tree): the session scratchpad's `android-assets/` (`phone_menu_ui83.png`,
   `phone_ext1.png`).
4. Left on the phone: this branch's debug apk, stopped; the three font files (copied by the app); the owner's
   `Documents/Banished/settings.cfg` unchanged (UI 42); the folder `files/p9font_user/` (a settings copy and an
   empty cache, not removed: the phone rules allow removing only the font files).

`adb shell input tap` hovers but does not click in the game (a second tap on Extensions only kept the tooltip), so
the Extensions dialog's new icons were not looked at on the phone either.

## Left open

- The in-dialog look of the `phone_menu` / `fps_cap` icons (§2) by hand (end of the Optional list).
- `files/p9font_user/` on the phone can be deleted (§4).
