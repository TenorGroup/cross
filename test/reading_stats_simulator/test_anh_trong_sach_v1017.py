"""v1.0.17: pictures inside a book, shrunk to the page.

The X3 draws a 780 x 1227 picture at 482 x 759, a factor of 0,618. JPEG took one source pixel
per output pixel and PNG skipped whole rows and columns, so fine stripes and rings turned into
bands (moire). JPEG now blends the two source rows and columns around each output pixel's center,
PNG averages each output pixel's source cell, and the 4x4 Bayer dither stays (stateless, so the
page can redraw from the pixel cache band by band).

1. A picture of thin stripes and a zone plate, as JPEG and as PNG, read back from the `.pxc` the
   page left (the page's 2-bit levels, independent of SCREENSHOT) and compared at reading distance
   (blurred_error: both images blurred, the reference shrunk with BOX) with the source must stay
   under a bound the point sampling exceeds.
2. A `.pxc` written before the file carried its format (width and height only) is not drawn: the
   picture is decoded again and the file rewritten with the current header.
3. One-row blocks (this simulator's decoder) make every source row a block edge: a picture of
   straight horizontal ramps must come out with no row off the blend of its two source rows.
"""
import io
import json
import math
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
SRC = (780, 1227)
DST = (482, 759)
PLACED = re.compile(r'Rendering image at (\d+),(\d+): (\S+) \((\d+)x(\d+)\)')


def blurred_error(levels, reference):
    """Mean absolute difference of two images seen at reading distance (dither blurred away)."""
    size = levels.size
    a = levels.filter(ImageFilter.GaussianBlur(2))
    b = reference.resize(size, Image.BOX).filter(ImageFilter.GaussianBlur(2))
    diff = ImageChops.difference(a, b)
    return sum(i * n for i, n in enumerate(diff.histogram())) / (size[0] * size[1])


def moire_source():
    """Thin vertical stripes, thin horizontal stripes and a zone plate (rings closing in)."""
    w, h = SRC
    image = Image.new('L', SRC)
    px = image.load()
    cx, cy = w / 2, h * 0.7
    for y in range(h):
        for x in range(w):
            if y < h * 0.2:
                v = 255 if x % 3 == 0 else 0
            elif y < h * 0.4:
                v = 255 if y % 3 == 0 else 0
            else:
                v = int(127.5 + 127.5 * math.cos(((x - cx) ** 2 + (y - cy) ** 2) / 900.0))
            px[x, y] = v
    return image


