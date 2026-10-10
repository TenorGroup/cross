#!/usr/bin/env python3
from pathlib import Path

from PIL import Image

SOURCE_DIR = Path(__file__).resolve().parents[1] / 'lib/GfxRenderer/symbol-sources'
GLYPHS = {
    # Enter: arrowhead from Back v1.0.56 mirrored about the shaft, stroke as thick as Back (3 / 4 px),
    # quarter-ring bend symmetric about its diagonal (R5 / R7), round top cap. Shaft as long as Back's
    # (24 / 31 px) and on the same rows as Back's shaft, so the head sits on the Back head's rows; the box is
    # 13 / 17 rows, from Back's top row down to the foot of the mirrored head.
    'Enter-net14': (24, [
        [(5, 6), (22, 22)],
        [(4, 7), (21, 23)],
        [(3, 7), (21, 23)],
        [(2, 6), (21, 23)],
        [(1, 5), (20, 23)],
        [(0, 22)],
        [(0, 21)],
        [(0, 20)],
        [(1, 5)],
        [(2, 6)],
        [(3, 7)],
        [(4, 7)],
        [(5, 6)],
    ]),
    'Enter-net18': (31, [
        [(7, 9), (28, 29)],
        [(6, 9), (27, 30)],
        [(5, 9), (27, 30)],
        [(4, 9), (27, 30)],
        [(3, 8), (27, 30)],
        [(2, 7), (27, 30)],
        [(1, 6), (26, 30)],
        [(0, 29)],
        [(0, 28)],
        [(0, 27)],
        [(1, 26)],
        [(2, 7)],
        [(3, 8)],
        [(4, 9)],
        [(5, 9)],
        [(6, 9)],
        [(7, 9)],
    ]),

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

# Chevrons for the button strip, drawn by hand pixel by pixel on the arrow bar of Back and Enter. Up runs from
# Enter's top row down to the foot of the shared shaft (8 / 11 rows); Down from the top edge of that shaft to Back's
# bottom row (10 / 12 rows). Same angle as the Back head (45 degrees, every row steps 1 px) and the same stroke: runs
# of 4 / 6 px on the diagonal, 2.83 / 4.24 px across, against 3 / 4 px for Back. The apex is a round tip 2 / 3 px wide
# like the cap of the Back head, the feet are cut flat. Left is drawn the same way and 15 / 19 rows tall, Right is
# Left mirrored.
CHEVRONS = {
    'up-net14': [
        '.......##.......',
        '......####......',
        '.....######.....',
        '....########....',
        '...####..####...',
        '..####....####..',
        '.####......####.',
        '####........####',
    ],
    'down-net14': [
        '####............####',
        '.####..........####.',
        '..####........####..',
        '...####......####...',
        '....####....####....',
        '.....####..####.....',
        '......########......',
        '.......######.......',
        '........####........',
        '.........##.........',
    ],
    'up-net18': [
        '..........###..........',
        '.........#####.........',
        '........#######........',
        '.......#########.......',
        '......###########......',
        '.....######.######.....',
        '....######...######....',
        '...######.....######...',
        '..######.......######..',
        '.######.........######.',
        '######...........######',
    ],
    'down-net18': [
        '######.............######',
        '.######...........######.',
        '..######.........######..',
        '...######.......######...',
        '....######.....######....',
        '.....######...######.....',
        '......######.######......',
        '.......###########.......',
        '........#########........',
        '.........#######.........',
        '..........#####..........',
        '...........###...........',
    ],
    'left-net14': [
        '......#',
        '.....##',
        '....###',
        '...####',
        '..####.',
        '.####..',
        '####...',
        '###....',
        '####...',
        '.####..',
        '..####.',
        '...####',
        '....###',
        '.....##',
        '......#',
    ],
    'left-net18': [
        '........#',
        '.......##',
        '......###',
        '.....####',
        '....#####',
        '...######',
        '..######.',
        '.######..',
        '######...',
        '#####....',
        '######...',
        '.######..',
        '..######.',
        '...######',
        '....#####',
        '.....####',
        '......###',
        '.......##',
        '........#',
    ],
}


def chevron_glyphs():
    glyphs = {}
    for net in (14, 18):
        up, down, left = (CHEVRONS[f'{name}-net{net}'] for name in ('up', 'down', 'left'))
        assert all(row == row[::-1] for row in up + down), f'up or down at {net} is not left-right symmetric'
        assert left == left[::-1], f'left-net{net} is not top-bottom symmetric'
        for name, art in (('up', up), ('down', down), ('left', left), ('right', [row[::-1] for row in left])):
            rows = []
            for line in art:
                spans, start = [], None
                for column, mark in enumerate(line + '.'):
                    if mark == '#' and start is None:
                        start = column
                    elif mark != '#' and start is not None:
                        spans.append((start, column - 1))
                        start = None
                rows.append(spans)
            glyphs[f'CHEV-{name}-net{net}'] = (len(art[0]), rows)
    return glyphs


def main():
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
