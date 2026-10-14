"""Every screen that writes a book's name writes the short name (v1.0.61).

A long title "Name: a subtitle of five words or more (note)" is shown as "Name (note)" (src/util/BookTitleDisplay.h).
Each scene opens one screen twice on the same card, once with the stored long title and once with the short name
stored as the title. The two screens must be pixel for pixel the same: the subtitle never reaches the panel. A
third run with another closing note proves the scene reads the title at all, so two blank screens cannot pass.

Needs a simulator build: simulator_x3_uc8279 (keys) or simulator_x4pro (finger). TEST_PROGRAM picks the
program, TEN_SACH_SHOTS a directory to keep the screens as PNG.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image

from cai_dat_truoc_tenor import truoc_tenor
from test_sleep_quote_v1011 import quote_id

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
SHOTS = os.environ.get('TEN_SACH_SHOTS')
X4PRO = 'x4pro' in str(PROGRAM)

LONG = 'Những việc lớn được hoàn thành như thế nào: Những yếu tố bất ngờ quyết định thành bại (tenor version)'
SHORT = 'Những việc lớn được hoàn thành như thế nào (tenor version)'
# The name that tells the scene reads the title: it differs from SHORT in the first words, not in the closing note,
# because a narrow slot cuts the title long before the note.
OTHER = 'Việc lớn được hoàn thành như thế nào (tenor version)'
# The same three for a narrow slot (a pill, the bar under a page, handwriting): the name is short enough to show whole.
NARROW = ('Việc lớn: Những yếu tố bất ngờ quyết định thành bại (tenor version)', 'Việc lớn (tenor version)',
           'Việc nhỏ (tenor version)')
BOOK = '/books/viec-lon.epub'
NOW = 1791700000  # the clock of the simulator, fixed: 2026-10-11
TODAY, YESTERDAY = 20261011, 20261010
QUOTE_TEXT = 'Việc lớn chỉ xong khi có người chịu làm phần việc nhàm chán nhất, mỗi ngày, đến cuối.'


def write_epub(path, title):
    path.parent.mkdir(parents=True, exist_ok=True)
    opf = ('<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata '
           'xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + title + '</dc:title><dc:creator>Tenor</dc:creator>'
           '<dc:identifier id="id">ten-sach-gon</dc:identifier><dc:language>vi</dc:language></metadata><manifest>'
           '<item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine>'
           '<itemref idref="body"/></spine></package>')
    body = ''.join('<p>Đoạn văn số %d của cuốn sách thử, đủ dài để trang đọc có chữ. </p>' % n * 3 for n in range(60))
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:'
                   'container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/>'
                   '</rootfiles></container>')
        z.writestr('book.opf', opf)
        z.writestr('body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body>'
                   + body + '</body></html>')


def make_card(root, title, settings, state=None, filler=False):
    """One card: the book, Recent, the statistics of the book and three quotations, all stored under `title`."""
    sd = Path(root)
    store = sd / '.crosspoint'
    (store / 'quotes').mkdir(parents=True)
    write_epub(sd / BOOK.lstrip('/'), title)
    (store / 'settings.json').write_text(json.dumps(truoc_tenor(
        {'language': 'VI', 'uiShellClockOnce': 1, 'clockShowInHeader': 0, 'statusBarClock': 0, 'sleepTimeout': 10,
         'readerTapTip': 0, **settings})))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, **(state or {})}))
    books = [{'path': BOOK, 'title': title, 'author': 'Tenor'}]
    if filler:  # the notebook lists the books after the one being read
        (sd / 'khac.txt').write_text('Một cuốn khác. ' * 50)
        books.insert(0, {'path': '/khac.txt', 'title': 'Sách đang đọc dở', 'author': 'Tenor'})
    (store / 'recent.json').write_text(json.dumps({'books': books}))
    (store / 'reading-stats.json').write_text(json.dumps({
        'schema': 3, 'bookEpoch': 0, 'ngay': [[YESTERDAY, 12, 30]], 'lacPhut': 3, 'lacTrang': 4,
        'activeBook': {'bookEpoch': 0, 'path': BOOK, 'title': title, 'minutes': 12, 'ms': 0, 'turns': 30,
                       'first': YESTERDAY, 'last': YESTERDAY, 'days': 1, 'progress': 42, 'startProgress': 5}}))
    (store / 'quotes' / '.ten-v2').write_text('2')
    for slot in range(3):
        record = {'schema': 1, 'path': BOOK, 'title': title, 'text': QUOTE_TEXT, 'spine': 0, 'page': 3 + slot,
                  'day': YESTERDAY, 'gio': 600}
        (store / 'quotes' / f'{quote_id(record, slot):016x}.json').write_text(json.dumps(record, ensure_ascii=False))
    return sd


def run(sd, script, shots, extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_NOW=str(NOW),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{sd / (name + ".bmp")}' for ms, name in shots))
    env.update(extra or {})
    done = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
    log = done.stdout + done.stderr
    assert done.returncode == 0, log[-3000:]
    return log, {name: Image.open(sd / (name + '.bmp')).convert('L') for _, name in shots}


# A scene: the script (keys on the X3, a finger on the X4 Pro), the settings, the shot time (ms), the activity it must
# have entered and whether the card holds one more book before this one. The screen is the last one of the script.
STATS = '1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:CONFIRM'  # Home, Stats tab, By book (X3)
QUOTES = '1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM'  # Home, Stats tab, Quotes (X3)
STATS_X = '3000:TAP:331,754;4500:TAP:240,592'  # the Stats tab, By book (X4 Pro)
QUOTES_X = '3000:TAP:331,754;4500:SWIPE:240,640,240,200,150;6500:TAP:240,559'  # the Stats tab, Quotes
READER = dict(settings={'wakeIntoBook': 1, 'statusBarTitle': 0, 'readerStatusBarMode': 2, 'statusBarItemsMode': 2},
              state={'openEpubPath': BOOK, 'lastSleepFromReader': True}, extra={'CROSSPOINT_SIM_WAKE_REASON': 'power'})
UGLY = {'uiShell': 1}
SCENES = {
    # name: (X3 script, X4 Pro script, shot ms X3, shot ms X4 Pro, enter, options); narrow: the narrow-slot titles
    'reader_status_bar': ('', '', 5000, 5000, 'EpubReader', dict(READER, narrow=True)),
    'stats_by_book': (STATS, STATS_X, 6000, 7000, 'BookStatsLibrary', {}),
    'stats_of_one_book': (STATS + ';6500:CONFIRM', STATS_X + ';7000:TAP:240,60', 9000, 9500, 'BookStats',
                          dict(narrow=True)),
    'quotes_by_book': (QUOTES, QUOTES_X, 8000, 9000, 'Quotes', dict(narrow=True)),
    'quotes_of_one_book': (QUOTES + ';8500:CONFIRM', QUOTES_X + ';9500:TAP:240,100', 11000, 12000, 'Quotes',
                           dict(narrow=True)),
    'quote_detail': (QUOTES + ';8500:CONFIRM;11500:CONFIRM', QUOTES_X + ';9500:TAP:240,100;12500:TAP:240,150',
                     14000, 15000, 'QuoteDetail', {}),
    'sleep_quote': ('2500:SLEEP', '2500:SLEEP', 6500, 6500, 'Sleep', dict(settings={'sleepScreen': 10, 'sleepBwFold': 1})),
    'ugly_diary': ('', '', 2500, 2500, 'UglyDiary', dict(settings=UGLY, narrow=True)),
    'ugly_desk': ('1000:DOWN;2000:BACK', '2000:TAP:240,670', 4000, 4000, 'UglyDesk',
                  dict(settings=UGLY, narrow=True)),
    'ugly_notebook': ('1000:DOWN', '2000:TAP:240,670;3500:TAP:240,150', 3000, 5500, 'UglyNotebook',
                      dict(settings=UGLY, filler=True, narrow=True)),
}


def capture(name, title):
    script_x3, script_x4pro, at_x3, at_x4pro, enter, options = SCENES[name]
    script, at = (script_x4pro, at_x4pro) if X4PRO else (script_x3, at_x3)
    with tempfile.TemporaryDirectory(prefix='cross-ten-sach-') as root:
        sd = make_card(root, title, options.get('settings', {}), options.get('state'), options.get('filler', False))
        log, images = run(sd, (script + ';' if script else '') + f'{at + 1500}:QUIT', [(at, 'shot')], options.get('extra'))
        assert f'Entering activity: {enter}' in log, f'{name}: never entered {enter}\n{log[-2500:]}'
        return images['shot']


def keep(name, tag, image):
    if SHOTS:
        Path(SHOTS).mkdir(parents=True, exist_ok=True)
        image.save(Path(SHOTS) / f"{'x4pro' if X4PRO else 'x3'}-{name}-{tag}.png")


def changed(a, b):
    """Pixels that differ between two screens."""
    return sum(x != y for x, y in zip(a.getdata(), b.getdata()))


class TenSachGonTest(unittest.TestCase):
    def check(self, name):
        titles = NARROW if SCENES[name][5].get('narrow') else (LONG, SHORT, OTHER)
        long_screen, short_screen, other_screen = (capture(name, t) for t in titles)
        for tag, image in (('long', long_screen), ('short', short_screen), ('other', other_screen)):
            keep(name, tag, image)
        self.assertGreater(changed(short_screen, other_screen), 0, f'{name}: the screen does not show the title')
        self.assertEqual(changed(long_screen, short_screen), 0, f'{name}: the subtitle reaches the panel')

    def test_reader_status_bar(self):
        self.check('reader_status_bar')

    def test_stats_by_book(self):
        self.check('stats_by_book')

    def test_stats_of_one_book(self):
        self.check('stats_of_one_book')

    def test_quotes_by_book(self):
        self.check('quotes_by_book')

    def test_quotes_of_one_book(self):
        self.check('quotes_of_one_book')

    def test_quote_detail(self):
        self.check('quote_detail')

    def test_sleep_quote(self):
        self.check('sleep_quote')

    def test_ugly_diary(self):
        self.check('ugly_diary')

    def test_ugly_desk(self):
        self.check('ugly_desk')

    def test_ugly_notebook(self):
        self.check('ugly_notebook')


if __name__ == '__main__':
    unittest.main()
