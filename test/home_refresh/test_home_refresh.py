#!/usr/bin/env python3
"""Refreshes per book change on the Home Recent card, on the simulator: test_home_refresh.py <x3|x4> <program>

5 books (2 baseline JPEG covers, 1 progressive, 1 damaged after it was read, 1 without a cover) are read one by one, as a reader would
have them, then Home steps through them twice on the same card (sideways swipe on the X4 Pro, Right on
the X3), each change left 8 s to settle. Every Home frame ends with the log line "Frame row=" and pushes
the panel once (one refresh, or one gray pass of base and levels); the panel trace must hold as many
pushes as the log has frames. Pass:
  - first pass (covers not written at the card's size yet): at most 2 frames per change, the plain frame
    and then the cover; with the card's cover as the only cover slot (styles 0 and 1), at most 1 gray pass:
    a gray first frame there is the placeholder drawn for a book that has a cover (the Shelf style's first
    frame is gray for the other books' covers);
  - second pass (every cover, card and placeholder written): exactly 1 frame per change. The damaged
    cover is never written: a decode that writes nothing must not draw Home again.
--old <program>: the card is first used by a former firmware's Home (an upgrade: what it wrote stays).
Not part of ctest: it needs a simulator binary (pio run -e simulator_x4pro / simulator_x3_uc8279).
"""
import argparse
import io
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
BOOKS = [('bia-b.epub', 'Sach bia baseline', 2, False), ('bia-p.epub', 'Sach bia progressive', 1, True),
         ('sach.epub', 'Sach khong bia', None, False), ('bia-hong.epub', 'Sach bia hong', -5, False),
         ('bia-b2.epub', 'Sach bia hai', 3, False)]  # Home's Recent card holds 5 books
FIRST, WAIT = 7000, 8000


def cover(seed):
    im = Image.new('RGB', (600, 900), (60 + seed * 30, 90, 160 - seed * 20))
    d = ImageDraw.Draw(im)
    d.rectangle([30, 30, 570, 870], outline=(250, 245, 230), width=6)
    for i in range(20):
        d.line([(40, 300 + i * 24), (560, 300 + i * 24)], fill=(seed * 40 % 255, 20, 20), width=3)
    return im


def make_book(path, title, picture, progressive):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('c1.xhtml', f'{head}<h1>Chuong 1</h1>' + '<p>Doan van de trang co noi dung.</p>' * 30 + '</body></html>')
        meta = item = ''
        if picture is not None:
            b = io.BytesIO()
            if picture == 'damaged':
                b.write(b'\xff\xd8\xff\xe0' + bytes(range(256)) * 8)  # a JPEG no decoder reads
            else:
                picture.save(b, 'JPEG', quality=90, progressive=progressive)
            z.writestr('images/cover.jpg', b.getvalue())
            meta = '<meta name="cover" content="cover-img"/>'
            item = '<item id="cover-img" href="images/cover.jpg" media-type="image/jpeg"/>'
        z.writestr('toc.ncx', f'<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>{title}</text></docTitle><navMap><navPoint id="n1" playOrder="1"><navLabel><text>Chuong 1</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('book.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>{title}</dc:title><dc:creator>Tac gia</dc:creator><dc:identifier id="id">{path.stem}</dc:identifier><dc:language>vi</dc:language>{meta}</metadata>'
                   f'<manifest>{item}<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/></spine></package>')


def run(program, sd, folder, open_book, style, acts, quit_ms, port):
    store = sd / '.crosspoint'
    settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': 0, 'homeStyle': style,
                'wakeIntoBook': 1 if open_book else 0, 'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3,
                'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1,
                'uiShellSleepMemo': 0, 'sleepTimeout': 10, 'tipsEnabled': 0, 'sdFontFamilyName': ''}
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/' + open_book if open_book else '',
                                                  'lastSleepFromReader': bool(open_book)}))
    folder.mkdir(parents=True, exist_ok=True)
    trace = folder / 'trace.bin'
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_HTTP_PORT=str(port),
               CROSSPOINT_SIM_USB='1', CROSSPOINT_SIM_PANEL_TRACE=str(trace),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(acts + [f'{quit_ms}:QUIT']))
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=300)
    log = p.stdout + p.stderr
    (folder / 'run.log').write_text(log)
    assert p.returncode == 0, log[-1500:]
    return log, trace


def pushes(trace):
    """Panel pushes in the trace: a refresh (op 2) outside a gray pass, or a gray pass (ops 4..8)."""
    data, i, n, gray = trace.read_bytes(), 0, 0, False
    while i + 12 <= len(data):
        op, size = data[i + 1], struct.unpack_from('<I', data, i + 8)[0]
        if op == 4:
            gray = True
        elif op == 8:
            gray, n = False, n + 1
        elif op == 2 and not gray:
            n += 1
        i += 12 + size
    return n


