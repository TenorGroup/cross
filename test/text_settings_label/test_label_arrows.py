#!/usr/bin/env python3
"""X3 Cai dat van ban: the "Xem truoc ..." label keeps 6 px of clear ink from the list's edge arrows.

The arrows (UiListActivity::drawPageHints) ink x 4..9 and W-11..W-6, y 191..199. The test opens Cai dat van ban in
the X3 simulator at text sizes 0, 1 and 2, checks the arrows are where they were and that the strips 6 px wide
beside them (x 10..15 and W-17..W-12, y 185..205) hold no ink. Runs simulator_x3_uc8279; X3_PROGRAM picks the build.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('X3_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
W = 528
ARROW_L, ARROW_R = (4, 191, 9, 199), (W - 11, 191, W - 6, 199)
ACTIONS = ['CONFIRM', 'DOWN', 'DOWN', 'DOWN', 'DOWN', 'CONFIRM']  # reader menu, Cong cu tab, Cai dat van ban
SHOT_MS = 2000 + (len(ACTIONS) - 1) * 1700 + 1100


def book(path):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    import zipfile
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:'
                   'container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package'
                   '+xml"/></rootfiles></container>')
        z.writestr('c1.xhtml', head + '<h1>Mo dau</h1>' + '<p>Mot doan van de co noi dung cho menu.</p>' * 30 + '</body></html>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/>'
                   '<docTitle><text>x</text></docTitle><navMap><navPoint id="n1" playOrder="1"><navLabel><text>Mo dau</text>'
                   '</navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                   'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sach</dc:title>'
                   '<dc:identifier id="id">x</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="c1" '
                   'href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type='
                   '"application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/></spine></package>')


def shoot(tmp, tier):
    sd = Path(tmp) / f'sd{tier}'
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    book(sd / 'sach.epub')
    (store / 'settings.json').write_text(json.dumps(dict(
        language='VI', uiShell=0, uiTheme=4, uiTextSize=tier, readerMenuStyle=0, wakeIntoBook=1, fontSize=18, fontFamily=0,
        textSpacingVersion=3, paragraphIndentVersion=1, readerInkWeightVersion=1, tenorPresetVersion=1, sleepTimeout=10,
        readerTapTip=0, tipsEnabled=1, tenorButtonSymbols=1, tenorSideArrows=1, sdFontFamilyName='')))
    (store / 'state.json').write_text(json.dumps(dict(showBootScreen=False, openEpubPath='/sach.epub', lastSleepFromReader=True)))
    (store / 'recent.json').write_text(json.dumps(dict(books=[dict(path='/sach.epub', title='Sach')])))
    shot = sd / 'shot.bmp'
    script = ';'.join(f'{2000 + i * 1700}:{a}' for i, a in enumerate(ACTIONS)) + f';{SHOT_MS + 700}:QUIT'
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=f'{SHOT_MS}:{shot}')
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
    assert run.returncode == 0 and shot.exists(), (run.stdout + run.stderr)[-1500:]
    out = os.environ.get('X3_TEST_SHOTS')
    image = Image.open(shot).convert('L')
    if out:
        Path(out).mkdir(parents=True, exist_ok=True)
        image.save(Path(out) / f'label-co{tier}.png')
    return np.asarray(image) < 128


def box(ink, x0, y0, x1, y1):
    r = ink[y0:y1 + 1, x0:x1 + 1]
    ys, xs = np.nonzero(r.any(axis=1))[0], np.nonzero(r.any(axis=0))[0]
    return tuple(int(v) for v in (x0 + xs.min(), y0 + ys.min(), x0 + xs.max(), y0 + ys.max())) if len(xs) else None


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='label-arrows-', dir=os.environ.get('TMPDIR')) as tmp:
        for tier in (0, 1, 2):
            ink = shoot(tmp, tier)
            left, right = box(ink, 0, 185, 9, 205), box(ink, W - 11, 185, W - 1, 205)
            moved = (left, right) != (ARROW_L, ARROW_R)
            beside_left = ink[185:206, 10:16].sum()
            beside_right = ink[185:206, W - 17:W - 11].sum()
            ok = not moved and beside_left == 0 and beside_right == 0
            print(('GREEN' if ok else 'RED') + f' size {tier}: arrows {left} {right}{" moved" if moved else ""}; ink px beside '
                  f'them: left {beside_left}, right {beside_right}', flush=True)
            if not ok:
                failures.append(tier)
    assert not failures, f'the preview label is within 6 px of an edge arrow at sizes {failures}'
    print('GREEN: the preview label keeps 6 px from the edge arrows at sizes 0, 1 and 2')


if __name__ == '__main__':
    main()
