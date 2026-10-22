// ============================================================================
// src/ext/terrain_resort/terrain_resort_rule.h -- fix.terrain_resort: the pure rule
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINRESORT.md. Standard C++ only, so
// tests/unit/ext_terrain_resort_test.cpp links it without the world; the map walk
// is terrain_resort.cpp.
//
// A cell is healed when it is stored as type 10 (too steep), the classifier
// (Map::ClassifyTerrain 0x14009e2c0) now calls it 2 (buildable) from the
// current heights, and nothing claims it: no blocker, no occupant, a zero
// footprint count and no road or pending road (+0x2A == 0xFF). The reverse
// (stored 2, now steep) is left alone: a cell beside a flattened road stays
// buildable, as retail leaves it.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace TerrainResort {

constexpr uint8_t kTypeBuildable = 2;    // MapCell +0x29
constexpr uint8_t kTypeSteep     = 10;   // the classifier's "mountain, below 2.0"
constexpr uint8_t kNoRoad        = 0xFF; // MapCell +0x2A

struct CellView {
    uint8_t terrainType;      // +0x29
    uint8_t footprintCount;   // +0x28
    uint8_t road;             // +0x2A
    bool    blocked;          // +0x00 blocker set
    bool    occupied;         // +0x08 occupant set
};

inline bool ShouldHeal(const CellView& cell, int classified) {
    if (cell.terrainType != kTypeSteep) return false;
    if (classified != kTypeBuildable) return false;
    if (cell.blocked || cell.occupied) return false;
    if (cell.footprintCount != 0) return false;
    return cell.road == kNoRoad;
}

}  // namespace TerrainResort
}  // namespace Ext