def ramp_source():
    """Rows of one gray each, rising and falling 32 steps a row: straight between the rows."""
    w, h = SRC
    image = Image.new('L', SRC)
    for y in range(h):
        phase = y % 16
        image.paste((phase if phase < 8 else 16 - phase) * 255 // 8, (0, y, w, y + 1))
    return image


def bayer(gray, x, y):
    matrix = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
    adjusted = min(max(gray + (matrix[y & 3][x & 3] - 8) * 5, 0), 255)
    return 0 if adjusted < 64 else 1 if adjusted < 128 else 2 if adjusted < 192 else 3


def read_pxc(path):
    """(width, height, levels as an 'L' image at 85 per level, has_format_header)."""
    data = path.read_bytes()
    header = 8 if data[:3] == b'PXC' else 4
    w = data[header - 4] | data[header - 3] << 8
    h = data[header - 2] | data[header - 1] << 8
    row = (w + 3) // 4
    image = Image.new('L', (w, h))
    px = image.load()
    for y in range(h):
        base = header + y * row
        for x in range(w):
            px[x, y] = ((data[base + x // 4] >> (6 - (x & 3) * 2)) & 3) * 85
    return w, h, image, header == 8


def book(target, pictures):
    """An EPUB with one page per picture: [(name, bytes, media type)]."""
    page = ('<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
            '<title>p</title></head><body><div><img src="%s" alt="p"/></div></body></html>')
    items = ''.join(f'<item id="p{i}" href="p{i}.xhtml" media-type="application/xhtml+xml"/>'
                    f'<item id="i{i}" href="{name}" media-type="{kind}"/>' for i, (name, _, kind) in enumerate(pictures))
    spine = ''.join(f'<itemref idref="p{i}"/>' for i in range(len(pictures)))
    with zipfile.ZipFile(target, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        epub.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:'
                      'opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Anh'
                      '</dc:title><dc:identifier id="id">anh-v1017</dc:identifier></metadata><manifest>' + items +
                      '</manifest><spine>' + spine + '</spine></package>')
        for i, (name, data, _) in enumerate(pictures):
            epub.writestr(f'p{i}.xhtml', page % name)
            epub.writestr(name, data, compress_type=zipfile.ZIP_STORED)


def encoded(image, fmt, **options):
    out = io.BytesIO()
    image.save(out, fmt, **options)
    return out.getvalue()


class BookPictureTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-anh-v1017-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps({'language': 'VI'}))
        (self.store / 'state.json').write_text(
            json.dumps({'openEpubPath': '', 'lastSleepFromReader': False, 'showBootScreen': False}))
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': '/anh.epub', 'title': 'Anh', 'author': 'Tenor', 'coverBmpPath': ''}]}))

    def read_book(self, pages):
        """Home, Select opens the book; Right turns to each further page."""
        script = ['1500:CONFIRM'] + [f'{9000 + 6000 * i}:RIGHT' for i in range(pages - 1)]
        script.append(f'{9000 + 6000 * (pages - 1) + 3000}:QUIT')
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(script))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        return log

    def pxc(self, log, picture):
        """The .pxc of the page picture named `picture` (img_<spine>_<n>) and where it was placed."""
        for x, y, path, w, h in PLACED.findall(log):
            if Path(path).stem == picture:
                return self.sd / path.lstrip('/').rsplit('.', 1)[0], (int(x), int(y))
        self.fail(f'{picture} never drawn\n' + log[-3000:])

    def test_shrunk_stripes_and_rings_keep_their_tone(self):
        source = moire_source()
        book(self.sd / 'anh.epub', [('m.jpg', encoded(source, 'JPEG', quality=95), 'image/jpeg'),
                                     ('m.png', encoded(source, 'PNG'), 'image/png')])
        log = self.read_book(2)
        # The bound: what the 4x4 Bayer dither of the ideal (BOX) shrink scores, plus a margin; the
        # point sampling scored far above it (measured in the docstring of each assert below).
        ideal = source.resize(DST, Image.BOX)
        dithered = Image.new('L', DST)
        ip, dp = ideal.load(), dithered.load()
        for y in range(DST[1]):
            for x in range(DST[0]):
                dp[x, y] = bayer(ip[x, y], x, y) * 85
        floor = blurred_error(dithered, source)
        errors = {}
        for picture in ('img_0_0', 'img_1_0'):
            stem, _ = self.pxc(log, picture)
            w, h, levels, _ = read_pxc(Path(str(stem) + '.pxc'))
            self.assertEqual((w, h), DST)
            errors[picture] = blurred_error(levels, source)
        print('blurred_error ' + ' '.join(f'{k}={v:.2f}' for k, v in errors.items()) + f' bayer floor={floor:.2f}')
        # Measured 26/09 in this simulator: point sampling JPEG 19,07 and PNG 16,10; bilinear JPEG
        # 7,53, area PNG 6,89. The floor is PIL's BOX (center-sampled whole pixels) dithered the same
        # way, not an exact area average, so a correct filter does not reach it on period-3 stripes.
        self.assertLess(errors['img_0_0'], 10.0, errors)
        self.assertLess(errors['img_1_0'], 10.0, errors)

    def test_cache_without_its_format_is_decoded_again(self):
        source = moire_source()
        book(self.sd / 'anh.epub', [('m.jpg', encoded(source, 'JPEG', quality=95), 'image/jpeg')])
        stem, _ = self.pxc(self.read_book(1), 'img_0_0')
        path = Path(str(stem) + '.pxc')
        row = (DST[0] + 3) // 4
        size = bytes([DST[0] & 255, DST[0] >> 8, DST[1] & 255, DST[1] >> 8])
        # What an earlier release left (width, height and rows), and a file of an earlier format,
        # both all black.
        for old in (size, b'PXC\x01' + size):
            path.write_bytes(old + bytes(row * DST[1]))
            log = self.read_book(1)
            self.assertIn('Decoding and caching', log, log[-3000:])
            data = path.read_bytes()
            self.assertEqual(data[:4], b'PXC\x02', old)
            self.assertGreater(sum(data[8:]), 0, 'the picture is still the old black file')

    def test_every_row_blends_its_two_source_rows(self):
        source = ramp_source()
        book(self.sd / 'anh.epub', [('r.jpg', encoded(source, 'JPEG', quality=100), 'image/jpeg')])
        log = self.read_book(1)
        stem, (ox, oy) = self.pxc(log, 'img_0_0')
        w, h, levels, _ = read_pxc(Path(str(stem) + '.pxc'))
        decoded = Image.open(io.BytesIO(encoded(source, 'JPEG', quality=100))).convert('L').load()
        rows = [decoded[w // 2, y] for y in range(SRC[1])]
        step = SRC[1] / h
        lv = levels.load()
        off_rows = []
        for y in range(h):
            fy = (y + 0.5) * step - 0.5
            y0 = min(int(math.floor(fy)), SRC[1] - 1)
            y1 = min(y0 + 1, SRC[1] - 1)
            gray = rows[y0] + (rows[y1] - rows[y0]) * (fy - y0)
            # The levels a gray within three steps of the blend dithers to, per Bayer column.
            allowed = [{bayer(g, k, oy + y) for g in range(max(0, round(gray) - 3), min(255, round(gray) + 3) + 1)}
                       for k in range(4)]
            off = sum(1 for x in range(w) if lv[x, y] // 85 not in allowed[(ox + x) & 3])
            if off * 20 > w:
                off_rows.append(y)
        print(f'rows off the blend: {len(off_rows)} of {h}')
        self.assertEqual(off_rows, [])


if __name__ == '__main__':
    unittest.main()
