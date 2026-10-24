#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_edit.h -- ext.terrain_edit (class B; NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. A brush that reshapes the terrain under the mouse with five brushes
// (raise, lower, flatten, mountain, smooth) plus Undo, started from the Terrain page of the extension window,
// which also sets its size and strength.
// ============================================================================

#include "ext/terrain_edit/terrain_edit_math.h"

#include <cstddef>

namespace Ext {
namespace TerrainEdit {

// The brush's mode, radius and strength: what the Terrain page sets and the tool reads. One per process.
BrushState& State();

// Installs the brush as the player's current tool (the Terrain page's mode buttons). Right click ends it.
void StartBrush();
bool BrushActive();

// Reverses the most recent stroke (a held left button is one stroke): its heights, the terrain types it
// changed, and the trees and rocks standing on the ground it moved. False when there is nothing to undo, or
// when the world is not the one the strokes were painted on (a new game or a load): the strokes are then
// dropped.
bool UndoLast();
size_t UndoDepth();

// Called whenever the number of undo steps may have changed (a stroke ended, an undo ran): the Terrain page
// refreshes its status line from it. Null clears it.
void SetUndoChangedHook(void (*hook)());

// Called every frame the brush tool is the active world tool: the Terrain page reads its size and strength
// sliders from it. Null clears it.
void SetFrameHook(void (*hook)());

}  // namespace TerrainEdit
}  // namespace Ext
