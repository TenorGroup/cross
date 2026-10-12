"""X3 Button hints switch (v1.0.59): the row in Display, the clock and battery strip at the top while the hints are
off, and the reader left as it was.

TEST_PROGRAM=<simulator_x3_uc8279 program> python3 test_button_hints_v1059.py [switch|layout|reader|all] [--out DIR]
"""
import argparse
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
W, H = 528, 792
STRIP = 32  # the touch status strip's height, the X3 top strip reuses it
BASE = dict(language='VI', uiTheme=4, uiShell=0, sleepTimeoutMinutes=10, textSpacingVersion=3,
            paragraphIndentVersion=1, readerInkWeightVersion=1, tenorPresetVersion=1, uiShellSleepMemo=0,
            tipsEnabled=1, tenorButtonSymbols=1, globalStatusBarMode=0, statusBarClock=1, clockHasBeenSynced=1,
            clockAutoTimezone=0, clockTimezone=255, clockUtcOffsetQ=28, buttonHintsVersion=1)


def make_book(path):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    body = ''.join(f'<p>Paragraph {n} keeps the page full so the menu has text under it to cover.</p>' for n in range(40))
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:'
                   'xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" '
                   'media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('c1.xhtml', f'{head}<h1>One</h1>{body}</body></html>')
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                   'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Book</dc:title>'
                   '<dc:identifier id="id">hints59</dc:identifier><dc:language>en</dc:language></metadata><manifest>'
                   '<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/></manifest><spine>'
                   '<itemref idref="c1"/></spine></package>')


def run(out, name, script, shots, settings, book=False):
    folder = out / name
    if folder.exists():
        shutil.rmtree(folder)
    store = folder / 'sd/.crosspoint'
    store.mkdir(parents=True)
    make_book(folder / 'sd/book.epub')
    (store / 'settings.json').write_text(json.dumps({**BASE, 'wakeIntoBook': int(book), **settings}))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/book.epub' if book else '',
                                                  'lastSleepFromReader': book}))
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Book'}]}))
    last = max(shots) if shots else 1500
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(folder / 'sd'), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345',
               CROSSPOINT_SIM_INPUT_SCRIPT=(script + ';' if script else '') + f'{last + 700}:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{label}.bmp' for ms, label in shots.items()))
    p = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
    (folder / 'run.log').write_text(p.stdout + p.stderr)
    assert p.returncode == 0, (name, (p.stdout + p.stderr)[-1500:])
    images = {label: Image.open(folder / f'{label}.bmp').convert('L') for label in shots.values()}
    return images, json.loads((store / 'settings.json').read_text())


def ink_rows(im, x0, x1, y0, y1):
    return [y for y in range(y0, y1) if any(im.getpixel((x, y)) < 128 for x in range(x0, x1))]


def ink_box(im, box):
    mask = im.crop(box).point(lambda v: 255 if v < 128 else 0)
    b = mask.getbbox()
    return None if b is None else (b[0] + box[0], b[1] + box[1], b[2] + box[0], b[3] + box[1])


def switch(out):
    # Home Settings opens on Display: UI size, General status bar, then Button hints.
    _, saved = run(out, 'switch', '1000:UP;1600:CONFIRM;2600:RIGHT;3200:RIGHT;3800:CONFIRM;4600:BACK',
                   {4400: 'row'}, {})
    assert saved.get('tenorButtonHints') == 0, ('Button hints row toggles tenorButtonHints', saved.get('tenorButtonHints'))
    assert saved['tipsEnabled'] == 1 and saved['globalStatusBarMode'] == 0, 'only the Button hints row changes'
    print('GREEN switch: Display row 3 toggles tenorButtonHints 1 -> 0', flush=True)


def layout(out):
    shots = {}
    for clock in (1, 2):
        for hints in (1, 0):
            images, _ = run(out, f'home-c{clock}-h{hints}', '', {2500: 'home'},
                            {'statusBarClock': clock, 'tenorButtonHints': hints})
            shots[clock, hints] = images['home']
    result = {}
    for clock in (1, 2):
        off, on = shots[clock, 0], shots[clock, 1]
        rows = ink_rows(off, 0, W, 0, STRIP)
        assert rows and max(rows) < STRIP - 2, ('clock and battery in the top strip', clock, rows[-3:] if rows else rows)
        left, right = ink_box(off, (0, 0, W // 2, STRIP)), ink_box(off, (W // 2, 0, W, STRIP))
        assert left and right, ('one item at each end of the strip', clock, left, right)
        assert not ink_rows(off, 0, W, STRIP, 48), ('content starts 16 px under the strip', clock)
        assert not ink_rows(off, 0, W, H - 16, H), ('the bar sits 16 px over the foot', clock)
        assert ink_rows(on, 0, W, H - 16, H), ('hints on keep the foot band', clock)
        centre = [(b[1] + b[3] - 1) / 2 for b in (left, right)]
        assert abs(centre[0] - centre[1]) <= 0.5, ('clock and battery share a centre line', clock, centre)
        result[clock] = {'left': left, 'right': right, 'centre_delta': centre[0] - centre[1]}
    # One placement setting for both places: the clock moves to the other end of the strip.
    clock_right = shots[1, 0].crop(result[1]['right'])
    clock_left = shots[2, 0].crop(result[2]['left'])
    assert ImageChops.difference(clock_right, clock_left).getbbox() is None, 'the same clock ink at either end'
    # General status bar Off keeps its old meaning: no hints, no clock, no battery, whatever the switch says.
    a, _ = run(out, 'off-h1', '', {2500: 'home'}, {'globalStatusBarMode': 1, 'tenorButtonHints': 1})
    b, _ = run(out, 'off-h0', '', {2500: 'home'}, {'globalStatusBarMode': 1, 'tenorButtonHints': 0})
    assert ImageChops.difference(a['home'], b['home']).getbbox() is None, 'status bar Off ignores the switch'
    print('GREEN layout:', json.dumps(result), flush=True)


def reader(out):
    frames = {}
    for hints in (1, 0):
        images, _ = run(out, f'reader-h{hints}', '2600:CONFIRM', {2300: 'page', 4200: 'menu'},
                        {'tenorButtonHints': hints, 'readerTapTip': 0}, book=True)
        frames[hints] = images
    for label in ('page', 'menu'):
        diff = ImageChops.difference(frames[1][label], frames[0][label]).getbbox()
        assert diff is None, ('the reader does not change with the switch', label, diff)
    print('GREEN reader: page and menu identical with the hints on and off', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('which', nargs='?', default='all', choices=('switch', 'layout', 'reader', 'all'))
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = args.out or Path(tempfile.mkdtemp(prefix='button-hints-'))
    out.mkdir(parents=True, exist_ok=True)
    for test in (switch, layout, reader):
        if args.which in ('all', test.__name__):
            test(out)
