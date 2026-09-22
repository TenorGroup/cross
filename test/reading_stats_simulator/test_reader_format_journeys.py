"""Real reader turns, restart positions, preview isolation and EPUB spine boundaries."""
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageChops


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('CROSSPOINT_SIM_PROGRAM',
                              REPO / '.pio/build/simulator_x3_uc8279/program'))


def write_epub(path, boundary=False, illustrated=False):
    count = 3 if boundary else 1
    with zipfile.ZipFile(path, 'w') as book:
        book.writestr('mimetype', 'application/epub+zip')
        book.writestr('META-INF/container.xml',
                      '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">'
                      '<rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        manifest = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
                           for i in range(count))
        if illustrated:
            manifest += '<item id="picture" href="picture.png" media-type="image/png"/>'
            picture = Image.new('L', (400, 1000), 255)
            picture.paste(0, (100, 50, 300, 950))
            encoded = io.BytesIO()
            picture.save(encoded, format='PNG')
            book.writestr('picture.png', encoded.getvalue())
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(count))
        book.writestr('book.opf',
                      '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
                      '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Reader journey</dc:title><dc:identifier id="id">reader-journey</dc:identifier>'
                      '<dc:language>en</dc:language></metadata>'
                      f'<manifest>{manifest}</manifest><spine>{spine}</spine></package>')
        for i in range(count):
            body = (f'<h1>Distinct chapter {i + 1}</h1><p>Chapter marker {i + 1}.</p>' if boundary else
                    ''.join(f'<p>Paragraph {n:04d}. ' +
                            'A different place in this book remains visible after reopening. ' * 5 + '</p>'
                            for n in range(100)))
            if illustrated:
                body = '<p><img src="picture.png" alt="Tall illustration"/></p>' + body
            book.writestr(f'c{i}.xhtml',
                          '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Journey</title></head>'
                          f'<body>{body}</body></html>')


