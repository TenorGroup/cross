#!/usr/bin/env python3
from pathlib import Path

from PIL import Image

SOURCE_DIR = Path(__file__).resolve().parents[1] / 'lib/GfxRenderer/symbol-sources'
GLYPHS = {
    'D-net14': (24, [
        [(7, 9)], [(6, 9)], [(5, 9)], [(4, 8)], [(4, 7)], [(3, 6)], [(2, 5)],
        [(1, 20)], [(0, 22)], [(0, 23)], [(1, 4), (21, 23)], [(2, 5), (21, 23)],
        [(3, 6), (21, 23)], [(4, 7), (20, 23)], [(5, 8), (14, 22)],
        [(6, 9), (13, 21)], [(7, 9), (14, 19)],
    ]),
    'D-net18': (31, [
        [(9, 12)], [(9, 12)], [(8, 12)], [(7, 12)], [(6, 11)], [(5, 10)], [(4, 9)],
        [(3, 8)], [(2, 7)], [(1, 6)], [(0, 26)], [(0, 28)], [(0, 29)], [(1, 30)],
        [(2, 6), (27, 30)], [(2, 7), (27, 30)], [(3, 8), (27, 30)], [(4, 9), (27, 30)],
        [(5, 10), (26, 30)], [(6, 11), (25, 29)], [(7, 12), (20, 28)],
        [(8, 12), (19, 27)], [(9, 12), (19, 26)], [(10, 12), (20, 24)],
    ]),
    'T3-tick-net14': (22, [
        [(20, 21)], [(19, 21)], [(18, 21)], [(17, 20)], [(16, 19)], [(15, 18)],
        [(14, 17)], [(13, 16)], [(12, 15)], [(0, 1), (11, 14)], [(0, 2), (10, 13)],
        [(0, 3), (9, 12)], [(1, 4), (8, 11)], [(2, 5), (7, 10)], [(3, 9)],
        [(4, 8)], [(5, 7)],
    ]),
    'T3-tick-net18': (29, [
        [(26, 28)], [(25, 28)], [(24, 28)], [(23, 28)], [(22, 27)], [(21, 26)],
        [(20, 25)], [(19, 24)], [(18, 23)], [(17, 22)], [(16, 21)], [(15, 20)],
        [(0, 2), (14, 19)], [(0, 3), (13, 18)], [(0, 4), (12, 17)],
        [(0, 5), (11, 16)], [(1, 6), (10, 15)], [(2, 7), (9, 14)],
        [(3, 13)], [(4, 12)], [(5, 11)], [(6, 11)], [(7, 10)], [(8, 10)],
    ]),
}

# Enter, built from Back so the two arrows match: the head is Back's own pixels (columns left of the tail, rows 0 down
# to the last row of the lower wing, wing included, nothing mirrored) and sits on Back's rows, the shaft is on
# Back's shaft rows. Only the tail differs: Back hooks down, Enter's goes up, with the B2 bend (R5 / R7, round cap).
# The tail rises exactly as far above the shaft as Back's tail falls below it: 7 rows at net 14, 8 at net 18, so the box
# stands 2 / 1 rows above Back's top row. Art below is the tail, from the first column right of the head, from Enter's
# top row down to the bottom row of the shaft.
ENTER_TAILS = {
    14: (8, 2, [
        '..............#.',
        '.............###',
        '.............###',
        '.............###',
        '.............###',
        '.............###',
        '............####',
        '###############.',
        '##############..',
        '#############...',
    ]),
    18: (10, 1, [
        '..................##.',
        '.................####',
        '.................####',
        '.................####',
        '.................####',
        '.................####',
        '.................####',
        '................#####',
        '####################.',
        '###################..',
        '##################...',
        '#################....',
    ]),
}

