#!/usr/bin/env python3
"""X4 Pro: a held card of Home's bar (and of the text settings' bar) offers Reorder; on the order screen a tap moves
the held card, Done keeps the order and it is still there after a restart, "<" leaves it as it was.

x4_tab_order.py <simulator_x4pro program> <output dir>
RED on v1.0.58: a held card did nothing, so the X4 Pro had no way to reorder the cards (the X3 holds an edge key).
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import ImageChops

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30821)
a = ap.parse_args()
out = a.output.resolve()
PINS = ['action/15', 'action/5', 'action/14', 'action/9', 'action/11']
BAR = (0, 720, 480, 790)
HOLD_FILE = 'TAP:150,754,900'  # the 2nd card of Home's bar
REORDER = 'TAP:160,681'        # the one line of the menu over the held card
FIRST_ROW = 'TAP:240,80'
DONE = 'TAP:420,754'           # the pill at the bar's right end
BACK = 'TAP:46,754'


def card(name):
    sd = sim.card(out / name)
    (sd / '.crosspoint/menu-customization.json').write_text(json.dumps({'version': 1, 'tabs': {}, 'pins': PINS}))
    return sd


def tabs(sd):
    return json.loads((sd / '.crosspoint/menu-customization.json').read_text())['tabs']


# Home: File moved to the front, kept, and kept after a restart.
sd = card('home')
log, shots = sim.run(a.program, out / 'home', sd, [('00-home', None), ('01-hold', HOLD_FILE), ('02-order', REORDER),
                                                   ('03-moved', FIRST_ROW), ('04-done', DONE)], a.port)
opened = 'enter ListOrder' in log
print(f'order screen={opened}')
assert opened, 'RED: a held card offers no Reorder'
saved = tabs(sd).get('home')
print(f'home={saved}')
assert saved[:5] == [1, 0, 4, 2, 3], f'RED: Home order not saved as File first: {saved}'
assert ImageChops.difference(shots['00-home'].crop(BAR), shots['04-done'].crop(BAR)).getbbox(), 'RED: the bar did not change'
_, again = sim.run(a.program, out / 'home-restart', sd, [('00-home', None)], a.port)
assert not ImageChops.difference(shots['04-done'].crop(BAR), again['00-home'].crop(BAR)).getbbox(), \
    'RED: the bar lost its order after a restart'

# "<" leaves the order as it was.
sd = card('cancel')
log, shots = sim.run(a.program, out / 'cancel', sd, [('00-home', None), ('01-hold', HOLD_FILE), ('02-order', REORDER),
                                                     ('03-moved', FIRST_ROW), ('04-back', BACK)], a.port)
assert 'enter ListOrder' in log, 'RED: a held card offers no Reorder'
assert tabs(sd).get('home', [0, 1, 4, 2, 3])[:5] == [0, 1, 4, 2, 3], f'"<" kept a new order: {tabs(sd)}'
assert not ImageChops.difference(shots['00-home'].crop(BAR), shots['04-back'].crop(BAR)).getbbox(), '"<" changed the bar'

# Text settings: its Size card moved to the front of its 4.
sd = card('text')
log, shots = sim.run(a.program, out / 'text', sd, [('00-fav', 'TAP:240,754'), ('01-text', 'TAP:240,330'),
                                                   ('02-hold', 'TAP:226,754,900'), ('03-order', 'TAP:250,681'),
                                                   ('04-moved', FIRST_ROW), ('05-done', DONE)], a.port)
assert 'enter ListOrder' in log, 'RED: a held text settings card offers no Reorder'
saved = tabs(sd).get('text')
print(f'text={saved}')
assert saved == [4, 1, 0, 2, 3], f'text settings order not saved as Size first: {saved}'
assert ImageChops.difference(shots['01-text'].crop(BAR), shots['05-done'].crop(BAR)).getbbox(), 'text bar did not change'
print('GREEN')
