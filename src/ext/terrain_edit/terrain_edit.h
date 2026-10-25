#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_edit.h -- ext.terrain_edit (class B; NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. A brush that reshapes the terrain under the mouse with five brushes
// (raise, lower, flatten, mountain, smooth) plus Undo, started from the Terrain page of the extension window,
// which also sets its size and strength.
// ============================================================================

#include "ext/terrain_edit/plant_math.h"
#include "ext/terrain_edit/terrain_edit_math.h"

#include <cstddef>

namespace Ext {
namespace TerrainEdit {

// The brush's mode, radius and strength: what the Terrain page sets and the tool reads. One per process.
BrushState& State();

// Installs the brush as the player's current tool (the Terrain page's mode buttons). Right click ends it.
void StartBrush();
bool BrushActive();
// True from the moment the brush is installed until the player's tool changes (right click, or another
// tool): the Terrain page keeps the button of the tool in use lit while it is.
bool BrushRunning();

// Reverses the most recent stroke (a held left button is one stroke): its heights, the terrain types it
// changed, and the trees and rocks standing on the ground it moved. False when there is nothing to undo, or
// when the world is not the one the strokes were painted on (a new game or a load): the strokes are then
// dropped.
bool UndoLast();
// Paints again the stroke the last Undo reversed, through the brush code that painted it. False when there is
// none, or when something was built since the Undo (that ends the way forward).
bool RedoLast();
// The resource tool's palette: the templates of the natural resources standing on the map (trees first, then
// plants, then rocks and ore), by the order the Resource drop down lists them in. PlantSet picks the one a
// Plant stroke places.
// RescanPlantList adds the resources that have come onto the map since the last scan (nothing is taken off).
void RescanPlantList();
int PlantCount();
int PlantIndex();
const char* PlantName();
const char* PlantNameAt(int index);
void PlantSet(int index);
// How many strokes an Undo can reach: the ones above the first with something built on it.
size_t UndoDepth();
// The strokes that a Redo would paint again.
size_t RedoDepth();
// True after an Undo that was refused because something was built on the stroke since (a building, a road, a
// field, or a cell that no longer holds the type the stroke gave it): the Terrain page says so until the
// next stroke or a successful Undo.
bool UndoRefused();
// True after the redo list was dropped because something was built since the Undo. Cleared by the next stroke
// or Undo, or by another world.
bool RedoRefused();

// Called whenever the number of undo steps may have changed (a stroke ended, an undo ran): the Terrain page
// refreshes its status line from it. Null clears it.
void SetUndoChangedHook(void (*hook)());

// Called every frame the brush tool is the active world tool: the Terrain page reads its size and strength
// sliders from it. Null clears it.
void SetFrameHook(void (*hook)());

}  // namespace TerrainEdit
}  // namespace Ext
