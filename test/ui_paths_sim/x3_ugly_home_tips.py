#!/usr/bin/env python3
"""tenor/ugly on the X3, Home notebook: a tip promises a hold only on the page where the hold does something.

x3_ugly_home_tips.py <simulator_x3_uc8279 program> <output dir>
Holding Select acts on the Recent page only (Pin, Remove from Recent). Starting on Recent, the edge Down button turns to the
other pages; the tip band over the bar must be empty there and written on Recent.
RED on v1.0.58: File promised "Hold Select: browse files" and the others promised pinning and reordering.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30831)
a = ap.parse_args()
out = a.output.resolve()
sd = sim.card(out, files={'/a-sach.txt': b'Mot dong.\n' * 50}, extra={'uiShell': 1, 'uglyStartScreen': 2})
steps = [('00-recent', None)] + [(f'{i:02d}-down', 'DOWN') for i in range(1, 4)]  # File, Favorites, Stats
log, shots = sim.run(a.program, out, sd, steps, a.port)
BAND = (40, 590, 504, 716)  # the tip band over the page hints, right of the notebook's margin rule


def ink(image):
    return sum(1 for v in image.crop(BAND).getdata() if v < 128)


counts = {name: ink(image) for name, image in shots.items()}
print(counts)
assert counts['00-recent'] > 200, 'RED: Recent lost its hold tip'
others = [name for name in counts if name != '00-recent' and counts[name] > 0]
assert not others, f'RED: pages without a hold still promise one: {others}'
print('GREEN')
