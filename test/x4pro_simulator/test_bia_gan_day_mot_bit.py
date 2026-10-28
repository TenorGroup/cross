"""v1.1.1: the X4 Pro Recent cover is the 1 bit tenor/ink one even when only a 4-level one is on the card.

The Recent card draws 1 bit (HomeRefresh: no gray pass there). A 4-level file left from another surface or an
older card size used to stand in for the missing 1 bit one, and the black and white renderer thresholds it: a
dark gray cover came out solid black (How Big Things on the founder's X4 Pro after v1.1.0 moved the card to
281x424). Run 1 opens the book and goes Home, which makes the card's covers; run 2 finds only a dark gray
4-level file for the card's size and must make and draw the 1 bit one.
"""
import io
import json
import os
import re
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
FRAME = re.compile(r'tenor/ink frame cover=(\d+),(\d+),(\d+),(\d+)')


def write_epub(path):
    cover = Image.new('L', (600, 900), 128)  # mid gray: 1 bit tenor/ink is about half black
    jpeg = io.BytesIO()
    cover.convert('RGB').save(jpeg, 'JPEG', quality=90)
    body = '<p>' + 'Mot doan van. ' * 200 + '</p>'
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml',
                   '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                   '<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf',
                   '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
                   '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Bia xam</dc:title>'
                   '<dc:identifier id="id">bia-xam-v111</dc:identifier><dc:language>vi</dc:language>'
                   '<meta name="cover" content="cov"/></metadata><manifest>'
                   '<item id="cov" href="cover.jpg" media-type="image/jpeg"/>'
                   '<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>'
                   '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                   '<spine toc="ncx"><itemref idref="c1"/></spine></package>')
        z.writestr('toc.ncx',
                   '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/>'
                   '<docTitle><text>Bia xam</text></docTitle><navMap><navPoint id="n1" playOrder="1">'
                   '<navLabel><text>Mot</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('c1.xhtml', '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>c1</title>'
                               f'</head><body>{body}</body></html>')
        z.writestr('cover.jpg', jpeg.getvalue())


def dark_four_level_bmp(path, w, h):
    """A top-down 2 bpp BMP, every pixel on palette entry 1 (dark gray), laid out as the converter writes it."""
    row = (w * 2 + 31) // 32 * 4
    pixels = bytes([0x55]) * row * h  # four 2-bit pixels of value 1 a byte
    palette = bytes([0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0])
    offset = 14 + 40 + len(palette)
    header = b'BM' + struct.pack('<IHHI', offset + len(pixels), 0, 0, offset)
    info = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 2, 0, len(pixels), 2835, 2835, 4, 4)
    path.write_bytes(header + info + palette + pixels)


def run(sd, script, shot=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT=script)
    if shot:
        env['CROSSPOINT_SIM_SCREENSHOTS'] = shot
    done = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = done.stdout + done.stderr
    assert done.returncode == 0, log[-2000:]
    return log


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-bia-mot-bit-') as tmp:
        sd = Path(tmp) / 'sd'
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        write_epub(sd / 'book.epub')
        (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
        (store / 'recent.json').write_text(json.dumps({'books': [
            {'path': '/book.epub', 'title': 'Bia xam', 'author': 'Tenor', 'coverBmpPath': ''}]}))
        (store / 'settings.json').write_text(json.dumps({'language': 'VI', 'sleepTimeout': 120, 'tipsEnabled': 0}))
        first = run(sd, '3000:TAP:180,300;12000:HOME;20000:QUIT')
        frames = FRAME.findall(first)
        assert frames, 'Home drew no tenor/ink card\n' + first[-2000:]
        x, y, w, h = map(int, frames[-1])
        cache = next(store.glob('epub_*'))
        for f in {*cache.glob('ink2_*'), *cache.glob('*.card*')}:
            f.unlink()
        dark_four_level_bmp(cache / f'ink2_4_{w}x{h}.bmp', w, h)

        shot = Path(tmp) / 'home.bmp'
        second = run(sd, '9000:QUIT', f'8000:{shot}')
        assert FRAME.findall(second)[-1:] == [(str(x), str(y), str(w), str(h))], 'run 2 laid the card out elsewhere'
        with Image.open(shot) as image:
            box = image.convert('L').crop((x + 12, y + 12, x + w - 12, y + h - 12))
            black = sum(1 for p in box.getdata() if p < 128) / (box.width * box.height)
        print(f'cover {w}x{h} black={black:.2f}')
        assert (cache / f'ink2_1_{w}x{h}.bmp').exists(), 'Home did not make the 1 bit cover\n' + second[-2000:]
        assert black < 0.8, f'the cover is solid black ({black:.2f}): the 4-level file was drawn in black and white'
        # A mid gray source dithers to about half black; an empty frame or ring is near 0.
        assert black > 0.3, f'no cover in the frame ({black:.2f}): the 1 bit cover was made but not drawn'
        print('GREEN: the Recent cover is the 1 bit tenor/ink one')


if __name__ == '__main__':
    main()
