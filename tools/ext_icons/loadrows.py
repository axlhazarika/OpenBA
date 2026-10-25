"""tools/ext_icons/loadrows.py -- white pictograms for the rows that had none: rock_seat, terrain_resort,
large_packages, boot_device_lost (fix tier) and decode_ahead, async_gpu_create, stream_cache,
visit_replay, find_fastpath, heap_slack, texture_quality (class A). Programmer art, our own.

Same conventions as hitch.py: each draw(px) paints the 12 x 12 interior (x, y in [2, 13]) of a 16 x 16
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


def draw_rock_seat(px):
    """A rock sitting on a downhill slope line: the rock rests on the ground, not above it."""
    _paint(px, [
        "............",
        "............",
        "....###.....",
        "...#####....",
        "..#######...",
        "##k######...",
        "..##k####...",
        "....######..",
        "......##.###",
        "........##..",
        "..........##",
        "............",
    ])


def draw_terrain_resort(px):
    """A flat tile with a dark tick and no mark on it: ground that stays buildable."""
    _paint(px, [
        "............",
        "..########..",
        "..#......#..",
        "..#.....k#..",
        "..#....k.#..",
        "..#k..k..#..",
        "..#.kk...#..",
        "..#..k...#..",
        "..#......#..",
        "..########..",
        "............",
        "............",
    ])


def draw_large_packages(px):
    """A big box with a wide lid and a dark tape strip: a package bigger than usual."""
    _paint(px, [
        "............",
        "####kk######",
        "####kk######",
        "kkkkkkkkkkkk",
        ".###kk#####.",
        ".###kk#####.",
        ".###kk#####.",
        ".###kk#####.",
        ".###kk#####.",
        ".###kk#####.",
        ".###kk#####.",
        "............",
    ])


def draw_boot_device_lost(px):
    """A screen with a circular reset arrow on it: the graphics device resets and the game goes on."""
    _paint(px, [
        "############",
        "#..........#",
        "#...####...#",
        "#..#....##.#",
        "#..#...###.#",
        "#..#.......#",
        "#...####...#",
        "#..........#",
        "############",
        "....####....",
        "..########..",
        "............",
    ])


def draw_decode_ahead(px):
    """Two lanes of blocks running ahead on their own: decoding on other cores before it is needed."""
    _paint(px, [
        "........#...",
        "###.###..#..",
        "###.###...#.",
        "###.###..#..",
        "........#...",
        "............",
        "........#...",
        "###.###..#..",
        "###.###...#.",
        "###.###..#..",
        "........#...",
        "............",
    ])


def draw_async_gpu_create(px):
    """A GPU chip with pins and a plus on it: graphics resources created on the side."""
    _paint(px, [
        "...#.#.#....",
        "..########..",
        ".##########.",
        "..###kk###..",
        ".##kkkkkk##.",
        "..#kkkkkk#..",
        ".##kkkkkk##.",
        "..###kk###..",
        ".##########.",
        "..########..",
        "...#.#.#....",
        "............",
    ])


def draw_stream_cache(px):
    """A disk-drum (database) stack: the decoded data kept on disk."""
    _paint(px, [
        "............",
        "...######...",
        "..########..",
        "..########..",
        "..kkkkkkkk..",
        "..########..",
        "..########..",
        "..kkkkkkkk..",
        "..########..",
        "..########..",
        "...######...",
        "............",
    ])


def draw_visit_replay(px):
    """A parent node linked to two child nodes: the remembered resource links."""
    _paint(px, [
        "............",
        ".....###....",
        ".....#k#....",
        ".....###....",
        "....#...#...",
        "...#.....#..",
        "..#.......#.",
        ".###.....###",
        ".#k#.....#k#",
        ".###.....###",
        "............",
        "............",
    ])


def draw_find_fastpath(px):
    """A magnifier with a dark bolt in its lens: the lookup that answers fast."""
    _paint(px, [
        "......####..",
        ".....#...k#.",
        ".....#..kk#.",
        ".....#.kk.#.",
        ".....#kk..#.",
        "......####..",
        "....##......",
        "...##.......",
        "..##........",
        ".##.........",
        "............",
        "............",
    ])


def draw_heap_slack(px):
    """Memory blocks with one dashed block of slack kept on top: freed memory held ready."""
    _paint(px, [
        "............",
        "#.#.#.#.#.#.",
        "#..........#",
        "............",
        "#.#.#.#.#.#.",
        "............",
        "############",
        "############",
        "kkkkkkkkkkkk",
        "############",
        "############",
        "............",
    ])


def draw_texture_quality(px):
    """A tile with a fine checker on one half and a coarse checker on the other: reduced resolution."""
    rows = []
    for y in range(12):
        row = ""
        for x in range(12):
            if y in (0, 11) or x in (0, 11):
                row += "#"
            elif x < 6:
                row += "#" if (x + y) % 2 == 0 else "."
            else:
                row += "#" if ((x // 2) + (y // 2)) % 2 == 0 else "."
        rows.append(row)
    _paint(px, rows)


ICONS = {
    "rock_seat": ("fix", draw_rock_seat),
    "terrain_resort": ("fix", draw_terrain_resort),
    "large_packages": ("fix", draw_large_packages),
    "boot_device_lost": ("fix", draw_boot_device_lost),
    "decode_ahead": ("A", draw_decode_ahead),
    "async_gpu_create": ("A", draw_async_gpu_create),
    "stream_cache": ("A", draw_stream_cache),
    "visit_replay": ("A", draw_visit_replay),
    "find_fastpath": ("A", draw_find_fastpath),
    "heap_slack": ("A", draw_heap_slack),
    "texture_quality": ("A", draw_texture_quality),
}
