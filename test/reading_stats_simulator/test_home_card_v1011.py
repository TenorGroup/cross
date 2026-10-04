"""v1.0.11 Recent card, second pass: a sharp cover, the newest quote after a restart, reading stats.

1. The reader writes a cover thumbnail at the card's own height (450 px since 04/10, 356 before) on the idle pass after the
   first page, beside the theme's smaller one, and the card draws it. A book opened before that
   (only the small thumbnail on the card) gets the large one the next time it is opened; until
   then the card falls back to the small one.
2. A quote saved in the reader is remembered on the card, so Home shows it even when the device
   restarted in between (it wakes straight into the book, and Home never showed the book since).
3. Two columns: the cover on the left, the book's reading stats on its right. A book with no
   reading record shows the percent row alone.
4. Back opens the most recent book from Home; its hint is the return symbol every screen draws for
   Back, and it is hidden when there is no book to open.
Screenshots land in CROSSPOINT_TEST_ARTIFACTS when it is set. THUMB_TEST_EPUB points the cover test
at another book (for timing a real cover); the default is a fixture built here.
"""
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageChops, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FONTS = REPO / 'lib/EpdFont/builtinFonts/source'
FIXTURE = REPO / 'test/epubs/test_dictionary_synonyms.epub'

CARD_BUILD = re.compile(r'Recent card build=(\d+)ms cache=(\d+) cover=(\d+)')
CARD_QUOTE = re.compile(r'Card quote ([0-9a-f]{16}\.json) of (\d+)')
CARD_STATS = re.compile(r'Card stats rows=([0-9a-f]{2}) bar=(-?\d+)')
THUMB = re.compile(r'Cover thumbnail (\d+) px: (\d+) ms, ok=(\d)')

TITLE_A = 'Một cuốn sách có tên rất dài'
PATH_A = '/sach/lang-ven-bien.epub'


