#!/usr/bin/env python3
"""X3 File: holding Select on a file opens Pin, Rename, Delete; Rename opens the keyboard.

x3_file_hold.py <simulator_x3_uc8279 program> <output dir>
RED on v1.0.58: the generic hold pinned the file at 700 ms, so the actions menu (and Rename) never opened.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30801)
a = ap.parse_args()
out = a.output.resolve()
sd = sim.card(out, files={'/sach/a-sach.txt': b'Mot dong.\n' * 50, '/sach/b-sach.txt': b'Hai dong.\n' * 50})
# Recent -> File tab (edge Down), its first row is the folder /sach; Select opens it in the file browser; hold
# Select on the first file; Right to Rename, Select.
log, shots = sim.run(a.program, out, sd, [('00-home', None), ('01-tab-file', 'DOWN'), ('02-browser', 'CONFIRM'),
                                          ('03-hold', 'CONFIRM:900'), ('04-right', 'RIGHT'), ('05-confirm', 'CONFIRM')],
                     a.port)
pinned = 'pin select' in log
menu = log.count('[DIALOG] popup box') >= 1
keyboard = 'enter KeyboardEntry' in log
print(f'pinned={pinned} menu={menu} keyboard={keyboard}')
assert not pinned and menu and keyboard, 'RED: hold Select on a file did not lead to Rename'
print('GREEN')
