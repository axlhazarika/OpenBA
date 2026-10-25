#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_undo.h -- ext.terrain_edit, the undo stack (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. Standard C++ only, so tests/unit/ext_terrain_edit_test.cpp can
// check it without the game. One stroke (a held left button) is one undo step: the height words it changed,
// each as it was before the stroke, and the terrain type of each cell it retyped, as it was before the
// stroke. The first record of an index wins, so a long stroke over the same ground keeps the original and
// not the last value.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Ext {
namespace TerrainEdit {

constexpr size_t kMaxUndoStrokes = 30;   // the oldest stroke is dropped past this

// A water quad a stroke took out of the mesh (its six indices made into one repeated vertex, so nothing is
// drawn): where it was in the index array, and the indices it had.
struct KilledQuad {
    int      position;
    uint16_t indices[6];
};

struct Stroke {
    // vertex index (stride * vy + vx) -> the word before the stroke
    std::unordered_map<int, int16_t> heights;
    // cell index (row * size + col) -> the type before the stroke
    std::unordered_map<int, uint8_t> types;
    // cell index -> the type the stroke last wrote there (an undo leaves a cell alone that holds another)
    std::unordered_map<int, uint8_t> written;
    // The water mesh's sizes from before the stroke added water to it (the vertex, index and feature-range
    // counts), so an undo can cut the mesh back. Only strokes that added water have them. The ground
    // texture's bytes of the cells the stroke repainted (cell index dim * row + col -> R, G, B, A packed as
    // the four bytes are in memory), as they were before the stroke.
    std::unordered_map<int, uint32_t> texels;
    std::vector<KilledQuad> killedQuads;   // restored by an undo, before the mesh is cut back
    // The box of height-grid vertices the stroke wrote (where the town's water access is looked at again
    // after an undo).
    bool hasBox = false;
    int  boxMinX = 0;
    int  boxMinY = 0;
    int  boxMaxX = 0;
    int  boxMaxY = 0;
    bool hasWater = false;
    int  waterVertices = 0;
    int  waterIndices = 0;
    int  waterRanges = 0;
    int  waterRangesA = 0;   // the river-style ranges' count before the stroke
    bool Empty() const { return heights.empty() && types.empty() && !hasWater && texels.empty(); }
};

class UndoStack {
public:
    // The stroke being painted. A press starts a fresh one.
    void BeginStroke() {
        _current = Stroke();
        _open = true;
    }

    void RecordHeight(int vertex, int16_t before) {
        if (_open) _current.heights.emplace(vertex, before);   // emplace keeps the first value
    }

    void RecordType(int cell, uint8_t before) {
        if (_open) _current.types.emplace(cell, before);
    }

    void RecordWrittenType(int cell, uint8_t after) {
        if (_open) _current.written[cell] = after;
    }

    // Once per stroke: the water mesh's sizes before the stroke changed it, and every quad the stroke took
    // out of it.
    void RecordWater(int vertices, int indices, int ranges, int rangesA = 0) {
        if (!_open || _current.hasWater) return;
        _current.hasWater = true;
        _current.waterVertices = vertices;
        _current.waterIndices = indices;
        _current.waterRanges = ranges;
        _current.waterRangesA = rangesA;
    }

    void RecordBox(int minX, int minY, int maxX, int maxY) {
        if (!_open) return;
        _current.hasBox = true;
        _current.boxMinX = minX;
        _current.boxMinY = minY;
        _current.boxMaxX = maxX;
        _current.boxMaxY = maxY;
    }

    void RecordTexel(int cell, uint32_t before) {
        if (_open) _current.texels.emplace(cell, before);
    }

    // The stroke being painted (its vertices written so far).
    const Stroke& Current() const { return _current; }

    void RecordKilledQuad(int position, const uint16_t* indices) {
        if (!_open) return;
        KilledQuad q;
        q.position = position;
        for (int i = 0; i < 6; ++i) q.indices[i] = indices[i];
        _current.killedQuads.push_back(q);
    }

    // A release (or the tool going away): keep the stroke if it changed anything.
    void CommitStroke() {
        if (!_open) return;
        _open = false;
        if (_current.Empty()) return;
        _strokes.push_back(std::move(_current));
        _current = Stroke();
        if (_strokes.size() > kMaxUndoStrokes) _strokes.erase(_strokes.begin());
    }

    bool Open() const { return _open; }
    size_t Depth() const { return _strokes.size(); }

    // The most recent stroke, left on the stack; null when there is none.
    const Stroke* Top() const {
        return _strokes.empty() ? nullptr : &_strokes.back();
    }

    // The most recent stroke, removed from the stack. False when there is none.
    bool Pop(Stroke& out) {
        if (_strokes.empty()) return false;
        out = std::move(_strokes.back());
        _strokes.pop_back();
        return true;
    }

    void Clear() {
        _strokes.clear();
        _current = Stroke();
        _open = false;
    }

private:
    std::vector<Stroke> _strokes;
    Stroke              _current;
    bool                _open = false;
};

}  // namespace TerrainEdit
}  // namespace Ext
