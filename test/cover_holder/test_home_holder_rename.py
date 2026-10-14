#!/usr/bin/env python3
"""A book with no cover is renamed between two starts: test_home_holder_rename.py <x3|x4> <program> [--out DIR]

The Home card keeps the cover it drew in a file beside the book's thumbnail. The placeholder of a book with no
cover has the book's name in it, so the name is part of that cover's key. A card that showed the old name, the
book renamed, then Home booted again must show the same cover as a card that always had the new name.
Not part of ctest: it needs a simulator binary (pio run -e simulator_x4pro / simulator_x3_uc8279).
"""
import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

from test_home_holder_upgrade import make_book, simulate

OLD, NEW = 'Mot cuon sach khong co bia', 'Cuon sach da doi ten'


def card(sd, title):
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_book(sd / 'sach.epub', title, False)
    make_book(sd / 'khac.epub', 'Sach khac', False)
    settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': 1, 'homeStyle': 0, 'wakeIntoBook': 1,
                'readerMenuStyle': 1, 'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3,
                'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1, 'uiShellSleepMemo': 0,
                'sleepTimeout': 10, 'readerTapTip': 0, 'tipsEnabled': 1, 'tenorButtonSymbols': 1, 'tenorSideArrows': 1,
                'readerFavoritesDaDat': 1, 'readerFavoriteCount': 0, 'sdFontFamilyName': ''}
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/sach.epub',
                                                  'lastSleepFromReader': True}))
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/sach.epub'}, {'path': '/khac.epub'}]}))


def visit(program, sd, folder, port):
    """The book, Back to Home, 20 s on Home (the idle pass writes the card files); then Home booted again."""
    folder.mkdir(parents=True, exist_ok=True)
    first, log = simulate(program, sd, folder, 'a', ['2500:BACK'], [20000], port)
    store = sd / '.crosspoint'
    state = json.loads((store / 'state.json').read_text())
    state.update(openEpubPath='', lastSleepFromReader=False)
    (store / 'state.json').write_text(json.dumps(state))
    return log


def home_again(program, sd, folder, port):
    shots, log = simulate(program, sd, folder, 'b', [], [2500, 9000], port)
    return shots[1], log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dev', choices=['x3', 'x4'])
    ap.add_argument('program', type=Path)
    ap.add_argument('--out', type=Path)
    a = ap.parse_args()
    program = a.program.resolve()
    out = a.out or Path(tempfile.mkdtemp())
    out.mkdir(parents=True, exist_ok=True)
    port = 29800 if a.dev == 'x4' else 29820
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        sd = work / 'old' / 'sd'
        card(sd, OLD)
        visit(program, sd, work / 'old', port)
        cards = sorted(str(p.relative_to(sd)) for p in sd.rglob('*.card?'))
        # The book is renamed: the file and the entry the reader keeps in the recent list.
        make_book(sd / 'sach.epub', NEW, False)
        recent = json.loads((sd / '.crosspoint' / 'recent.json').read_text())
        for entry in recent['books']:
            if entry['path'] == '/sach.epub':
                entry['title'] = NEW
        (sd / '.crosspoint' / 'recent.json').write_text(json.dumps(recent))
        renamed, _ = home_again(program, sd, work / 'old', port + 1)
        fresh_sd = work / 'new' / 'sd'
        card(fresh_sd, NEW)
        log = visit(program, fresh_sd, work / 'new', port + 2)
        fresh, _ = home_again(program, fresh_sd, work / 'new', port + 3)
        Image.fromarray(renamed).save(out / f'{a.dev}-renamed.png')
        Image.fromarray(fresh).save(out / f'{a.dev}-new-card.png')
    print(f'{a.dev}: {len(cards)} card files after the first visit')
    rects = sorted({tuple(map(int, m)) for m in re.findall(r'Cover holder frame=(\d+),(\d+),(\d+),(\d+)', log)})
    if not rects or not cards:
        print('RED: no placeholder frame or no card file to keep')
        return 1
    bad = []
    for x, y, w, h in rects:
        differ = int((renamed[y:y + h, x:x + w] != fresh[y:y + h, x:x + w]).sum())
        print(f'{a.dev} cover {w}x{h}: {differ} pixels differ from a card that always had the new name', flush=True)
        if differ:
            bad.append(f'{differ} px')
    print('RED: ' + '; '.join(bad) if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
