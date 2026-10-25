// ============================================================================
// src/ext/phone_menu/phone_math.h -- ext.phone_menu's pure decisions
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_phone_menu_test.cpp links
// phone_math.cpp on its own). docs/extensions/handins/PHONEMENU.md has the why.
//
// UNITS. Every extent here is in the units the caller hands in: the UI layout
// space (back buffer * _uiScale), which is also the space UserInterface::Draw
// computes a dialog's backdrop rectangle in. The answers depend only on ratios
// of their inputs, so they hold at any UI scale.
//
// TWO LAYOUTS. The main menu is a 3D scene (Game/MenuScene) under the menu
// dialog, whose backdrop is a 16:9 overlay: the Banished logo near its top and
// the dark torn borders, transparent between them.
//   * On a screen WIDER than the overlay (a 19.5:9 phone) retail's centred cover
//     fit cuts the logo's top off. There the overlay is drawn with its top on
//     the screen's top (LogoTop: the logo whole, centred at the top) and, when
//     it fits, the menu's buttons become ONE HORIZONTAL ROW OF SQUARE BUTTONS
//     across the middle of the safe area (RowFits), the version line centred at
//     the bottom.
//   * Otherwise (a desktop's 16:9 or 16:10, or a row too wide for the screen)
//     the overlay keeps retail's fit on a desktop and the column stays centred,
//     kept inside the safe area and clear of the logo and the version line
//     (MenuBand, VersionAside).
// Every decision is made from THIS frame's sizes (the caller sizes the tree
// before it asks), so the first frame a menu is drawn is already its final one.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace PhoneMenu {

// The safe-area margin as a fraction of the screen height. A phone held in
// landscape loses its corners to the rounded display (the S25's corner radius
// is about 3.5 % of its 1080-pixel height) and the middle of one short edge to
// the camera cut-out (about 3 % of the height deep). 4.5 % clears both with a
// little air and is still small enough that nothing centred moves: on a desktop
// screen the inset only shows on an element anchored to an edge.
constexpr float kSafeMarginFraction = 0.045f;

// The Options > UI scale slider's lowest value with the row on (percent).
// _uiScale = percent / 100 is LAYOUT UNITS PER DEVICE PIXEL, so 25 draws the UI
// four times larger than 100: what a 6-inch 1080-pixel screen needs to come near
// a desktop's physical size. Retail's lowest is 75.
constexpr float kUiScaleMinPercent = 25.0f;

// Where the Banished logo ends inside the main menu's backdrop, as a fraction of the drawn
// picture's height from its top: measured on the installed pictures (the logo's lowest
// pixel, the descender of its long s, at 0.210 of the drawn height), plus about one percent
// of air. A number about the user's own data, not a copy of it; a mod that repaints the
// backdrop with the logo elsewhere only moves the band the buttons are kept out of.
constexpr float kLogoBottomFraction = 0.22f;

// The gap kept between the buttons and the version line (layout units).
constexpr int32_t kVersionGap = 4;

struct Rect {
    int32_t x1, y1, x2, y2;
};

// round(height * kSafeMarginFraction); 0 for a height <= 0.
int32_t SafeMargin(int32_t height);

// The rectangle a dialog is laid out in on a width x height screen: the screen
// inset by SafeMargin(height) on every side (DialogInstance::ComputeLayout takes
// the right and bottom EDGES, not sizes). A margin that would leave nothing is
// dropped (the whole screen).
Rect InsetLayout(int32_t width, int32_t height);

// ---- the main menu's backdrop ----------------------------------------------

// True when the screen is wider than the backdrop picture (spriteW x spriteH), by retail's own
// compare in UserInterface::Draw (0x180121dbe): `spriteAspect > screenAspect` fits the height
// (false here), anything else fits the width and overhangs top and bottom (true). A picture or
// screen with no size: false.
bool WiderThanBackdrop(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH);

// The height retail draws the backdrop at: on a wider screen the width-fitted picture
// (`2 * ((int)(width / aspect + 0.5) >> 1)`, Draw's arithmetic), otherwise the screen's height.
int32_t BackdropHeight(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH);

// Retail's backdrop rectangle (`retail`) moved so its top is on the screen's top when it
// overhangs it (a wider screen): the logo is whole and stays centred; the cut moves to the
// bottom border. A rectangle that does not overhang the top is returned unchanged.
Rect LogoTop(const Rect& retail);

// Where the logo ends (layout units from the screen's top) in a backdrop drawn
// `backdropHeight` tall with its top on the screen's top.
int32_t LogoBottom(int32_t backdropHeight);

// ---- the button row (a screen wider than the backdrop) ----------------------