# Chevrons for the button strip, drawn by hand pixel by pixel. Up runs from Enter's top row down to the bottom row of the
# shared shaft, Down from the top row of that shaft to Back's bottom row: same height, so Down is Up flipped top to
# bottom, point for point. Same angle as the Back head (45 degrees, every row steps 1 px) and the same stroke: runs of
# 4 / 6 px on the diagonal, 2.83 / 4.24 px across, against 3 / 4 px for Back. The apex is a round tip like the cap of the
# Back head, the feet are cut flat. Left is drawn the same way and as tall as the whole box (Enter's top row to Back's
# bottom row), Right is Left mirrored.
CHEVRONS = {
    'up-net14': [
        '.........##.........',
        '........####........',
        '.......######.......',
        '......########......',
        '.....####..####.....',
        '....####....####....',
        '...####......####...',
        '..####........####..',
        '.####..........####.',
        '####............####',
    ],
    'up-net18': [
        '...........###...........',
        '..........#####..........',
        '.........#######.........',
        '........#########........',
        '.......###########.......',
        '......######.######......',
        '.....######...######.....',
        '....######.....######....',
        '...######.......######...',
        '..######.........######..',
        '.######...........######.',
        '######.............######',
    ],
    'left-net14': [
        '.......#',
        '......##',
        '.....###',
        '....####',
        '...####.',
        '..####..',
        '.####...',
        '####....',
        '###.....',
        '####....',
        '.####...',
        '..####..',
        '...####.',
        '....####',
        '.....###',
        '......##',
        '.......#',
    ],
    'left-net18': [
        '.........#',
        '........##',
        '.......###',
        '......####',
        '.....#####',
        '....######',
        '...######.',
        '..######..',
        '.######...',
        '######....',
        '######....',
        '.######...',
        '..######..',
        '...######.',
        '....######',
        '.....#####',
        '......####',
        '.......###',
        '........##',
        '.........#',
    ],
}


def spans_of(line):
    spans, start = [], None
    for column, mark in enumerate(line + '.'):
        if mark == '#' and start is None:
            start = column
        elif mark != '#' and start is not None:
            spans.append((start, column - 1))
            start = None
    return spans


def enter_glyphs():
    glyphs = {}
    for net, (head, above, tail) in ENTER_TAILS.items():
        back = Image.open(SOURCE_DIR / f'B-net{net}.png')
        ink = lambda x, y: back.getpixel((x, y)) == 0
        last = max(y for y in range(back.height) for x in range(head) if ink(x, y))
        lines = []
        for row in range(-above, last + 1):
            line = ['.'] * back.width
            for x in range(head):
                if row >= 0 and ink(x, row):
                    line[x] = '#'
            if row + above < len(tail):
                for index, mark in enumerate(tail[row + above]):
                    if mark == '#':
                        line[head + index] = '#'
            lines.append(''.join(line))
        glyphs[f'Enter-net{net}'] = (back.width, [spans_of(line) for line in lines])
    return glyphs


def chevron_glyphs():
    glyphs = {}
    for net in (14, 18):
        up, left = CHEVRONS[f'up-net{net}'], CHEVRONS[f'left-net{net}']
        assert all(row == row[::-1] for row in up), f'up-net{net} is not left-right symmetric'
        assert left == left[::-1], f'left-net{net} is not top-bottom symmetric'
        for name, art in (('up', up), ('down', up[::-1]), ('left', left), ('right', [row[::-1] for row in left])):
            glyphs[f'CHEV-{name}-net{net}'] = (len(art[0]), [spans_of(line) for line in art])
    return glyphs


def main():
    GLYPHS.update(enter_glyphs())
    GLYPHS.update(chevron_glyphs())
    for name, (width, rows) in GLYPHS.items():
        image = Image.new('1', (width, len(rows)), 1)
        for row, spans in enumerate(rows):
            for start, end in spans:
                assert 0 <= start <= end < width
                for column in range(start, end + 1):
                    image.putpixel((column, row), 0)
        ink = {(column, row) for row in range(image.height) for column in range(width)
               if image.getpixel((column, row)) == 0}
        reached = {next(iter(ink))}
        pending = list(reached)
        while pending:
            column, row = pending.pop()
            for delta_x, delta_y in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                neighbor = column + delta_x, row + delta_y
                if neighbor in ink and neighbor not in reached:
                    reached.add(neighbor)
                    pending.append(neighbor)
        assert reached == ink, name
        assert {column for column, row in ink} >= {0, width - 1}
        assert {row for column, row in ink} >= {0, image.height - 1}
        image.save(SOURCE_DIR / f'{name}.png', optimize=False)
        print(f'{name}: {width}x{image.height}, ink={len(ink)}')


if __name__ == '__main__':
    main()
