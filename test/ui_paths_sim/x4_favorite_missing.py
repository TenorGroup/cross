#!/usr/bin/env python3
"""X4 Pro Favorites: opening a pin whose file is gone says so in words the touch shell shows.

x4_favorite_missing.py <simulator_x4pro program> <output dir>
Run 1 pins /a-sach.txt from the File card (touch hold, Pin); the file is then removed; run 2 taps the pin on
the Favorites card. The message must be drawn over the bar.
RED on v1.0.58: the message names the Select button, and the touch shell hides every line naming a button,
so the tap showed nothing.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30821)
a = ap.parse_args()
out = a.output.resolve()
sd = sim.card(out, files={'/a-sach.txt': b'Mot dong.\n' * 50})
sim.run(a.program, out / 'ghim', sd, [('00-home', None), ('01-tab-file', 'TAP:150,754'), ('02-hold', 'TAP:240,90,900'),
                                      ('03-pin', 'TAP:55,150')], a.port)
(sd / 'a-sach.txt').unlink()
log, shots = sim.run(a.program, out, sd, [('00-home', None), ('01-tab-favorites', 'TAP:240,754'),
                                          ('02-open', 'TAP:240,90')], a.port)
band = (16, 560, 464, 716)  # over the bar, where the message goes
ink = sum(1 for v in shots['02-open'].crop(band).getdata() if v < 128)
before = sum(1 for v in shots['01-tab-favorites'].crop(band).getdata() if v < 128)
print(f'dark pixels over the bar: before={before} after the tap={ink}')
assert ink > before + 200, 'RED: tapping a missing pin shows nothing'
print('GREEN')