def write_xtc(path):
    pages = []
    for number in range(5):
        bitmap = bytearray(b'\xff' * (528 * 792 // 8))
        for y in range(80 + number * 90, 120 + number * 90):
            bitmap[y * 66 + 8:y * 66 + 40] = b'\x00' * 32
        pages.append(struct.pack('<IHHBBIQ', 0x00475458, 528, 792, 0, 0, len(bitmap), 0) + bitmap)
    start = 56 + 16 * len(pages)
    header = struct.pack('<IBBHBBBBIQQQQII', 0x00435458, 1, 0, len(pages), 0, 0, 0, 0, 1,
                         0, 56, start, 0, 0, 0)
    table = b''.join(struct.pack('<QIHH', start + i * len(page), len(page), 528, 792)
                     for i, page in enumerate(pages))
    path.write_bytes(header + table + b''.join(pages))


class ReaderFormatJourneysTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cross-reader-journey-')
        self.sd = Path(self.temporary.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        (self.store / 'settings.json').write_text(json.dumps({
            'language': 'EN', 'fontSize': 14, 'sleepTimeout': 10, 'longPressButtonBehavior': 0}))
        self.stage = 0

    def tearDown(self):
        evidence = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        if evidence:
            shutil.copytree(self.sd, Path(evidence) / self._testMethodName, dirs_exist_ok=True)
        self.temporary.cleanup()

    def fixture(self, extension, boundary=False, illustrated=False):
        self.extension = extension
        self.book = self.sd / 'books' / ('journey.' + extension)
        if extension == 'epub':
            write_epub(self.book, boundary, illustrated)
        elif extension == 'xtc':
            write_xtc(self.book)
        else:
            self.book.write_text(''.join(
                f'Paragraph {number:04d}. ' + 'This place in the book has a unique numbered marker. ' * 8 + '\n\n'
                for number in range(100)))
        self.book_hash = hashlib.sha256(self.book.read_bytes()).hexdigest()
        (self.store / 'recent.json').write_text(json.dumps({
            'books': [{'path': '/books/' + self.book.name, 'title': 'Reader journey'}]}))
        (self.store / 'quotes').mkdir()
        (self.store / 'quotes' / 'saved.json').write_text(json.dumps({
            'path': '/books/' + self.book.name, 'title': 'Reader journey',
            'text': 'Saved passage', 'spine': 0, 'page': 0, 'day': 20260921}))
        (self.store / 'bookmarks').mkdir()
        (self.store / 'bookmarks' / 'books_journey.json').write_text(json.dumps({
            'bookmarks': [{'xpath': '', 'percentage': 0, 'summary': 'Saved place',
                           'si': 0, 'pc': 1, 'pp': 0, 'vo': 0}]}))

    def run_reader(self, events, shots=(), preview=False):
        self.stage += 1
        negative = os.environ.get('CROSSPOINT_JOURNEY_DROP_TURNS')
        if negative == '1' or (negative == 'preview' and preview):
            events = ';'.join(event for event in events.split(';') if ':RIGHT' not in event)
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=events)
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(
                f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=25)
        log = run.stdout + run.stderr
        (self.sd / f'stage-{self.stage}.log').write_text(log)
        self.assertEqual(run.returncode, 0, log)
        activity = {'epub': 'EpubReader', 'txt': 'TxtReader', 'md': 'TxtReader', 'xtc': 'XtcReader'}[self.extension]
        self.assertIn('Entering activity: ' + activity, log)
        self.assertNotRegex(log, r'Guru Meditation|assert failed|Failed to load page|Page index incomplete')
        if preview:
            self.assertIn('Preview: /books/' + self.book.name, log)
        self.assertEqual(hashlib.sha256(self.book.read_bytes()).hexdigest(), self.book_hash)
        return log

    def progress(self):
        files = list(self.store.glob('*/progress.bin'))
        self.assertEqual(len(files), 1, files)
        data = files[0].read_bytes()
        if self.extension == 'epub':
            self.assertGreaterEqual(len(data), 4)
            return struct.unpack('<HH', data[:4])
        self.assertEqual(len(data), 4)
        return (0, struct.unpack('<I', data)[0])

    def compare_content(self, first, second, equal):
        with Image.open(self.sd / (first + '.bmp')) as left, Image.open(self.sd / (second + '.bmp')) as right:
            self.assertEqual(left.size, right.size)
            crop = (0, 0, left.width, left.height - 80)
            difference = ImageChops.difference(left.crop(crop).convert('RGB'), right.crop(crop).convert('RGB'))
            self.assertEqual(difference.getbbox() is None, equal,
                             f'content comparison {first}/{second}, expected equality={equal}')

    def warm_page_two(self):
        log = self.run_reader('1000:CONFIRM;3000:RIGHT;4800:RIGHT;6800:BACK;7800:QUIT',
                              ((2400, 'normal-first'), (6000, 'normal-saved')))
        self.assertEqual(self.progress(), (0, 2), log)
        self.compare_content('normal-first', 'normal-saved', False)
        self.assertIn('Exiting activity:', log)

    def reopen_page_two(self):
        log = self.run_reader('1000:CONFIRM;3800:BACK;4800:QUIT', ((2800, 'reopened'),))
        self.assertEqual(self.progress(), (0, 2), log)
        self.compare_content('normal-saved', 'reopened', True)
        if self.extension == 'epub':
            self.assertIn('Section cache HIT:', log)
            self.assertNotIn('Section cache MISS:', log)
        elif self.extension in ('txt', 'md'):
            self.assertIn('Loaded page index cache:', log)
            self.assertNotIn('Saved page index cache:', log)

    def ordinary(self, extension):
        self.fixture(extension)
        self.warm_page_two()
        self.reopen_page_two()

    def preview(self, extension, illustrated=False):
        self.fixture(extension, illustrated=illustrated)
        self.warm_page_two()
        if illustrated:
            self.assertTrue(list(self.store.rglob('*.pxc')), 'normal image was not cached')
        before = {str(path.relative_to(self.store)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in self.store.rglob('*') if path.is_file()}
        log = self.run_reader(
            '1600:DOWN;2400:CONFIRM;4000:DOWN:900;6000:RIGHT;7800:RIGHT;10000:BACK;11200:BACK;12200:QUIT',
            ((5400, 'preview-first'), (7000, 'preview-second'), (9200, 'preview-third')), preview=True)
        self.compare_content('preview-first', 'preview-second', False)
        self.compare_content('preview-second', 'preview-third', False)
        after = {str(path.relative_to(self.store)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in self.store.rglob('*') if path.is_file()}
        self.assertEqual({name: after.get(name) for name in before}, before,
                         'preview changed an existing store or warm cache')
        self.assertEqual(self.progress(), (0, 2), log)
        self.assertIn('Popped from activity stack, new size = 0', log)
        self.reopen_page_two()

    def test_epub_restart(self):
        self.ordinary('epub')

    def test_txt_restart(self):
        self.ordinary('txt')

    def test_markdown_restart(self):
        self.ordinary('md')

    def test_xtc_restart(self):
        self.ordinary('xtc')

    def test_epub_preview_turns(self):
        self.preview('epub')

    def test_epub_preview_preserves_image_cache(self):
        self.preview('epub', illustrated=True)

    def test_txt_preview_turns(self):
        self.preview('txt')

    def test_markdown_preview_turns(self):
        self.preview('md')

    def test_xtc_preview_turns(self):
        self.preview('xtc')

    def test_epub_short_turn_boundaries_and_restart(self):
        self.fixture('epub', boundary=True)
        log = self.run_reader('1000:CONFIRM;3000:RIGHT;4800:RIGHT;6600:LEFT;8600:BACK;9600:QUIT',
                              ((8000, 'boundary-saved'),))
        positions = [(int(spine), int(page)) for spine, page in
                     re.findall(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)', log)]
        self.assertEqual(positions, [(0, 0), (1, 0), (2, 0), (1, 0)], log)
        self.assertEqual(self.progress(), (1, 0))
        self.run_reader('1000:CONFIRM;3800:BACK;4800:QUIT', ((2800, 'boundary-reopened'),))
        self.assertEqual(self.progress(), (1, 0))
        self.compare_content('boundary-saved', 'boundary-reopened', True)


if __name__ == '__main__':
    unittest.main()
