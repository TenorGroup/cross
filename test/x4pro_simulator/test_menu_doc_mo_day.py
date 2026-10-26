"""X4 Pro reader Text menu (founder 26/10/2026): a list that goes on below fades at its foot, as Settings and the X3's
reader panels do. The 14 text settings do not fit the panel: the row past the last full one shows, faded, and the fade
stays once the scroll bar has hidden itself.

The fade (tenorchrome::fadeBand, Bayer 8x8) keeps no 2x2 block of ink past the top quarter of its band: every 2x2
window of the matrix holds a threshold of 48 or more. An unfaded row's letters are full of such blocks.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run
from test_menu_chu_14 import TEXT_MENU

LIST_FOOT = 716            # the panel's list ends at the frame's foot (frame.y + 40 + 310)
ROW_H = 62
LAST_SLOT = LIST_FOOT - ROW_H  # the 5th slot: the glimpse of the next row
X0, X1 = 24, 456           # inside the frame's sides, clear of the scroll bar


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def blocks(image, y0, y1):
    """2x2 all-ink blocks with their top row in [y0, y1)."""
    return sum(1 for y in range(y0, y1) for x in range(X0, X1 - 1)
               if dark(image, x, y) and dark(image, x + 1, y) and dark(image, x, y + 1) and dark(image, x + 1, y + 1))


def ink(image, y0, y1):
    return sum(1 for y in range(y0, y1) for x in range(X0, X1) if dark(image, x, y))


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-reader-fade-') as tmp:
        # The Text panel just opened (scroll bar shown), then after the bar hid itself.
        opened, idle = run(Path(tmp), TEXT_MENU, [11500, 14500])
        for name, shot in (('opened', opened), ('after the scroll bar hid', idle)):
            glimpse = ink(shot, LAST_SLOT + 2, LIST_FOOT - 16)
            assert glimpse > 50, f'{name}: no row shows past the last full one (ink {glimpse})'
            solid = blocks(shot, LAST_SLOT + ROW_H // 4 + 2, LIST_FOOT - 1)
            assert solid == 0, f'{name}: the row past the last full one is not faded ({solid} solid 2x2 blocks)'
    print('GREEN: X4 Pro reader Text menu fades the row past its last full one, before and after the scroll bar hides')


if __name__ == '__main__':
    main()