def windows(log, edges):
    """(frames, gray passes) between each step and the next. A frame the X3's idle step asks for once it has
    made the sharp cover of a progressive JPEG (outcome=1, Home standing still) is not counted."""
    frames, sharp = [], []
    gray = False
    for line in log.splitlines():
        m = re.match(r'^\[(\d+)\] \[\w+\] \[HOME\] (.*)$', line)
        if not m:
            continue
        if m.group(2).startswith('Idle cover outcome=1 '):
            sharp.append(int(m.group(1)))
        elif m.group(2).startswith('tenor/ink gray covers=') and 'ready=1' in m.group(2):
            gray = True
        elif m.group(2).startswith('Frame row='):
            frames.append((int(m.group(1)), gray))
            gray = False
    out = []
    for lo, hi in zip(edges, edges[1:] + [10 ** 9]):
        inside = [g for t, g in frames if lo <= t < hi]
        out.append((len(inside) - sum(1 for t in sharp if lo <= t < hi), sum(inside)))
    return out, len(frames)


def check_style(dev, program, old, work, style, port):
    sd = work / f's{style}' / 'sd'
    (sd / '.crosspoint').mkdir(parents=True)
    for name, title, seed, progressive in BOOKS:
        make_book(sd / name, title, None if seed is None else cover(abs(seed)), progressive)
    (sd / '.crosspoint' / 'recent.json').write_text(json.dumps({'books': [{'path': '/' + b[0], 'title': b[1]} for b in BOOKS]}))
    for i, (name, *_rest) in enumerate(reversed(BOOKS)):
        # The book without a cover stays on Home 2,5 s, long enough for its placeholder (1 s idle) only.
        run(program, sd, work / f's{style}' / f'read{i}', name, style, ['2500:BACK'],
            5000 if name == 'sach.epub' else 3300, port)
    # Read with a good cover, then damaged: the book keeps its thumbnails and cover.ref, Home's decode fails.
    for name, title, seed, progressive in BOOKS:
        if seed is not None and seed < 0:
            make_book(sd / name, title, 'damaged', progressive)
    key = 'SWIPE:420,400,60,400,150' if dev == 'x4' else 'RIGHT'
    edges = [FIRST + i * WAIT for i in range(len(BOOKS))]
    acts = [f'{t}:{key}' for t in edges]
    if old:
        # The card a former firmware left: its pass writes what it writes (placeholders at the frames of
        # books with a cover, for v1.0.58 before this check); the books' covers go, so they are first again.
        run(old, sd, work / f's{style}' / 'old', None, style, acts, edges[-1] + WAIT, port)
        for f in (sd / '.crosspoint').glob('epub_*/ink2_*'):
            f.unlink()
    bad = []
    for name in ('first', 'second'):
        log, trace = run(program, sd, work / f's{style}' / name, None, style, acts, edges[-1] + WAIT, port)
        counts, frames = windows(log, edges)
        panel = pushes(trace)
        print(f'{dev} style={style} {name}: (frames, gray passes) per change {counts}; '
              f'{frames} frames, {panel} panel pushes', flush=True)
        if panel != frames:
            bad.append(f'style={style} {name}: {panel} panel pushes for {frames} frames')
        for k, (n, g) in enumerate(counts):
            if name == 'first' and (n > 2 or (g > 1 and style != 2)):
                bad.append(f'style={style} first change {k + 1}: {n} frames, {g} gray passes')
            if name == 'second' and n != 1:
                bad.append(f'style={style} second change {k + 1}: {n} frames')
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dev', choices=['x3', 'x4'])
    ap.add_argument('program', type=Path)
    ap.add_argument('--style', type=int, nargs='*')
    ap.add_argument('--old', type=Path, help='a former firmware Home runs once on the card first (an upgrade)')
    ap.add_argument('--work', type=Path, help='keep the cards and logs here')
    a = ap.parse_args()
    program = a.program.resolve()
    styles = a.style if a.style is not None else ([0, 1, 2] if a.dev == 'x4' else [0])
    import concurrent.futures
    old = a.old.resolve() if a.old else None
    with tempfile.TemporaryDirectory(prefix='home-refresh-') as tmp:
        work = a.work.resolve() if a.work else Path(tmp)
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            results = list(pool.map(lambda s: check_style(a.dev, program, old, work, s, 29900 + s), styles))
    bad = [b for r in results for b in r]
    print('RED: ' + '; '.join(bad) if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
