"""tools/ext_icons/phone.py -- white pictograms for the rows added in Android P8: phone_menu and fps_cap
(class A). Programmer art, our own.

Same conventions as loadrows.py: each draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16
tile. '#' = white, 'k' = the dark accent, '.' = leave the tier colour showing.
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


def draw_phone_menu(px):
    """A phone held in landscape (rounded corners, a dark camera cut-out at the left) with the interface
    drawn inside a smaller safe area."""
    _paint(px, [
        "............",
        "............",
        ".##########.",
        "#..........#",
        "#...######.#",
        "#.k.######.#",
        "#...######.#",
        "#...######.#",
        "#..........#",
        ".##########.",
        "............",
        "............",
    ])


def draw_fps_cap(px):
    """A gauge whose dark needle stops short of the top of the dial: a frame-rate ceiling."""
    _paint(px, [
        "............",
        "...######...",
        "..#......#..",
        ".#........#.",
        ".#.......k#.",
        "#.......k..#",
        "#......k...#",
        "#.....#....#",
        "#....###...#",
        "############",
        "............",
        "............",
    ])


ICONS = {
    "phone_menu": ("A", draw_phone_menu),
    "fps_cap": ("A", draw_fps_cap),
}
