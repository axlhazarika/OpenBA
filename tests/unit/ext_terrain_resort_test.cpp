// ============================================================================
// tests/unit/ext_terrain_resort_test.cpp -- fix.terrain_resort's heal rule
// ============================================================================
// src/ext/terrain_resort/terrain_resort_rule.h without the world: only a free
// cell stored "too steep" (10) that the classifier now calls buildable (2) is
// healed; a claimed cell, a road, another type or another class is left alone,
// and a buildable cell is never demoted.
// ============================================================================
#include "unit.h"
#include "ext/terrain_resort/terrain_resort_rule.h"

#include <cstdint>
#include <initializer_list>

namespace {

using namespace Ext::TerrainResort;

CellView FreeSteep() {
    CellView cell;
    cell.terrainType    = kTypeSteep;
    cell.footprintCount = 0;
    cell.road           = kNoRoad;
    cell.blocked        = false;
    cell.occupied       = false;
    return cell;
}

}  // namespace

UNIT_TEST(ext_terrain_resort_heals_free_steep_cell_now_flat) {
    CHECK(ShouldHeal(FreeSteep(), kTypeBuildable));
}

UNIT_TEST(ext_terrain_resort_keeps_cells_still_steep_or_other_class) {
    for (int classified : {6, 7, 8, 10}) {
        CHECK(!ShouldHeal(FreeSteep(), classified));
    }
}

UNIT_TEST(ext_terrain_resort_never_demotes_or_touches_other_types) {
    for (uint8_t type : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        CellView cell = FreeSteep();
        cell.terrainType = type;
        CHECK(!ShouldHeal(cell, kTypeBuildable));
        CHECK(!ShouldHeal(cell, kTypeSteep));
    }
}

UNIT_TEST(ext_terrain_resort_leaves_claimed_cells) {
    CellView blocked = FreeSteep();
    blocked.blocked = true;
    CHECK(!ShouldHeal(blocked, kTypeBuildable));

    CellView occupied = FreeSteep();
    occupied.occupied = true;
    CHECK(!ShouldHeal(occupied, kTypeBuildable));

    CellView counted = FreeSteep();
    counted.footprintCount = 1;
    CHECK(!ShouldHeal(counted, kTypeBuildable));

    CellView road = FreeSteep();
    road.road = 3;
    CHECK(!ShouldHeal(road, kTypeBuildable));
}