def fnv64(text):
    value = 14695981039346656037
    for byte in text.encode('utf-8'):
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def cover_jpeg(width=600, height=900):
    """A baseline JPEG cover with smooth tones and lettering, so dither and scaling both show."""
    image = Image.new('L', (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        draw.line((0, y, width, y), fill=40 + y * 180 // height)
    for i in range(12):
        draw.ellipse((40 + i * 30, 520 + i * 10, 200 + i * 30, 680 + i * 10), outline=255 - i * 12, width=3)
    font = ImageFont.truetype(str(FONTS / 'Geist/Geist-Bold.ttf'), 96)
    for i, word in enumerate(('WIND', 'OVER', 'SAND')):
        draw.text((width // 2, 90 + i * 120), word, font=font, fill=250, anchor='ma')
    small = ImageFont.truetype(str(FONTS / 'Geist/Geist-Bold.ttf'), 34)
    draw.text((width // 2, 800), 'TÁC GIẢ MẪU', font=small, fill=10, anchor='ma')
    out = io.BytesIO()
    image.convert('RGB').save(out, 'JPEG', quality=88)
    return out.getvalue()


def epub_with_cover(target):
    """The synonyms fixture with a JPEG cover declared the way Calibre declares it."""
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(FIXTURE) as source, zipfile.ZipFile(target, 'w') as book:
        for item in source.infolist():
            data = source.read(item.filename)
            if item.filename == 'book.opf':
                text = data.decode('utf-8')
                text = text.replace('</metadata>', '<meta name="cover" content="cover"/></metadata>')
                text = text.replace('</manifest>', '<item id="cover" href="cover.jpg" media-type="image/jpeg"/>'
                                    '</manifest>')
                data = text.encode('utf-8')
            compress = zipfile.ZIP_STORED if item.filename == 'mimetype' else zipfile.ZIP_DEFLATED
            book.writestr(item, data, compress_type=compress)
        book.writestr('cover.jpg', cover_jpeg(), compress_type=zipfile.ZIP_STORED)


class HomeCardFollowupTest(unittest.TestCase):
    # Regions of the default X3 card (528 x 792): the cover at the left margin, the stats column to
    # its right, the text block under both.
    COVER = (24, 124, 322, 574)
    STATS = (344, 124, 504, 574)
    # The first footer hint cell, clear of the battery reading on its left, and the band above it
    # that a two-line text label reached.
    BACK_HINT = (68, 770, 150, 792)
    ABOVE_BACK_HINT = (68, 757, 150, 770)

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-home-card-v1011-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        self.settings({})
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None

    def settings(self, extra):
        values = {'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120}
        values.update(extra)
        (self.store / 'settings.json').write_text(json.dumps(values))

    def write_recent(self, books):
        (self.store / 'recent.json').write_text(json.dumps({'books': books}, ensure_ascii=False), encoding='utf-8')

    def launch(self, events, shots, wake=False):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
        if wake:
            env['CROSSPOINT_SIM_WAKE_REASON'] = 'power'
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
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

    @staticmethod
    def ink(image, box):
        return ImageChops.invert(image.crop(box)).getbbox() is not None

    def thumbs(self, height):
        return sorted(self.store.glob(f'epub_*/thumb2_{height}.bmp'))

    # --- 1. cover thumbnail at the card's height ---------------------------------------------
    def test_idle_pass_writes_the_card_sized_thumbnail_and_the_card_draws_it(self):
        source = os.environ.get('THUMB_TEST_EPUB')
        if source:
            (self.sd / PATH_A.lstrip('/')).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(source, self.sd / PATH_A.lstrip('/'))
        else:
            epub_with_cover(self.sd / PATH_A.lstrip('/'))
        self.write_recent([{'path': PATH_A, 'title': TITLE_A, 'author': 'Tác giả Mẫu', 'coverBmpPath': ''}])

        # First open: Select opens the book, the idle pass after the first page writes both
        # thumbnails, Back returns to the card.
        log, first = self.launch('1500:CONFIRM;9000:BACK;12500:QUIT', [(12000, 'first-open')])
        big, = self.thumbs(450) or [None]
        self.assertIsNotNone(big, 'no 450 px thumbnail after the idle pass\n' + log[-6000:])
        timings = THUMB.findall(log)
        heights = [int(h) for h, _, ok in timings if ok == '1']
        self.assertIn(450, heights, log[-4000:])
        # The card's own height first: it is the one the card draws.
        self.assertEqual(int(timings[0][0]), 450, timings)
        small_height = next(h for h in heights if h != 450)
        small = self.thumbs(small_height)
        self.assertTrue(small, 'the theme thumbnail is still written')
        with Image.open(big) as image:
            self.assertEqual(image.height, 450)
            self.assertGreaterEqual(image.width, 270)
            big_size = (image.width, image.height)
        builds = CARD_BUILD.findall(log)
        self.assertTrue(builds, log[-4000:])
        self.assertEqual(int(builds[-1][2]), 450, 'card did not draw the 450 px thumbnail')
        self.assertTrue(self.ink(first['first-open'], self.COVER))
        self.report = {'timings': timings, 'big': (big.stat().st_size, big_size),
                       'small': (small[0].stat().st_size, small_height)}
        print('THUMB_MEASURE', json.dumps(self.report))

        # A book opened before this release: only the small thumbnail on the card. The card falls
        # back to it; the next open writes the large one and the card switches to it.
        big.unlink()
        log, shots = self.launch('2500:CONFIRM;9500:BACK;13000:QUIT', [(2000, 'old-book'), (12500, 'reopened')])
        builds = CARD_BUILD.findall(log)
        self.assertEqual(int(builds[0][2]), small_height, 'card did not fall back to the small thumbnail')
        self.assertEqual(int(builds[-1][2]), 450)
        self.assertTrue(self.thumbs(450))
        self.assertEqual([int(h) for h, _, _ in THUMB.findall(log)], [450], 'only the missing one is written')
        self.assertTrue(self.ink(shots['old-book'], self.COVER))
        self.assertIsNotNone(ImageChops.difference(shots['old-book'].crop(self.COVER),
                                                   shots['reopened'].crop(self.COVER)).getbbox(),
                             'the sharper thumbnail should change the drawn cover')

    # --- 2. newest quote across a restart ------------------------------------------------------
    def test_quote_saved_before_a_restart_is_the_one_on_the_card(self):
        shutil.copy(FIXTURE, self.sd / 'a.epub')
        self.write_recent([{'path': '/a.epub', 'title': 'Synonym Lookup Test', 'author': 'Tenor',
                            'coverBmpPath': '', 'excerpt': 'The page the reader left on.'}])
        folder = self.store / 'quotes'
        folder.mkdir()
        key = 0x811c9dc5
        for byte in '/a.epub'.encode():
            key = ((key ^ byte) * 16777619) & 0xFFFFFFFF
        # Fifteen older quotes of the book, so a random pick lands on the new one rarely.
        for slot in range(15):
            (folder / ('%08x%04x%03x%x.json' % (key, 2501, 0, slot))).write_text(json.dumps(
                {'schema': 1, 'path': '/a.epub', 'title': 'Synonym Lookup Test', 'text': f'older quote {slot}',
                 'spine': 0, 'page': 0, 'day': 20260922}))
        (folder / '.ten-v2').write_text('2')
        seeded = set(p.name for p in folder.glob('*.json'))
        # Home, Select opens the book; reader menu, Tools, Save quotation, pick two words, save,
        # dismiss; then the power goes (the program ends in the reader).
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;'
                  '9900:RIGHT;11000:CONFIRM;12500:CONFIRM;15000:QUIT')
        log, first = self.launch(events, [(4200, 'reader-menu')])
        saved = sorted(set(p.name for p in folder.glob('*.json')) - seeded)
        self.assertEqual(len(saved), 1, log[-6000:])

        # Wake from deep sleep straight into the book, then Back to Home.
        self.settings({'wakeIntoBook': 1})
        state = json.loads((self.store / 'state.json').read_text())
        state.update(showBootScreen=False, openEpubPath='/a.epub', lastSleepFromReader=True)
        (self.store / 'state.json').write_text(json.dumps(state))
        log, shots = self.launch('3000:BACK;6500:QUIT', [(6000, 'after-restart')], wake=True)
        entered = re.findall(r'Entering activity: (\S+)', log)
        self.assertEqual(entered[:2], ['EpubReader', 'Home'], entered)
        found = CARD_QUOTE.findall(log)
        self.assertEqual(found, [(saved[0], '16')], log[-4000:])
        # Shown once: the mark is spent, so the next visit is free to pick at random again.
        self.assertFalse((folder / '.latest').exists())
        self.assertTrue(self.ink(shots['after-restart'], (24, 498, 504, 712)))
        # Back opens the book from Home and reads as the same return symbol every other screen
        # draws for Back (here the reader menu's), not as a word.
        self.assertTrue(self.ink(shots['after-restart'], self.BACK_HINT))
        self.assertIsNone(ImageChops.difference(first['reader-menu'].crop(self.BACK_HINT),
                                                shots['after-restart'].crop(self.BACK_HINT)).getbbox(),
                          'Home Back hint differs from the Back symbol')
        self.assertFalse(self.ink(shots['after-restart'], self.ABOVE_BACK_HINT), 'Back hint drawn as text')

    def test_no_recent_book_hides_the_back_hint(self):
        self.write_recent([])
        _, shots = self.launch('2500:QUIT', [(2000, 'empty')])
        for box in (self.BACK_HINT, self.ABOVE_BACK_HINT):
            self.assertFalse(self.ink(shots['empty'], box), 'Back hint drawn with no book to open')

    # --- 3. reading stats beside the cover -----------------------------------------------------
    def test_stats_column_beside_the_cover(self):
        books = []
        for path, title in ((PATH_A, TITLE_A), ('/b.epub', 'Bến sông ngày gió'), ('/c.epub', 'No record yet')):
            (self.sd / path.lstrip('/')).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(FIXTURE, self.sd / path.lstrip('/'))
            books.append({'path': path, 'title': title, 'author': 'Tenor', 'coverBmpPath': '',
                          'excerpt': 'The page the reader left on.'})
        self.write_recent(books)
        # The first book is the active one; the second has its own file; the
        # third was never recorded.
        record = {'bookEpoch': 0, 'path': PATH_A, 'title': TITLE_A, 'minutes': 7 * 60 + 25, 'ms': 0, 'turns': 412,
                  'first': 20260805, 'last': 20260817, 'days': 6, 'progress': 34, 'startProgress': 0}
        (self.store / 'reading-stats.json').write_text(json.dumps({'schema': 3, 'activeBook': record}))
        folder = self.store / 'reading-stats'
        folder.mkdir()
        other = dict(record, path='/b.epub', title='Bến sông ngày gió', minutes=45, turns=30, first=20260810,
                     last=20260811, days=2, progress=7)
        (folder / ('tenor_%016x.json' % fnv64('/b.epub'))).write_text(json.dumps(other))
        log, shots = self.launch('2500:RIGHT;4500:RIGHT;7000:QUIT',
                                 [(2000, 'stats-a'), (4000, 'stats-b'), (6500, 'stats-c')])
        stats = CARD_STATS.findall(log)
        # v1.0.14: the expected finish is the second row. A has a date, B is more than a year out
        # (7 % since 10/08), C has no record.
        self.assertEqual([rows for rows, _ in stats], ['3f', '3f', '01'], log[-4000:])
        a, b, c = shots['stats-a'], shots['stats-b'], shots['stats-c']
        for image in (a, b, c):
            self.assertTrue(self.ink(image, self.COVER))
            self.assertTrue(self.ink(image, (344, 124, 504, 200)), 'no percent row')
        # A and B fill the column; C stops after its percent row.
        self.assertTrue(self.ink(a, (344, 400, 504, 574)))
        self.assertFalse(self.ink(c, (344, 205, 504, 574)), 'rows drawn for a book with no record')
        # The progress bar: filled for 34 % of its width, outlined after that.
        bar = int(stats[0][1])
        self.assertGreater(bar, 124)
        mid = bar + 5
        self.assertEqual(a.getpixel((352, mid)), 0)
        self.assertEqual(a.getpixel((450, mid)), 255)
        self.assertEqual(a.getpixel((450, bar)), 0)
        self.assertNotEqual(ImageChops.difference(a.crop(self.STATS), b.crop(self.STATS)).getbbox(), None)


if __name__ == '__main__':
    unittest.main()
