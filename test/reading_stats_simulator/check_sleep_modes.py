#!/usr/bin/env python3
"""Every sleep screen of the Settings list, entered from Home, on the X3 (UC8279) and the X4 Pro simulators.

check_sleep_modes.py <out dir> --x3 <simulator_x3_uc8279 program> --x4pro <simulator_x4pro program>
                     [--compare <out dir of an earlier run>]

The card holds what the screens can show: a book with a cover open in the state (Cover), /sleep.bmp
(Custom), a transparent overlay (Transparent), one quote (Book + quote). "10-none" is Book + quote with
no quote saved, which falls back to the tenor/cross screen. Each run keeps <device>-<mode>.png and a
table (table.md). The check: on the X4 Pro the tenor/cross screen, and the quote screen's fallback to
it, are not the default "CrossPoint / SLEEPING" screen (mode 0 Dark) but the gray tenor/cross art.
RED before the X4 Pro had a sleep art. --compare counts the pixels each screen changed against the
earlier run.
"""
import argparse
import io
import json
import os
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

REPO = Path(__file__).resolve().parents[2]
MODES = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '10', '10-none', '11']
NAMES = {'0': 'Tối (Dark)', '1': 'Sáng (Light)', '2': 'Ảnh riêng (Custom)', '3': 'Bìa (Cover)',
         '4': 'Bìa hoặc ảnh riêng', '5': 'Không có (Blank)', '6': 'Khôi phục nhanh', '7': 'Trong suốt',
         '8': 'tenor/cross', '9': 'Thống kê', '10': 'Sách + trích dẫn', '10-none': 'Sách + trích dẫn, chưa có trích',
         '11': 'tenor/ugly'}
BOOK = '/books/bia.epub'
SLEEP_AT = 3000


def cover_jpeg():
    image = Image.new('L', (600, 900), 230)
    draw = ImageDraw.Draw(image)
    for i, tone in enumerate((32, 96, 160)):
        draw.rectangle((60, 120 + i * 220, 540, 300 + i * 220), fill=tone)
    out = io.BytesIO()
    image.convert('RGB').save(out, 'JPEG', quality=95)
    return out.getvalue()


def write_epub(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Bia thu</dc:title><dc:identifier id="id">sleep-modes</dc:identifier><dc:language>vi</dc:language><meta name="cover" content="cover"/></metadata><manifest><item id="cover" href="cover.jpg" media-type="image/jpeg"/><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('cover.jpg', cover_jpeg())
        z.writestr('body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body><p>' + 'Một đoạn văn thử. ' * 100 + '</p></body></html>')


def custom_bmp(size):
    image = Image.new('L', size, 255)
    draw = ImageDraw.Draw(image)
    for i in range(0, size[0], 40):
        draw.rectangle([i, 100, i + 19, size[1] - 100], fill=40)
    out = io.BytesIO()
    image.convert('RGB').save(out, 'BMP')
    return out.getvalue()


def overlay_bmp(size):
    """32-bit BGRA, a black disc on transparent: what the Transparent mode reads."""
    w, h = size
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            inside = (x - w // 2) ** 2 + (y - h // 2) ** 2 < (w // 4) ** 2
            row += bytes((0, 0, 0, 255 if inside else 0))
        rows.append(bytes(row))
    data = b''.join(rows)
    header = struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 0, len(data), 2835, 2835, 0, 0)
    return header + info + data


def card(root, device, mode, size):
    sd = root / f'sd-{device}-{mode}'
    store = sd / '.crosspoint'
    store.mkdir(parents=True, exist_ok=True)
    value = int(mode.split('-')[0])
    settings = {'language': 'VI', 'uiShell': 1 if value == 11 else 0, 'uiTheme': 4, 'sleepScreen': value,
                'sleepTimeout': 10, 'wakeIntoBook': 0, 'textSpacingVersion': 3, 'paragraphIndentVersion': 1,
                'readerInkWeightVersion': 1, 'tenorPresetVersion': 1, 'uiShellSleepMemo': 0, 'sdFontFamilyName': ''}
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': BOOK}))
    write_epub(sd / BOOK.lstrip('/'))
    (sd / 'sleep.bmp').write_bytes(custom_bmp(size))
    (sd / 'sleep-overlay.bmp').write_bytes(overlay_bmp(size))
    if mode == '10':
        sys.path.insert(0, str(REPO / 'test/reading_stats_simulator'))
        from test_sleep_quote_v1011 import quote_id
        record = {'schema': 1, 'path': BOOK, 'title': 'Bia thu', 'text': 'Một câu trích để thử màn ngủ.',
                  'spine': 0, 'page': 0, 'day': 20261012, 'gio': 480}
        quotes = store / 'quotes'
        quotes.mkdir()
        (quotes / '.ten-v2').write_text('2')
        (quotes / f'{quote_id(record, 0):016x}.json').write_text(json.dumps(record, ensure_ascii=False))
    return sd


def sleep_shot(program, sd, out_png):
    times = [SLEEP_AT + 250 * i for i in range(1, 24)]
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT=f'{SLEEP_AT}:SLEEP;{times[-1] + 500}:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{sd / f"shot-{t}.bmp"}' for t in times))
    run = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = run.stdout + run.stderr
    assert run.returncode == 0, log[-3000:]
    assert 'Entering deep sleep' in log, log[-3000:]
    taken = [sd / f'shot-{t}.bmp' for t in times if (sd / f'shot-{t}.bmp').exists()]
    image = Image.open(taken[-1]).convert('L')
    image.save(out_png)
    for path in taken:
        path.unlink()
    sleep = log.split('Entering activity: Sleep', 1)[1].split('Entering deep sleep', 1)[0]
    (out_png.with_suffix('.log')).write_text(sleep)
    return image, sleep


