"""The "Book + quotation" sleep screen (sleepScreen 10) on the real X3 simulator.

Evidence read here, nothing assumed:
  - firmware log: `Sleep screen mode=`, `Sleep quote <name>`, the fit line
    `Sleep quote size=<i> lines=<n> cut=<0|1> cover=<0|1> title=<n>`, the panel calls
    `displayBuffer, mode=<m>` and the Tenor fallback `[BRAND] sleep ready=1`;
  - `.crosspoint/state.json` keys `lastSleepQuoteHi` and `lastSleepQuoteLo`;
  - screenshots taken while the sleep screen is on the panel.

Quote files use the v1.0.11 names (QuoteStore.cpp): 16 hex digits, the FNV-1a 32 key of the
book path, the day code, the minute and a slot. The four records come from the
long-title fixture, renamed; two more sit in a second, generated book.

A book's cover reaches this screen only through the Cover sleep mode's cache, so the setup
runs one real Cover sleep (sleepScreen 3) per book first and keeps what it cached.

Set SLEEP_QUOTE_SHOTS to a directory to keep the screenshots as PNG.
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

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
# Four records shaped like a real reader's store: one book with a very long title, one day,
# no minute, anchored, and a 190-character quote among them.
REAL_QUOTES = REPO / 'test/reading_stats_simulator/fixtures/quotes-one-long-title'
SHOTS = os.environ.get('SLEEP_QUOTE_SHOTS')

REAL_BOOK = '/sach/thu-vien-mau/ban-thu/Chuyện dài về một làng ven biển (bản thử) [Một tựa sách rất dài] - Người Kể & Người Chép.epub'
REAL_217 = 'b1a75d94e5d467dc.json'  # the 190-character quote of the fixture
SECOND_BOOK = '/books/sach-thu-co-bia.epub'
SECOND_TITLE = 'Sách thử có bìa'
SHORT_TEXT = 'Làm chậm để đi nhanh.'
BROKEN_BOOK = '/books/sach-bia-hong.epub'
BROKEN_TITLE = 'Sách có ảnh bìa hỏng'
SLEEP_AT = 2500

# Mockup S2 geometry (SleepQuoteLayout.h): glyph ink, cover tile, title column.
GLYPH_BOX = (47, 82, 83, 109)
COVER_BOX = (48, 596, 144, 741)
FIT = re.compile(r'Sleep quote size=(\d) lines=(\d+) cut=(\d) cover=(\d) title=(\d)')
PICK = re.compile(r'Sleep quote ([0-9a-f]{16}\.json)')


def book_key(path):
    h = 2166136261
    for c in path.encode('utf-8'):
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def day_code(day):
    year, month, date = day // 10000, day // 100 % 100, day % 100
    if year < 2020 or not 1 <= month <= 12 or not 1 <= date <= 31:
        return 0
    return min((year - 2020) * 372 + (month - 1) * 31 + (date - 1), 0xFFFF)


def quote_id(record, slot):
    minute = record.get('gio', 0)
    return book_key(record['path']) << 32 | day_code(record['day']) << 16 | minute << 4 | slot


def cover_jpeg():
    """A four-tone cover drawn here, so the fixture carries no published artwork."""
    im = Image.new('L', (600, 900), 40)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 560, 600, 900), fill=150)
    d.rectangle((60, 120, 540, 420), fill=220)
    d.rectangle((60, 640, 540, 700), fill=90)
    for y in range(160, 400, 60):
        d.rectangle((110, y, 490, y + 24), fill=30)
    out = io.BytesIO()
    im.convert('RGB').save(out, 'JPEG', quality=90)
    return out.getvalue()


def write_epub(path, title, cover=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + title + '</dc:title><dc:identifier id="id">sleep-quote</dc:identifier><dc:language>vi</dc:language><meta name="cover" content="cover"/></metadata><manifest><item id="cover" href="cover.jpg" media-type="image/jpeg"/><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('cover.jpg', cover_jpeg() if cover is None else cover)
        z.writestr('body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body><p>' + 'Một đoạn văn thử. ' * 200 + '</p></body></html>')


def long_text():
    """Exactly 1024 bytes of Vietnamese, the store's own limit."""
    base = ('Sáng sớm, sương phủ kín mặt hồ, mấy con cò đứng im trên bờ ruộng, còn người đi chợ thì '
            'lặng lẽ gánh hàng qua cây cầu tre bắc ngang con lạch nhỏ. ')
    text = ''
    for ch in base * 8:
        if len((text + ch).encode()) > 1022:
            break
        text += ch
    return text.rstrip() + '.' * (1024 - len(text.rstrip().encode()))


class SleepQuoteTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='cross-sleep-quote-')
        cls.root = Path(cls.temp.name)
        cls.real = {}
        for i, name in enumerate(sorted(os.listdir(REAL_QUOTES))):
            raw = (REAL_QUOTES / name).read_bytes()
            record = json.loads(raw)
            cls.real[name] = (f'{quote_id(record, i):016x}.json', raw)
        # One Cover sleep per book fills the cover cache this screen reads.
        cls.caches = {}
        cls.books = {}
        for book, title in ((REAL_BOOK, 'Chuyện dài về một làng ven biển: những mùa gió, những con th'), (SECOND_BOOK, SECOND_TITLE)):
            sd = cls.root / ('cover-' + str(book_key(book)))
            write_epub(sd / book.lstrip('/'), title)
            store = sd / '.crosspoint'
            store.mkdir(parents=True)
            (store / 'settings.json').write_text(json.dumps({'language': 'VI', 'sleepScreen': 3}))
            (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': book}))
            log = cls.run_sim(sd, f'{SLEEP_AT}:SLEEP;9000:QUIT')
            made = list(store.glob('epub_*/cover_*.bmp'))
            assert made, 'Cover sleep made no cover cache\n' + log
            cls.caches[book] = list(store.glob('epub_*'))
            cls.books[book] = sd / book.lstrip('/')
        # A book whose cover image is broken: the Cover sleep builds its metadata cache but
        # makes no cover.
        sd = cls.root / 'cover-broken'
        write_epub(sd / BROKEN_BOOK.lstrip('/'), BROKEN_TITLE, cover=b'\xff\xd8\xff\xe0 not a jpeg ' * 40)
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (store / 'settings.json').write_text(json.dumps({'language': 'VI', 'sleepScreen': 3}))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': BROKEN_BOOK}))
        log = cls.run_sim(sd, f'{SLEEP_AT}:SLEEP;9000:QUIT')
        assert list(store.glob('epub_*/book.bin')) and not list(store.glob('epub_*/cover_*')), log
        cls.caches[BROKEN_BOOK] = list(store.glob('epub_*'))
        cls.books[BROKEN_BOOK] = sd / BROKEN_BOOK.lstrip('/')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def run_sim(sd, script, shots=None, after_wake=None, shots_after=None, extra=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        env.update(extra or {})
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = shots
        if after_wake:
            env['CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE'] = after_wake
        if shots_after:
            env['CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE'] = shots_after
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        assert run.returncode == 0, log
        return log

    def make_sd(self, quotes, covers=(), settings=None):
        temp = tempfile.TemporaryDirectory(prefix='cross-sleep-quote-case-')
        self.addCleanup(temp.cleanup)
        sd = Path(temp.name)
        store = sd / '.crosspoint'
        (store / 'quotes').mkdir(parents=True)
        (store / 'quotes' / '.ten-v2').write_text('2')
        for name, raw in quotes:
            (store / 'quotes' / name).write_bytes(raw)
        # Only the cache is copied, not the book: the screen must not need the book itself.
        for book in covers:
            for cache in self.caches[book]:
                shutil.copytree(cache, store / cache.name)
        (store / 'settings.json').write_text(json.dumps(settings or {'language': 'VI', 'sleepScreen': 10}))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
        return sd

    def sleep_once(self, sd, label, extra=None):
        """One sleep from Home. Every screenshot due before the panel sleeps is taken at the
        deep-sleep present with the finished sleep frame, so the last one on disk is it."""
        times = [SLEEP_AT + 50 * i for i in range(1, 80)]
        for old in sd.glob('shot-*.bmp'):
            old.unlink()
        shots = ';'.join(f'{t}:{sd / f"shot-{t}.bmp"}' for t in times)
        log = self.run_sim(sd, f'{SLEEP_AT}:SLEEP;{SLEEP_AT + 4500}:QUIT', shots=shots, extra=extra)
        return log, self.last_shot(sd, times, label)

    def last_shot(self, sd, times, label):
        taken = [sd / f'shot-{t}.bmp' for t in times if (sd / f'shot-{t}.bmp').exists()]
        self.assertTrue(taken, 'no screenshot taken')
        image = Image.open(taken[-1]).convert('L')
        if image.size != (528, 792):
            image = image.resize((528, 792))
        if SHOTS:
            Path(SHOTS).mkdir(parents=True, exist_ok=True)
            image.save(Path(SHOTS) / f'{label}.png')
        return image

    def sleep_part(self, log):
        self.assertIn('Entering activity: Sleep', log)
        return log.split('Entering activity: Sleep', 1)[1].split('Entering deep sleep', 1)[0]

    def ink(self, image, box, below=128):
        return sum(1 for p in image.crop(box).getdata() if p < below)

    def grays(self, image, box):
        return sum(1 for p in image.crop(box).getdata() if 20 < p < 235)

    def fit(self, log):
        match = FIT.search(log)
        self.assertIsNotNone(match, log)
        return tuple(int(x) for x in match.groups())

    def assert_quote_frame(self, image):
        glyph = self.ink(image, GLYPH_BOX)
        self.assertGreater(glyph, 36 * 27 // 3, 'opening quote mark missing')

    def test_short_quote_with_cover(self):
        record = {'schema': 1, 'path': SECOND_BOOK, 'title': SECOND_TITLE, 'text': SHORT_TEXT,
                  'spine': 2, 'page': 4, 'day': 20260923, 'gio': 600}
        name = f'{quote_id(record, 0):016x}.json'
        sd = self.make_sd([(name, json.dumps(record, ensure_ascii=False).encode())], covers=[SECOND_BOOK])
        log, image = self.sleep_once(sd, 'S2-ngan-co-bia')
        self.assertIn('Sleep screen mode=10', log)
        self.assertIn(f'Sleep quote {name}', log)
        self.assertEqual(self.fit(log), (0, 1, 0, 1, 1))
        self.assert_quote_frame(image)
        self.assertGreater(self.grays(image, COVER_BOX), 1000, 'cover tile has no gray levels')
        sleep = self.sleep_part(log)
        # One GC pass clears the reader's page before the gray planes (as the Tenor screen).
        self.assertEqual(re.findall(r'displayBuffer, mode=(\d)', sleep), ['0'], sleep)
        self.assertIn('Sleep quote gray ready=1', sleep)
        self.assertNotIn('[BRAND] sleep ready=', sleep)

    def test_real_quote_fits_whole(self):
        name, raw = self.real[REAL_217]
        sd = self.make_sd([(name, raw)], covers=[REAL_BOOK])
        log, image = self.sleep_once(sd, 'S2-that-217-co-bia')
        size, lines, cut, cover, title = self.fit(log)
        self.assertEqual(cut, 0, log)
        self.assertEqual(cover, 1, log)
        # The fixture's very long title keeps to three lines.
        self.assertEqual(title, 3, log)
        self.assertIn(size, (0, 1, 2))
        self.assert_quote_frame(image)
        self.assertGreater(self.grays(image, COVER_BOX), 1000)

    def test_long_quote_is_cut_at_fourteen(self):
        record = {'schema': 1, 'path': SECOND_BOOK, 'title': SECOND_TITLE, 'text': long_text(),
                  'spine': 0, 'page': 0, 'day': 20260923, 'gio': 601}
        self.assertEqual(len(record['text'].encode()), 1024)
        name = f'{quote_id(record, 0):016x}.json'
        sd = self.make_sd([(name, json.dumps(record, ensure_ascii=False).encode())], covers=[SECOND_BOOK])
        log, image = self.sleep_once(sd, 'S2-1024-byte-cat')
        size, lines, cut, cover, _ = self.fit(log)
        self.assertEqual((size, cut, cover), (2, 1, 1), log)
        self.assert_quote_frame(image)
        # The cut body stops above the cover row.
        self.assertEqual(self.ink(image, (0, 566, 528, 596)), 0, 'body runs into the cover row')

    def test_no_cached_cover_moves_title_to_margin(self):
        name, raw = self.real[REAL_217]
        sd = self.make_sd([(name, raw)])
        log, image = self.sleep_once(sd, 'S2-that-217-khong-bia')
        _, _, cut, cover, title = self.fit(log)
        self.assertEqual((cut, cover, title), (0, 0, 3), log)
        self.assert_quote_frame(image)
        self.assertEqual(self.grays(image, COVER_BOX), 0)
        # The title starts at the margin, inside where the tile would have been.
        self.assertGreater(self.ink(image, (48, 596, 144, 700)), 50)
        sleep = self.sleep_part(log)
        self.assertEqual(re.findall(r'displayBuffer, mode=(\d)', sleep), ['0'], sleep)

    def book_without_cover(self, name, raw, book_on_card=True):
        """The quote's book as a reader leaves it: the book file and its metadata cache, but
        no sleep cover, because the Cover sleep mode never ran for it."""
        sd = self.make_sd([(name, raw)], covers=[REAL_BOOK])
        for made in (sd / '.crosspoint').glob('epub_*/cover_*.bmp'):
            made.unlink()
        if book_on_card:
            (sd / REAL_BOOK.lstrip('/')).parent.mkdir(parents=True)
            shutil.copy(self.books[REAL_BOOK], sd / REAL_BOOK.lstrip('/'))
        return sd

    def test_missing_cover_is_made_once_then_reused(self):
        name, raw = self.real[REAL_217]
        sd = self.book_without_cover(name, raw)
        log, image = self.sleep_once(sd, 'S2-bia-tao-luc-ngu')
        made = re.search(r'Sleep quote cover made=1 ms=(\d+) bytes=(\d+)', log)
        self.assertIsNotNone(made, log)
        self.assertEqual(self.fit(log)[3], 1, log)
        self.assertGreater(self.grays(image, COVER_BOX), 1000, 'made cover not drawn')
        covers = list((sd / '.crosspoint').glob('epub_*/cover_*.bmp'))
        self.assertEqual(len(covers), 1, covers)
        self.assertEqual(covers[0].stat().st_size, int(made.group(2)))
        first = int(re.search(r'Timing image-ready=(\d+) ms', log).group(1))
        # The next sleep finds it cached, and so would the Cover sleep mode.
        again, image2 = self.sleep_once(sd, 'S2-bia-da-co')
        self.assertNotIn('Sleep quote cover made=', again)
        self.assertEqual(self.fit(again)[3], 1, again)
        self.assertEqual(list(image.crop(COVER_BOX).getdata()), list(image2.crop(COVER_BOX).getdata()))
        cached = int(re.search(r'Timing image-ready=(\d+) ms', again).group(1))
        measured = dict(make_ms=int(made.group(1)), bytes=int(made.group(2)), first_sleep_ms=first,
                        cached_sleep_ms=cached, cover=covers[0].name)
        print('DO_DUOC', json.dumps(measured))
        if SHOTS:
            (Path(SHOTS) / 'do-bia-luc-ngu.json').write_text(json.dumps(measured, indent=2) + '\n')

    def test_missing_book_file_draws_text_only(self):
        name, raw = self.real[REAL_217]
        sd = self.book_without_cover(name, raw, book_on_card=False)
        log, image = self.sleep_once(sd, 'S2-thieu-tep-sach')
        self.assertIn('Sleep quote cover skipped: book not on card', log)
        self.assertEqual(self.fit(log)[3], 0, log)
        self.assertEqual(self.grays(image, COVER_BOX), 0)
        self.assertEqual(list((sd / '.crosspoint').glob('epub_*/cover_*.bmp')), [])

    def test_short_heap_skips_making_the_cover(self):
        name, raw = self.real[REAL_217]
        sd = self.book_without_cover(name, raw)
        log, image = self.sleep_once(sd, 'S2-thieu-heap',
                                     extra={'CROSSPOINT_SIM_FREE_HEAP': '40000', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '30000'})
        self.assertIn('Sleep quote cover skipped: heap', log)
        self.assertEqual(self.fit(log)[3], 0, log)
        self.assertEqual(list((sd / '.crosspoint').glob('epub_*/cover_*.bmp')), [])

    def test_broken_cover_is_tried_once(self):
        record = {'schema': 1, 'path': BROKEN_BOOK, 'title': BROKEN_TITLE, 'text': SHORT_TEXT,
                  'spine': 0, 'page': 0, 'day': 20260923, 'gio': 700}
        name = f'{quote_id(record, 0):016x}.json'
        sd = self.make_sd([(name, json.dumps(record, ensure_ascii=False).encode())], covers=[BROKEN_BOOK])
        (sd / BROKEN_BOOK.lstrip('/')).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(self.books[BROKEN_BOOK], sd / BROKEN_BOOK.lstrip('/'))
        cache = sd / '.crosspoint'
        log, image = self.sleep_once(sd, 'S2-bia-hong-lan-1')
        self.assertIn('Sleep quote cover skipped: not made', log)
        self.assertIn('Generating BMP from JPG cover image', log)
        self.assertEqual(self.fit(log)[3], 0, log)
        self.assertEqual(self.grays(image, COVER_BOX), 0)
        markers = list(cache.glob('epub_*/cover_*.fail'))
        self.assertEqual(len(markers), 1, list(cache.rglob('*')))
        self.assertEqual(list(cache.glob('epub_*/cover_*.bmp')), [])
        # The next sleep reads the marker and leaves the generator alone.
        again, _ = self.sleep_once(sd, 'S2-bia-hong-lan-2')
        self.assertIn('Sleep quote cover skipped: failed before', again)
        self.assertNotIn('Generating BMP from JPG cover image', again)
        self.assertNotIn('Sleep quote cover skipped: not made', again)
        self.assertEqual(self.fit(again)[3], 0, again)

    def test_missing_book_or_short_heap_leaves_no_marker(self):
        name, raw = self.real[REAL_217]
        sd = self.book_without_cover(name, raw, book_on_card=False)
        self.sleep_once(sd, 'S2-khong-dau-thieu-sach')
        sd2 = self.book_without_cover(name, raw)
        self.sleep_once(sd2, 'S2-khong-dau-thieu-heap',
                        extra={'CROSSPOINT_SIM_FREE_HEAP': '40000', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '30000'})
        for card in (sd, sd2):
            self.assertEqual(list((card / '.crosspoint').glob('epub_*/cover_*.fail')), [])
        # Once the book and the heap are back, the cover is made.
        log, _ = self.sleep_once(sd2, 'S2-du-heap-tao-bia')
        self.assertIn('Sleep quote cover made=1', log)

    def test_empty_store_falls_back_to_tenor(self):
        sd = self.make_sd([])
        log, _ = self.sleep_once(sd, 'S2-kho-trong')
        self.assertIn('Sleep screen mode=10', log)
        self.assertIn('Sleep quote: none of 0 readable', log)
        self.assertIn('[BRAND] sleep ready=1', log)

    def test_two_sleeps_show_different_quotes(self):
        quotes = list(self.real.values())
        sd = self.make_sd(quotes, covers=[REAL_BOOK])
        times = [SLEEP_AT + 50 * i for i in range(1, 80)]
        wake_times = [2500 + 50 * i for i in range(1, 80)]
        log = self.run_sim(sd, f'{SLEEP_AT}:SLEEP;{SLEEP_AT + 4000}:POWER',
                           shots=';'.join(f'{t}:{sd / f"shot-{t}.bmp"}' for t in times),
                           after_wake=f'2500:SLEEP;{2500 + 4500}:QUIT',
                           shots_after=';'.join(f'{t}:{sd / f"wake-{t}.bmp"}' for t in wake_times))
        picks = PICK.findall(log)
        self.assertEqual(len(picks), 2, log)
        self.assertNotEqual(picks[0], picks[1], log)
        state = json.loads((sd / '.crosspoint/state.json').read_text())
        last = state['lastSleepQuoteHi'] << 32 | state['lastSleepQuoteLo']
        self.assertEqual(f'{last:016x}.json', picks[1])
        first = self.last_shot(sd, times, 'S2-lan-ngu-1')
        taken = [sd / f'wake-{t}.bmp' for t in wake_times if (sd / f'wake-{t}.bmp').exists()]
        self.assertTrue(taken)
        second = Image.open(taken[-1]).convert('L').resize((528, 792))
        if SHOTS:
            second.save(Path(SHOTS) / 'S2-lan-ngu-2.png')
        self.assertNotEqual(list(first.crop((0, 130, 528, 566)).getdata()),
                            list(second.crop((0, 130, 528, 566)).getdata()))

    def test_settings_value_ten_loads_and_older_values_stay(self):
        for value in (10, 9, 8, 3):
            with self.subTest(sleepScreen=value):
                sd = self.make_sd([], settings={'language': 'VI', 'sleepScreen': value})
                log = self.run_sim(sd, f'{SLEEP_AT}:SLEEP;{SLEEP_AT + 3000}:QUIT')
                self.assertIn(f'Sleep screen mode={value},', log)

    def test_settings_list_offers_the_value_in_three_languages(self):
        # Settings, Sleep tab, first row is the sleep screen; its popup lists every value.
        for language in ('VI', 'EN', 'ZH_HANS'):
            with self.subTest(language=language):
                sd = self.make_sd([], settings={'language': language, 'uiTheme': 4, 'sleepScreen': 10})
                shot = sd / 'popup.bmp'
                self.run_sim(sd, '1000:UP;1500:RIGHT;2000:CONFIRM;3000:DOWN;4000:CONFIRM;6000:QUIT',
                             shots=f'5200:{shot}')
                image = Image.open(shot).convert('L')
                if SHOTS:
                    image.save(Path(SHOTS) / f'cai-dat-{language}.png')
                # The popup's selected row (the new value) is a filled bar with light text.
                self.assertGreater(self.ink(image.resize((528, 792)), (90, 445, 440, 492)), 8000)
                self.assertEqual(json.loads((sd / '.crosspoint/settings.json').read_text())['sleepScreen'], 10)

    def test_chinese_place_line(self):
        record = {'schema': 1, 'path': SECOND_BOOK, 'title': SECOND_TITLE, 'text': SHORT_TEXT,
                  'spine': 2, 'page': 4, 'day': 20260923, 'gio': 600}
        name = f'{quote_id(record, 0):016x}.json'
        sd = self.make_sd([(name, json.dumps(record, ensure_ascii=False).encode())], covers=[SECOND_BOOK],
                          settings={'language': 'ZH_HANS', 'sleepScreen': 10})
        log, image = self.sleep_once(sd, 'S2-tieng-trung')
        self.assertEqual(self.fit(log)[3], 1, log)
        # "第 3 章第 5 页" sits under the title, beside the cover.
        self.assertGreater(self.ink(image, (164, 650, 484, 690)), 50)


if __name__ == '__main__':
    unittest.main()
