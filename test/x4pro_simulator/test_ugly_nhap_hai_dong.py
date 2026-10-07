"""X4 Pro, tenor/ugly: the line under a notebook page's title holds 2 lines where the page has room, and keeps 1 where it has none.

The hint under Recent, Folder and Favorites is 2 lines. A page of 7 rows or fewer pushes its rows down 26 px for the
second line (every row still reached by finger, the last one above the foot); a page of 8 rows has no room and keeps 1 line.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import sys
from pathlib import Path

from test_thanh_day import PROGRAM
from test_ugly_giay_nhap import RECENT, run

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import entered, ink

STACK = ['TAP:240,670', 'TAP:120,620']  # from the diary: the desk, the stack of books (the card's root)
SECOND_LINE = (64, 150, 464, 160)      # empty when the rows are not pushed: the first line ends at 148, the first row begins at 161
GAP = 'TAP:240,160'                    # between the second line and the first row once pushed; the first row otherwise


def opened(log):
    return 'TxtReader' in entered(log)


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    wrong = []

    # Recent, 4 rows (the open book is not listed): the second line is there, the gap holds no row, the last row is reached.
    _, log, shots = run([RECENT], shots=[(0, 'page')])
    if not ink(shots['page'], SECOND_LINE):
        wrong.append('Recent (4 rows) shows no second line')
    _, log, _ = run([RECENT, GAP])
    if opened(log):
        wrong.append('Recent (4 rows): a tap between the second line and the first row opened a book')
    _, log, _ = run([RECENT, 'TAP:240,200'])
    if not opened(log):
        wrong.append('Recent (4 rows): the first row is not under the finger at y 200')
    _, log, _ = run([RECENT, 'TAP:240,%d' % (144 + 26 + 3 * 64 + 32)])
    if not opened(log):
        wrong.append('Recent (4 rows): the last row is not under the finger')

    # Folder, 7 rows (5 books and 2 more): pushed, the last row above the foot and reached.
    seven = ('x0.txt', 'x1.txt')
    _, log, shots = run(STACK, shots=[(1, 'page')], files=seven)
    if not ink(shots['page'], SECOND_LINE):
        wrong.append('Folder (7 rows) shows no second line')
    if not ink(shots['page'], (64, 574, 464, 602)):
        wrong.append('Folder (7 rows): the last row is not drawn where it was pushed to')
    _, log, _ = run(STACK + ['TAP:240,%d' % (144 + 26 + 6 * 64 + 32)], files=seven)
    if not opened(log):
        wrong.append('Folder (7 rows): the last row is not under the finger')

    # Folder, 8 rows: no room, one line, the first row where it always was.
    eight = ('x0.txt', 'x1.txt', 'x2.txt')
    _, log, shots = run(STACK, shots=[(1, 'page')], files=eight)
    if ink(shots['page'], SECOND_LINE):
        wrong.append('Folder (8 rows) shows a second line over the rows')
    _, log, _ = run(STACK + [GAP], files=eight)
    if not opened(log):
        wrong.append('Folder (8 rows): the first row moved')
    _, log, _ = run(STACK + ['TAP:240,%d' % (144 + 7 * 64 + 32)], files=eight)
    if not opened(log):
        wrong.append('Folder (8 rows): the last row is not under the finger')

    assert not wrong, '; '.join(wrong)
    print('GREEN: a notebook page holds a 2-line reminder where it has room and keeps 1 line where it has none')


if __name__ == '__main__':
    main()
