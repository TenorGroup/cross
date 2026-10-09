"""X4 Pro Contents: the external scrollbar stays in its track and has round ends."""
from pathlib import Path
import tempfile

from test_thanh_day import run, TABS_X
import test_thanh_dong as td

# readerFrame(screen, 350): x 16..463, y 362..711, corner radius 20.
RIGHT, BOTTOM, R = 464, 712, 20
OPEN = f'3000:TAP:{TABS_X[1]},{td.BAR_Y};5000:TAP:240,68;7000:TAP:240,130;9500:TAP:240,775;12000:TAP:226,754'


def outside_corner(image):
    """Ink in the bottom right corner box beyond the frame's own ring: something poking out of the frame."""
    cx, cy = RIGHT - R, BOTTOM - R
    return [(x, y) for x in range(cx, RIGHT) for y in range(cy, BOTTOM)
            if image.getpixel((x, y)) < 128 and (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2 > (R + 0.5) ** 2]


def thumb_rows(image):
    """Ink count per row of the external scrollbar."""
    rows = {}
    for y in range(402, BOTTOM):
        n = sum(1 for x in range(469, 475) if image.getpixel((x, y)) < 128)
        if n:
            rows[y] = n
    return rows


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-scroll-') as tmp:
        swipes = ';'.join(f'{14000 + i * 1500}:SWIPE:240,650,240,420,200' for i in range(10))
        top, end, hidden = run(Path(tmp) / 'toc', OPEN + ';' + swipes, [13000, 28700, 30700], write_books=td.toc_book,
                       settings=dict(readerTapTip=0))
        assert thumb_rows(top), 'no scroll bar on a long contents list'
        poke = outside_corner(end)
        assert not poke, f'the scroll bar pokes out of the frame at the end: {poke[:6]}'
        rows = thumb_rows(end)
        ys = sorted(rows)
        middle = max(rows.values())
        assert rows[ys[-1]] < middle and rows[ys[0]] < middle, f'the thumb ends are square: {rows[ys[0]]}/{middle}/{rows[ys[-1]]}'
        assert not thumb_rows(hidden), 'the Contents scrollbar remained after 2000 ms'
    print('GREEN: X4 Pro Contents external scrollbar, clear frame corner, round ends and idle hide')


if __name__ == '__main__':
    main()
