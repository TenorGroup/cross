"""X4 Pro, tenor/ugly: the Home key and the swipe up from the bottom edge land on the desk from every ugly screen.

From the diary, the notebook and a Settings form (from a Home group or from Favorites) they go to the desk; from a book to the diary; on the desk they do nothing.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import re
import sys
from pathlib import Path

from test_thanh_day import PROGRAM

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import Card, entered

# From boot (the diary): the taps that reach each screen, 2.5 s apart.
REACH = {
    'diary': [], 'recent': ['TAP:400,750'], 'desk': ['TAP:400,750', 'TAP:240,750'],
    'favorites': ['TAP:400,750', 'TAP:240,750', 'TAP:360,630'],
    'form': ['TAP:60,750', 'TAP:200,240'], 'reader': ['TAP:400,750', 'TAP:240,750', 'TAP:240,420'],
    # A pinned setting opens its form from Favorites, outside a Home group.
    'favorite-form': ['TAP:400,750', 'TAP:240,750', 'TAP:360,630', 'TAP:240,176'],
}
PINS = {'favorite-form': ['settings/sleepTimeoutMinutes']}
ACT = {'home': 'HOME', 'swipe': 'SWIPE:240,795,240,600,250'}


def lands(steps, verb, pins=None):
    """The screen in front after `verb`: the last one entered (or resumed) from then on, else the one already there."""
    t, items = 2500, []
    for s in steps:
        items.append(f'{t}:{s}')
        t += 2500
    items += [f'{t}:{verb}', f'{t + 3000}:QUIT']
    card = Card(shell=1)
    if pins:
        (card.store / 'menu-customization.json').write_text(json.dumps({'version': 1, 'tabs': {}, 'pins': pins}))
    try:
        log, _ = card.run(';'.join(items), timeout=120)
    finally:
        card.close()
    after = [m.group(2) or 'popped' for m in re.finditer(r'\[(\d+)\].*?(?:Entering activity: (\w+)|Popped from activity stack)', log)
             if int(m.group(1)) >= t]
    return after[-1] if after else entered(log)[-1]


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    # A book goes back to the diary; every other screen to the desk.
    want = lambda name: 'UglyDiary' if name == 'reader' else 'UglyDesk'
    wrong = [f'{name}/{act} -> {got}' for name, steps in REACH.items() for act, verb in ACT.items()
             if (got := lands(steps, verb, PINS.get(name))) != want(name)]
    assert not wrong, f'{len(wrong)}/{2 * len(REACH)} land off target: ' + '; '.join(wrong)
    print('GREEN: ugly Home key and bottom swipe land on the desk, and on the diary from a book')


if __name__ == '__main__':
    main()
