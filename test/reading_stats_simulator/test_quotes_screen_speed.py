"""Statistics > Quotes with a reading font that lives on the card.

The founder's X3 took 61.8 seconds to paint this screen for three saved quotes, and the
serial log named the cost: 6452 lines of "[SDCF] Overflow: loaded U+... on demand". Nothing
behind getTextWidth() reads the advance table, so wrapping a preview with a card font loads
every glyph it measures through an eight-slot cache. The two checks below are the shape of
that fault: the screen has to paint well inside a second, and it has to measure without
touching the card at all.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

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

    def seed_quotes(self, count):
        quotes = self.store / 'quotes'
        quotes.mkdir(exist_ok=True)
        for i in range(count):
            (quotes / ('%016x.json' % (0x1000000000000000 + i))).write_text(json.dumps(
                {'schema': 1, 'path': '/audit.epub', 'title': 'Synonym Lookup Test',
                 'text': BODY + ('Đoạn số %d.' % (i + 1)), 'spine': i % 5, 'page': i + 1,
                 'day': 20260921, 'gio': 1241}, ensure_ascii=False), encoding='utf-8')

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

    def open_time_and_card_reads(self, log):
        """Milliseconds from entering Quotes to its first painted frame, and the card
        reads charged to that window."""
        lines = log.splitlines()
        start = next(i for i, line in enumerate(lines) if 'Entering activity: Quotes' in line)
        entered = stamp(lines[start])
        self.assertIsNotNone(entered, lines[start])
        for i in range(start + 1, len(lines)):
            if 'clearScreen to displayBuffer' in lines[i]:
                painted = stamp(lines[i])
                reads = sum(1 for line in lines[start:i] if 'Overflow: loaded' in line)
                return painted - entered, reads
        self.fail('Quotes never painted:\n' + '\n'.join(lines[start:start + 40]))

    def test_quotes_screen_paints_under_a_second_without_reading_the_card(self):
        self.seed_quotes(22)
        # Home: three DOWN to the Statistics tab, three RIGHT to its fourth row (Quotes).
        events = ('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM;'
                  '20000:BACK;21000:QUIT')
        log = self.launch(events)
        self.assertIn('Entering activity: Quotes', log)
        elapsed, reads = self.open_time_and_card_reads(log)
        if evidence := os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR'):
            target = Path(evidence)
            target.mkdir(parents=True, exist_ok=True)
            (target / 'quotes-open.txt').write_text(
                f'22 saved quotes, reading font Literata on the card\n'
                f'open_ms={elapsed}\ncard_glyph_reads={reads}\n')
        self.assertEqual(reads, 0, f'the quote body was measured through the card {reads} times '
                                   f'({elapsed} ms to paint)')
        self.assertLess(elapsed, 1000, f'Quotes took {elapsed} ms to paint with 22 saved quotes')

    def test_detail_view_names_the_book_chapter_moment_and_page(self):
        self.seed_quotes(3)
        events = ('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM;'
                  '9000:CONFIRM;13000:BACK;14000:BACK;15000:QUIT')
        log = self.launch(events, ((12000, 'detail'),))
        self.assertIn('Entering activity: DictionaryDefinition', log)
        # Book, chapter, moment, page: four short rows, and only then the quote, which runs
        # the full width of the body. A screen that opens straight into the quote has a
        # full-width first row, which is what this build replaced.
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
            self.assertGreaterEqual(len(rows), 6, f'detail view shows only {len(rows)} rows of text')

            def ink_width(top, bottom):
                columns = [x for x in range(width) if any(pixels[x, y] < 128 for y in range(top, bottom + 1))]
                return max(columns) - min(columns) + 1 if columns else 0

            header = [ink_width(*rows[i]) for i in range(4)]
            body = [ink_width(*row) for row in rows[4:8]]
            self.assertTrue(all(w < width * 0.72 for w in header),
                            f'a source row runs the full width: {header} of {width}')
            self.assertTrue(any(w > width * 0.82 for w in body),
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
