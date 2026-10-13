#!/usr/bin/env python3
"""Home after a firmware upgrade, on the simulator, on the card the former firmware wrote:
test_home_holder_upgrade.py <x3|x4> <old-program> <new-program> [--out DIR] [--tier N ...]

Each case opens one book with the first program, goes back to Home and waits for the card files, then
boots the second program straight into Home on the same card (the book is not opened again):
  upgrade   old -> new, a book without a cover: after the idle pass the frame shows the placeholder of
            (white, the name and author; test_home_holder.check_holder), not a card file the old firmware drew.
  fresh     new -> new, the same book: the card file of the first visit must not keep the plain frame
            that stood before the idle pass wrote the placeholder.
  cover     old -> new, a book with a JPEG cover: the new Home is the old Home to the pixel (the cover
            and its card are kept; only the card file is drawn again).
The old program is the simulator of v1.0.59 (pio run -e simulator_x4pro / simulator_x3_uc8279 at that tag).
Not part of ctest: it needs 2 simulator binaries.
"""
import argparse
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

from test_home_holder import REPO, check_holder


def make_book(path, title, cover):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    meta = item = ''
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        if cover:
            y, x = np.mgrid[0:900, 0:600]
            picture = Image.fromarray(((x * 3 + y * 2) % 256).astype(np.uint8))
            jpeg = io.BytesIO()
            picture.save(jpeg, 'JPEG', quality=90)
            z.writestr('cover.jpg', jpeg.getvalue())
            meta = '<meta name="cover" content="cover-img"/>'
            item = '<item id="cover-img" href="cover.jpg" media-type="image/jpeg"/>'
        z.writestr('c1.xhtml', f'{head}<h1>Chuong 1</h1>' + '<p>Doan van de co noi dung.</p>' * 40 + '</body></html>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>x</text></docTitle><navMap><navPoint id="n1" playOrder="1"><navLabel><text>Chuong 1</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('book.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>{title}</dc:title><dc:creator>Tac gia</dc:creator><dc:identifier id="id">{title}</dc:identifier><dc:language>vi</dc:language>{meta}</metadata><manifest>{item}<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/></spine></package>')


def simulate(program, sd, folder, name, script, shots, port):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_HTTP_PORT=str(port),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(script + [f'{max(shots) + 700}:QUIT']),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{folder}/{name}{i}.bmp' for i, t in enumerate(shots)))
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
    (folder / f'{name}.log').write_text(p.stdout + p.stderr)
    return [np.array(Image.open(folder / f'{name}{i}.bmp').convert('L')) for i in range(len(shots))], p.stdout + p.stderr


def journey(first, second, folder, tier, cover, port):
    """First program: the book, Back to Home, 20 s on Home. Second: boot into Home, shots at 2,5 s and 9 s."""
    sd = folder / 'sd'
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_book(sd / 'sach.epub', 'Sach thu', cover)
    make_book(sd / 'khac.epub', 'Sach khac', False)
    # The reader's first tips are already seen, or the first Back would close a tip and not the book.
    settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': tier, 'homeStyle': 0, 'wakeIntoBook': 1,
                'readerMenuStyle': 1, 'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3,
                'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1, 'uiShellSleepMemo': 0,
                'sleepTimeout': 10, 'readerTapTip': 0, 'tipsEnabled': 1, 'tenorButtonSymbols': 1, 'tenorSideArrows': 1,
                'readerFavoritesDaDat': 1, 'readerFavoriteCount': 0, 'sdFontFamilyName': ''}
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/sach.epub',
                                                  'lastSleepFromReader': True}))
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/sach.epub'}, {'path': '/khac.epub'}]}))
    before, _ = simulate(first, sd, folder, 'a', ['2500:BACK'], [20000], port)
    state = json.loads((store / 'state.json').read_text())
    state.update(openEpubPath='', lastSleepFromReader=False)
    (store / 'state.json').write_text(json.dumps(state))
    after, log = simulate(second, sd, folder, 'b', [], [2500, 9000], port)
    cards = sorted(str(p.relative_to(sd)) for p in sd.rglob('*.card?'))
    return before[0], after, log, cards


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dev', choices=['x3', 'x4'])
    ap.add_argument('old', type=Path)
    ap.add_argument('new', type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--tier', type=int, nargs='*', default=[0, 1, 2])
    a = ap.parse_args()
    old, new = a.old.resolve(), a.new.resolve()
    out = a.out or Path(tempfile.mkdtemp())
    out.mkdir(parents=True, exist_ok=True)
    bad = []
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        for tier in a.tier:
            frame = None
            for k, (case, first, cover) in enumerate([('fresh', new, False), ('upgrade', old, False), ('cover', old, True)]):
                folder = work / f'{case}-t{tier}'
                folder.mkdir()
                before, after, log, cards = journey(first, new, folder, tier, cover, 29700 + tier * 10 + k)
                Image.fromarray(before).save(out / f'{a.dev}-{case}-t{tier}-first.png')
                Image.fromarray(after[1]).save(out / f'{a.dev}-{case}-t{tier}.png')
                (out / f'{a.dev}-{case}-t{tier}.log').write_text(log)
                tag = f'{a.dev} {case} text={tier}'
                if case == 'cover':
                    # The cover in the old Home: the longest run of rows a third inked or more in the cover
                    # column (a text line is sparser), and the ink's columns there.
                    h, w = before.shape
                    dense = (before[:, :int(w * 0.6)] != 255).mean(axis=1) >= 1 / 3
                    runs, start = [], None
                    for row, on in enumerate(list(dense) + [False]):
                        if on and start is None:
                            start = row
                        elif not on and start is not None:
                            runs.append((row - start, start, row))
                            start = None
                    _, y0, y1 = max(runs)
                    xs = np.nonzero((before[y0:y1, :int(w * 0.6)] != 255).any(axis=0))[0]
                    x0, x1 = int(xs.min()), int(xs.max()) + 1
                    inside = int((after[1][y0:y1, x0:x1] != before[y0:y1, x0:x1]).sum())
                    total = int((after[1] != before).sum())
                    print(f'{tag}: cover {x1 - x0}x{y1 - y0} at {x0},{y0}: {inside} pixels differ from the old Home, '
                          f'{total - inside} outside it ({len(cards)} card files)', flush=True)
                    if inside:
                        bad.append(f'{tag}: {inside}')
                    continue
                # The frame of the placeholder: the new firmware draws it on the first visit of a blank card.
                if frame is None:
                    first_log = (folder / 'a.log').read_text()
                    found = re.findall(r'Cover holder frame=(\d+),(\d+),(\d+),(\d+)', first_log)
                    frame = tuple(map(int, found[0])) if found else None
                if frame is None:
                    bad.append(f'{tag}: the new firmware drew no placeholder frame on a blank card')
                    continue
                x, y, w, h = frame
                wrong = check_holder(after[1], x, y, w, h)
                drawn = 'drawn' if re.search(r'Cover holder frame=', log) else 'not drawn, a card file came back'
                print(f'{tag} frame {w}x{h}: {"; ".join(wrong) or "the name placeholder"} after the idle pass '
                      f'(placeholder {drawn}; {", ".join(cards)})', flush=True)
                if wrong:
                    bad.append(f'{tag}: {"; ".join(wrong)}')
    print('RED: ' + '; '.join(bad) if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
