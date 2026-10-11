#!/usr/bin/env python3
"""A baseline JPEG cover on Home after reading, on the simulator: test_home_baseline_cover.py <x3|x4> <program>

3 books (a large baseline JPEG cover, a progressive one, none) are read one by one, so the reader writes its
1 bit thumbnails. Home then runs with the heap the X3 has after a book (73.520 B free, 27.636 B in one
block): the missing cover pass, which writes a baseline cover's tenor/ink picture, does not run there (its
copy out of the book needs a 32 KB inflate window in one block), and the idle step leaves baseline covers
alone. Pass (X3): the card of each book with a cover shows a picture (its frame is not one flat pattern)
while the missing cover pass has not run. --out keeps the 3 screenshots; on the X4 Pro the script only takes them.
Not part of ctest: it needs a simulator binary (pio run -e simulator_x3_uc8279 / simulator_x4pro).
"""
import argparse
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
# Read last to first: the first is the newest, on the card when Home opens.
BOOKS = [('bia-b.epub', 'Sach bia baseline lon', 'baseline'), ('bia-p.epub', 'Sach bia progressive', 'progressive'),
         ('sach.epub', 'Sach khong bia', None)]
HEAP = {'CROSSPOINT_SIM_FREE_HEAP': '73520', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '27636'}  # X3 Home, log 10/10
STEP = 6000


def cover(kind):
    # The size of the cover that showed the bug (1457 x 2200).
    im = Image.new('RGB', (1457, 2200), (70, 100, 150) if kind == 'baseline' else (150, 90, 60))
    d = ImageDraw.Draw(im)
    d.rectangle([60, 60, 1397, 2140], outline=(250, 245, 230), width=16)
    for i in range(40):
        d.line([(100, 600 + i * 36), (1357, 600 + i * 36)], fill=(i * 6, 20, 20), width=8)
    d.ellipse([400, 150, 1057, 500], fill=(240, 240, 240))
    return im


def make_book(path, title, kind):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('c1.xhtml', f'{head}<h1>Chuong 1</h1>' + '<p>Doan van de trang co noi dung.</p>' * 30 + '</body></html>')
        meta = item = ''
        if kind:
            b = io.BytesIO()
            cover(kind).save(b, 'JPEG', quality=90, progressive=kind == 'progressive')
            z.writestr('images/cover.jpg', b.getvalue())  # deflated, as calibre stores it
            meta = '<meta name="cover" content="cover-img"/>'
            item = '<item id="cover-img" href="images/cover.jpg" media-type="image/jpeg"/>'
        z.writestr('toc.ncx', f'<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>{title}</text></docTitle><navMap><navPoint id="n1" playOrder="1"><navLabel><text>Chuong 1</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('book.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>{title}</dc:title><dc:creator>Tac gia</dc:creator><dc:identifier id="id">{path.stem}</dc:identifier><dc:language>vi</dc:language>{meta}</metadata>'
                   f'<manifest>{item}<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/></spine></package>')


def run(program, sd, folder, open_book, tier, acts, shots, quit_ms, port, heap):
    store = sd / '.crosspoint'
    settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': tier, 'homeStyle': 0,
                'wakeIntoBook': 1 if open_book else 0, 'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3,
                'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1,
                'uiShellSleepMemo': 0, 'sleepTimeout': 10, 'tipsEnabled': 0, 'sdFontFamilyName': ''}
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/' + open_book if open_book else '',
                                                  'lastSleepFromReader': bool(open_book)}))
    folder.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_HTTP_PORT=str(port),
               CROSSPOINT_SIM_USB='1', CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(acts + [f'{quit_ms}:QUIT']),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{name}.bmp' for ms, name in shots.items()))
    if heap:
        env.update(HEAP)
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=300)
    log = p.stdout + p.stderr
    (folder / 'run.log').write_text(log)
    assert p.returncode == 0, log[-1500:]
    return log


def flat(image, rect):
    """True when the frame's inside is one repeated 4 x 4 tile (a plain fill or its dither)."""
    x, y, w, h = rect
    tiles = {image.crop((tx, ty, tx + 4, ty + 4)).tobytes()
             for ty in range(y + 8, y + h - 12, 4) for tx in range(x + 8, x + w - 12, 4)}
    return len(tiles) <= 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dev', choices=['x3', 'x4'])
    ap.add_argument('program', type=Path)
    ap.add_argument('--tier', type=int, default=0)
    ap.add_argument('--out', type=Path, help='keep the screenshots and logs here')
    a = ap.parse_args()
    program = a.program.resolve()
    port = 29950 + a.tier + (10 if a.dev == 'x4' else 0)
    with tempfile.TemporaryDirectory(prefix='home-baseline-') as tmp:
        work = a.out.resolve() if a.out else Path(tmp)
        if work.exists():
            shutil.rmtree(work)
        sd = work / 'sd'
        (sd / '.crosspoint').mkdir(parents=True)
        for name, title, kind in BOOKS:
            make_book(sd / name, title, kind)
        (sd / '.crosspoint' / 'recent.json').write_text(json.dumps({'books': [{'path': '/' + b[0], 'title': b[1]} for b in BOOKS]}))
        for i, (name, *_rest) in enumerate(reversed(BOOKS)):
            # Back on Home under 3 s: neither the missing cover pass nor the idle step runs yet.
            run(program, sd, work / f'read{i}', name, a.tier, ['2500:BACK'], {}, 4000 if name == 'sach.epub' else 3300,
                port, False)
        edges = [STEP * (i + 1) for i in range(len(BOOKS) - 1)]
        key = 'SWIPE:420,400,60,400,150' if a.dev == 'x4' else 'RIGHT'
        shots = {STEP * (i + 1) - 300: f'home-{name[:-5]}' for i, (name, *_r) in enumerate(BOOKS)}
        log = run(program, sd, work / 'home', None, a.tier, [f'{t}:{key}' for t in edges], shots,
                  STEP * len(BOOKS), port, True)
        builds = [int(m) for m in re.findall(r'\[HOME\] Recent card build=\d+ms cache=\d+ cover=(-?\d+)', log)]
        frames = [tuple(map(int, m)) for m in re.findall(r'tenor/ink frame cover=(\d+),(\d+),(\d+),(\d+)', log)]
        print(f'{a.dev} tier={a.tier}: card builds cover={builds} frames={frames[:1]}', flush=True)
        bad = []
        if a.dev == 'x3':
            # The X3 after reading: the missing cover pass must not have run (it would write the ink cover).
            if 'Home thumb book=' in log:
                bad.append('the heap let the missing cover pass run')
            if not frames:
                bad.append('no tenor/ink frame in the log')
            else:
                for name, _title, kind in BOOKS:
                    image = Image.open(work / 'home' / f'home-{name[:-5]}.bmp').convert('L')
                    if kind and flat(image, frames[0]):
                        bad.append(f'{name}: plain frame on the card')
        for f in (work / 'home').glob('*.bmp'):
            Image.open(f).convert('L').save(f.with_suffix('.png'))
            f.unlink()
        shutil.rmtree(sd)
    print('RED: ' + '; '.join(bad) if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
