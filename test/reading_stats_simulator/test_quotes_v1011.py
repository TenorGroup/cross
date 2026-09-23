"""The v1.0.11 Quotes screens, driven through the real simulator UI.

Covers the list of books, one book's list, the "all quotes" lists and their paging, the
order each top row cycles through, the detail screen, moving between quotes, deleting and
trimming a quote, and the one-time rename of files kept by v1.0.10.

Quote files are seeded straight into the simulated card under the names the firmware
itself gives them (quote_name below mirrors src/QuoteStore.cpp), so a listing on screen
can be checked against the order the names imply. The screens log what they load
("Quote books ...", "Quote list ...", "Quote detail ..."), which is how each step is
checked; the screenshots are checked for the few pixels that make the design (a filled
number box, a filled row, the big opening mark) and kept as evidence.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / 'fixtures'
PROGRAM = Path(os.environ.get('CROSSPOINT_SIM_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

# Home: three DOWN to the Statistics tab, three RIGHT to its fourth row (Quotes), Select.
HOME_TO_QUOTES = '1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM'


# ---- Quote file names, exactly as src/QuoteStore.cpp computes them ----

def book_key(path):
    """FNV-1a 32 over the book path's bytes."""
    value = 2166136261
    for byte in path.encode('utf-8'):
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def day_code(day):
    """Days since 2020-01-01 on a calendar of 31-day months; 0 when unknown."""
    year, month, date = day // 10000, day // 100 % 100, day % 100
    if year < 2020 or not 1 <= month <= 12 or not 1 <= date <= 31:
        return 0
    return min((year - 2020) * 372 + (month - 1) * 31 + (date - 1), 0xFFFF)


def quote_id(path, day, minute=None, slot=0):
    minute = minute if minute is not None and 0 <= minute < 1440 else 0
    return book_key(path) << 32 | day_code(day) << 16 | minute << 4 | slot


def quote_name(path, day, minute=None, slot=0):
    return '%016x.json' % quote_id(path, day, minute, slot)


def newest_first(ids):
    """The store's listing order: later moment first, ties by id."""
    return sorted(ids, key=lambda i: (-(i & 0xFFFFFFFF), i))


def write_quote(directory, path, title, text, spine, page, day, minute=None, slot=0, anchor=None, name=None):
    """One quote record, as QuoteStore writes it. Returns the file written."""
    directory.mkdir(parents=True, exist_ok=True)
    record = {'schema': 1, 'path': path, 'title': title, 'text': text, 'spine': spine, 'page': page, 'day': day}
    if minute is not None:
        record['gio'] = minute
    if anchor is not None:
        record['vo'], record['ve'] = anchor
    target = directory / (name or quote_name(path, day, minute, slot))
    target.write_text(json.dumps(record, ensure_ascii=False), encoding='utf-8')
    return target


RIVER = ('/sach/ben-song.epub', 'Bến sông ngày gió')
HILL = ('/sach/doi-che.epub', 'Mùa hái chè trên đồi')
SEA = ('/sach/lang-bien.epub', 'Làng biển mùa gió')

LINE = re.compile(r'Quote list (\w+) (\w+) page (\d+)/(\d+) of (\d+):((?: [0-9a-f]{16})*)')
BOOKS = re.compile(r'Quote books page (\d+)/(\d+) of (\d+) books, (\d+) quotes:((?: [0-9a-f]{8})*)')
DETAIL = re.compile(r'Quote detail (\d+)/(\d+) ([0-9a-f]{16}): Noto Serif (\d+), page (\d+)/(\d+)')


def lists(log):
    """Every page the list loaded: (scope, order, page, pages, count, [ids])."""
    return [(m[1], m[2], int(m[3]), int(m[4]), int(m[5]), [int(x, 16) for x in m[6].split()])
            for m in LINE.finditer(log)]


def book_pages(log):
    return [(int(m[1]), int(m[2]), int(m[3]), int(m[4]), [int(x, 16) for x in m[5].split()])
            for m in BOOKS.finditer(log)]


def details(log):
    return [(int(m[1]), int(m[2]), int(m[3], 16), int(m[4]), int(m[5]), int(m[6])) for m in DETAIL.finditer(log)]


def dark_runs(image, x0, x1, y0, y1):
    """Vertical runs of rows with ink inside the column strip [x0, x1)."""
    pixels = image.load()
    runs, start = [], None
    for y in range(y0, y1):
        dark = any(pixels[x, y] < 128 for x in range(x0, x1))
        if dark and start is None:
            start = y
        if not dark and start is not None:
            runs.append((start, y - 1))
            start = None
    if start is not None:
        runs.append((start, y1 - 1))
    return runs


# Footer cells of the third and fourth front buttons on the 528 px wide X3 panel
# (components/themes/TenorTheme.cpp, wideButtonPositions 291 and 383, 80 px wide), above the
# dotted rule under each cell: x0, x1, y0, y1.
LEFT_HINT = (291, 371, 760, 786)
RIGHT_HINT = (383, 463, 760, 786)


