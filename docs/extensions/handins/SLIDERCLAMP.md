# SLIDERCLAMP: a dragged slider follows the pointer and clamps (`fix.slider_drag_clamp`, 2026-10-25)

> **Built: `fix.slider_drag_clamp` (tier 1, default ON, off under `/profile:faithful`), UI only.**
> While a slider's thumb is held, the thumb keeps following the pointer's horizontal position and
> stops at the slider's minimum / maximum, however far the pointer strays from the slider. A
> release anywhere keeps the value. Retail snaps the value back to where the drag began whenever
> the pointer is more than 32 pixels off the slider. The page-step arm (a press left or right of
> the thumb) is unchanged.

Retail entry: `docs/retail_bugs.md` §12. Inventory row F12. Reported by the user on the Options
dialog's "User interface scale" slider.

## 1. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `SliderElement::Update` (0x1800d67e0, `src/engine/ui/sliderelement.cpp`), the band test | dragging and outside the 0x20 band: `_value = _dragAnchor`, `UpdateNumber`, return | the pointer counts as inside the band; the function runs on as retail's in-band path |
| `ScrollElement::Update` (`src/engine/ui/scrollelement.cpp`), the same band test | dragging and outside the band: the else arm `_currentValue = _dragAnchor` | the pointer counts as inside the band; the drag arm's own `[0, _maxValue]` clamp holds the ends |

The engine seams are `UI::SliderElement::_extFixDragClamp` (`SliderElement.h`) and
`UI::ScrollElement::_extFixDragClamp` (`ScrollElement.h`), null-by-default `const bool*`s that
`Ext::InitFromSettings` (`src/ext/registry_boot.cpp`) both points at
`Ext::Fix().slider_drag_clamp` (DESIGN.md §3 hook rule 5: `src/engine` never includes `src/ext`).
Fixes toggle live, so the pointer follows the Extensions dialog's toggle at once. The row has no
`src/ext/slider_drag_clamp/` directory: the corrected arm is one assignment at the seam (the Names
table: "a row with no code has none"), as `fix.ui_scale_layout`'s.

Everything after the seam is retail's code, unchanged:
- **Drag.** `_value = _dragAnchor - dragDx / span * (max - min)` with `dragDx` = press x minus
  current x (`UserInterface::Update` 0x1801213c0), queued as event 0x17 when it changes. Only the
  horizontal distance counts; the vertical distance only decided the band in retail.
- **Clamp.** The tail's `_value < _minimum` / `_maximum < _value` stores hold the value at the ends.
- **Release.** `b04` clears `kFlagDragging` and plays sound 5 (retail's in-band release). The
  release frame's `dragDx` is 0, but the drag arm re-reads the flag after the release cleared it,
  so the value from the previous frame is kept.

Retail also leaves `kFlagDragging` set after a release outside the band (the early return skips
the release test); with the row on, the release always clears it.

## 2. The scroll bar, and other sliders

`grep dragAnchor / kFlagDragging` over `src/`: the only other drag of this shape is
`ScrollElement::Update` (`src/engine/ui/scrollelement.cpp`, a scroll bar, same band and snap-back:
dragging off the band takes the else arm `_currentValue = _dragAnchor`; a release off the band clears
the flag and returns). Ruled in by the user 2026-10-25: the same row covers it through the second
seam. With it on, the drag arm runs as retail's in-band path along the bar's axis (`dragDy` for a
vertical bar, `dragDx` otherwise), its own clamp holds `[0, _maxValue]`, and a release anywhere
clears the flag and plays sound 5. `src/ext/terrain_edit/toolbar_page.cpp` reads its two
`SliderElement`s' `_value` only (no drag of its own) and gets the fix through `SliderElement::Update`;
`src/ext/sharp_ui/` has no slider code.

## 3. Save, sim and perf

- Save / sim: none. A slider's value reaches settings or game state only through the dialog's own
  event handlers, exactly as a retail drag that stayed inside the band.
- Perf: off (or faithful) costs one null test, and only on a frame where a slider or scroll bar is
  being dragged outside its band.

## 4. Tests and gates

- `tests/unit/ext_registry_test.cpp` `ext_registry_table_shape`: the row is a fix, no class,
  default ON, with a name and a description.
- Row icon `Entry_slider_drag_clamp.png` (`tools/ext_icons/fixes.py` `draw_slider`, baked by
  `tools/ext_sprites_bake.py` into `src/ext/resources/ext_sprites_data.h`).
- The gates run `/profile:faithful`, which forces the row off: the faithful path is retail's.
- Not run by the author: the Windows gate and a hands-on drag in Options (the maintainer's run).
