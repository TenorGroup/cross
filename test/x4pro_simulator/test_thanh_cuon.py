"""X4 Pro reader menu: the scroll bar of a long list stays inside the round frame at the end of the list, and
its thumb has round ends (founder 06/10). Contents of a 24-chapter book, scrolled to its last row."""
from pathlib import Path
import tempfile

from test_thanh_day import run, TABS_X
import test_thanh_dong as td

# readerFrame(screen, 350): x 16..463, y 362..711, corner radius 20.
RIGHT, BOTTOM, R = 464, 712, 20
OPEN = f'3000:TAP:{TABS_X[1]},{td.BAR_Y};5000:TAP:240,68;7000:TAP:240,68;9500:TAP:240,775;12000:TAP:226,754'


def outside_corner(image):
    """Ink in the bottom right corner box beyond the frame's own ring: something poking out of the frame."""
    cx, cy = RIGHT - R, BOTTOM - R
    return [(x, y) for x in range(cx, RIGHT) for y in range(cy, BOTTOM)
            if image.getpixel((x, y)) < 128 and (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2 > (R + 0.5) ** 2]


def thumb_rows(image):
    """Ink count per row of the scroll bar's columns (just left of the frame's right ring)."""
    rows = {}
    for y in range(402, BOTTOM - 2):
        n = sum(1 for x in range(RIGHT - 12, RIGHT - 4) if image.getpixel((x, y)) < 128)
        if n:
            rows[y] = n
    return rows


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-scroll-') as tmp:
        swipes = ';'.join(f'{14000 + i * 1500}:SWIPE:240,650,240,420,200' for i in range(10))
        top, end = run(Path(tmp) / 'toc', OPEN + ';' + swipes, [13500, 30000], write_books=td.toc_book,
                       settings=dict(readerTapTip=0))
        assert thumb_rows(top), 'no scroll bar on a long contents list'
        poke = outside_corner(end)
        assert not poke, f'the scroll bar pokes out of the frame at the end: {poke[:6]}'
        rows = thumb_rows(end)
        ys = sorted(rows)
        middle = max(rows.values())
        assert rows[ys[-1]] < middle and rows[ys[0]] < middle, f'the thumb ends are square: {rows[ys[0]]}/{middle}/{rows[ys[-1]]}'
    print('GREEN: X4 Pro reader menu scroll bar inside the round frame at the end, round thumb ends')


if __name__ == '__main__':
    main()
