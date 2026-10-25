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
// dialog, whose backdrop is a 16:9 overlay: the Banished logo and the dark torn
// borders, transparent between them. On a screen WIDER than the overlay (a
// 19.5:9 phone) retail's centred cover fit cuts the logo's top off; there the
// menu becomes a logo-left layout: the overlay drawn larger and against the
// right edge so the logo sits whole on the left, and the button column in the
// space right of the logo. On any other screen (a desktop's 16:9 or 16:10) the
// overlay keeps retail's cover fit and the column stays centred, kept inside the
// safe area and clear of the logo and the version line.
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
// backdrop with the logo elsewhere only moves the centred layout's band.
constexpr float kLogoBottomFraction = 0.22f;

// The gap kept between the button column and the version line (layout units).
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

// ---- the logo-left layout (a screen wider than the overlay) ----------------

// Where the logo's centre line is put, as a fraction of the screen width from its
// left edge: left of centre by enough that the column fits right of the logo at
// the phone's UI scales, while the overlay is drawn at most 1.28x wider than the
// screen (its upscale stays near retail's own cover fit on a 19.5:9 phone).
constexpr float kLogoCentreFraction = 0.36f;

// The logo's horizontal extent inside the overlay, as fractions of the drawn
// overlay's width (measured on the installed overlay: 0.330 .. 0.675, centred on
// the overlay's own centre line), with about one percent of air on the right.
constexpr float kLogoRightFraction = 0.685f;

// Retail draws a dialog's backdrop "cover" fitted and centred: on a screen wider
// than the overlay it is scaled to the width and the same amount is cut off its
// top and bottom, which cuts into the logo near the overlay's top. This draws the
// overlay 2 * (1 - kLogoCentreFraction) screen widths wide, its right edge on the
// screen's right edge and its top on the screen's top, so the logo is whole and
// centred on kLogoCentreFraction of the width, and the overlay still covers the
// screen. A rectangle that does not overhang the top (a screen not wider than the
// overlay) is returned unchanged.
Rect LogoLeft(const Rect& retail, int32_t width);

// The logo's right edge (layout units) in an overlay drawn at `overlay`.
int32_t LogoRight(const Rect& overlay);

// The button column's layout rectangle right of the logo (`logoRight`): centred
// in the space between the logo and the safe area's right edge when the column
// (`columnWidth`, last frame's; <= 0 when unknown) fits there; otherwise
// right-aligned on the safe margin (overlapping the logo as little as it can).
Rect ColumnBeside(int32_t width, int32_t height, int32_t logoRight, int32_t columnWidth);

// The version line's layout rectangle in the logo-left layout: centred on the logo's
// centre line (`logoCentre`), on the safe area's bottom edge.
Rect VersionUnderLogo(int32_t width, int32_t height, int32_t logoCentre);

// ---- the centred layout (any other screen) ---------------------------------

// The band the main menu's button column is centred in, on a width x height screen:
//   * by default the safe area (InsetLayout);
//   * below the logo (`logoBottom`, < 0 when unknown) and above the version line
//     (`versionTop` less kVersionGap, < 0 when unknown), when the column (`columnHeight`, its
//     last laid-out height, <= 0 when unknown) fits there: at a large UI scale the centred
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
//   * an axis on which the dialog (last frame's laid-out `lastWidth` / `lastHeight`; <= 0 when
//     not laid out yet) is wider than the safe area keeps retail's whole extent (0 .. width): the
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
