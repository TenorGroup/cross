"""v1.0.14 Recent card cover drawn at the thumbnail's own pixels.

1. The thumbnail rule was width = 0.6 x height, the card's cover is 298 x 450 (0.662; 236 x 356 before 04/10). A cover
   narrower than the card came out narrower than 298 and the card stretched it by repeating
   columns and rows: on the X3 a 780 x 1227 cover showed 9 doubled columns and 15 doubled rows.
   The thumbnail now takes the card's own shape, so the card draws it 1:1, cropped at the centre.
   Both routes are checked: the cover file decoded when the reader closes, and the cover page's
   own decode (GrayThumb).
2. A thumbnail written under the old rule keeps its old name, so the card never draws one: every
   book gets a new thumbnail the next time it is opened.
3. The 1-bit dither keeps the cover's tones. Atkinson spread 3/4 of each error, so flat tones
   drifted (32 came out black, 224 white) and the thumbnail seen at reading distance (blurred)
   was five times further from the cover than Floyd-Steinberg's (a2/do_a2.py, v1.0.14).

The cover is fine noise, so no two neighbouring columns or rows of a correct card are alike.
Set CROSSPOINT_TEST_ARTIFACTS to keep the screenshots.
"""
import io
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image

from test_cover_thumb_v1013 import blurred_error
from test_home_card_v1011 import CARD_BUILD, FIXTURE, PROGRAM

PATH = '/sach/bia-hep.epub'
TITLE = 'Bìa hẹp hơn khung thẻ'
COVER_W, COVER_H = 780, 1227  # a common portrait cover, narrower than the card's 298:450
CARD = (24, 124, 322, 574)  # the cover on the default X3 card (HomeExcerptStyle.h)
INSET = 12  # clear of the rounded corners
TONES = (32, 64, 128, 192, 224)
THUMBS = re.compile(r'Cover thumbnail (\d+) px: \d+ ms, ok=1, page=(\d)')


