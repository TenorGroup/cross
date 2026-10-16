#!/usr/bin/env python3
"""File browsing journeys with fixed Home references and visible row checks."""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).absolute().parents[1] / 'ui_paths_sim'))
import sim

REFERENCES = Path(__file__).parent / 'reference'


def pin(sd, path):
    value = 14695981039346656037
    for byte in path.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    key = f'folder/{value ^ 1:016x}'
    store = sd / '.crosspoint'
    (store / 'favorite-files').mkdir(exist_ok=True)
    (store / 'favorite-files' / (key[7:] + '.txt')).write_text(path)
    (store / 'menu-customization.json').write_text(json.dumps({'version': 1, 'tabs': {}, 'pins': [key]}))


def fixture(out, case, touch):
    files = {f'/books/folder-{i:02d}/book.txt': b'Test.\n' for i in range(18)}
    if case == 'reopen':
        files = {'/a/book.txt': b'A\n', '/books/child/leaf/book.txt': b'B\n', '/z/book.txt': b'Z\n'}
    elif case == 'favorite':
        files = {'/books/child/leaf/book.txt': b'Test.\n'}
    sd = sim.card(out, files=files, extra={'statusBarClock': 0, 'clockShowInHeader': 0})
    if case == 'favorite':
        pin(sd, '/books')
        if touch:
            steps = [('home', None), ('favorite', 'TAP:240,754'), ('books', 'TAP:130,80'),
                     ('child', 'TAP:130,80'), ('leaf', 'TAP:130,80'), ('back1', 'TAP:46,754'),
                     ('back2', 'TAP:46,754'), ('result', 'TAP:46,754')]
        else:
            steps = [('home', None), ('file', 'DOWN'), ('favorite', 'DOWN'), ('books', 'CONFIRM'),
                     ('child', 'CONFIRM'), ('leaf', 'CONFIRM'), ('back1', 'BACK'), ('back2', 'BACK'), ('result', 'BACK')]
    elif case == 'reopen':
        (sd / '.crosspoint/file-browser.json').write_text(json.dumps({'version': 1, 'crossPath': '/books/child/leaf'}))
        if touch:
            steps = [('home', None), ('file', 'TAP:150,754'), ('leaf', None),
                     ('child', 'TAP:46,754'), ('books', 'TAP:46,754'), ('result', 'TAP:46,754')]
        else:
            steps = [('home', None), ('file', 'DOWN'), ('leaf', 'CONFIRM'), ('child', 'BACK'),
                     ('books', 'BACK'), ('result', 'BACK'), ('selected', 'CONFIRM')]
    else:
        steps = [('home', None), ('file', 'DOWN'), ('books', 'CONFIRM'), ('last', 'LEFT'),
                 ('up1', 'LEFT'), ('up2', 'LEFT'), ('up3', 'LEFT'), ('parent', 'LEFT')]
        # folder-13 sits in the middle of the retained viewport. Remember it,
        # visit folder-14 by another route, then return to the same parent.
        steps += [('expected', 'RIGHT'), ('oldrow', 'LEFT'), ('child', 'CONFIRM')]
        if case == 'sibling':
            steps += [('sibling', 'DOWN'), ('result', 'BACK')]
        else:
            pin(sd, '/books/folder-14')
            steps += [('home-again', 'BACK:1200'), ('favorite', 'DOWN'), ('direct', 'CONFIRM'), ('result', 'BACK')]
    return sd, steps


def count(a, b):
    return sum(value != 0 for value in ImageChops.difference(a, b).getdata())


def run(program, out, case, machine, port):
    touch = machine == 'X4Pro'
    sd, steps = fixture(out, case, touch)
    log, shots = sim.run(program, out, sd, steps, port, gap=2800, settle=1800)
    if case in ('sibling', 'direct'):
        changed = count(shots['expected'], shots['result'])
    elif case == 'reopen' and not touch:
        # X3 and X4C intentionally return to Home File in .62. The baseline
        # image is the old FileBrowser root, so the destination is checked by
        # the Home transition and the selected reopen row in the log below.
        changed = 0
    else:
        name = f'{machine}-{case}.png'
        manifest = json.loads((REFERENCES / 'sha256.json').read_text())
        ref = REFERENCES / name
        assert hashlib.sha256(ref.read_bytes()).hexdigest() == manifest['images'][name], 'Reference hash changed'
        changed = count(Image.open(ref).convert('L'), shots['result'])
    print(f'{machine} {case} changed_pixels={changed}', flush=True)
    if case == 'reopen' and not touch:
        home_count = log.count('Entering activity: Home')
        assert home_count >= 2, 'RED: Back from a level-1 folder did not return to Home File'
        tabs = re.findall(r'Restored Home tab=(\d+)', log)
        assert tabs and tabs[-1] == '1', f'RED: Home came back on the wrong card: {tabs[-1:]}'
        paths = re.findall(r'X3PILL.*current=1 .*text=(/[^\n]*)', log)
        assert paths and paths[-1] == '/books', f'Reopened the wrong root row: {paths[-1:]}'
    if case == 'reopen' and touch:
        assert log.count('Entering activity: Home') >= 2, 'RED: X4 Pro reopen did not return to Home File'
    if case == 'favorite':
        assert log.count('Entering activity: Home') == 2, 'Back did not return to Home'
    assert changed == 0, f'{case}: wrong destination, row, or viewport'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('program')
    parser.add_argument('output', type=Path)
    parser.add_argument('case', choices=('favorite', 'reopen', 'sibling', 'direct'))
    parser.add_argument('--machine', choices=('X3', 'X4C', 'X4Pro'), default='X3')
    parser.add_argument('--port', type=int, default=33600)
    args = parser.parse_args()
    run(args.program, args.output.absolute(), args.case, args.machine, args.port)
