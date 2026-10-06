"""X4 Pro, tenor/ugly as scratch paper: a ring keeps, a strike out and back takes away.

A strike forgets a Recent book, takes a favourite off and puts a form's question back to its default; a ring
pins and leaves a pin as it is; on the top band a strike hides the battery or the clock and a ring shows it;
a scribble nobody can read earns a new line of abuse each time (Settings, Text and Status bar forms alike for
the strike); up from the diary is the desk, down from the desk the diary, 120 px or more. Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import re
import sys
from pathlib import Path

from test_thanh_day import PROGRAM

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import Card, entered

RECENT = 'TAP:400,750'                                    # the diary's "next": the Recent page
DESK = 'TAP:240,750'                                      # the notebook's bottom middle: the desk
FAVORITES = 'TAP:360,630'                                 # the desk's heart
HOLD_PIN = ['TAP:240,176,1200', 'TAP:240,176']            # hold row 1, "pin it"
STRIKE_ROW_1 = 'STROKE:400,100,172,380,176,110,182'      # out along row 1 and back


def ring(cx, cy, rx, ry, ms=500):
    pts = []
    for k in range(13):
        import math
        a = 2 * math.pi * k / 12 - math.pi / 2
        pts += [round(cx + rx * math.cos(a)), round(cy + ry * math.sin(a))]
    return f'STROKE:{ms},' + ','.join(map(str, pts))


def run(steps, shots=(), pins=None, **settings):
    """Each step 2.5 s after the one before. Returns (card files, log, shots)."""
    t, items = 2500, []
    for s in steps:
        items.append(f'{t}:{s}')
        t += 2500
    items.append(f'{t + 500}:QUIT')
    card = Card(shell=1, **settings)
    if pins:
        (card.store / 'menu-customization.json').write_text(json.dumps({'version': 1, 'tabs': {}, 'pins': pins}))
    try:
        log, images = card.run(';'.join(items), shots=[(2500 * (i + 1) + 1500, n) for i, n in shots], timeout=120)
        recent = [b['path'] for b in json.loads((card.store / 'recent.json').read_text())['books']]
        f = card.store / 'menu-customization.json'
        pins = json.loads(f.read_text())['pins'] if f.exists() else []
        return dict(recent=recent, pins=pins, settings=card.settings()), log, images
    finally:
        card.close()


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    wrong = []

    files, _, _ = run([RECENT, STRIKE_ROW_1])
    if '/b1.txt' in files['recent'] or len(files['recent']) != 4:
        wrong.append(f'a strike on Recent row 1 left {files["recent"]}')

    files, _, _ = run([RECENT] + HOLD_PIN + [ring(240, 176, 160, 26)])
    if len(files['pins']) != 1:
        wrong.append(f'a ring on a pinned row took the pin off: {files["pins"]}')

    files, _, _ = run([RECENT] + HOLD_PIN + [DESK, FAVORITES, STRIKE_ROW_1])
    if files['pins']:
        wrong.append(f'a strike on Favorites left the pin: {files["pins"]}')

    files, _, _ = run([RECENT, 'STROKE:300,370,24,470,26,375,28'])
    if files['settings'].get('uglyBatteryHidden') != 1:
        wrong.append('a strike over the battery did not hide it')
    files, _, _ = run([RECENT, ring(434, 30, 40, 22)], uglyBatteryHidden=1)
    if files['settings'].get('uglyBatteryHidden') != 0:
        wrong.append('a ring round the battery corner did not bring it back')
    files, _, _ = run([RECENT, 'STROKE:300,270,24,390,26,275,28'], clockShowHeader=2)
    if files['settings'].get('clockShowHeader') != 0:
        wrong.append('a strike over the clock did not hide it')
    files, _, _ = run([RECENT, ring(330, 30, 60, 22)], clockShowHeader=0)
    if files['settings'].get('clockShowHeader') != 1:
        wrong.append('a ring where the clock was did not show the time')

    # Two scribbles nobody can read: two different lines under the title.
    v = 'STROKE:300,180,300,240,420,300,300'
    _, _, shots = run([RECENT, v, v], shots=[(0, 'page'), (1, 'first'), (2, 'second')])
    line = (64, 100, 464, 150)
    a, b, c = (list(shots[n].crop(line).getdata()) for n in ('page', 'first', 'second'))
    if a == b or b == c:
        wrong.append('a scribble did not earn a new line each time')

    # A form's question struck out goes back to its default (the status bar question, second on the sheet).
    files, _, _ = run(['TAP:60,750', 'TAP:200,240', 'STROKE:400,30,380,330,384,40,390'], globalStatusBarMode=1)
    if files['settings'].get('globalStatusBarMode') == 1:
        wrong.append('a strike on a form question left its value')

    # The Text and Status bar forms, opened from a pinned setting: the question struck out goes back too.
    to_pin = [RECENT, DESK, FAVORITES, 'TAP:240,176']
    # A pinned toggle opens ticked over (off by default, so 0 opens as 1); the strike puts it back to 0.
    opened, _, _ = run(to_pin, pins=['text/hyphenationEnabled'], hyphenationEnabled=0)
    files, _, _ = run(to_pin + ['STROKE:400,30,550,330,554,40,560'], pins=['text/hyphenationEnabled'], hyphenationEnabled=0)
    if opened['settings'].get('hyphenationEnabled') != 1 or files['settings'].get('hyphenationEnabled') != 0:
        wrong.append('a strike on a Text form question left its value')
    files, _, _ = run(to_pin + ['STROKE:400,30,480,330,484,40,490'], pins=['status/statusBarClock'], statusBarClock=2)
    if files['settings'].get('statusBarClock') == 2:
        wrong.append('a strike on a Status bar form question left its value')

    # N1: up from the diary is the desk, down from the desk the diary; a short swipe (70 px) is no step.
    _, log, _ = run(['SWIPE:240,520,240,250,250'])
    if entered(log)[-1] != 'UglyDesk':
        wrong.append(f'up from the diary went to {entered(log)[-1]}')
    _, log, _ = run([RECENT, DESK, 'SWIPE:240,300,240,600,250'])
    if entered(log)[-1] != 'UglyDiary':
        wrong.append(f'down from the desk went to {entered(log)[-1]}')
    _, log, _ = run(['SWIPE:240,400,240,330,250'])
    if entered(log)[-1] != 'UglyDiary':
        wrong.append(f'a 70 px swipe up from the diary went to {entered(log)[-1]}')

    assert not wrong, '\n'.join(wrong)
    print('GREEN: ugly scratch paper: strike forgets, unpins, hides, resets; ring pins, keeps, shows; abuse rotates; N1')


if __name__ == '__main__':
    main()
