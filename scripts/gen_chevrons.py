#!/usr/bin/env python3
"""Generate the footer's 1-bit chevrons with Pillow."""
from pathlib import Path

from PIL import Image, ImageDraw

SOURCE_DIR = Path(__file__).resolve().parents[1] / 'lib/GfxRenderer/symbol-sources'
SUPERSAMPLE = 8


def chevron(width, height, stroke):
    canvas = Image.new('L', (width * SUPERSAMPLE, height * SUPERSAMPLE), 0)
    draw = ImageDraw.Draw(canvas)
    radius = stroke * SUPERSAMPLE / 2
    wide, tall = canvas.size
    points = [(radius, tall - radius), (wide / 2, radius), (wide - radius, tall - radius)]
    draw.line(points, fill=255, width=stroke * SUPERSAMPLE, joint='curve')
    for point_x, point_y in points:
        draw.ellipse((point_x - radius, point_y - radius, point_x + radius, point_y + radius), fill=255)
    small = canvas.resize((width, height), Image.Resampling.LANCZOS)
    return small.point(lambda value: 0 if value >= 90 else 255, mode='1')


def main():
    for net, width, height, stroke in ((14, 18, 10, 3), (18, 24, 13, 4)):
        up = chevron(width, height, stroke)
        glyphs = {'up': up, 'down': up.transpose(Image.Transpose.ROTATE_180),
                  'left': up.transpose(Image.Transpose.ROTATE_90),
                  'right': up.transpose(Image.Transpose.ROTATE_270)}
        for direction, glyph in glyphs.items():
            ink = glyph.convert('L').point(lambda value: 255 if value == 0 else 0)
            assert ink.getbbox() == (0, 0, glyph.width, glyph.height)
            count = sum(value == 0 for value in glyph.getdata())
            assert count * 100 < glyph.width * glyph.height * 50
            path = SOURCE_DIR / f'CHEV-{direction}-net{net}.png'
            glyph.save(path, optimize=False)
            print(f'{path.name}: {glyph.width}x{glyph.height}, stroke={stroke}, ink={count}')


if __name__ == '__main__':
    main()
