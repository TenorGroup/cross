#!/usr/bin/env python3
"""Home with books that have no cover, on the simulator: test_home_holder.py <x3|x4> <program> [--out DIR]

Every frame Home draws the placeholder in (3 styles x 3 text sizes on the X4 Pro, 3 text sizes on the X3;
the frames are read from the simulator's log line "Cover holder frame=x,y,w,h") is compared with the picture
reference.py makes of the master at that size: 4 levels on the X4 Pro, 1 bit on the X3, whose Home draws
tenor/ink 1 bit (kInkX3Gray in HomeActivity.cpp). Two shots per run: the first, before the idle pass
has written the placeholder, shows a plain frame; the second shows the picture. The corners (the card's
rounded mask) are left out of the comparison.
Not part of ctest: it needs a simulator binary (pio run -e simulator_x4pro / simulator_x3_uc8279).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

import reference as ref

REPO = Path(__file__).resolve().parents[2]
TITLES = ['Mot cuon sach khong co bia', 'Sach thu hai', 'Sach thu ba', 'Sach thu tu']
# (Home style, text size) of the runs: 3 styles on the X4 Pro, the card of the X3.
RUNS_X4 = [(style, tier) for style in range(3) for tier in range(3)]
RUNS_X3 = [(0, tier) for tier in range(3)]
# The frame sizes the picture may be drawn at; a frame the log shows outside this list is a failure.
KNOWN_SIZES = set(ref.X4_SIZES + ref.X3_SIZES)


def make_book(path, title):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('c1.xhtml', f'{head}<h1>Chuong 1</h1>' + '<p>Doan van de co noi dung.</p>' * 40 + '</body></html>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>x</text></docTitle><navMap><navPoint id="n1" playOrder="1"><navLabel><text>Chuong 1</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('book.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>{title}</dc:title><dc:creator>Tac gia</dc:creator><dc:identifier id="id">{title}</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/></spine></package>')


def shoot(program, folder, style, tier, port, times=(700, 5000), extra=None):
    """Runs the simulator on a card with 4 books without a cover; returns the shots and the sd folder."""
    sd = folder / 'sd'
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    for i, t in enumerate(TITLES):
        make_book(sd / f'sach{i}.epub', t)
    settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': tier, 'homeStyle': style, 'wakeIntoBook': 0,
                'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3, 'paragraphIndentVersion': 1,
                'readerInkWeightVersion': 1, 'tenorPresetVersion': 1, 'uiShellSleepMemo': 0, 'sleepTimeout': 10,
                'tipsEnabled': 0, 'sdFontFamilyName': ''}
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '', 'lastSleepFromReader': False}))
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': f'/sach{i}.epub', 'title': t} for i, t in enumerate(TITLES)]}))
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_HTTP_PORT=str(port),
               CROSSPOINT_SIM_INPUT_SCRIPT=f'{times[-1] + 1500}:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{folder}/s{i}.bmp' for i, t in enumerate(times)))
    env.update(extra or {})
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    (folder / 'run.log').write_text(p.stdout + p.stderr)
    shots = [np.array(Image.open(folder / f's{i}.bmp').convert('L')) for i in range(len(times))]
    return shots, sd, p.stdout + p.stderr


# The simulator paints the 4 absolute gray levels as 0, 96, 200, 255.
SIM_LEVELS = {0: 0, 96: 85, 200: 170, 255: 255}


def levels(shot):
    out = np.full(shot.shape, 255, np.uint8)
    for painted, level in SIM_LEVELS.items():
        out[shot == painted] = level
    return out


def interior(w, h):
    """Pixels away from the rounded corners."""
    m = np.ones((h, w), bool)
    r = max(8, w // 8)
    m[:r, :r] = m[:r, -r:] = m[-r:, :r] = m[-r:, -r:] = False
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dev', choices=['x3', 'x4'])
    ap.add_argument('program', type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--floor-refuses', action='store_true',
                    help='X3: the heap floor also refuses the gray strip (CROSSPOINT_SIM_INK_GRAY_FAIL); the frame stays 1 bit tenor/ink')
    a = ap.parse_args()
    a.program = a.program.resolve()
    runs = RUNS_X4 if a.dev == 'x4' else RUNS_X3
    out = a.out or Path(tempfile.mkdtemp())
    out.mkdir(parents=True, exist_ok=True)
    bad = []
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        seen = set()
        for style, tier in runs:
            folder = work / f's{style}t{tier}'
            folder.mkdir()
            shots, sd, log = shoot(a.program, folder, style, tier, 29600 + style * 10 + tier,
                                   extra={'CROSSPOINT_SIM_INK_GRAY_FAIL': '1'} if a.floor_refuses else None)
            Image.fromarray(shots[1]).save(out / f'{a.dev}-s{style}-t{tier}.png')
            four = a.dev == 'x4'  # X3 Home draws tenor/ink 1 bit until its gray pass is turned on
            if four:
                shots = [levels(s) for s in shots]
            rects = sorted({tuple(map(int, m)) for m in re.findall(r'Cover holder frame=(\d+),(\d+),(\d+),(\d+)', log)})
            if not rects:
                bad.append(f'style={style} text={tier}: no frame in the log')
            for k, (x, y, w, h) in enumerate(rects):
                if (w, h) not in KNOWN_SIZES:
                    bad.append(f'style={style} text={tier}: frame {w}x{h} is not a known size')
                want = ref.expected(w, h, four)
                mask = interior(w, h)
                got = shots[1][y:y + h, x:x + w]
                diff = int(((got != want) & mask).sum())
                plain = shots[0][y:y + h, x:x + w]
                waiting = int(((plain != want) & mask).sum())
                print(f'{a.dev} style={style} text={tier} frame {k} {w}x{h}: {diff} pixels differ after the idle pass, '
                      f'{waiting} differ before it', flush=True)
                if diff:
                    bad.append(f'style={style} text={tier} frame {k}: {diff}')
    print('RED: ' + '; '.join(bad) if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
