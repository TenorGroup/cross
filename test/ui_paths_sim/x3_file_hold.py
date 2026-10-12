#!/usr/bin/env python3
"""X3 File: holding Select on a file opens Pin, Rename, Delete; Rename opens the keyboard.

x3_file_hold.py <simulator_x3_uc8279 program> <output dir> [--home]
In the file browser (a folder), or with --home on Home's File card (the card's root).
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
ap.add_argument('--home', action='store_true')
ap.add_argument('--delete', action='store_true', help='Delete (third line) instead of Rename, then confirm')
a = ap.parse_args()
out = a.output.resolve()
folder = '' if a.home else '/sach'
sd = sim.card(out, files={f'{folder}/a-sach.txt': b'Mot dong.\n' * 50, f'{folder}/b-sach.txt': b'Hai dong.\n' * 50})
# Recent -> File tab (edge Down), its first row selected; without --home that row is the folder /sach and Select
# opens it in the file browser. Then hold Select on the first file; Right to Rename, Select.
steps = [('00-home', None), ('01-tab-file', 'DOWN')] + ([] if a.home else [('02-browser', 'CONFIRM')])
if a.delete:
    log, shots = sim.run(a.program, out, sd, steps + [('03-hold', 'CONFIRM:900'), ('04-right', 'RIGHT'),
                                                      ('05-right', 'RIGHT'), ('06-confirm', 'CONFIRM'), ('07-up', 'LEFT'),
                                                      ('08-yes', 'CONFIRM')], a.port)
    gone = not (sd / folder.lstrip('/') / 'a-sach.txt').exists()
    print(f'pinned={"pin select" in log} confirmation={"enter Confirmation" in log} gone={gone}')
    assert 'pin select' not in log and 'enter Confirmation' in log and gone, 'RED: hold Select did not delete'
    print('GREEN')
    sys.exit(0)
log, shots = sim.run(a.program, out, sd, steps + [('03-hold', 'CONFIRM:900'), ('04-right', 'RIGHT'),
                                                  ('05-confirm', 'CONFIRM')], a.port)
pinned = 'pin select' in log
menu = log.count('[DIALOG] popup box') >= 1
keyboard = 'enter KeyboardEntry' in log
print(f'pinned={pinned} menu={menu} keyboard={keyboard}')
assert not pinned and menu and keyboard, 'RED: hold Select on a file did not lead to Rename'
print('GREEN')
