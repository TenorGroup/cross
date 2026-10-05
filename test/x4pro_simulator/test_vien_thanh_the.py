"""X4 Pro: a list's fade under its last full row stops at the list, the tab bar's ring stays whole.

The bar is taller at the larger text sizes and starts higher; the fade below a list that goes on used to run to the
foot reserve and thinned the bar's top edge. The bar band of a tab opened at its first rows must equal the same band
once the list is scrolled to its end, where no fade is drawn.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, TABS_X, H

SWIPES = ';'.join(f'{5000 + 1200 * i}:SWIPE:240,640,240,300,150' for i in range(6))


def root_files(sach):
    for i in range(30):
        (sach.parent / f'tep-{i:02d}.txt').write_text('Tep thu.\n' * 20)


def bar_top(image):
    # The bar ring's bottom edge is the lowest long dotted row; its top edge sits 30 to 75 rows above it.
    def long(y):
        return sum(1 for x in range(image.width) if image.getpixel((x, y)) < 128) > 150
    bottom = max(y for y in range(H // 2, H) if long(y))
    return min(y for y in range(bottom - 75, bottom - 30) if long(y))


def check_bar_ring(folder, tab, size):
    tap = f'3000:TAP:{TABS_X[tab]},754'
    first, end = run(folder, f'{tap};{SWIPES}', [4600, 12800], settings=dict(uiTextSize=size), write_books=root_files)
    top = bar_top(end)
    band = (0, top, first.width, H)
    assert first.crop(band).tobytes() == end.crop(band).tobytes(), \
        f'tab {tab}, text size {size}: the bar from y={top} differs while the list goes on below its last row'


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='x4pro-bar-ring-') as tmp:
        # Folder (30 files), Stats (its rows under the panel), Recent and Settings (no framed list: controls).
        for tab in (1, 3, 0, 4):
            for size in (0, 1, 2):
                folder = Path(tmp) / f'tab{tab}-size{size}'
                folder.mkdir()
                try:
                    check_bar_ring(folder, tab, size)
                except AssertionError as error:
                    failures.append(str(error))
        assert not failures, '\n'.join(failures)
    print('GREEN: X4 Pro tab bar ring whole under a list that goes on, 4 tabs x 3 text sizes')


if __name__ == '__main__':
    main()
