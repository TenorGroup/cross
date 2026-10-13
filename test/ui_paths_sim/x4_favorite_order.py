#!/usr/bin/env python3
"""X4 Pro Home Favorites: a held row offers Unpin and Reorder; on the order screen a tap moves the held row, Done keeps
the order and it is still there after a restart, "<" leaves it as it was. Unpin still unpins.

x4_favorite_order.py <simulator_x4pro program> <output dir>
RED on v1.0.58: the held row offered Unpin only, so the X4 Pro could not reorder Favorites (the X3 holds Left or Right).
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
ap.add_argument('--port', type=int, default=30831)
a = ap.parse_args()
out = a.output.resolve()
PINS = ['action/15', 'action/5', 'action/14', 'action/9', 'action/11']
LIST = (0, 40, 480, 400)
FAVORITES = 'TAP:240,754'     # the 3rd card of Home's bar
HOLD_LANGUAGE = 'TAP:240,270,900'  # the 4th row
UNPIN, REORDER = 'TAP:70,335', 'TAP:70,391'  # the menu under the held row
FIRST_ROW = 'TAP:240,80'
DONE, BACK = 'TAP:420,754', 'TAP:46,754'


def card(name):
    sd = sim.card(out / name)
    (sd / '.crosspoint/menu-customization.json').write_text(json.dumps({'version': 1, 'tabs': {}, 'pins': PINS}))
    return sd


def pins(sd):
    return json.loads((sd / '.crosspoint/menu-customization.json').read_text())['pins']


sd = card('order')
log, shots = sim.run(a.program, out / 'order', sd, [('00-fav', FAVORITES), ('01-hold', HOLD_LANGUAGE),
                                                    ('02-order', REORDER), ('03-moved', FIRST_ROW),
                                                    ('04-done', DONE)], a.port)
opened = 'enter ListOrder' in log
print(f'order screen={opened}')
assert opened, 'RED: a held favourite offers no Reorder'
print(f'pins={pins(sd)}')
assert pins(sd) == ['action/9', 'action/15', 'action/5', 'action/14', 'action/11'], 'RED: the new order was not saved'
_, again = sim.run(a.program, out / 'order-restart', sd, [('00-fav', FAVORITES)], a.port)
assert not ImageChops.difference(shots['04-done'].crop(LIST), again['00-fav'].crop(LIST)).getbbox(), \
    'RED: Favorites lost its order after a restart'

sd = card('cancel')
log, shots = sim.run(a.program, out / 'cancel', sd, [('00-fav', FAVORITES), ('01-hold', HOLD_LANGUAGE),
                                                     ('02-order', REORDER), ('03-moved', FIRST_ROW),
                                                     ('04-back', BACK)], a.port)
assert pins(sd) == PINS, f'"<" kept a new order: {pins(sd)}'
assert not ImageChops.difference(shots['00-fav'].crop(LIST), shots['04-back'].crop(LIST)).getbbox(), '"<" changed the list'

sd = card('unpin')
sim.run(a.program, out / 'unpin', sd, [('00-fav', FAVORITES), ('01-hold', HOLD_LANGUAGE), ('02-unpin', UNPIN)], a.port)
assert pins(sd) == ['action/15', 'action/5', 'action/14', 'action/11'], f'Unpin no longer unpins: {pins(sd)}'
print('GREEN')
