"""X4 Pro, tenor/ugly: a device already in the shell when this build arrives shows the clock, once.

A settings file saved before the build carries no uiShellClockOnce. In tenor/ugly such a file with the header
clock hidden shows the time (the shell's default) and remembers it turned it on, so going back to tenor/cross hides it
again; a clock the user had set stays as it is. The flag then says it is done: a clock hidden after that stays hidden.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import sys
from pathlib import Path

from test_thanh_day import PROGRAM
from test_ugly_giay_nhap import RECENT, run

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import ink

STRIKE_BATTERY = 'STROKE:300,370,24,470,26,375,28'  # saves the settings
CLOCK_BOX = (200, 8, 410, 70)


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    wrong = []

    # Saved by the build before: no flag. The hidden clock is on, remembered, and the flag is set.
    files, _, shots = run([RECENT, STRIKE_BATTERY], shots=[(0, 'page')], uiShellClockOnce=0, clockShowHeader=0)
    s = files['settings']
    if (s.get('clockShowHeader'), s.get('uiShellClockOnce'), s.get('uiShellClockMemo')) != (1, 1, 1):
        wrong.append(f'a legacy hidden clock in ugly saved {s.get("clockShowHeader")}/{s.get("uiShellClockOnce")}/{s.get("uiShellClockMemo")}, not 1/1/1')
    legacy_ink = ink(shots['page'], CLOCK_BOX)

    # The flag is set: the user's hidden clock stays hidden, and nothing was drawn there.
    files, _, shots = run([RECENT, STRIKE_BATTERY], shots=[(0, 'page')], uiShellClockOnce=1, clockShowHeader=0)
    s = files['settings']
    if (s.get('clockShowHeader'), s.get('uiShellClockMemo', 0)) != (0, 0):
        wrong.append('a clock hidden after the flag came on again')
    if ink(shots['page'], CLOCK_BOX) >= legacy_ink:
        wrong.append(f'the clock box has as much ink with the flag ({ink(shots["page"], CLOCK_BOX)}) as without ({legacy_ink})')

    # A clock the user set (time and date) stays; only the flag is set, and nothing is remembered.
    files, _, _ = run([RECENT, STRIKE_BATTERY], uiShellClockOnce=0, clockShowHeader=2)
    s = files['settings']
    if (s.get('clockShowHeader'), s.get('uiShellClockOnce'), s.get('uiShellClockMemo', 0)) != (2, 1, 0):
        wrong.append('a clock the user set was changed by the one-time turn on')

    assert not wrong, '; '.join(wrong)
    print('GREEN: a legacy ugly device shows the clock once, and the flag keeps a later choice')


if __name__ == '__main__':
    main()
