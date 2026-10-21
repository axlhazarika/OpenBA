# cell_dump — map-cell dumps for the "red square" reports

`docs/retail_bugs.md` §7 has the findings. These tools show which of `Map::TestPlacement`'s
tests fails on a red cell and which code path got it there.

## Dump a save

```bash
tools/build.sh build debug
tools/cell_dump/cell_dump.sh path/to/Before.sav runs/before
tools/cell_dump/cell_dump.sh path/to/After.sav  runs/after
```

A path is copied into the save directory for the run and removed afterwards; a bare save name
loads `<save dir>/<name>.sav`. The run is faithful (`/profile:faithful`). Each run writes:

- `cells_raw.txt`: the cells as the save holds them, before the loader's fix-up pass. That
  pass turns a `footprintCount` of 0xFF back into 0 and sets 1 on road cells, so it hides
  exactly the wraps being looked for.
- `cells_fixed.txt`: the same cells after the fix-up, as the game plays them.
- `standalone_error.log`, with any `[CellTrace]` lines from the run.

A save written with mods active needs the same mods installed to load.

## Compare

```bash
tools/cell_dump/cell_diff.py runs/before/cells_raw.txt runs/after/cells_raw.txt
tools/cell_dump/cell_diff.py runs/before/cells_raw.txt runs/after/cells_raw.txt --at 120,88 --radius 4
tools/cell_dump/cell_diff.py runs/after/cells_raw.txt --suspects
```

`--suspects` lists cells with `footprintCount` 0xFF, above 2, or non-zero with no road and no
building within 2 cells.

Columns: `count` = `footprintCount` (+0x28), `terrain` = `terrainType` (+0x29; 0 / 1 roads,
2 buildable), `road` = +0x2A (255 = none), `roadState` = +0x2B (0x80 = queued),
`growth` = +0x2C, `floor` = `floorCount` (+0x2D), `zone` = +0x26, `blocker` / `occupant` =
the entity's description name or `-`. Heights are not in the dump.

## Trace a live game

`BANISHED_CELL_TRACE=1` (`src/app/options.h`) writes one `standalone_error.log` line per
`footprintCount` change that wraps below 0 or climbs above 2 (the writing site is named:
`footprint`, `PlaceRoadCell`, `FinishRoadCell`, `RemovePendingRoad`), and per removal re-sort
that turns a buildable cell (terrain 2) into another type. Nothing is logged while all
counts stay within 0..2.