def quote_mark_ink():
    """Ink of the big opening mark, upright, relative to its top-left ink pixel. The header
    packs it rotated for drawImage; this is drawIcon's mapping back, (rows - 1 - row, col)."""
    header = (REPO / 'src/components/QuoteMarkGlyph.h').read_text()
    width = int(re.search(r'QUOTE_MARK_DRAW_WIDTH = (\d+)', header)[1])
    height = int(re.search(r'QUOTE_MARK_DRAW_HEIGHT = (\d+)', header)[1])
    data = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', header.split('kQuoteMarkGlyphBitmap[]')[1])]
    stride = (width + 7) // 8
    ink = {(height - 1 - row, col) for row in range(height) for col in range(width)
           if not (data[row * stride + col // 8] >> (7 - col % 8)) & 1}
    x0, y0 = min(x for x, _ in ink), min(y for _, y in ink)
    return {(x - x0, y - y0) for x, y in ink}


def ink_in(image, x0, x1, y0, y1):
    """Ink pixels inside a box, relative to the box's own top-left ink pixel."""
    pixels = image.load()
    ink = {(x, y) for x in range(x0, x1) for y in range(y0, y1) if pixels[x, y] < 128}
    if not ink:
        return set()
    left, top = min(x for x, _ in ink), min(y for _, y in ink)
    return {(x - left, y - top) for x, y in ink}


def filled_rows(image, x0, x1, y0, y1, share=0.9):
    """Rows whose strip [x0, x1) is almost all ink: a filled selection band."""
    pixels = image.load()
    return [y for y in range(y0, y1) if sum(1 for x in range(x0, x1) if pixels[x, y] < 128) >= (x1 - x0) * share]


class QuotesV1011Test(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-quotes-v1011-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.quotes = self.store / 'quotes'
        self.store.mkdir()
        shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', self.sd / 'audit.epub')
        (self.store / 'settings.json').write_text(json.dumps({'language': 'VI', 'sleepTimeout': 10}))
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': '/audit.epub', 'title': 'Synonym Lookup Test'}]}))
        # A seeded store is already on the v1.0.11 names; the migration test takes it away.
        self.quotes.mkdir()
        (self.quotes / '.ten-v2').write_text('2')

    # ---- running the simulator ----

    def journey(self, *steps, gap=1200, start=HOME_TO_QUOTES, at=7600, tail=400):
        """Home to Quotes (or `start`, ending before `at` ms), then `steps`: a key ('RIGHT',
        'DOWN:1000') every `gap` ms, or '@name' for a screenshot shortly after the key before it."""
        events, shots = [start], []
        for step in steps:
            if step.startswith('@'):
                shots.append((at - gap // 5, step[1:]))
            else:
                events.append(f'{at}:{step}')
                at += gap
        events.append(f'{at + tail}:QUIT')
        return self.run_sim(';'.join(events), shots)

    def run_sim(self, events, shots):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events)
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        with tempfile.TemporaryFile(mode='w+') as log:
            process = subprocess.Popen([str(PROGRAM)], cwd=REPO, env=env, stdout=log, stderr=log)
            try:
                process.wait(timeout=120)
            finally:
                if process.poll() is None:
                    process.kill()
            log.seek(0)
            text = log.read()
        self.assertEqual(process.returncode, 0, text[-6000:])
        self.keep(name for _, name in shots)
        return text

    def keep(self, names):
        evidence = os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR')
        if not evidence:
            return
        target = Path(evidence)
        target.mkdir(parents=True, exist_ok=True)
        for name in names:
            shot = self.sd / f'{name}.bmp'
            if shot.exists():
                shutil.copy2(shot, target / f'{name}.bmp')

    def shot(self, name):
        with Image.open(self.sd / f'{name}.bmp') as image:
            return image.convert('L')

    # ---- seeds ----

    def three_books(self):
        """Sea (1 quote, newest), River (2), Hill (3, oldest): newest book first."""
        q = self.quotes
        ids = {}
        ids['hill'] = [
            write_quote(q, *HILL, 'Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi.',
                        3, 12, 20260919, 1290),
            write_quote(q, *HILL, 'Buổi trưa cả nhóm ngồi dưới gốc cây, chia nhau nắm xôi và ấm nước chè.',
                        5, 40, 20260920, 600),
            write_quote(q, *HILL, 'Chiều xuống, gùi chè đã đầy.', 1, 2, 20260921, 480)]
        ids['river'] = [
            write_quote(q, *RIVER, 'Nước lên từ sáng, bến vắng người.', 1, 4, 20260920, 700),
            write_quote(q, *RIVER, 'Ông lái đò buộc thuyền vào gốc bần rồi ngồi hút thuốc.', 2, 17, 20260922, 1200)]
        ids['sea'] = [
            write_quote(q, *SEA, 'Tối nào bà cũng kể một chuyện về con thuyền đầu tiên.', 0, 2, 20260923, 540)]
        return ids

    @staticmethod
    def id_of(path):
        return int(path.name[:16], 16)

    # ---- tests ----

    def test_book_list_shows_three_books_newest_first(self):
        self.three_books()
        log = self.journey('@b1-books')
        self.assertIn('Entering activity: Quotes', log)
        pages = book_pages(log)
        self.assertTrue(pages, log[-4000:])
        page, total, books, count, keys = pages[-1]
        self.assertEqual((page, total, books, count), (1, 1, 3, 6))
        self.assertEqual(keys, [book_key(SEA[0]), book_key(RIVER[0]), book_key(HILL[0])])
        # The first row is the cursor's stop on entry: a filled band across the row.
        image = self.shot('b1-books')
        band = filled_rows(image, 40, image.width - 40, 90, 220)
        self.assertGreaterEqual(len(band), 40, f'no filled row for the selected book: {band[:3]}')

    def test_list_inside_one_book_is_its_own_quotes_newest_first(self):
        ids = self.three_books()
        # Row 2 of the book list is River (two quotes).
        log = self.journey('RIGHT', 'CONFIRM', '@b2-book', 'BACK', '@b2-back')
        self.assertEqual(log.count('Entering activity: Quotes'), 2, log[-4000:])
        book = [entry for entry in lists(log) if entry[0] == 'book']
        self.assertTrue(book, log[-4000:])
        self.assertEqual(book[0][1:5], ('newest', 1, 1, 2))
        self.assertEqual(book[0][5], newest_first([self.id_of(p) for p in ids['river']]))
        # One number box is filled, on the first quote.
        image = self.shot('b2-book')
        boxes = dark_runs(image, 21, 25, 100, image.height - 60)
        self.assertEqual(len(boxes), 1, boxes)
        # Back returns to the list of books, which is still on screen.
        self.assertIn('Popped from activity stack', log)
        after = self.shot('b2-back')
        self.assertGreaterEqual(len(filled_rows(after, 40, after.width - 40, 170, 300)), 40)

    def test_all_newest_with_two_quotes_fits_one_page(self):
        q = self.quotes
        first = write_quote(q, *HILL, 'Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi, còn mặt '
                            'trời thì vừa ló sau rặng núi.', 3, 12, 20260919, 1290)
        second = write_quote(q, *RIVER, 'Nước lên từ sáng, bến vắng người.', 1, 4, 20260920, 700)
        # Left from the first book row reaches the top row; Select turns it to "All, newest".
        log = self.journey('LEFT', '@a4-top-row', 'CONFIRM', 'RIGHT', '@a1-all-newest', 'RIGHT', '@a1-second')
        entry = [e for e in lists(log) if e[:2] == ('all', 'newest')]
        self.assertTrue(entry, log[-4000:])
        self.assertEqual(entry[-1][2:], (1, 1, 2, [self.id_of(second), self.id_of(first)]))
        top = self.shot('a4-top-row')
        self.assertGreaterEqual(len(filled_rows(top, 22, 150, 50, 100)), 10, 'top row is not marked')
        tops = []
        for name in ('a1-all-newest', 'a1-second'):
            image = self.shot(name)
            boxes = dark_runs(image, 21, 25, 100, image.height - 60)
            self.assertEqual(len(boxes), 1, f'{name}: {boxes}')
            self.assertGreaterEqual(boxes[0][1] - boxes[0][0], 20, boxes)
            # Nothing between the number box and the hanging opening quote.
            self.assertEqual(dark_runs(image, 52, 62, 100, image.height - 60), [], name)
            tops.append(boxes[0][0])
        self.assertGreater(tops[1], tops[0], 'Right did not move the cursor down')

    def seed_many(self, count, book=HILL):
        written = []
        for i in range(count):
            written.append(write_quote(self.quotes, *book, f'Đoạn trích số {i + 1} về một buổi hái chè.',
                                       i % 4, i + 1, 20260901 + i // 60, i % 60 * 20))
        return [self.id_of(p) for p in written]

    def test_five_quotes_page_by_cursor_and_by_side_button(self):
        ids = newest_first(self.seed_many(5))
        log = self.journey('LEFT', 'CONFIRM', 'RIGHT', '@a2-page1', 'RIGHT', 'RIGHT', 'RIGHT', '@a3-page2',
                           'LEFT', '@a2-back', 'DOWN', '@a3-side', 'UP', '@a2-side-back')
        pages = [(e[2], e[3], e[5]) for e in lists(log) if e[:2] == ('all', 'newest')]
        self.assertEqual([p[:2] for p in pages], [(1, 2), (2, 2), (1, 2), (2, 2), (1, 2)], log[-4000:])
        self.assertEqual(pages[0][2], ids[:3])
        self.assertEqual(pages[1][2], ids[3:])
        # Past the third quote the cursor lands on the fourth, the first box of page 2.
        for name, box_expected in (('a2-page1', 1), ('a3-page2', 1), ('a2-back', 1)):
            image = self.shot(name)
            self.assertEqual(len(dark_runs(image, 21, 25, 100, image.height - 60)), box_expected, name)
        page2 = self.shot('a3-page2')
        page1 = self.shot('a2-back')
        first_box_page2 = dark_runs(page2, 21, 25, 100, page2.height - 60)[0][0]
        third_box_page1 = dark_runs(page1, 21, 25, 100, page1.height - 60)[0][0]
        self.assertLess(first_box_page2, third_box_page1, 'Left from the fourth quote should mark the third')

    def test_holding_a_side_button_jumps_ten_pages(self):
        self.seed_many(40)  # 14 pages of three
        log = self.journey('LEFT', 'CONFIRM', 'DOWN:1000', 'DOWN:1000', 'UP:1000', '@jump')
        pages = [e[2:4] for e in lists(log) if e[:2] == ('all', 'newest')]
        self.assertEqual(pages, [(1, 14), (11, 14), (14, 14), (4, 14)], log[-4000:])

    def test_top_rows_cycle_views_and_sorts_in_order(self):
        ids = self.three_books()
        # Top level: Books -> All newest -> All oldest -> Books. Then into Hill (row 3),
        # whose own top row cycles Newest -> Oldest -> Book order -> Newest.
        log = self.journey('LEFT', 'CONFIRM', '@view-newest', 'CONFIRM', '@view-oldest', 'CONFIRM',
                           'RIGHT', 'RIGHT', 'RIGHT', 'CONFIRM', 'LEFT', 'CONFIRM', 'CONFIRM', '@sort-page',
                           'CONFIRM')
        top = [e for e in lists(log) if e[0] == 'all']
        self.assertEqual([e[1] for e in top], ['newest', 'oldest'], log[-4000:])
        every = newest_first([self.id_of(p) for group in ids.values() for p in group])
        self.assertEqual(top[0][5], every[:3])
        self.assertEqual(top[1][5], list(reversed(every))[:3])
        self.assertEqual(len(book_pages(log)), 2, 'Books view should load on entry and after the cycle')
        book = [e for e in lists(log) if e[0] == 'book']
        self.assertEqual([e[1] for e in book], ['newest', 'oldest', 'page', 'newest'])
        hill = [self.id_of(p) for p in ids['hill']]
        self.assertEqual(book[0][5], newest_first(hill))
        self.assertEqual(book[1][5], list(reversed(newest_first(hill))))
        # Book order: spine 1, then 3, then 5.
        self.assertEqual(book[2][5], [hill[2], hill[0], hill[1]])

    def long_text(self):
        words = ('Con đường lên đồi chè ngoằn ngoèo qua ba con suối nhỏ. Mùa mưa nước tràn qua mặt đá, '
                 'người đi phải xắn quần lội qua, tay giữ chặt gùi, chân dò từng bước cho tới khi sang '
                 'được bờ bên kia. ').split()
        text, i = '', 0
        while True:
            candidate = (text + ' ' + words[i % len(words)]).strip()
            if len(candidate.encode('utf-8')) > 1024:
                return text
            text, i = candidate, i + 1

    def test_detail_short_and_long_quote_and_moving_between_them(self):
        text = self.long_text()
        self.assertLessEqual(len(text.encode('utf-8')), 1024)
        self.assertGreater(len(text.encode('utf-8')), 1000)
        short = write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Nước lên từ sáng, bến vắng người.',
                            0, 0, 20260922, 1290)
        long = write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', text, 0, 1, 20260921, 600)
        log = self.journey('CONFIRM', 'CONFIRM', '@d2-short', 'RIGHT', '@d2-long-1', 'DOWN', '@d2-long-2',
                           'LEFT', '@d2-short-again', 'LEFT', 'BACK', '@d2-back-to-list')
        self.assertIn('Entering activity: QuoteDetail', log)
        seen = details(log)
        self.assertEqual([(d[0], d[1], d[2]) for d in seen][:3],
                         [(1, 2, self.id_of(short)), (2, 2, self.id_of(long)), (2, 2, self.id_of(long))],
                         log[-4000:])
        self.assertEqual(seen[0][3:], (18, 1, 1))
        self.assertEqual(seen[1][3], 16)
        self.assertGreaterEqual(seen[1][5], 2, 'a 1024-byte quote should page inside the detail')
        self.assertEqual(seen[2][4], 2)
        # Left at the first quote stays there: exactly one more load of quote 1.
        self.assertEqual([d[:3] for d in seen[3:]], [(1, 2, self.id_of(short))])
        # The big opening mark sits above the quote on the body column, drawn upright: the
        # ink in its box is exactly the glyph components/QuoteMarkGlyph.h holds.
        image = self.shot('d2-short')
        self.assertEqual(ink_in(image, 30, 100, 80, 150), quote_mark_ink(), 'the opening mark is not drawn upright')
        # Each footer hint says what its button does: on the first of two quotes there is no
        # quote before it, so the left-arrow hint is gone and the right one stays.
        self.assertEqual(ink_in(image, *LEFT_HINT), set(), 'a left-arrow hint with no quote before it')
        self.assertTrue(ink_in(image, *RIGHT_HINT), 'no right-arrow hint with a quote after it')
        last = self.shot('d2-long-1')
        self.assertTrue(ink_in(last, *LEFT_HINT), 'no left-arrow hint with a quote before it')
        self.assertEqual(ink_in(last, *RIGHT_HINT), set(), 'a right-arrow hint with no quote after it')
        # The short rule under the quote: a 48 px ink row starting at the body column.
        rule = filled_rows(image, 44, 90, 150, image.height - 60, share=0.95)
        self.assertTrue(rule, 'no rule between the quote and its source')

    def open_first_detail(self, *after):
        return self.journey('CONFIRM', 'CONFIRM', *after)

    def test_delete_confirmed_removes_the_file_and_the_list_shrinks(self):
        ids = self.three_books()
        victim = sorted(ids['sea'])[0]
        # Books: Sea is row 1 with one quote; use Hill instead (row 3, three quotes).
        log = self.journey('RIGHT', 'RIGHT', 'CONFIRM', 'CONFIRM', 'CONFIRM', '@m1-options', 'RIGHT', 'CONFIRM',
                           '@m2-delete', 'RIGHT', 'CONFIRM', '@d2-after-delete', 'BACK', '@list-after-delete')
        hill = newest_first([self.id_of(p) for p in ids['hill']])
        self.assertIn(f'Quote deleted {hill[0]:016x}', log, log[-4000:])
        self.assertFalse((self.quotes / ('%016x.json' % hill[0])).exists())
        self.assertTrue(victim.exists())
        seen = details(log)
        self.assertEqual([d[:3] for d in seen], [(1, 3, hill[0]), (1, 2, hill[1])])
        book = [e for e in lists(log) if e[0] == 'book']
        self.assertEqual([e[4] for e in book], [3, 2], 'the list did not reload after the delete')

    def test_delete_cancelled_keeps_the_file(self):
        ids = self.three_books()
        target = sorted(ids['sea'])[0]
        before = target.read_bytes()
        log = self.journey('CONFIRM', 'CONFIRM', 'CONFIRM', 'RIGHT', 'CONFIRM', '@m2-cancel', 'CONFIRM',
                           '@d2-kept', 'BACK')
        self.assertNotIn('Quote deleted', log)
        self.assertEqual(target.read_bytes(), before)
        self.assertEqual([d[:2] for d in details(log)], [(1, 1)])
        self.assertEqual(len([e for e in lists(log) if e[0] == 'book']), 1, 'nothing changed, nothing reloads')

    def trim_seed(self):
        text = 'Một hai ba bốn năm sáu'
        return write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', text, 0, 0, 20260922, 600,
                           anchor=(100, 100 + len(text)))

    def test_trim_drops_words_and_moves_the_anchor(self):
        target = self.trim_seed()
        # Edit (first option) opens the trim screen straight away outside the reader. The last
        # word boundary is active: Left twice drops "năm sáu"; a side button switches to the
        # first word; Right drops "Một". Select saves.
        log = self.open_first_detail('CONFIRM', 'CONFIRM', '@e2-trim-open', 'LEFT', 'LEFT', 'DOWN', 'RIGHT',
                                     '@e2-trim', 'CONFIRM', '@d2-trimmed', 'BACK')
        self.assertIn('Entering activity: QuoteTrim', log)
        record = json.loads(target.read_text())
        self.assertEqual(record['text'], 'hai ba bốn')
        self.assertEqual((record['vo'], record['ve']), (100 + 4, 100 + len('Một hai ba bốn năm sáu') - len(' năm sáu')))
        self.assertEqual(record['day'], 20260922)
        self.assertEqual(record['gio'], 600)
        seen = details(log)
        self.assertEqual(len(seen), 2, 'the detail should show the trimmed quote again')
        self.assertIn('Quote trimmed', log)

    def test_trim_back_leaves_the_quote_untouched(self):
        target = self.trim_seed()
        before = target.read_bytes()
        log = self.open_first_detail('CONFIRM', 'CONFIRM', 'LEFT', 'DOWN', 'RIGHT', 'BACK', '@d2-untrimmed', 'BACK')
        self.assertIn('Entering activity: QuoteTrim', log)
        self.assertEqual(target.read_bytes(), before)
        self.assertNotIn('Quote trimmed', log)
        self.assertEqual(len(details(log)), 1, 'Back from the trim screen changed nothing to reload')

    def test_empty_store_says_so_and_offers_only_back(self):
        log = self.journey('@empty', 'RIGHT', 'CONFIRM', '@empty-after')
        self.assertEqual(book_pages(log)[0][:4], (1, 1, 0, 0), log[-4000:])
        self.assertNotIn('Entering activity: QuoteDetail', log)
        image = self.shot('empty')
        self.assertEqual(ink_in(image, *LEFT_HINT), set(), 'a cursor hint on an empty list')
        self.assertEqual(ink_in(image, *RIGHT_HINT), set(), 'a cursor hint on an empty list')
        # The empty line stands alone under the header: no top row, so no rule under one.
        self.assertEqual(filled_rows(image, 20, 500, 60, 110, share=0.9), [], 'a top row over an empty list')

    def test_trim_pages_a_long_quote_to_keep_the_moved_word_on_screen(self):
        text = self.long_text()
        target = write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', text, 0, 1, 20260921, 600)
        before = target.read_bytes()
        # The last word starts as the one being moved, so the trim screen opens on the last
        # stretch of the quote; a side button switches to the first word and the area turns
        # back to the top.
        log = self.open_first_detail('CONFIRM', 'CONFIRM', '@e2-long-end', 'DOWN', '@e2-long-start', 'LEFT',
                                     'BACK', 'BACK')
        areas = [(m[1], int(m[2]), int(m[3]), int(m[4]), int(m[5]), int(m[6]))
                 for m in re.finditer(r'Quote trim (first|last) word (\d+) of (\d+) on line (\d+), lines (\d+)-(\d+)', log)]
        self.assertGreaterEqual(len(areas), 2, log[-4000:])
        for moving, word, words, line, top, bottom in areas:
            self.assertTrue(top <= line <= bottom, f'the {moving} word is off screen: {areas}')
        self.assertEqual(areas[0][0], 'last')
        self.assertEqual(areas[1][:2], ('first', 1))
        self.assertGreater(areas[0][4], 0, 'a long quote should open on a later stretch of its words')
        self.assertEqual(areas[1][4], 0)
        self.assertEqual(target.read_bytes(), before)

    def test_v1010_hash_names_are_renamed_once(self):
        shutil.rmtree(self.quotes)
        legacy = [
            ('a1b2c3d4e5f60718.json', RIVER, 'Nước lên từ sáng, bến vắng người.', 1, 4, 20260920, 700),
            ('0f1e2d3c4b5a6978.json', HILL, 'Chiều xuống, gùi chè đã đầy.', 1, 2, 20260921, None),
            ('77aa88bb99cc00dd.json', HILL, 'Sương còn đọng trên lá chè.', 3, 12, 20260919, 1290),
        ]
        for name, book, text, spine, page, day, minute in legacy:
            write_quote(self.quotes, *book, text, spine, page, day, minute, name=name)
        log = self.journey('@migrated')
        names = sorted(p.name for p in self.quotes.glob('*.json'))
        expected = sorted(quote_name(book[0], day, minute) for _, book, _, _, _, day, minute in legacy)
        self.assertEqual(names, expected, log[-4000:])
        self.assertTrue((self.quotes / '.ten-v2').exists())
        pages = book_pages(log)
        self.assertEqual(pages[-1][2:4], (2, 3))

    @staticmethod
    def book_loads(log):
        """Times the detail screen loaded a book (Epub::load logs each one) after it opened."""
        after = log.split('Entering activity: QuoteDetail', 1)
        return after[1].count('Loading ePub:') if len(after) > 1 else None

    def test_detail_names_each_chapter_once_per_quote(self):
        # Left and Right walk back to a quote already shown: its chapter line is kept, so the
        # book is loaded once per quote, never again for the same one.
        write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Một câu ngắn để thử.', 0, 0, 20260922, 1290)
        write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Một câu khác, cũng ngắn.', 0, 1, 20260921, 600)
        log = self.journey('CONFIRM', 'CONFIRM', 'RIGHT', 'LEFT', 'RIGHT', 'BACK')
        self.assertEqual([d[0] for d in details(log)], [1, 2, 1, 2], log[-4000:])
        self.assertEqual(self.book_loads(log), 2, log[-6000:])

    # Home opens /audit.epub; Select opens the reader menu, three Down reach its Tools tab and
    # three Right its fourth row, "Quotes of this book".
    READER_QUOTES = '1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;6600:RIGHT;7200:RIGHT;7800:RIGHT;8800:CONFIRM'

    def test_detail_opened_from_the_reader_uses_the_open_book(self):
        # From the reader the book is already loaded: the detail names chapters from it, and
        # loads no second copy of the same book.
        write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Một câu ngắn để thử.', 0, 0, 20260922, 1290)
        write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Một câu khác, cũng ngắn.', 0, 1, 20260921, 600)
        log = self.journey('CONFIRM', '@r-detail', 'RIGHT', 'LEFT', 'BACK', 'BACK', start=self.READER_QUOTES, at=10400)
        self.assertIn('Entering activity: EpubReader', log)
        self.assertEqual([d[0] for d in details(log)], [1, 2, 1], log[-6000:])
        self.assertEqual(self.book_loads(log), 0, log[-6000:])

    def test_reselect_from_the_reader_still_opens_the_selector(self):
        # Edit, then "Reselect on the book page": the reader turns to the quote's page and the
        # selector opens over it.
        write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Một câu ngắn để thử.', 0, 0, 20260922, 1290)
        log = self.journey('CONFIRM', 'CONFIRM', 'CONFIRM', 'RIGHT', 'CONFIRM', '@r-reselect',
                           start=self.READER_QUOTES, at=10400, tail=4000)
        after = log.split('Entering activity: QuoteDetail', 1)
        self.assertEqual(len(after), 2, log[-6000:])
        self.assertIn('Entering activity: QuoteSelect', after[1], log[-6000:])

    def test_reselect_on_a_chapter_that_fails_does_not_open_over_a_later_page(self):
        # The quote's chapter cannot be laid out (its file is missing from the book): the reader
        # says so and the reselection ends there. Going on to a chapter that does show, through
        # the chapter list, must not open the selector over a page the quote is not on.
        book = self.sd / 'missing-middle.epub'
        body = '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body><p>' + \
            'Một đoạn văn thử cho chương này. ' * 40 + '</p></body></html>'
        with zipfile.ZipFile(book, 'w') as z:
            z.writestr('mimetype', 'application/epub+zip')
            z.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Ba chương</dc:title><dc:identifier id="id">missing-middle</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest><spine toc="ncx"><itemref idref="a"/><itemref idref="b"/><itemref idref="c"/></spine></package>')
            z.writestr('toc.ncx', '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>Ba chương</text></docTitle><navMap>' +
                       ''.join(f'<navPoint id="{c}" playOrder="{i + 1}"><navLabel><text>Chương {i + 1}</text></navLabel><content src="{c}.xhtml"/></navPoint>'
                               for i, c in enumerate('abc')) + '</navMap></ncx>')
            z.writestr('a.xhtml', body)
            z.writestr('c.xhtml', body)
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/missing-middle.epub', 'title': 'Ba chương'}]}))
        write_quote(self.quotes, '/missing-middle.epub', 'Ba chương', 'Một câu ở chương hỏng.', 1, 0, 20260922, 600)
        log = self.journey('CONFIRM', 'CONFIRM', 'CONFIRM', 'RIGHT', 'CONFIRM', '@f-failed', 'CONFIRM', 'DOWN', '@f-menu', 'CONFIRM', '@f-chapters', 'RIGHT', 'CONFIRM', '@f-third',
                           start=self.READER_QUOTES, at=10400, gap=1500, tail=3000)
        after = log.split('Exiting activity: QuoteDetail', 1)
        self.assertEqual(len(after), 2, log[-6000:])
        self.assertIn('Failed to start section build', after[1], log[-6000:])
        # The chapter list then takes the reader to the third chapter, which shows.
        self.assertIn('Loading file: c.xhtml', after[1], log[-6000:])
        self.assertIn('Rendered page', after[1].split('Loading file: c.xhtml', 1)[1], log[-6000:])
        self.assertNotIn('Entering activity: QuoteSelect', after[1], log[-6000:])

    def test_unreadable_record_offers_delete_alone_without_the_previous_words(self):
        # The detail of a record that cannot be read: Options holds only Delete (and Cancel),
        # and the confirmation quotes no words, never those of the quote shown before it.
        kept = write_quote(self.quotes, *RIVER, 'Gió thổi qua bến sông suốt buổi chiều.', 1, 2, 20260922, 600)
        broken = self.quotes / quote_name(RIVER[0], 20260920, 300)
        broken.write_text('{ this is not a record', encoding='utf-8')
        log = self.journey('CONFIRM', 'CONFIRM', 'RIGHT', '@u-detail', 'CONFIRM', '@u-options', 'CONFIRM',
                           '@u-delete', 'RIGHT', 'CONFIRM', '@u-after')
        self.assertEqual([d[2] for d in details(log)][:2], [self.id_of(kept), self.id_of(broken)], log[-4000:])
        self.assertNotIn('Entering activity: QuoteTrim', log)
        asks = re.findall(r'Quote delete asks ([0-9a-f]{16}): (.*)', log)
        self.assertEqual(asks, [('%016x' % self.id_of(broken), 'Không đọc được đoạn này')], log[-4000:])
        self.assertIn('Quote deleted %016x' % self.id_of(broken), log)
        self.assertFalse(broken.exists())
        self.assertTrue(kept.exists())

    def test_home_card_picks_again_after_its_quote_is_deleted(self):
        # The card shows the quote kept last; deleting it on the Quotes screen sends the card
        # back to pick among the quotes left, instead of dropping to the page excerpt.
        newer = write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Câu mới lưu, sắp bị xoá.', 0, 1,
                            20260922, 1290)
        older = write_quote(self.quotes, '/audit.epub', 'Synonym Lookup Test', 'Câu cũ, vẫn còn.', 0, 0,
                            20260921, 600)
        (self.quotes / '.latest').write_text(newer.name[:16])
        log = self.journey('CONFIRM', 'CONFIRM', 'CONFIRM', 'RIGHT', 'CONFIRM', 'RIGHT', 'CONFIRM', 'BACK', 'BACK',
                           'BACK', 'UP', 'UP', 'UP', '@card-after-delete')
        self.assertIn('Quote deleted %016x' % self.id_of(newer), log, log[-4000:])
        cards = re.findall(r'Card quote ([0-9a-f]{16}\.json) of (\d+)', log)
        self.assertEqual(cards, [(newer.name, '2'), (older.name, '1')], log[-6000:])

    BAND = re.compile(r'Quote page band (\d+)-(\d+) tip (\d+) per page (\d+)')

    def test_three_line_quotes_clear_the_footer_at_every_text_size(self):
        # The tallest block a page holds: three lines of quote, then the book title and the
        # place under it (the "all quotes" view). A page holds as many as fit above the footer
        # at each UI text size; none may reach into the footer line or the hints.
        text = ('Buổi sáng hôm ấy cả xóm dậy sớm hơn thường lệ, người thì ra đồng thăm lúa, người thì '
                'đứng trước hiên nhìn trời, còn bọn trẻ chạy dọc bờ đê xem nước sông lên tới đâu.')
        self.assertLessEqual(len(text.encode('utf-8')), 240)
        for i in range(7):
            write_quote(self.quotes, *(RIVER if i % 2 else HILL), text, i, i + 3, 20260910 + i, 480 + i)
        per_page = {}
        for tier in (0, 1, 2):
            with self.subTest(tier=tier):
                (self.store / 'settings.json').write_text(json.dumps(
                    {'language': 'VI', 'sleepTimeout': 10, 'uiTextSize': tier}))
                shot = f'size-{tier}-all-newest'
                # Right walks the cursor through the first page and onto the next one.
                log = self.journey('LEFT', 'CONFIRM', '@' + shot, *(['RIGHT'] * 4), '@' + shot + '-cursor')
                top, bottom, tip, count = (int(x) for x in self.BAND.findall(log)[-1])
                per_page[tier] = count
                entry = [e for e in lists(log) if e[:2] == ('all', 'newest')]
                self.assertTrue(entry, log[-4000:])
                self.assertEqual(len(entry[0][5]), count)
                self.assertEqual(entry[0][3], -(-7 // count), 'page count does not follow the page size')
                # The fourth step lands on quote 4: on page 2 when two fit a page, still on page 2
                # when three do, and the page shows the quotes that follow the first page's.
                pages = [(e[2], e[5]) for e in entry]
                self.assertEqual(pages[-1][0], 2, pages)
                every = entry[0][5] + pages[-1][1]
                self.assertEqual(len(set(every)), len(every), 'a quote shows on two pages')
                image = self.shot(shot)
                keep = os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR')
                if keep:
                    Path(keep).mkdir(parents=True, exist_ok=True)
                    image.save(Path(keep) / f'{shot}.png')
                # The rows between the band's bottom and the footer line stay white.
                self.assertEqual(ink_in(image, 0, image.width, bottom, tip), set(),
                                 f'tier {tier}: a block runs past the band ({bottom}) into the footer ({tip})')
                # The footer line itself holds only its own centred text.
                pixels = image.load()
                columns = [x for x in range(image.width)
                           if any(pixels[x, y] < 128 for y in range(tip, tip + 20))]
                self.assertTrue(columns, f'tier {tier}: no footer text')
                self.assertLessEqual(abs(columns[0] + columns[-1] - image.width), 6,
                                     f'tier {tier}: ink beside the footer text, {columns[0]}-{columns[-1]}')
        # The smallest size keeps its three to a page; the larger sizes hold fewer, never none.
        self.assertEqual(per_page[0], 3)
        self.assertTrue(1 <= per_page[2] <= per_page[1] <= 3, per_page)

    # Long quotes of one invented book: each runs well past the four lines a block previews at the
    # smallest text size, so every block on the page is as tall as the page shape allows.
    LONG_QUOTES = [
        'Buổi sáng hôm ấy cả xóm dậy sớm hơn thường lệ, người thì ra đồng thăm lúa, người thì đứng trước '
        'hiên nhìn trời, còn bọn trẻ chạy dọc bờ đê xem nước sông lên tới đâu, rồi gọi nhau về ăn cơm khi '
        'mặt trời đã lên cao quá ngọn tre đầu làng và tiếng trống trường bắt đầu vang lên.',
        'Người thợ mộc già bảo rằng gỗ cũng biết thở, phải để nó nghỉ qua một mùa mưa rồi mới đem ra bào, '
        'vội vàng thì tấm ván sẽ cong, cái bàn đóng xong sẽ không bao giờ đứng cho vững, và người ngồi vào '
        'đó ăn bữa cơm nào cũng thấy chông chênh như đang ngồi trên một con thuyền nhỏ.',
        'Chị bán hàng nước đầu ngõ nhớ tên từng người đi qua, nhớ cả ai uống trà đặc ai uống trà loãng, '
        'và mỗi lần có người lạ tới chị lại hỏi thăm quê quán, hỏi đường đi lối lại, như thể đã quen nhau '
        'từ lâu lắm rồi và chỉ đang chờ một dịp để ngồi xuống kể cho hết chuyện cũ.',
    ]

    def test_long_quotes_fill_one_book_page_to_the_footer(self):
        # The reported screen: one book, long quotes, the smallest text size. Three blocks of the
        # tallest preview end at the band's bottom; no strip of white is left above the footer.
        for i in range(6):
            write_quote(self.quotes, *HILL, self.LONG_QUOTES[i % 3], i + 1, 10 + i * 7, 20260910 + i, 480 + i)
        log = self.journey('CONFIRM', '@b2-long-page1', 'DOWN', '@b2-long-page2')
        book = [e for e in lists(log) if e[0] == 'book']
        self.assertTrue(book, log[-4000:])
        self.assertEqual([e[2:4] for e in book], [(1, 2), (2, 2)])
        bands = [m for m in re.finditer(r'Quote page band (\d+)-(\d+) tip (\d+) per page (\d+) lines (\d+)/(\d+)/(\d+)',
                                         log)]
        top, bottom, tip, per_page, body, _, place = (int(x) for x in bands[-1].groups())
        self.assertEqual(per_page, 3)
        for name in ('b2-long-page1', 'b2-long-page2'):
            image = self.shot(name)
            pixels = image.load()
            inked = [y for y in range(top, tip) if any(pixels[x, y] < 128 for x in range(0, image.width))]
            self.assertTrue(inked, name)
            self.assertLessEqual(bottom - inked[-1], body,
                                 f'{name}: {bottom - inked[-1]} px of white between the last block and the footer')
        # The place line under each quote is set one tier over the caption face (21 px at this size).
        self.assertGreater(place, 21, 'the place line is still in the caption face')

    def test_one_book_of_four_quotes_kept_in_one_moment(self):
        # The shape of the first real store read off an X3: one book with a title of some 170
        # characters, four anchored quotes of one chapter, all kept on one day with no minute
        # stamped, under v1.0.10 hash names. CROSSPOINT_QUOTE_SEED_DIR points the scenario at
        # a copy of real files; the committed fixture has the same shape with other words.
        source = Path(os.environ.get('CROSSPOINT_QUOTE_SEED_DIR') or FIXTURES / 'quotes-one-long-title')
        shutil.rmtree(self.quotes)
        self.quotes.mkdir()
        for file in sorted(source.glob('*.json')):
            shutil.copy2(file, self.quotes / file.name)
        records = [json.loads(p.read_text(encoding='utf-8')) for p in sorted(self.quotes.glob('*.json'))]
        self.assertEqual(len(records), 4)
        path, day = records[0]['path'], records[0]['day']
        log = self.journey('@r1-books', 'CONFIRM', '@r1-list', 'LEFT', 'CONFIRM', 'CONFIRM', '@r1-book-order',
                           'RIGHT', 'CONFIRM', '@r1-detail', 'DOWN', '@r1-detail-2')
        # Four records of one book, day and (unknown) minute take the four slots of that moment.
        names = sorted(p.name for p in self.quotes.glob('*.json'))
        self.assertEqual(names, sorted(quote_name(path, day, None, slot) for slot in range(4)), log[-4000:])
        by_id = {self.id_of(p): json.loads(p.read_text(encoding='utf-8')) for p in self.quotes.glob('*.json')}
        self.assertEqual(book_pages(log)[0][2:4], (1, 4))
        book = [e for e in lists(log) if e[0] == 'book']
        self.assertEqual([e[1] for e in book], ['newest', 'oldest', 'page'])
        # One moment for all four: newest first falls back to the slot, the last taken first.
        self.assertEqual(book[0][5], newest_first(list(by_id))[:3])
        # Book order follows the anchors through the chapter, whatever order they were kept.
        self.assertEqual([by_id[i]['vo'] for i in book[2][5]], sorted(r['vo'] for r in records)[:3])
        first = details(log)[0]
        self.assertEqual(first[:3], (1, 4, book[2][5][0]))


if __name__ == '__main__':
    unittest.main()
