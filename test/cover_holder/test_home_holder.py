#!/usr/bin/env python3
"""Home with books that have no cover, on the simulator: test_home_holder.py <x3|x4> <program> [--out DIR]

Every frame Home draws the placeholder in (3 styles x 3 text sizes on the X4 Pro, 3 text sizes on the X3;
the frames are read from the simulator's log line "Cover holder frame=x,y,w,h") must be a white frame with
the book's name and author in it, as check_holder() says. Home on the Recent tab is 1 bit on both machines
(HomeRefresh: oneBit), so the placeholder is drawn straight into the frame and nothing is cached.
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


def check_holder(shot, x, y, w, h):
    """A name-and-author placeholder: white, little ink, ink only where text is, text block centred.
    shot is the 1 bit screen (0 black, 255 white). Returns the list of what is wrong."""
    bad = []
    frame = shot[y:y + h, x:x + w] < 128
    mask = interior(w, h)
    black = float((frame & mask).sum()) / float(mask.sum())
    if black > 0.2:
        bad.append(f'{black:.0%} black (at most 20%)')
    # The dotted ring and the rounded corners (radius: a sixteenth of the width, 6 px at least) stay out of
    # the text region; the text margin is 1.5 radii, so no text is cut off.
    inset = max(6, (w + 8) // 16) + 2
    text = frame[inset:h - inset, inset:w - inset]
    rows = np.nonzero(text.any(axis=1))[0]
    cols = np.nonzero(text.any(axis=0))[0]
    if rows.size == 0:
        return bad + ['no ink: the name is not drawn']
    white_rows = 1.0 - rows.size / text.shape[0]
    if white_rows < 0.3:
        bad.append(f'only {white_rows:.0%} of the rows are white: not lines of text')
    centre = (cols.min() + cols.max() + 1) / 2
    off = centre - text.shape[1] / 2
    if abs(off) > 0.5:
        bad.append(f'text block {off:+.1f} px off the centre (at most 0.5)')
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dev', choices=['x3', 'x4'])
    ap.add_argument('program', type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--floor-refuses', action='store_true',
                    help='X3: the heap floor also refuses the gray strip (CROSSPOINT_SIM_INK_GRAY_FAIL); the frame stays 1 bit')
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
            rects = sorted({tuple(map(int, m)) for m in re.findall(r'Cover holder frame=(\d+),(\d+),(\d+),(\d+)', log)})
            if not rects:
                bad.append(f'style={style} text={tier}: no frame in the log')
            for k, (x, y, w, h) in enumerate(rects):
                if (w, h) not in KNOWN_SIZES:
                    bad.append(f'style={style} text={tier}: frame {w}x{h} is not a known size')
                wrong = check_holder(shots[1], x, y, w, h)
                print(f'{a.dev} style={style} text={tier} frame {k} {w}x{h}: {"; ".join(wrong) or "ok"}', flush=True)
                if wrong:
                    bad.append(f'style={style} text={tier} frame {k}: {"; ".join(wrong)}')
            if any(folder.rglob('ink2_1_*.bmp')) and not a.floor_refuses:
                bad.append(f'style={style} text={tier}: a 1 bit placeholder file was written')
    print('RED: ' + '; '.join(bad) if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
