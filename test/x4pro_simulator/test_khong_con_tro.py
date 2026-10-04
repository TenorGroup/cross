"""X4 Pro: no list has a cursor row, and a held row gets no gray flash.

A tap opens or changes the row; nothing sits "selected". The first row of the file list used to be a
black block; holding it leaves it plain.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30
FIRST_ROW = (20, 130, 460, 185)


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-cursor-') as tmp:
        folder = Path(tmp)
        # File -> sach/: first row at rest, then held (the hold pins it and leaves the list on screen).
        rest, held = run(folder, f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,98;8000:TAP:240,157,900',
                         [7000, 9500], extra_books=3)
        assert ink(rest, FIRST_ROW) < 0.3, f'the first row is a cursor block (ink {ink(rest, FIRST_ROW):.2f})'
        assert ink(held, FIRST_ROW) < 0.3, f'the held row is filled or grayed (ink {ink(held, FIRST_ROW):.2f})'
    print('GREEN: X4 Pro lists without a cursor row')


if __name__ == '__main__':
    main()
