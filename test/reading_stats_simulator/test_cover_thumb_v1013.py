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
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageChops

from test_home_card_v1011 import CARD_BUILD, FIXTURE, PROGRAM, THUMB, cover_jpeg

PATH = '/sach/cuon-moi.epub'
TITLE = 'Cuốn sách mới mở'
# The radio's heap on the X3 right after the first page of a new book (cand-newbook-open.log:
# MEM Free: 61204 bytes, MaxAlloc: 55284 bytes).
RADIO_HEAP = {'CROSSPOINT_SIM_FREE_HEAP': '61204', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '55284'}
GRID = re.compile(r'Scaling source (\d+)x(\d+) \(decode grid (\d+)x(\d+)\) -> (\d+)x(\d+)')
COVER_BOX = (24, 124, 260, 480)


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

    def launch(self, heap):
        # Home, Select opens the book on its cover page; the reader sits idle for six seconds (well
        # past the idle pass), Right shows a text page, Back returns to the Recent card.
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        shots = [(5000, 'cover-page'), (8500, 'text-page'), (12500, 'home')]
        env.update(heap, SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='1500:CONFIRM;7500:RIGHT;9500:BACK;13000:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
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
        return sorted(self.store.glob(f'epub_*/thumb_{height}.bmp'))

    def test_radio_heap_still_gives_the_card_its_cover(self):
        log, shots = self.launch(RADIO_HEAP)
        self.assertTrue(self.thumbs(356), 'no 356 px thumbnail with the radio heap\n' + log[-6000:])
        builds = CARD_BUILD.findall(log)
        self.assertTrue(builds, log[-4000:])
        self.assertEqual(int(builds[-1][2]), 356, 'the card drew no cover for the new book')
        self.assertIsNotNone(ImageChops.invert(shots['home'].crop(COVER_BOX)).getbbox(), 'cover area is blank')

    def test_no_cover_decode_while_the_page_is_read(self):
        log, _ = self.launch({})
        # The text page is the last page painted before Back.
        last_page = log.rfind('Rendered page in')
        self.assertGreaterEqual(last_page, 0, log[-4000:])
        written = [(m.start(), int(m.group(1)), m.group(3)) for m in THUMB.finditer(log)]
        self.assertEqual([h for _, h, ok in written if ok == '1'], [356, 226], log[-6000:])
        early = [h for at, h, _ in written if at < last_page]
        self.assertEqual(early, [], 'cover thumbnail decoded while the page was on screen')

    def test_thumbnail_decodes_a_reduced_grid(self):
        log, _ = self.launch({})
        grids = [tuple(map(int, m.groups())) for m in GRID.finditer(log)]
        self.assertTrue(grids, log[-6000:])
        for src_w, src_h, grid_w, grid_h, out_w, out_h in grids:
            # The smallest grid that still covers the thumbnail in both axes.
            self.assertLess(grid_w, src_w, grids)
            self.assertGreaterEqual(grid_w, out_w, grids)
            self.assertGreaterEqual(grid_h, out_h, grids)


if __name__ == '__main__':
    unittest.main()
