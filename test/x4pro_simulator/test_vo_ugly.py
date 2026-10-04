"""X4 Pro keeps the chosen tenor/ugly shell and its touch settings journey.

The limited-edition day gate is shared with the button readers. While offered, a saved ugly shell
boots into the diary and a tap opens its Settings group without changing the saved shell or sleep memo.
X4PRO_PROGRAM selects the integrated X4 Pro simulator.
"""
import sys
from pathlib import Path

from test_thanh_day import PROGRAM

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import Card, entered, notebook_pages

QUOTE, UGLY = 10, 11


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    card = Card(shell=1, sleepScreen=UGLY, uiShellSleepMemo=QUOTE + 1)
    try:
        log, _ = card.run('2000:TAP:60,750;3500:TAP:200,240;5000:QUIT', timeout=120)
        assert entered(log) == ['Boot', 'UglyDiary', 'UglyNotebook'], log[-2000:]
        assert notebook_pages(log)[0] == 3, 'the finger opens the Settings page'
        assert 'group=0' in log, 'the Display group opens in the touch notebook'
        kept = card.settings()
        assert kept['uiShell'] == 1, kept
        assert kept['sleepScreen'] == UGLY, kept
        assert kept['uiShellSleepMemo'] == QUOTE + 1, kept
    finally:
        card.close()
    print('GREEN: X4 Pro keeps tenor/ugly and opens its touch Settings group')


if __name__ == '__main__':
    main()
