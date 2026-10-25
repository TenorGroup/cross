"""X4 Pro: More > Search in book, the whole journey on the simulator.

Open the reader's toolbar, More, the first row; type a word on the keyboard; the scan names its chapter; the hits are
listed (chapter name and excerpt); a tap on the third hit lands the reader on the page that holds the word. Also: Stop
halfway keeps the hits so far, a word the book lacks says so, and the last search is still there when More is opened
again. The page identity comes from the firmware's own `Progress saved:` line.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
BOOK = '/sach/tim.epub'
HITS = (2, 5, 9, 14, 20, 27, 33, 38)  # the chapters that hold the word "nghịch" (twice each)
FILLER = ('Mặt trời lên trên đồi cao, gió thổi qua rặng tre xanh và tiếng chim hót vang khắp xóm làng nhỏ bé. '
          'Con đường đất đỏ dẫn ra bờ sông, nơi những chiếc thuyền gỗ neo lại sau một ngày dài. ')

# Screen points of the X4 Pro (480 x 800).
TOOLBAR = (240, 775)       # the foot of the page opens the reader's toolbar
MORE_TOOL = (412, 754)     # the More tool in it
FIRST_ROW = (240, 436)     # the first row of the More panel
KEYS = {  # the Telex keyboard's letters on the English layout, row by row
    'q': (35, 473), 'w': (80, 473), 'e': (125, 473), 'r': (170, 473), 't': (215, 473), 'y': (260, 473),
    'u': (305, 473), 'i': (350, 473), 'o': (395, 473), 'p': (440, 473),
    'a': (35, 539), 's': (80, 539), 'd': (125, 539), 'f': (170, 539), 'g': (215, 539), 'h': (260, 539),
    'j': (305, 539), 'k': (350, 539), 'l': (395, 539),
    # the third row opens with the Shift key (Aa), which moves the letters one key to the right
    'z': (80, 605), 'x': (125, 605), 'c': (170, 605), 'v': (215, 605), 'b': (260, 605), 'n': (305, 605),
    'm': (350, 605)}
OK_KEY = (413, 670)
BACK = (46, 754)           # the bar's "<"
STOP = (420, 754)          # the bar's Stop pill while searching
QUERY_PILL = (200, 754)    # the bar's query pill on the results


def write_book(path, chapters=40, hits=HITS):
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        items = ''.join(f'<item id="c{n}" href="c{n}.xhtml" media-type="application/xhtml+xml"/>' for n in range(1, chapters + 1))
        refs = ''.join(f'<itemref idref="c{n}"/>' for n in range(1, chapters + 1))
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Tim</dc:title><dc:identifier id="id">tim-search-v1061</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata><manifest>' + items +
                      '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                      f'<spine toc="ncx">{refs}</spine></package>')
        points = ''.join(f'<navPoint id="n{n}" playOrder="{n}"><navLabel><text>Chương {n}. Tên chương {n}</text></navLabel>'
                         f'<content src="c{n}.xhtml"/></navPoint>' for n in range(1, chapters + 1))
        epub.writestr('toc.ncx',
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      f'<head/><docTitle><text>Tim</text></docTitle><navMap>{points}</navMap></ncx>')
        for n in range(1, chapters + 1):
            paragraphs = ''
            for i in range(30):
                text = FILLER * 2
                if n in hits and i == 18:
                    text = 'Đoạn này kể về một cậu bé nghịch ngợm, vì nghịch quá nên ai cũng nhớ. ' + FILLER
                paragraphs += f'<p>{text}</p>'
            epub.writestr(f'c{n}.xhtml',
                          '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f'<title>c{n}</title></head><body><h1>Chương {n}. Tên chương {n}</h1>{paragraphs}</body></html>')


def type_word(start, word):
    """Input script steps that tap the letters of `word`, 400 ms apart from `start`; the next free time."""
    steps = []
    for letter in word:
        steps.append(f'{start}:TAP:{KEYS[letter][0]},{KEYS[letter][1]}')
        start += 400
    return steps, start


def run(script, shots, chapters=40, hits=HITS, shell=0):
    with tempfile.TemporaryDirectory(prefix='x4-tim-') as tmp:
        sd = Path(tmp) / 'sd'
        (sd / '.crosspoint').mkdir(parents=True)
        (sd / 'sach').mkdir()
        write_book(sd / 'sach/tim.epub', chapters, hits)
        (sd / '.crosspoint/settings.json').write_text(json.dumps({
            'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1,
            'uiShellSleepMemo': 0, 'language': 'VI', 'uiTextSize': 0, 'wakeIntoBook': 1, 'fontSize': 18,
            'fontFamily': 0, 'readerMenuStyle': 1, 'uiShell': shell, 'sleepTimeout': 10, 'readerTapTip': 0,
            'uiShellClockOnce': 1, 'clockShowInHeader': 0}))
        (sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': BOOK,
                                                               'lastSleepFromReader': True}))
        (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Tim'}]}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_NOW='1790000000',
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(script) + f';{max(shots) + 700}:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{tmp}/{ms}.bmp' for ms in shots))
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
        log = result.stdout + result.stderr
        images = {ms: Image.open(f'{tmp}/{ms}.bmp').convert('L') for ms in shots if Path(f'{tmp}/{ms}.bmp').exists()}
        return log, images


def open_search(at):
    """Steps from `at`: toolbar, More, its first row. Returns (steps, next free time)."""
    return ([f'{at}:TAP:{TOOLBAR[0]},{TOOLBAR[1]}', f'{at + 1500}:TAP:{MORE_TOOL[0]},{MORE_TOOL[1]}',
             f'{at + 3000}:TAP:{FIRST_ROW[0]},{FIRST_ROW[1]}'], at + 4500)


class BookSearchJourney(unittest.TestCase):
    def test_open_type_search_and_jump_to_the_third_hit(self):
        steps, t = open_search(3000)
        typed, t = type_word(t, 'nghich')
        steps += typed + [f'{t}:TAP:{OK_KEY[0]},{OK_KEY[1]}']
        t += 6000
        results = t
        steps.append(f'{t + 500}:TAP:240,250')  # the third hit's row
        log, images = run(steps, [results, t + 5500])
        self.assertIn('[BKS] begin query=nghich chapters=40', log)
        done = re.search(r'\[BKS\] done results=(\d+) chapters=40 next=40 skipped=0 .* stopped=0 failed=0', log)
        self.assertIsNotNone(done, log[-3000:])
        self.assertEqual(int(done.group(1)), 2 * len(HITS))
        pick = re.search(r'\[BKS\] pick index=(\d+) spine=(\d+) offset=(\d+)', log)
        self.assertIsNotNone(pick, log[-3000:])
        # Hits come in book order, two to a chapter: the third is the first of the second chapter that holds the word.
        self.assertEqual((int(pick.group(1)), int(pick.group(2))), (2, HITS[1] - 1))
        saved = [(int(m.group(1)), int(m.group(2)), int(m.group(3)))
                 for m in re.finditer(r'Progress saved: spine=(\d+) offset=(\d+) page=(\d+)', log[log.index('[BKS] pick'):])]
        self.assertTrue(saved, 'no page was painted after the jump')
        spine, offset, page = saved[-1]
        self.assertEqual(spine, HITS[1] - 1)
        self.assertGreater(page, 0)
        # The page that holds the word starts at or before it, and not a whole page of text before it.
        self.assertLessEqual(offset, int(pick.group(3)))
        self.assertLess(int(pick.group(3)) - offset, 2500)

    def test_back_after_a_hit_returns_to_the_page_before_the_search(self):
        steps, t = open_search(3000)
        typed, t = type_word(t, 'nghich')
        steps += typed + [f'{t}:TAP:{OK_KEY[0]},{OK_KEY[1]}']
        t += 6000
        steps += [f'{t + 500}:TAP:240,250', f'{t + 6000}:BACK']
        log, _ = run(steps, [t + 11000])
        saved = [(int(m.group(1)), int(m.group(3)))
                 for m in re.finditer(r'Progress saved: spine=(\d+) offset=(\d+) page=(\d+)', log)]
        self.assertIn('[BKS] pick', log, log[-3000:])
        before = [(int(m.group(1)), int(m.group(3))) for m in re.finditer(
            r'Progress saved: spine=(\d+) offset=(\d+) page=(\d+)', log[:log.index('[BKS] begin')])]
        self.assertTrue(before and saved, log[-3000:])
        self.assertNotIn('Exiting activity: EpubReader', log[log.index('[BKS] pick'):], 'Back left the book')
        self.assertEqual(saved[-1], before[-1], saved)

    def test_a_hit_on_the_page_already_shown_is_no_jump_and_back_goes_to_the_page_before_the_search(self):
        steps, t = open_search(3000)
        typed, t = type_word(t, 'nghich')
        steps += typed + [f'{t}:TAP:{OK_KEY[0]},{OK_KEY[1]}']
        t += 6000
        steps.append(f'{t + 500}:TAP:240,250')  # the third hit: chapter 5, page 19
        t += 6500  # past the way-back bar's 3 seconds, which stands over the toolbar's tap
        again, t = open_search(t)
        # The list kept its hits: the fourth is in the same paragraph, on the page the reader is on.
        steps += again + [f'{t + 800}:TAP:240,333', f'{t + 6000}:BACK']
        log, _ = run(steps, [t + 11000])
        saved = [(int(m.group(1)), int(m.group(3)))
                 for m in re.finditer(r'Progress saved: spine=(\d+) offset=(\d+) page=(\d+)', log)]
        before = [(int(m.group(1)), int(m.group(3))) for m in re.finditer(
            r'Progress saved: spine=(\d+) offset=(\d+) page=(\d+)', log[:log.index('[BKS] begin')])]
        self.assertEqual(len(re.findall(r'\[BKS\] pick', log)), 2, log[-3000:])
        self.assertEqual(saved[-1], before[-1], f'one Back did not reach the page before the search: {saved}')
        self.assertNotIn('Exiting activity: EpubReader', log[log.index('[BKS] pick'):], 'Back left the book')

    def test_a_word_the_book_lacks(self):
        steps, t = open_search(3000)
        typed, t = type_word(t, 'zzz')
        steps += typed + [f'{t}:TAP:{OK_KEY[0]},{OK_KEY[1]}']
        log, _ = run(steps, [t + 6000])
        self.assertRegex(log, r'\[BKS\] done results=0 chapters=40')

    def test_stop_halfway_keeps_the_hits_so_far(self):
        steps, t = open_search(3000)
        typed, t = type_word(t, 'nghich')
        steps += typed + [f'{t}:TAP:{OK_KEY[0]},{OK_KEY[1]}', f'{t + 150}:TAP:{STOP[0]},{STOP[1]}']
        log, _ = run(steps, [t + 6000], chapters=400, hits=range(1, 400, 2))
        done = re.search(r'\[BKS\] done results=(\d+) chapters=400 next=(\d+) .* stopped=1', log)
        self.assertIsNotNone(done, log[-3000:])
        self.assertLess(int(done.group(2)), 400)


if __name__ == '__main__':
    unittest.main()
