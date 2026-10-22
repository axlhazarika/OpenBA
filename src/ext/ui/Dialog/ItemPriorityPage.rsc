// Dialog/ItemPriorityPage.rsc -- ext.item_priority's page in the extension window (NOT RETAIL; authored for this
// port, src/ext).
//
// docs/extensions/handins/ITEMPRIO.md, src/ext/item_priority/toolbar_page.cpp. The town ranks the items its
// citizens fetch from a storage, per category (tools, fuel, clothing, health): a row per item of the shown
// category, a check box to rank it (it goes to the bottom of the ranking), a number (its rank, best first) and
// Up / Down to move it. A citizen takes the best-ranked item the storages hold; an item that is not ranked comes
// after every ranked one. The window around the page is Dialog/ExtensionsWindow.rsc (one tab per active
// extension page); this file is only the page.
//
// The rows are laid out like the Foods page's (RestrictedFoodsPage.rsc): the controller sizes the table and
// clones ribbonItemPriorityCell into it, as TownHallUI::InitInventory does.
//
// Controller: src/ext/item_priority/toolbar_page.cpp. It looks up, by name: textItemPriorityShowing,
// ribbonItemPriorityCategories (its four children are the category buttons: tools, fuel, clothing, health),
// checkItemPriorityShowAll, tableItemPriority, and in each cell checkItemPriorityRow, labelItemPriorityRow,
// textItemPriorityRank and ribbonItemPriorityMoves (its two children are Up and Down).
//
// Texts: Dialog/ExtensionsStringTable.rsc (the ItemPriority* keys).

// The page: the hint, the category buttons, the title of the shown list, the list, the "Show all items" check
// box; 4 px between them. Left-aligned, as most of the game is.
RibbonDescription pageItemPriority
{
	Alignment _alignment = TopLeft;
	bool _vertical = true;
	int _cellPad = 4;
	ElementDescription _elements [ "textItemPriorityHint" "ribbonItemPriorityCategories" "textItemPriorityShowing" "ribbonItemPriorityList" "ribbonItemPriorityShowAll" ]
}

// The note under the tab: what the list does.
TextDescription textItemPriorityHint : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "ItemPriorityHint";
}

// The title of the list being shown ("Tools, best first"); the controller sets it.
TextDescription textItemPriorityShowing : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "ItemPriorityShowingTool";
}

// The four category buttons, side by side, in the order tools, fuel, clothing, health.
RibbonDescription ribbonItemPriorityCategories
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "buttonItemPriorityTool" "buttonItemPriorityFuel" "buttonItemPriorityClothing" "buttonItemPriorityHealth" ]
}

// Derived from buttonOk / buttonOkText, not from borderButton / commonButtonText: the compiled game only holds
// the blocks something refers to as an element, so a block used only as a base does not load by name.
ButtonDescription buttonItemPriorityTool : "SharedElements.rsc:buttonOk" { ElementDescription _content = "textItemPriorityTool"; }
ButtonDescription buttonItemPriorityFuel : "SharedElements.rsc:buttonOk" { ElementDescription _content = "textItemPriorityFuel"; }
ButtonDescription buttonItemPriorityClothing : "SharedElements.rsc:buttonOk" { ElementDescription _content = "textItemPriorityClothing"; }
ButtonDescription buttonItemPriorityHealth : "SharedElements.rsc:buttonOk" { ElementDescription _content = "textItemPriorityHealth"; }

TextDescription textItemPriorityTool : "SharedElements.rsc:buttonOkText"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatTool";
}
TextDescription textItemPriorityFuel : "SharedElements.rsc:buttonOkText"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatFuel";
}
TextDescription textItemPriorityClothing : "SharedElements.rsc:buttonOkText"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatClothing";
}
TextDescription textItemPriorityHealth : "SharedElements.rsc:buttonOkText"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityCatHealth";
}

// The check box under the list: unchecked shows the items the town has made or stored (and the ranked ones),
// checked shows every item of the category.
RibbonDescription ribbonItemPriorityShowAll
{
	Alignment _alignment = MidLeft;
	int _cellPad = 4;
	ElementDescription _elements [ "checkItemPriorityShowAll" "textItemPriorityShowAll" ]
}

CheckDescription checkItemPriorityShowAll : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ItemPriorityShowAllTip";
}

TextDescription textItemPriorityShowAll : "Extensions.rsc:textProfile"
{
	Alignment _alignment = MidLeft;
	String _text = "ItemPriorityShowAll";
}

// The list frame: the Extensions dialog's (by reference) around the scrolling box of items.
RibbonDescription ribbonItemPriorityList : "Extensions.rsc:entryList"
{
	ElementDescription _elements [ "containerItemPriorityBox" ]
}

// The scrolling box: the Extensions list's container (scroll bar, size) with the table as its content.
ContainerDescription containerItemPriorityBox : "Extensions.rsc:tableList"
{
	ElementDescription _content = "tableItemPriority";
}

// One column. The controller sizes it (rows = shown items) and fills it with ribbonItemPriorityCell.
TableDescription tableItemPriority
{
	int _width = 1;
	int _height = 1;
	TableItem _items [ { int _x = -1; int _y = -1; ElementDescription _element = "ribbonItemPriorityCell"; } ]
}

// One item: the check box, the compact label, its rank, and Up / Down. The border is the Mods list's row border,
// tinted by the controller while the item is ranked.
RibbonDescription ribbonItemPriorityCell
{
	ElementDescription _border = "Mod.rsc:borderLine";
	int _leftPad = 1;
	int _topPad = 1;
	int _bottomPad = 1;
	int _cellPad = 4;
	int _minWidth = 540;
	ElementDescription _elements [ "checkItemPriorityRow" "labelItemPriorityRow" "textItemPriorityRank" "ribbonItemPriorityMoves" ]
}

CheckDescription checkItemPriorityRow : "Extensions.rsc:checkLoaded"
{
	StringTable _toolTipStringTable = "ExtensionsStringTable.rsc";
	String _toolTipText = "ItemPriorityCheckTip";
}

// The Town Hall's inventory item label (20 px icon, small font, 132 px wide), its text from our table's "@0".
LabelDescription labelItemPriorityRow : "TownHall.rsc:storageItem"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityParam";
}

// The item's place in the ranking ("1" for the best), or a dash while it is not ranked; the controller sets it.
TextDescription textItemPriorityRank : "Extensions.rsc:textProfile"
{
	String _text = "ItemPriorityParam";
}

RibbonDescription ribbonItemPriorityMoves
{
	Alignment _alignment = MidLeft;
	int _cellPad = 2;
	ElementDescription _elements [ "buttonItemPriorityUp" "buttonItemPriorityDown" ]
}

ButtonDescription buttonItemPriorityUp : "SharedElements.rsc:buttonOk" { ElementDescription _content = "textItemPriorityUp"; }
ButtonDescription buttonItemPriorityDown : "SharedElements.rsc:buttonOk" { ElementDescription _content = "textItemPriorityDown"; }

TextDescription textItemPriorityUp : "SharedElements.rsc:buttonOkText"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityUp";
}
TextDescription textItemPriorityDown : "SharedElements.rsc:buttonOkText"
{
	StringTable _stringTable = "ExtensionsStringTable.rsc";
	String _text = "ItemPriorityDown";
}
