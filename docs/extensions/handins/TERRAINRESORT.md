# TERRAINRESORT: no "red squares" on flat ground (`fix.terrain_resort`, 2026-10-22)

> **Built: `fix.terrain_resort` (tier 1, default ON, off under `/profile:faithful`).** A free cell
> stored "too steep" (type 10) that the classifier now calls buildable (2) is set to 2: after every
> `Map::ResetTerrainHeights`, over its rectangle grown by one cell, and once over the whole map on
> a load, so an existing save heals. Sim-visible (the healed cells take buildings and trees) and
> written to the save like any terrain type.

Retail entry: `docs/retail_bugs.md` §8 ("Reproduced (2026-10-22)"). Inventory row F10.

## The desync

`MapCell::terrainType` (`+0x29`) is what the placement overlay tests (`Map::TestPlacement`
`0x14009f5d0`; the placement tool passes slope 0 and height tolerance 1000, so the mask, the
blocker and the footprint count are the live tests). Retail writes it from the heights in three
places only: world creation (`0x14009ef20`), an entity leaving its cells (`0x14009dee0`, through
`ClassifyTerrain` `0x14009e2c0`) and a version < 2 load. Heights change in more places:
`ResetTerrainHeights` (`0x14009e0e0`: `FinishRoadCell` `0x14009c640` over a road cell's four
corners, `ClearAreaComponent::FinishClear` `0x140118270` over a building's footprint), the quarry
dig and the tunnel terraform. None of them re-sorts a cell, so a cell sorted 10 while steep stays
10 once the ground under it is flat.

## Evidence

`tools/cell_dump/` with the classifier columns (`class height slope`), on a player's two save
pairs (two maps; the quarry overlay over a strip between a stream and a mountain):

| save | stored 10, classifier 2, free | after load with the fix |
|---|---|---|
| map 1, early | 227 | 0 |
| map 1, late | 294 | 0 |
| map 2, early | 222 | 0 |
| map 2, late | 445 | 0 |
| a fresh map (seed 12345, autosave at frame 600) | 0 | - |

"Free" = no blocker, no occupant, footprint count 0, `+0x2A == 0xFF`. The load log line is
`[TerrainResort] load healed <n> cells` (stderr).

Most of the shrinking green band in the player's screenshots is NOT this desync: a tree cell is
type 4, which the quarry's mask accepts, and clearing the tree re-sorts it from heights that were
already steep (474 of 533 such cells on map 1). That is retail as written and the fix leaves it.

## The rule (`src/ext/terrain_resort/terrain_resort_rule.h`)

Heal only 10 -> 2, only when the classifier says 2 from the current heights, only on a free cell.
The reverse (stored 2, classifier now 10, mostly cells beside a flattened road) is left as
retail leaves it: demoting it would add red cells next to every road on a slope. 2 and 10 are both
in the walkable set `0xe1f`, so a heal changes no zone and no region; the runtime arm still goes
through `Map::SetTerrainType` (`0x14009f2f0`), the load arm stores the byte before the zones, the
region graph and the pathfinder are built.

## Seams

- `Map::ResetTerrainHeights` (`src/game/Action/action_pf_roots_road.cpp`): `HealAround` over the
  reset rectangle grown by one, before the road-mesh rebuild.
- `Map::Serialize`'s load arm (`src/game/Game/gamegrid_serialize.cpp`): `HealAllOnLoad` after the
  footprint fix-up pass, before `FillZones`.

## Tests

- `tests/unit/ext_terrain_resort_test.cpp`: the rule (heal, keep, never demote, claimed cells).
- `tools/cell_dump/cell_dump.sh` with `CELL_DUMP_PROFILE=settings` on the four saves: table above.
- The Windows gate (`tools/build.sh run`, faithful) ran clean.
- Not exercised unattended: the runtime arm (a road finished during play). No harness finishes a
  road, and none of the saves holds pending road work. A hand test: on a slope edge, clear a tree
  so its cell turns red, build a road beside it, and the cell turns green when the road finishes.
