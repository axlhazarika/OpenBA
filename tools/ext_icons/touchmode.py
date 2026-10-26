"""tools/ext_icons/touchmode.py -- the white pictogram for the touch_mode row (programmer art, our own).

Same conventions as menuentry.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
'#' = white, 'k' = the dark accent, '.' = leave the tier colour showing.
"""

WHITE = (255, 255, 255, 255)
DARK = (42, 42, 42, 255)


def _paint(px, rows):
    assert len(rows) == 12 and all(len(r) == 12 for r in rows), rows
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                px[2 + i, 2 + j] = WHITE
            elif ch == "k":
                px[2 + i, 2 + j] = DARK


def draw_touch_mode(px):
    """A D-pad (a cross with a dark mark in each arm): the camera pad of the touch controls."""
    _paint(px, [
        "....####....",
        "....#kk#....",
        "....####....",
        "....####....",
        "############",
        "#k########k#",
        "#k########k#",
        "############",
        "....####....",
        "....####....",
        "....#kk#....",
        "....####....",
    ])


ICONS = {
    "touch_mode": ("A", draw_touch_mode),
}
