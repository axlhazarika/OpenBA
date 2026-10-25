#pragma once
// ============================================================================
// src/ext/terrain_edit/plant_math.h -- ext.terrain_edit's resource tool, the pure part (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. Standard C++ only: how the Resource drop down names and orders the
// templates found on the map, and which cells of the brush's disc get a resource, so that
// tests/unit/ext_terrain_edit_test.cpp can check them without the game.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace TerrainEdit {

// The share of the disc's eligible cells (free ones and those a resource stands on) that a stroke fills: 10% to 100% in steps of 10%.
constexpr float kMinDensity     = 0.1f;
constexpr float kMaxDensity     = 1.0f;
constexpr float kDensityStep    = 0.1f;
constexpr float kDefaultDensity = 0.5f;

// "Template\NaturalResourceWillowTree.rsc:resource" -> "WillowTree": the file name without its folder, its
// ".rsc" and the "NaturalResource" every template of the game starts with.
inline std::string ResourceShortName(const std::string& path) {
    size_t from = path.find_last_of("\\/");
    from = from == std::string::npos ? 0 : from + 1;
    const size_t to = path.find(".rsc", from);
    std::string name = path.substr(from, to == std::string::npos ? std::string::npos : to - from);
    const std::string prefix = "NaturalResource";
    if (name.compare(0, prefix.size(), prefix) == 0) name.erase(0, prefix.size());
    return name;
}

// The group a name sorts in: trees first (0), plants and everything else between (1), rocks, ore and salt
// last (2).
inline int ResourceGroup(const std::string& name) {
    if (name.find("Tree") != std::string::npos) return 0;
    if (name.find("Rock") != std::string::npos || name.find("Ore") != std::string::npos ||
        name.find("Salt") != std::string::npos) {
        return 2;
    }
    return 1;
}

// The order the drop down lists `names` in: by group, then by name. The result holds indices into `names`.
inline std::vector<size_t> ResourceOrder(const std::vector<std::string>& names) {
    std::vector<size_t> order(names.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int ga = ResourceGroup(names[a]);
        const int gb = ResourceGroup(names[b]);
        return ga != gb ? ga < gb : names[a] < names[b];
    });
    return order;
}

// How many of the `eligible` cells of the disc (the free ones and the ones a resource already stands on)
// the density asks to hold a resource: the density's share, rounded up, so a click on a small disc at a low
// density still plants one when nothing is there. Resources that already stand in the disc count toward it,
// so repeated clicks over one place fill it up to the density and no further.
inline int PlantTarget(int eligible, float density) {
    if (eligible <= 0 || density <= 0.0f) return 0;
    const int target = static_cast<int>(std::ceil(density * static_cast<float>(eligible) - 0.0001f));
    return target < 0 ? 0 : (target > eligible ? eligible : target);
}

// A number that orders the cells of one stroke in a fixed but scattered way: the same cell and seed always
// give the same number.
inline uint32_t CellRank(int cell, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(cell) * 2654435761u ^ (seed + 0x9e3779b9u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return h;
}

// `count` of the `cells`, chosen by the stroke's seed: the ones with the lowest CellRank (ties by cell
// number). All of them when `count` is not less than their number, none when it is not above 0.
inline std::vector<int> PickCells(std::vector<int> cells, int count, uint32_t seed) {
    if (count <= 0) return std::vector<int>();
    if (static_cast<size_t>(count) >= cells.size()) return cells;
    std::sort(cells.begin(), cells.end(), [seed](int a, int b) {
        const uint32_t ra = CellRank(a, seed);
        const uint32_t rb = CellRank(b, seed);
        return ra != rb ? ra < rb : a < b;
    });
    cells.resize(static_cast<size_t>(count));
    return cells;
}

}  // namespace TerrainEdit
}  // namespace Ext
