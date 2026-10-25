// Dialog/TerrainEditPage.rsc -- ext.terrain_edit's page in the extension window (NOT RETAIL; authored for this port,
// src/ext).
//
// docs/extensions/handins/TERRAINEDIT.md, src/ext/terrain_edit/toolbar_page.cpp. A note, one row of six picture buttons
// (the five brushes and Undo), two sliders (size and strength) and a caption; a brush button starts the brush tool, which
// is a world tool (src/ext/terrain_edit/brush_tool.cpp), not part of this window. The window around it is
// Dialog/ExtensionsWindow.rsc (one tab per active extension page); this file is only the page.
//
// Controller: it looks up, by name: textTerrainEditStatus, the row rowTerrainEditModes (six buttons: the brushes in the
// order of Ext::TerrainEdit::Mode, then Undo) and the sliders sliderTerrainEditSize and sliderTerrainEditStrength.
// Texts: Dialog/ExtensionsStringTable.rsc (the TerrainEdit* keys); the glyphs: Dialog/ExtensionsSprites.rsc
// (Glyph_terrain_*, tools/ext_icons/terrain_tools.py).

// The page: the note, the tool row, the two slider rows and the caption; 4 px between them.
RibbonDescription pageTerrainEdit
{
	Alignment _alignment = TopLeft; bool _vertical = true; int _cellPad = 4;
	ElementDescription _elements [ "textTerrainEditHint" "textTerrainEditHint2" "textTerrainEditHint3" "textTerrainEditHint4" "rowTerrainEditModes" "rowTerrainEditSize" "rowTerrainEditStrength" "textTerrainEditStatus" ]
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
	ElementDescription _elements [ "buttonTerrainEditRaise" "buttonTerrainEditLower" "buttonTerrainEditFlatten" "buttonTerrainEditMountain" "buttonTerrainEditSmooth" "buttonTerrainEditUndo" ]
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

// Every button is derived from buttonOk, not from borderButton: the compiled game only holds the blocks something refers to
// as an element, so a block used only as a base does not load by name. The content is the glyph (the Mods row icon's frame
// and size, as Extensions.rsc's imageTier), and the tip is the tool's name.
ImageDescription imageTerrainEditRaise : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_raise";
}

ButtonDescription buttonTerrainEditRaise : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "imageTerrainEditRaise";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditRaise";
}

ImageDescription imageTerrainEditLower : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_lower";
}

ButtonDescription buttonTerrainEditLower : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "imageTerrainEditLower";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditLower";
}

ImageDescription imageTerrainEditFlatten : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_flatten";
}

ButtonDescription buttonTerrainEditFlatten : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "imageTerrainEditFlatten";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditFlatten";
}

ImageDescription imageTerrainEditMountain : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_mountain";
}

ButtonDescription buttonTerrainEditMountain : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "imageTerrainEditMountain";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditMountain";
}

ImageDescription imageTerrainEditSmooth : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_smooth";
}

ButtonDescription buttonTerrainEditSmooth : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "imageTerrainEditSmooth";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditSmooth";
}

ImageDescription imageTerrainEditUndo : "Mod.rsc:imageIcon"
{
	SpriteSheet _spriteSheet = "ExtensionsSprites.rsc"; String _spriteName = "ExtensionsGlyph_terrain_undo";
}

ButtonDescription buttonTerrainEditUndo : "SharedElements.rsc:buttonOk"
{
	ElementDescription _content = "imageTerrainEditUndo";
	Dialog _toolTipDialog = "ToolTip.rsc"; StringTable _toolTipStringTable = "ExtensionsStringTable.rsc"; String _toolTipText = "TerrainEditUndo";
}
