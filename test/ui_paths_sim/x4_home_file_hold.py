#!/usr/bin/env python3
"""X4 Pro Home File card: a touch hold on a file opens Pin, Rename, Delete anchored to the row; Rename opens the keyboard.

x4_home_file_hold.py <simulator_x4pro program> <output dir>
RED on v1.0.58: the hold offered Pin only, so a file at the card's root could not be renamed or deleted on the device.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30811)
a = ap.parse_args()
out = a.output.resolve()
sd = sim.card(out, files={'/a-sach.txt': b'Mot dong.\n' * 50, '/b-sach.txt': b'Hai dong.\n' * 50})
FILE_TAB = 'TAP:150,754'
FIRST_ROW = (240, 90)
# The row menu (Pin, Rename, Delete) hangs from the held row: Rename is its second line.
RENAME = (75, 207)
log, shots = sim.run(a.program, out, sd, [('00-home', None), ('01-tab-file', FILE_TAB),
                                          ('02-hold', f'TAP:{FIRST_ROW[0]},{FIRST_ROW[1]},900'),
                                          ('03-rename', f'TAP:{RENAME[0]},{RENAME[1]}')], a.port)
keyboard = 'enter KeyboardEntry' in log
print(f'keyboard={keyboard}')
assert keyboard, 'RED: the held file has no Rename'
print('GREEN')
