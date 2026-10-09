#!/usr/bin/env python3
"""Hand-drawn size-8 chevrons, mirrored and transposed without resampling."""
from pathlib import Path


LEFT_A = ("....##", "...##.", "..##..", ".##...", "##....",
          ".##...", "..##..", "...##.", "....##")
LEFT_B = ("....#", "...#.", "..#..", ".#...", "#....",
          ".#...", "..#..", "...#.", "....#")


def glyphs(left):
    right = tuple(row[::-1] for row in left)
    up = tuple(''.join(left[row][column] for row in range(9)) for column in range(len(left[0])))
    down = up[::-1]
    return {'Left': left, 'Right': right, 'Up': up, 'Down': down}


def validate():
    for left, thickness in ((LEFT_A, 2), (LEFT_B, 1)):
        assert left == left[::-1]
        assert all(row.count('#') == thickness for row in left)
        assert all('#' * thickness in row for row in left)
        assert [row.index('#') for row in left] == [4, 3, 2, 1, 0, 1, 2, 3, 4]
        masks = glyphs(left)
        assert masks['Left'] == tuple(row[::-1] for row in masks['Right'])
        assert masks['Up'] == masks['Down'][::-1]
        assert all(len(rows) <= 9 and len(rows[0]) <= 9 for rows in masks.values())


def header_parts():
    from gen_back_arrow import emit_glyph
    validate()
    return [emit_glyph(direction + 'Net8', len(rows[0]), len(rows),
                       [[pixel == '#' for pixel in row] for row in rows])
            for direction, rows in glyphs(LEFT_A).items()]


if __name__ == '__main__':
    from PIL import Image
    validate()
    folder = Path(__file__).resolve().parents[1] / 'lib/GfxRenderer/symbol-sources'
    for variant, left in (('A', LEFT_A), ('B', LEFT_B)):
        for direction, rows in glyphs(left).items():
            image = Image.new('1', (len(rows[0]), len(rows)), 1)
            for row, pixels in enumerate(rows):
                for column, pixel in enumerate(pixels):
                    image.putpixel((column, row), int(pixel != '#'))
            image.save(folder / f'CHEV-{direction.lower()}-net8-{variant}.png')
            print(variant, direction, image.size, sum(row.count('#') for row in rows))