def path_of(sleep):
    for key, label in (('[BRAND] sleep ready=1', 'renderBrandScreen (ảnh xám tenor/cross)'),
                       ('[BRAND] sleep folded', 'khung gấp sẵn ManNguTenor'),
                       ('[UGLY] sleep ready=1', 'ugly::drawSleep'), ('Sleep quote ', 'renderQuoteSleepScreen'),
                       ('Sleep image ', 'renderBitmapSleepScreen')):
        if key in sleep:
            return label
    return ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', type=Path)
    ap.add_argument('--x3', type=Path, required=True)
    ap.add_argument('--x4pro', type=Path, required=True)
    ap.add_argument('--compare', type=Path)
    a = ap.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    work = out / 'sd'
    rows, images = [], {}
    for device, program, size in (('x3', a.x3, (528, 792)), ('x4pro', a.x4pro, (480, 800))):
        for mode in MODES:
            image, sleep = sleep_shot(program.resolve(), card(work, device, mode, size), out / f'{device}-{mode}.png')
            images[device, mode] = image
            rows.append((device, mode, image, sleep))
    lines = ['| Kiểu | Máy | Ra gì | Đường mã | Mức xám | Điểm ảnh đổi so lần trước |', '|---|---|---|---|---|---|']
    for device, mode, image, sleep in rows:
        default = mode != '0' and ImageChops.difference(image, images[device, '0']).getbbox() is None
        what = 'màn mặc định CrossPoint/SLEEPING' if default or mode == '0' else 'riêng'
        levels = len(set(image.getdata()))
        changed = ''
        if a.compare:
            before = Image.open(a.compare / f'{device}-{mode}.png').convert('L')
            changed = str(sum(1 for p in ImageChops.difference(image, before).getdata() if p))
        code = 'renderDefaultSleepScreen' if default or mode == '0' else path_of(sleep)
        lines.append(f'| {mode} {NAMES[mode]} | {device} | {what} | {code} | {levels} | {changed} |')
    (out / 'table.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    red = []
    for mode in ('8', '10-none'):
        image = images['x4pro', mode]
        if ImageChops.difference(image, images['x4pro', '0']).getbbox() is None or len(set(image.getdata())) < 4:
            red.append(mode)
    if red:
        print('RED: X4 Pro sleeps on the default SLEEPING screen for mode', ', '.join(red))
        sys.exit(1)
    print('GREEN: X4 Pro tenor/cross sleep screen is the gray art')


if __name__ == '__main__':
    main()
