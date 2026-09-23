"""Statistics > Quotes with a reading font that lives on the card.

A real X3 took 61.8 seconds to paint this screen for three saved quotes, and the
serial log named the cost: 6452 lines of "[SDCF] Overflow: loaded U+... on demand". Nothing
behind getTextWidth() reads the advance table, so wrapping a preview with a card font loads
every glyph it measures through an eight-slot cache. The checks below are the shape of that
fault: each Quotes screen has to paint well inside a second, and the whole visit has to
measure without touching the card at all, for a small store and for 300 quotes in 6 books.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from test_quotes_v1011 import write_quote

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('CROSSPOINT_SIM_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
PACK = Path(os.environ.get('READER_INK_PACK', ''))

STAMP = re.compile(r'^\[(\d+)\]')

# One long paragraph per quote, so every block wraps to the full four-line preview.
BODY = ('Tiêu chuẩn của một xưởng nằm ở việc nhỏ nhất mà người trong xưởng vẫn làm đúng khi '
        'không ai nhìn, và điều đó chỉ giữ được khi có người đứng ra đo, ghi lại, rồi đối '
        'chiếu với lần trước. Một dòng số đo thật đáng giá hơn mười dòng hứa hẹn. ')


def stamp(line):
    found = STAMP.match(line.strip())
    return int(found.group(1)) if found else None


class QuotesScreenSpeedTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-quotes-speed-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', self.sd / 'audit.epub')
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': '/audit.epub', 'title': 'Synonym Lookup Test'}]}))
        self.install_card_font()
        (self.store / 'settings.json').write_text(json.dumps(
            {'language': 'VI', 'uiTheme': 4, 'fontSize': 16, 'sdFontFamilyName': 'Literata',
             'readerInkWeightVersion': 1, 'readerInkWeight': 0, 'sleepTimeout': 120}))

    def install_card_font(self):
        """The reading font the screen must not measure through: Literata, on the card."""
        self.assertTrue(PACK.is_dir(), 'READER_INK_PACK must point at the font pack')
        manifest = json.loads((PACK / 'font-manifest.json').read_text())
        copied = 0
        for entry in manifest:
            if entry['family'] != 'Literata' or entry['size'] != 16:
                continue
            target = self.sd / entry['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((PACK / entry['path']).read_bytes())
            copied += 1
        self.assertGreaterEqual(copied, 2, 'Literata 16 missing from the pack')

    def seed_quotes(self, count, books=1):
        """`count` quotes under the names the firmware gives them, spread over `books` books;
        the first book is the fixture on the card, so its detail can name a chapter."""
        quotes = self.store / 'quotes'
        quotes.mkdir(exist_ok=True)
        (quotes / '.ten-v2').write_text('2')
        for i in range(count):
            book = i % books
            path, title = ('/audit.epub', 'Synonym Lookup Test') if book == 0 else \
                (f'/sach/cuon-{book}.epub', f'Cuốn sách số {book}')
            write_quote(quotes, path, title, BODY + ('Đoạn số %d.' % (i + 1)), i % 5, i + 1,
                        20260901 + i // 60 % 28, i % 60 * 20 + book)

    def launch(self, events, shots=()):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events)
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        log = tempfile.TemporaryFile(mode='w+')
        self.addCleanup(log.close)
        process = subprocess.Popen([str(PROGRAM)], cwd=REPO, env=env, stdout=log, stderr=log)
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        process.wait(timeout=180)
        log.seek(0)
        text = log.read()
        self.assertEqual(process.returncode, 0, text[-8000:])
        return text

    def paint_after(self, lines, start):
        """Milliseconds from line `start` to the next painted frame."""
        began = stamp(lines[start])
        self.assertIsNotNone(began, lines[start])
        for i in range(start + 1, len(lines)):
            if 'clearScreen to displayBuffer' in lines[i]:
                return stamp(lines[i]) - began
        self.fail('never painted after:\n' + '\n'.join(lines[start:start + 40]))

    def timings(self, log):
        """Paint time of the list of books on entry and of the "all quotes" list after the
        top row switches to it, and the card reads charged to the whole visit."""
        lines = log.splitlines()
        entered = next(i for i, line in enumerate(lines) if 'Entering activity: Quotes' in line)
        switched = next(i for i, line in enumerate(lines) if 'Quote view all newest' in line)
        left = next((i for i, line in enumerate(lines) if 'Exiting activity: Quotes' in line), len(lines))
        reads = sum(1 for line in lines[entered:left] if 'Overflow: loaded' in line)
        return self.paint_after(lines, entered), self.paint_after(lines, switched), reads

    # Home: three DOWN to the Statistics tab, three RIGHT to its fourth row (Quotes). Left
    # reaches the top row, Select turns it to every quote newest first, Right walks the
    # list, a side button turns its page.
    VISIT = ('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM;'
             '9000:LEFT;10500:CONFIRM;13000:RIGHT;14500:DOWN;17000:BACK;18000:QUIT')

    def check_visit(self, count, books):
        self.seed_quotes(count, books)
        log = self.launch(self.VISIT)
        self.assertIn('Entering activity: Quotes', log)
        opened, switched, reads = self.timings(log)
        if evidence := os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR'):
            target = Path(evidence)
            target.mkdir(parents=True, exist_ok=True)
            (target / f'quotes-open-{count}.txt').write_text(
                f'{count} saved quotes in {books} books, reading font Literata on the card\n'
                f'books_open_ms={opened}\nall_newest_ms={switched}\ncard_glyph_reads={reads}\n')
        self.assertEqual(reads, 0, f'the Quotes screens measured through the card {reads} times')
        self.assertLess(opened, 1000, f'the list of books took {opened} ms with {count} quotes')
        self.assertLess(switched, 1000, f'every quote, newest first, took {switched} ms with {count} quotes')

    def test_quotes_screen_paints_under_a_second_without_reading_the_card(self):
        self.check_visit(22, 1)

    def test_three_hundred_quotes_in_six_books_paint_under_a_second(self):
        self.check_visit(300, 6)

    def test_detail_view_names_the_book_chapter_moment_and_page(self):
        self.seed_quotes(3)
        events = ('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM;'
                  '8000:CONFIRM;9500:CONFIRM;13000:BACK;14000:BACK;15000:BACK;16000:QUIT')
        log = self.launch(events, ((12000, 'detail'),))
        self.assertIn('Entering activity: QuoteDetail', log)
        # Mockup D2: the quote first, running the width of the body, then under a short rule
        # four short rows: the book, the chapter, the moment, the page.
        from PIL import Image
        with Image.open(self.sd / 'detail.bmp') as shot:
            image = shot.convert('L')
            pixels = image.load()
            width, height = image.size
            rows, run = [], None
            for y in range(70, height - 50):
                dark = any(pixels[x, y] < 128 for x in range(20, width - 20))
                if dark and run is None:
                    run = y
                if not dark and run is not None:
                    rows.append((run, y - 1))
                    run = None
            if evidence := os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR'):
                target = Path(evidence)
                target.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.sd / 'detail.bmp', target / 'quote-detail.bmp')
            self.assertGreaterEqual(len(rows), 8, f'detail view shows only {len(rows)} rows of text')

            def ink_width(top, bottom):
                columns = [x for x in range(width) if any(pixels[x, y] < 128 for y in range(top, bottom + 1))]
                return max(columns) - min(columns) + 1 if columns else 0

            meta = [ink_width(*row) for row in rows[-4:]]
            body = [ink_width(*row) for row in rows[:-5]]
            self.assertTrue(all(w < width * 0.72 for w in meta),
                            f'a source row runs the full width: {meta} of {width}')
            self.assertTrue(any(w > width * 0.7 for w in body),
                            f'the quote never reaches the body width: {body} of {width}')

    def test_selection_screen_exits_after_saving_a_quote(self):
        # Open the book, reach Tools > Save quotation, pick two words, save, dismiss, leave.
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;'
                  '9900:RIGHT;11000:CONFIRM;12500:CONFIRM;14500:BACK;16000:QUIT')
        log = self.launch(events)
        self.assertIn('Entering activity: QuoteSelect', log)
        self.assertIn('Exiting activity: QuoteSelect', log)
        self.assertEqual(len(list((self.store / 'quotes').glob('*.json'))), 1, log)
        # Back in the book with the highlight drawn, and still running.
        self.assertIn('Popped from activity stack', log)

    def test_selection_screen_exits_without_saving(self):
        # Start a selection, cancel it with Back, then leave the screen with a second Back.
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;'
                  '9900:RIGHT;11000:BACK;12200:BACK;14000:QUIT')
        log = self.launch(events)
        self.assertIn('Entering activity: QuoteSelect', log)
        self.assertIn('Exiting activity: QuoteSelect', log)
        self.assertFalse(list((self.store / 'quotes').glob('*.json')) if (self.store / 'quotes').is_dir() else [])


if __name__ == '__main__':
    unittest.main()
