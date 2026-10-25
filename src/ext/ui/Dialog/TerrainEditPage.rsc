// Dialog/TerrainEditPage.rsc -- ext.terrain_edit's page in the extension window (NOT RETAIL; authored for this port,
// src/ext).
//
// docs/extensions/handins/TERRAINEDIT.md, src/ext/terrain_edit/toolbar_page.cpp. Three sections: the terrain brushes (five
// tool buttons, a strength slider), the resource tool (a Plant button, a drop down of resources, a density slider) and what
// both share (the size slider, Undo, and the status line). A tool button starts the brush tool, which is a world tool
// (src/ext/terrain_edit/brush_tool.cpp), not part of this window; the tool in use stays lit. The window around it is
// Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file is only the page.
//
// Controller: it looks up, by name: textTerrainEditStatus, the six tool buttons (buttonTerrainEdit + Raise, Lower, Flatten,
// Mountain, Smooth, Plant: check boxes), buttonTerrainEditUndo, the sliders sliderTerrainEditSize, sliderTerrainEditStrength
// and sliderTerrainEditDensity, and the drop down comboTerrainEditPlant.
// Texts: Dialog/ExtensionsStringTable.rsc (the TerrainEdit* keys); the glyphs: Dialog/ExtensionsSprites.rsc
// (Glyph_terrain_*, tools/ext_icons/terrain_tools.py).

// The page, in three sections: the terrain brushes (the five brushes and their strength), the resource tool (Plant, its
// resource and density) and what both share (the size of the area, Undo); 4 px between the rows.
RibbonDescription pageTerrainEdit
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 4;
	ElementDescription _elements
	[
		"textTerrainEditHeadingBrushes" "textTerrainEditHint" "textTerrainEditHint2" "rowTerrainEditModes"
		"rowTerrainEditStrength"
		"textTerrainEditHeadingResources" "textTerrainEditPlantHint" "textTerrainEditPlantHint2" "rowTerrainEditPlant"
		"rowTerrainEditDensity"
		"textTerrainEditHeadingShared" "rowTerrainEditSize" "rowTerrainEditUndo" "textTerrainEditHint3" "textTerrainEditHint4"
		"textTerrainEditStatus"
	]
}

// The section headings.
TextDescription textTerrainEditHeadingBrushes : "Extensions.rsc:header"
{
	int _minWidth = 0; int _topPad = 2; String _text = "TerrainEditHeadingBrushes";
}

TextDescription textTerrainEditHeadingResources : "Extensions.rsc:header"
{
	int _minWidth = 0; int _topPad = 10; String _text = "TerrainEditHeadingResources";
}

TextDescription textTerrainEditHeadingShared : "Extensions.rsc:header"
{
	int _minWidth = 0; int _topPad = 10; String _text = "TerrainEditHeadingShared";
}

// The note is short lines (a long one does not wrap and would make the window as wide as the line).
TextDescription textTerrainEditHint : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditHint";
}

TextDescription textTerrainEditHint2 : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditHint2";
}

TextDescription textTerrainEditHint3 : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditHint3";
}

TextDescription textTerrainEditHint4 : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditHint4";
}

TextDescription textTerrainEditStatus : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditStatus";
}

TextDescription textTerrainEditSizeLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "TerrainEditSizeLabel";
}

TextDescription textTerrainEditStrengthLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "TerrainEditStrengthLabel";
}

// The tools in one row, 4 px apart.
RibbonDescription rowTerrainEditModes
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "buttonTerrainEditRaise" "buttonTerrainEditLower" "buttonTerrainEditFlatten" "buttonTerrainEditMountain" "buttonTerrainEditSmooth" ]
}

// Undo, under the shared heading, with what it covers.
TextDescription textTerrainEditUndoNote : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditUndoNote";
}

RibbonDescription rowTerrainEditUndo
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "buttonTerrainEditUndo" "textTerrainEditUndoNote" ]
}

// A label and a slider; the controller sets the slider's range and value when the page opens, and reads it back.
RibbonDescription rowTerrainEditSize
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textTerrainEditSizeLabel" "sliderTerrainEditSize" ]
}

RibbonDescription rowTerrainEditStrength
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textTerrainEditStrengthLabel" "sliderTerrainEditStrength" ]
}

// The sliders derive from the Options dialog's own (as EnhancedOptions.rsc's does): the compiled game only holds the
// blocks something refers to as an element.
SliderDescription sliderTerrainEditSize : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

SliderDescription sliderTerrainEditStrength : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

// The buttons are the game's own toolbar buttons (ToolBar.rsc: checkToolbar for a tool, whose check mark stays lit while
// it is in use, and buttonToolbar for Undo): the compiled game only holds the blocks something refers to as an element,
// and those two are. The content is the glyph (the Mods row icon's frame and size, as Extensions.rsc's imageTier), dimmed
// by the toolbar's state colors; the check mark is the same glyph in white. The tip is the tool's name.
ImageDescription imageTerrainEditRaise : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_raise";
}

