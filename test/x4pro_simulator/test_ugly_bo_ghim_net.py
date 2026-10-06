"""X4 Pro, tenor/ugly: an X drawn over a Favorites row takes it off Favorites.

A Recent book is pinned with the hold menu, then crossed out on the Favorites page: no pin is left.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import sys
from pathlib import Path

from test_thanh_day import PROGRAM

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import Card

PIN = ['TAP:400,750', 'TAP:240,176,1200', 'TAP:240,176']  # diary -> Recent, hold row 1, "pin"
TO_FAVORITES = ['TAP:240,750', 'TAP:360,630']               # the desk, then its Favorites object
X_ROW_1 = 'SWIPE:170,150,320,200,250;{t2}:SWIPE:170,200,320,150,250'  # two slanted arms crossing on row 1


def pins(steps):
    t, items = 2500, []
    for s in steps:
        items.append(f'{t}:{s.format(t2=t + 450)}')
        t += 2500
    items.append(f'{t + 500}:QUIT')
    card = Card(shell=1)
    try:
        card.run(';'.join(items), timeout=120)
        return json.loads((card.store / 'menu-customization.json').read_text())['pins']
    finally:
        card.close()


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    assert len(pins(PIN + TO_FAVORITES)) == 1, 'the hold menu did not pin the book'
    left = pins(PIN + TO_FAVORITES + [X_ROW_1])
    assert left == [], f'an X over the Favorites row left it pinned: {left}'
    print('GREEN: an X takes a row off Favorites')


if __name__ == '__main__':
    main()