// The side of the square buttons: the largest of the labels' padded widths and heights
// (`count` of each), so every label fits inside its square. 0 when there is none.
int32_t SquareSide(const int32_t* labelWidths, const int32_t* labelHeights, int32_t count);

// The row's dialog (rowWidth x rowHeight, padded, as laid out) is centred in the safe area
// (its root is MidCenter-aligned and handed InsetLayout); true when it fits there across, and
// its top is at or below `logoBottom` (< 0: unknown) and its bottom kVersionGap above
// `versionTop` (< 0: unknown). False sends the menu back to the column.
bool RowFits(int32_t width, int32_t height, int32_t rowWidth, int32_t rowHeight, int32_t logoBottom,
             int32_t versionTop);

// ---- the centred column (any other screen, and the pause menu) ---------------

// The band the menu's button column is centred in, on a width x height screen:
//   * by default the safe area (InsetLayout);
//   * below the logo (`logoBottom`, < 0 when unknown) and above the version line
//     (`versionTop` less kVersionGap, < 0 when unknown), when the column (`columnHeight`, its
//     laid-out height, <= 0 when unknown) fits there: at a large UI scale the centred
//     column would otherwise cover the logo and run under the version line;
//   * else above the version line alone, when it fits between the screen's top edge and
//     the line (its top may then rise into the margin: the buttons stay whole and clear
//     of the version line, which matters more than the margin at a 25 % UI);
//   * else the safe area (the column is taller than the screen allows; retail's own
//     ComputeLayout then keeps its bottom on screen).
Rect MenuBand(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop,
              int32_t columnHeight);

// True when the version line should leave the bottom centre for the bottom-left corner of
// the safe area: the button column (centred, `columnWidth` x `columnHeight`) cannot sit
// above the line (`versionTop`) and the line (`versionWidth` wide) fits beside the column
// with kVersionGap to spare. Any size <= 0 (not laid out yet): false.
bool VersionAside(int32_t width, int32_t height, int32_t versionTop, int32_t versionWidth,
                  int32_t columnWidth, int32_t columnHeight);

// The version line's layout rectangle when it is aside: the safe area's left part, as wide
// as the line, so its bottom-centre alignment puts it in the bottom-left corner.
Rect VersionAsideLayout(int32_t width, int32_t height, int32_t versionWidth);

// The top of the version line (`versionHeight` tall, padded) laid out bottom-centred in the
// safe area; -1 for a height <= 0.
int32_t VersionTop(int32_t width, int32_t height, int32_t versionHeight);

// ---- every other dialog (the in-game HUD, its panels, the front end's other dialogs) ----

// The rectangle DialogInstance::ComputeLayout (retail 0x180113d90) is handed for a dialog that is
// not one of the menus above, so that what retail anchors to an edge or a corner (the toolbar and
// the event notices bottom-right, the autosave notice top-left, a panel opened at its default
// upper-left (0, 0)) sits inside the safe area. ComputeLayout places a dialog at
// `_upperLeftX + x1` (left / top alignment) or against `x2` less the dialog's width (right /
// bottom), centres it between the two, and pulls one that would cross `x2` back to `x2`. So:
//   * the far edges are the safe area's (x2 = width - m, y2 = height - m): right / bottom anchors
//     move in, centred dialogs do not move, and nothing is pushed past the safe edge;
//   * the near edges are a FLOOR, not an offset: x1 = max(0, m - upperLeftX), so the dialog's
//     left is max(upperLeftX, m). A dialog put at a screen point (a tooltip at the cursor, a combo
//     box's drop-down under its box, a panel dragged by the player) stays exactly where retail
//     puts it unless that point is inside the margin; a panel at its default (0, 0) opens at the
//     safe corner. (Dragging a panel out of the corner then moves it once the cursor has gone
//     past the margin: the dead zone retail itself has at the right and bottom edges.)
//   * an axis on which the dialog (`lastWidth` / `lastHeight`, as laid out; <= 0 when not laid
//     out yet) is wider than the safe area keeps retail's whole extent (0 .. width): the
//     margin is not worth cutting a dialog off for;
//   * a screen too small for the margin keeps retail's rectangle (InsetLayout's rule).
Rect SafeArea(int32_t width, int32_t height, int32_t upperLeftX, int32_t upperLeftY, int32_t lastWidth,
              int32_t lastHeight);

// ---- the Options slider -----------------------------------------------------

// The slider minimum with the row on: the lower of the platform's answer
// (Platform::Display::UiScaleSliderRange) and kUiScaleMinPercent.
float SliderMinimum(float platformMinPercent);

}  // namespace PhoneMenu
}  // namespace Ext