// ... and white while it is (drawn over the button as its check mark)
ImageDescription imageTerrainEditLitRaise : "imageTerrainEditRaise"
{
	Color _color = 0xFFFFFFFF;
}

CheckDescription buttonTerrainEditRaise : "ToolBar.rsc:checkToolbar"
{
	ElementDescription _content = "imageTerrainEditRaise";
	ElementDescription _check = "imageTerrainEditLitRaise";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditRaise";
}

ImageDescription imageTerrainEditLower : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_lower";
}

// ... and white while it is (drawn over the button as its check mark)
ImageDescription imageTerrainEditLitLower : "imageTerrainEditLower"
{
	Color _color = 0xFFFFFFFF;
}

CheckDescription buttonTerrainEditLower : "ToolBar.rsc:checkToolbar"
{
	ElementDescription _content = "imageTerrainEditLower";
	ElementDescription _check = "imageTerrainEditLitLower";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditLower";
}

ImageDescription imageTerrainEditFlatten : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_flatten";
}

// ... and white while it is (drawn over the button as its check mark)
ImageDescription imageTerrainEditLitFlatten : "imageTerrainEditFlatten"
{
	Color _color = 0xFFFFFFFF;
}

CheckDescription buttonTerrainEditFlatten : "ToolBar.rsc:checkToolbar"
{
	ElementDescription _content = "imageTerrainEditFlatten";
	ElementDescription _check = "imageTerrainEditLitFlatten";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditFlatten";
}

ImageDescription imageTerrainEditMountain : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_mountain";
}

// ... and white while it is (drawn over the button as its check mark)
ImageDescription imageTerrainEditLitMountain : "imageTerrainEditMountain"
{
	Color _color = 0xFFFFFFFF;
}

CheckDescription buttonTerrainEditMountain : "ToolBar.rsc:checkToolbar"
{
	ElementDescription _content = "imageTerrainEditMountain";
	ElementDescription _check = "imageTerrainEditLitMountain";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditMountain";
}

ImageDescription imageTerrainEditSmooth : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_smooth";
}

// ... and white while it is (drawn over the button as its check mark)
ImageDescription imageTerrainEditLitSmooth : "imageTerrainEditSmooth"
{
	Color _color = 0xFFFFFFFF;
}

CheckDescription buttonTerrainEditSmooth : "ToolBar.rsc:checkToolbar"
{
	ElementDescription _content = "imageTerrainEditSmooth";
	ElementDescription _check = "imageTerrainEditLitSmooth";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditSmooth";
}

// The resource tool: the Plant button, with its own glyph.
ImageDescription imageTerrainEditPlant : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_plant";
}

// ... and white while it is (drawn over the button as its check mark)
ImageDescription imageTerrainEditLitPlant : "imageTerrainEditPlant"
{
	Color _color = 0xFFFFFFFF;
}

CheckDescription buttonTerrainEditPlant : "ToolBar.rsc:checkToolbar"
{
	ElementDescription _content = "imageTerrainEditPlant";
	ElementDescription _check = "imageTerrainEditLitPlant";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditPlant";
}

// The resource tool's note, its drop down (the controller fills it with the resource templates on the map) and the
// density slider.
TextDescription textTerrainEditPlantHint : "Extensions.rsc:textProfile"
{
	int _topPad = 10; String _text = "TerrainEditPlantHint";
}

TextDescription textTerrainEditPlantHint2 : "Extensions.rsc:textProfile"
{
	String _text = "TerrainEditPlantHint2";
}

TextDescription textTerrainEditDensityLabel : "Extensions.rsc:textProfile"
{
	int _minWidth = 70; String _text = "TerrainEditDensityLabel";
}

SliderDescription sliderTerrainEditDensity : "Options.rsc:sliderUI"
{
	int _bottomPad = 2;
	int _minWidth = 180;
	ElementDescription _number = "Options.rsc:number";
}

RibbonDescription rowTerrainEditDensity
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "textTerrainEditDensityLabel" "sliderTerrainEditDensity" ]
}

ComboDescription comboTerrainEditPlant : "Options.rsc:comboAutosave"
{
	int _minWidth = 200;
}

RibbonDescription rowTerrainEditPlant
{
	Alignment _alignment = MidLeft; int _cellPad = 4;
	ElementDescription _elements [ "buttonTerrainEditPlant" "comboTerrainEditPlant" ]
}

ImageDescription imageTerrainEditUndo : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_undo";
}

ButtonDescription buttonTerrainEditUndo : "ToolBar.rsc:buttonToolbar"
{
	ElementDescription _content = "imageTerrainEditUndo";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditUndo";
}
