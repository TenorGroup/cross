"""v1.0.13 cover thumbnail of a book opened for the first time.

1. With the page-turner radio on, the device reads with about 61 KB free (the radio holds the
   rest). The thumbnail used to wait for 96 KB on an idle pass while reading, so it never came,
   and the Recent card had no cover for every new book. The run below holds the heap at the
   radio's level from start to end; the card must still draw the book's cover after Back.
2. The two cover decodes used to run on idle passes while the page was being read, each holding
   the buttons for seconds on the X3. They now run when the reader closes, so no thumbnail line
   may appear before the last page painted in the reader.
3. A thumbnail keeps a fraction of the cover's pixels, so the JPEG is decoded on a reduced grid
   instead of at full size.
4. The reading pages (the cover page and a text page) are unchanged: their screenshots land in
   CROSSPOINT_TEST_ARTIFACTS for a pixel comparison between two builds.
5. A book that opens on its cover page decodes the cover for that page anyway (the X3 spent 0,9 s
   copying it out of the book and 2,1 s decoding it). Writing the thumbnails copied and decoded
   it twice more when the reader closed: 5,6 s on the Home key. They now take the rows of the
   page's own decode, and must match what the cover decode gives.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageChops, ImageFilter

from test_home_card_v1011 import CARD_BUILD, FIXTURE, PROGRAM, THUMB, cover_jpeg, epub_with_cover

PATH = '/sach/cuon-moi.epub'
TITLE = 'Cuốn sách mới mở'
# The radio's heap on the X3 right after the first page of a new book (cand-newbook-open.log:
# MEM Free: 61204 bytes, MaxAlloc: 55284 bytes).
RADIO_HEAP = {'CROSSPOINT_SIM_FREE_HEAP': '61204', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '55284'}
GRID = re.compile(r'Scaling source (\d+)x(\d+) \(decode grid (\d+)x(\d+)\) -> (\d+)x(\d+)')
COVER_BOX = (24, 124, 322, 574)
# The route a thumbnail took: page=1 when it was written from the cover page's own decode.
THUMB_ROUTE = re.compile(r'Cover thumbnail (\d+) px: \d+ ms, ok=1, page=(\d)')
# v1.0.16: one decode of the cover file for both thumbnails, logged with its grid's scale.
SHARED_DECODE = re.compile(r'Cover thumbnail decode: \d+ ms, scale=1/(\d), ok=1')


def blurred_error(thumb, reference):
    """Mean absolute difference of two images seen at reading distance (dither blurred away)."""
    size = thumb.size
    a = thumb.convert('L').filter(ImageFilter.GaussianBlur(2))
    b = reference.resize(size, Image.BOX).filter(ImageFilter.GaussianBlur(2))
    diff = ImageChops.difference(a, b)
    return sum(i * n for i, n in enumerate(diff.histogram())) / (size[0] * size[1])


def book_with_cover_page(target):
    """The synonyms fixture with a declared JPEG cover and a cover page as the first spine item."""
    target.parent.mkdir(parents=True, exist_ok=True)
    page = ('<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
            '<title>Cover</title></head><body><div><img src="cover.jpg" alt="cover"/></div></body></html>')
    with zipfile.ZipFile(FIXTURE) as source, zipfile.ZipFile(target, 'w') as book:
        for item in source.infolist():
            data = source.read(item.filename)
            if item.filename == 'book.opf':
                text = data.decode('utf-8')
                text = text.replace('</metadata>', '<meta name="cover" content="cover"/></metadata>')
                text = text.replace('</manifest>', '<item id="cover" href="cover.jpg" media-type="image/jpeg"/>'
                                    '<item id="coverpage" href="cover.xhtml" media-type="application/xhtml+xml"/>'
                                    '</manifest>')
                text = text.replace('<spine>', '<spine><itemref idref="coverpage"/>')
                data = text.encode('utf-8')
            compress = zipfile.ZIP_STORED if item.filename == 'mimetype' else zipfile.ZIP_DEFLATED
            book.writestr(item, data, compress_type=compress)
        book.writestr('cover.xhtml', page, compress_type=zipfile.ZIP_DEFLATED)
        book.writestr('cover.jpg', cover_jpeg(), compress_type=zipfile.ZIP_STORED)


class NewBookCoverTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-cover-thumb-v1013-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text('{"language": "VI", "uiTheme": 4, "sleepTimeout": 120}')
        (self.store / 'recent.json').write_text(
            '{"books": [{"path": "%s", "title": "%s", "author": "Tenor", "coverBmpPath": ""}]}' % (PATH, TITLE),
            encoding='utf-8')
        book_with_cover_page(self.sd / PATH.lstrip('/'))
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None

    def launch(self, heap, book_path=None):
        # Home, Select opens the book on its cover page; the reader sits idle for six seconds (well
        # past the idle pass), Right shows a text page, Back returns to the Recent card.
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        shots = [(5000, 'cover-page'), (8500, 'text-page'), (12500, 'home')]
        env.update(heap, SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='1500:CONFIRM;7500:RIGHT;9500:BACK;13000:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
        if book_path:
            (self.store / 'recent.json').write_text(
                '{"books": [{"path": "%s", "title": "%s", "author": "Tenor", "coverBmpPath": ""}]}' % (book_path, TITLE),
                encoding='utf-8')
        run = subprocess.run([str(PROGRAM)], cwd=Path(__file__).resolve().parents[2], env=env,
                             capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        images = {}
        for _, name in shots:
            with Image.open(self.sd / (name + '.bmp')) as shot:
                images[name] = shot.convert('L')
            if self.artifacts:
                self.artifacts.mkdir(parents=True, exist_ok=True)
                images[name].save(self.artifacts / f'{self._testMethodName}-{name}.png')
        return log, images

    def thumbs(self, height):
        return sorted(self.store.glob(f'epub_*/thumb2_{height}.bmp'))

    def test_radio_heap_still_gives_the_card_its_cover(self):
        log, shots = self.launch(RADIO_HEAP)
        self.assertTrue(self.thumbs(450), 'no 450 px thumbnail with the radio heap\n' + log[-6000:])
        builds = CARD_BUILD.findall(log)
        self.assertTrue(builds, log[-4000:])
        self.assertEqual(int(builds[-1][2]), 450, 'the card drew no cover for the new book')
        self.assertIsNotNone(ImageChops.invert(shots['home'].crop(COVER_BOX)).getbbox(), 'cover area is blank')

    def test_no_cover_decode_while_the_page_is_read(self):
        log, _ = self.launch({})
        # The text page is the last page painted before Back.
        last_page = log.rfind('Rendered page in')
        self.assertGreaterEqual(last_page, 0, log[-4000:])
        written = [(m.start(), int(m.group(1)), m.group(3)) for m in THUMB.finditer(log)]
        self.assertEqual([h for _, h, ok in written if ok == '1'], [450, 226], log[-6000:])
        # A thumbnail taken from the cover page's own decode costs no decode of its own.
        separate = [m.start() for m in THUMB.finditer(log) if not log.startswith(', page=1', m.end())]
        early = [at for at in separate if at < last_page]
        self.assertEqual(early, [], 'cover thumbnail decoded while the page was on screen')

    def test_thumbnail_decodes_a_reduced_grid(self):
        # A book with a cover but no cover page: the thumbnails decode the cover file.
        other = '/sach/khong-trang-bia.epub'
        epub_with_cover(self.sd / other.lstrip('/'))
        log, _ = self.launch({}, other)
        grids = [tuple(map(int, m.groups())) for m in GRID.finditer(log)]
        scales = [int(scale) for scale in SHARED_DECODE.findall(log)]
        self.assertTrue(grids or scales, log[-6000:])
        # The shared decode picks its grid by the same rule (JpegScale.h): a half covers 298 x 450 here.
        self.assertTrue(all(scale > 1 for scale in scales), scales)
        for src_w, src_h, grid_w, grid_h, out_w, out_h in grids:
            # The smallest grid that still covers the thumbnail in both axes.
            self.assertLess(grid_w, src_w, grids)
            self.assertGreaterEqual(grid_w, out_w, grids)
            self.assertGreaterEqual(grid_h, out_h, grids)

    def test_cover_page_pixels_make_the_thumbnails(self):
        # The radio starts only after the first page, so the decode sees the heap before it.
        log, shots = self.launch({})
        routes = THUMB_ROUTE.findall(log)
        self.assertEqual(routes, [('450', '1'), ('226', '1')], log[-6000:])
        # Nothing reaches the card while the cover decodes: the files come after the first page.
        first_page = log.find('Rendered page in')
        self.assertGreaterEqual(first_page, 0, log[-4000:])
        self.assertGreater(THUMB_ROUTE.search(log).start(), first_page, 'thumbnail written during the page decode')
        # No second decode of the cover: the JPEG converter logs its grid for every thumbnail.
        self.assertIsNone(GRID.search(log), 'the cover was decoded again for the thumbnails')
        builds = CARD_BUILD.findall(log)
        self.assertEqual(int(builds[-1][2]), 450, 'the card drew no cover for the new book')

    def test_page_thumbnail_matches_the_cover_decode(self):
        log, _ = self.launch({})
        self.assertEqual([h for h, route in THUMB_ROUTE.findall(log)], ['450', '226'], log[-6000:])
        from_page = {h: Image.open(self.thumbs(h)[0]).copy() for h in (450, 226)}
        # The same cover in a book with no cover page takes the cover decode.
        other = '/sach/khong-trang-bia.epub'
        epub_with_cover(self.sd / other.lstrip('/'))
        log, _ = self.launch({}, other)
        self.assertEqual(len(GRID.findall(log)) + len(SHARED_DECODE.findall(log)), 1, log[-6000:])
        cover = Image.open(__import__('io').BytesIO(cover_jpeg())).convert('L')
        report = {}
        for height in (450, 226):
            decoded = [p for p in self.thumbs(height) if Image.open(p).copy() != from_page[height]]
            self.assertEqual(len(decoded), 1, self.thumbs(height))
            decoded = Image.open(decoded[0]).copy()
            page = from_page[height]
            self.assertEqual(page.height, height)
            self.assertLessEqual(abs(page.width - decoded.width), 1, (page.size, decoded.size))
            page_error = blurred_error(page, cover)
            decode_error = blurred_error(decoded, cover)
            report[height] = (page.size, decoded.size, round(page_error, 2), round(decode_error, 2))
            if self.artifacts:
                page.save(self.artifacts / f'thumb-{height}-page.png')
                decoded.save(self.artifacts / f'thumb-{height}-decode.png')
            # Within the dither of the cover decode: a 1-bit thumbnail blurred at radius 2 still
            # carries several gray levels of error either way. The page's 226 was scaled from its 450
            # after that one was dithered, so it was dithered twice: v1.0.14 measured 6.75 against
            # 2.93 there, where the Atkinson thumbnails of v1.0.13 gave 12.85 and 10.81. v1.0.16 feeds
            # it the page decode's gray as well (GrayThumb::alsoFeed): 2.96 against 2.93.
            self.assertLessEqual(page_error, decode_error + 3.0, report)
            # Dithered twice, either route's 226 lands at 6,6 to 6,8 here.
            self.assertLess(max(page_error, decode_error), 4.5, report)
        print('THUMB_ROUTE_MEASURE', report)


if __name__ == '__main__':
    unittest.main()
