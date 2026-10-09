"""X4 Pro: a list has no "more below" arrow, a flick turns a whole page, a slow drag moves the rows dragged.

The scroll bar on the right says there is more, so the arrow and the room kept for it are gone. A
swipe up shows the next page: the first row of the new page is the row after the last of the old one.
Books are named n, nn, nnn... so the width of a row's text says which book it is.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30
NAMES = ['n' * (i + 1) for i in range(22)]
ROW_TOP, ROW_STEP = 50, 62


def text_width(image, row):
    """Right edge of the ink of a row's name (x 70 to 380), less its left edge."""
    top = ROW_TOP + row * ROW_STEP
    xs = [x for x in range(70, 380) if ink(image, (x, top + 18, x + 1, top + 42)) > 0]
    return (max(xs) - min(xs) + 1) if xs else 0


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-list-') as tmp:
        folder = Path(tmp) / 'a'
        folder.mkdir()
        # File -> sach/: 22 books, no tab bar.
        (files,) = run(folder, f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68', [7000], extra_books=NAMES)
        # Under the last row, over the bar: where the arrow sat.
        assert ink(files, (215, 716, 265, 723)) == 0, 'the "more below" arrow is still drawn'
        # Rows above the bar at the foot.
        widths = [text_width(files, r) for r in range(1, (716 - ROW_TOP) // ROW_STEP)]
        shown = sum(1 for w in widths if w > 0)
        assert shown + 1 >= 9, f'only {shown + 1} rows including Search on the first page'
        step = widths[1] - widths[0]
        assert step > 0, f'name widths do not increase: {widths}'
        folder = Path(tmp) / 'b'
        folder.mkdir()
        (turned,) = run(folder, f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;8000:SWIPE:240,600,240,200,150', [10000],
                        extra_books=NAMES)
        first = text_width(turned, 0)
        index = round((first - widths[0]) / step)
        # The edges fade: a flick keeps the last full row as the faded first row of the next page.
        assert index == shown - 1, f'a flick turned to book {index + 1}, not book {shown} (after the Search row)'
        # A slow drag down of 250 px (1.2 s) moves the list back 4 rows (62 px each), not a page (rule 11).
        folder = Path(tmp) / 'c'
        folder.mkdir()
        (dragged,) = run(folder, f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;8000:SWIPE:240,600,240,200,150;'
                                 '11000:SWIPE:240,300,240,550,1200', [14000], extra_books=NAMES)
        moved = index - round((text_width(dragged, 0) - widths[0]) / step)
        assert moved == 4, f'a slow drag of 250 px moved the list {moved} rows, not 4'
    print('GREEN: X4 Pro lists: no more-below arrow, a flick turns a page, a slow drag moves the rows dragged')


if __name__ == '__main__':
    main()
