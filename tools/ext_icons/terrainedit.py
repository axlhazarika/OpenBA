"""tools/ext_icons/terrainedit.py -- the white pictogram for the terrain_edit row (programmer art, our own).

Same conventions as camera.py: draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16 tile.
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


def draw_terrain_edit(px):
    """A hill rising out of flat ground with an arrow above it: the brush raises the terrain."""
    _paint(px, [
        ".....##.....",
        "....####....",
        "...######...",
        ".....##.....",
        ".....##.....",
        "............",
        ".....##.....",
        "....####....",
        "..########..",
        ".##########.",
        "############",
        "kkkkkkkkkkkk",
    ])


ICONS = {
    "terrain_edit": ("B", draw_terrain_edit),
}
