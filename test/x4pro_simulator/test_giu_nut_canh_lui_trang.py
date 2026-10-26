"""X4 Pro: the side keys as on the X3 (founder 26/10/2026).

"Nut canh lat trang" = "Sau/Sau" (sideButtonLayout 3) turns forward on either side key, and "Giu nut lat trang khi
doc" = "Nut canh: lui trang" (longPressButtonBehavior 5) turns on the press and, held 700 ms, ends on the page before
the one the press started on, in one paint. A tap on the page still turns as before. Both rows are in Settings >
Reader on the X4 Pro (test/settings_catalog/category pins the rows of every board). The page identity comes from the
firmware's own `Progress saved:` line.

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

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
BOOK = '/sach/giu.epub'
TEXT = 'Mặt trời lên trên đồi cao, gió thổi qua rặng tre xanh và tiếng chim hót vang khắp xóm làng nhỏ bé. ' * 4
SIDE_HOLD_PREV = 5
NEXT_NEXT = 3


def write_book(path, paragraphs):
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(len(paragraphs)))
    items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
                    for i in range(len(paragraphs)))
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Giu</dc:title><dc:identifier id="id">giu-x4pro</dc:identifier>'
                      f'<dc:language>vi</dc:language></metadata><manifest>{items}</manifest>'
                      f'<spine>{spine}</spine></package>')
        for i, count in enumerate(paragraphs):
            epub.writestr(f'c{i}.xhtml',
                          '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>c</title>'
                          '</head><body>' + ''.join(f'<p>{TEXT}</p>' for _ in range(count)) + '</body></html>')


def run(script, paragraphs=(60,), **settings):
    with tempfile.TemporaryDirectory(prefix='x4-giu-') as tmp:
        sd = Path(tmp) / 'sd'
        (sd / '.crosspoint').mkdir(parents=True)
        (sd / 'sach').mkdir()
        write_book(sd / 'sach/giu.epub', paragraphs)
        (sd / '.crosspoint/settings.json').write_text(json.dumps({
            'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1,
            'uiShellSleepMemo': 0, 'language': 'VI', 'uiTextSize': 0, 'wakeIntoBook': 1, 'fontSize': 18,
            'fontFamily': 0, 'uiShell': 0, 'sleepTimeout': 10, 'readerTapTip': 0, 'uiShellClockOnce': 1,
            'clockShowInHeader': 0, **settings}))
        (sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': BOOK,
                                                               'lastSleepFromReader': True}))
        (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Giu'}]}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_INPUT_SCRIPT=script)
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        return result.stdout + result.stderr


def places(log):
    return [(int(s), int(p)) for s, p in re.findall(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)', log)]


def paints_after(log, ms):
    return [int(t) for t in re.findall(r'^\[(\d+)\] \[\w+\] \[ERS\] Rendered page', log, re.M) if int(t) >= ms]


class SideKeysTest(unittest.TestCase):
    def test_hold_ends_on_the_page_before_the_press_in_one_paint(self):
        # Page 3; the press turns to 4, the hold ends on 2 with one paint after the 700 ms mark.
        log = run('4000:DOWN;5500:DOWN;7000:DOWN;8500:DOWN:1000;11000:QUIT', longPressButtonBehavior=SIDE_HOLD_PREV)
        self.assertEqual([p for _, p in places(log)], [0, 1, 2, 3, 4, 2], places(log))
        self.assertEqual(len(paints_after(log, 9200)), 1, paints_after(log, 8500))
        self.assertNotIn('Entering activity: Home', log.split('Entering activity: EpubReader', 1)[1])

    def test_next_next_turns_forward_on_either_key_and_its_hold_goes_back(self):
        log = run('4000:UP;5500:DOWN;7000:UP:1000;10000:QUIT', longPressButtonBehavior=SIDE_HOLD_PREV,
                  sideButtonLayout=NEXT_NEXT)
        self.assertEqual([p for _, p in places(log)], [0, 1, 2, 3, 1], places(log))

    def test_hold_on_the_back_key_goes_back_no_further(self):
        log = run('4000:DOWN;5500:DOWN;7000:UP:1000;10000:QUIT', longPressButtonBehavior=SIDE_HOLD_PREV)
        self.assertEqual(places(log)[-1], (0, 1), places(log))

    def test_hold_at_a_chapter_start_ends_on_the_last_page_of_the_chapter_before(self):
        # Chapter 0 has 3 pages: 3 presses reach (1, 0); the press turns to (1, 1), the hold ends on (0, 2).
        log = run('4000:DOWN;5500:DOWN;7000:DOWN;8500:DOWN:1000;11500:QUIT', paragraphs=(2, 60),
                  longPressButtonBehavior=SIDE_HOLD_PREV)
        self.assertEqual(places(log), [(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (0, 2)], places(log))

    def test_hold_on_the_first_page_stays(self):
        log = run('4000:DOWN:1000;7000:QUIT', longPressButtonBehavior=SIDE_HOLD_PREV)
        self.assertEqual(places(log)[-1], (0, 0), places(log))

    def test_a_tap_still_turns_forward(self):
        log = run('4000:TAP:240,400;6000:QUIT', longPressButtonBehavior=SIDE_HOLD_PREV, sideButtonLayout=NEXT_NEXT)
        self.assertEqual(places(log)[-1], (0, 1), places(log))


if __name__ == '__main__':
    unittest.main()