def noise_jpeg(width=COVER_W, height=COVER_H, seed=3):
    rng = random.Random(seed)
    # 2 x 2 cells keep the file small; every thumbnail pixel still averages several of them.
    small = Image.new('L', ((width + 1) // 2, (height + 1) // 2))
    small.putdata([rng.randrange(40, 216) for _ in range(small.width * small.height)])
    out = io.BytesIO()
    small.resize((width, height), Image.NEAREST).convert('RGB').save(out, 'JPEG', quality=92)
    return out.getvalue()


def write_book(target, cover_page, cover=None):
    """The synonyms fixture with a JPEG cover, and with it as the first page when `cover_page`."""
    target.parent.mkdir(parents=True, exist_ok=True)
    page = ('<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
            '<title>Cover</title></head><body><div><img src="cover.jpg" alt="cover"/></div></body></html>')
    with zipfile.ZipFile(FIXTURE) as source, zipfile.ZipFile(target, 'w') as book:
        for item in source.infolist():
            data = source.read(item.filename)
            if item.filename == 'book.opf':
                text = data.decode('utf-8')
                text = text.replace('</metadata>', '<meta name="cover" content="cover"/></metadata>')
                extra = '<item id="cover" href="cover.jpg" media-type="image/jpeg"/>'
                if cover_page:
                    extra += '<item id="coverpage" href="cover.xhtml" media-type="application/xhtml+xml"/>'
                    text = text.replace('<spine>', '<spine><itemref idref="coverpage"/>')
                text = text.replace('</manifest>', extra + '</manifest>')
                data = text.encode('utf-8')
            compress = zipfile.ZIP_STORED if item.filename == 'mimetype' else zipfile.ZIP_DEFLATED
            book.writestr(item, data, compress_type=compress)
        if cover_page:
            book.writestr('cover.xhtml', page, compress_type=zipfile.ZIP_DEFLATED)
        book.writestr('cover.jpg', cover or noise_jpeg(), compress_type=zipfile.ZIP_STORED)


def tone_jpeg(width=600, height=906):
    """Five flat tones in bands, darkest to lightest, over a smooth ramp on the right third."""
    image = Image.new('L', (width, height))
    px = image.load()
    for y in range(height):
        for x in range(width):
            px[x, y] = TONES[y * len(TONES) // height] if x < width * 2 // 3 else y * 255 // (height - 1)
    out = io.BytesIO()
    image.convert('RGB').save(out, 'JPEG', quality=95)
    return out.getvalue()


def doubled(image, box):
    """Neighbouring columns, then neighbouring rows, that are pixel for pixel alike inside `box`."""
    crop = image.crop(box)
    px = crop.load()
    w, h = crop.size
    cols = [tuple(px[x, y] for y in range(h)) for x in range(w)]
    rows = [tuple(px[x, y] for x in range(w)) for y in range(h)]
    return (sum(cols[i] == cols[i + 1] for i in range(w - 1)), sum(rows[i] == rows[i + 1] for i in range(h - 1)))


class CardCoverShapeTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-card-shape-v1014-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text('{"language": "VI", "uiTheme": 4, "sleepTimeout": 120}')
        (self.store / 'recent.json').write_text(
            '{"books": [{"path": "%s", "title": "%s", "author": "Tenor", "coverBmpPath": ""}]}' % (PATH, TITLE),
            encoding='utf-8')
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None

    def home_after_reading(self, label):
        # Home, Select opens the book, Right turns a page, Back closes it (the thumbnails are written
        # as it closes), the card draws the new cover.
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        shot = self.sd / 'home.bmp'
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='1500:CONFIRM;6500:RIGHT;8500:BACK;13000:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'12500:{shot}')
        run = subprocess.run([str(PROGRAM)], cwd=Path(__file__).resolve().parents[2], env=env,
                             capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        with Image.open(shot) as image:
            home = image.convert('L')
        if self.artifacts:
            self.artifacts.mkdir(parents=True, exist_ok=True)
            home.save(self.artifacts / f'{label}-home.png')
        return log, home

    def check_card(self, cover_page, label):
        write_book(self.sd / PATH.lstrip('/'), cover_page)
        log, home = self.home_after_reading(label)
        routes = THUMBS.findall(log)
        self.assertIn(('450', '1' if cover_page else '0'), routes, log[-6000:])
        builds = CARD_BUILD.findall(log)
        self.assertTrue(builds and int(builds[-1][2]) == 450, 'the card drew no 450 px thumbnail\n' + log[-4000:])
        thumbs = sorted(self.store.glob('epub_*/thumb*_450.bmp'))
        self.assertEqual(len(thumbs), 1, thumbs)
        self.assertNotEqual(thumbs[0].name, 'thumb_450.bmp', 'a thumbnail of the old rule would be kept')
        with Image.open(thumbs[0]) as thumb:
            size = thumb.size
        x0, y0, x1, y1 = CARD
        cols, _ = doubled(home, (x0, y0 + INSET, x1, y1 - INSET))
        _, rows = doubled(home, (x0 + INSET, y0, x1 - INSET, y1))
        print('CARD_SHAPE', label, 'thumbnail', size, 'doubled columns', cols, 'rows', rows)
        self.assertEqual((cols, rows), (0, 0), f'{label}: the card stretched its thumbnail')
        # The card's width, and the cover's height at that width.
        # The cover decode rounds the height (469), the page decode floors it (468); the card crops both to 450.
        self.assertEqual(size[0], 298, size)
        self.assertIn(size[1], (COVER_H * 298 // COVER_W, COVER_H * 298 // COVER_W + 1), size)

    def test_thumbnail_keeps_the_cover_tones(self):
        cover = tone_jpeg()
        write_book(self.sd / PATH.lstrip('/'), False, cover)
        self.home_after_reading('tong')
        thumbs = sorted(self.store.glob('epub_*/thumb*_450.bmp'))
        self.assertEqual(len(thumbs), 1, thumbs)
        with Image.open(thumbs[0]) as image:
            thumb = image.convert('L')
        if self.artifacts:
            thumb.save(self.artifacts / 'tong-thumbnail.png')
        source = Image.open(io.BytesIO(cover)).convert('L')
        error = blurred_error(thumb, source)
        # Share of white in the middle of each flat band, as a tone.
        w, h = thumb.size
        tones = []
        for i, tone in enumerate(TONES):
            band = thumb.crop((4, h * i // len(TONES) + 8, w * 2 // 3 - 4, h * (i + 1) // len(TONES) - 8))
            tones.append((tone, round(sum(band.getdata()) / (band.width * band.height))))
        print('THUMB_TONES', thumbs[0].name, thumb.size, 'blurred_error', round(error, 2), tones)
        self.assertLessEqual(error, 5.0, tones)
        for tone, seen in tones:
            self.assertLessEqual(abs(seen - tone), 12, tones)

    def test_cover_file_thumbnail_fills_the_card_one_to_one(self):
        self.check_card(False, 'tep-bia')

    def test_cover_page_thumbnail_fills_the_card_one_to_one(self):
        self.check_card(True, 'trang-bia')


if __name__ == '__main__':
    unittest.main()
