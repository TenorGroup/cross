"""X4 Pro: a side page key closes the reader's tap tip and the page turns (v1.0.76 debt N5).

The tap-zone tip opens with the book while `readerTapTip` is 1. It used to take every input but a tap, a swipe, Back
or Confirm, so a reader on the side keys alone never got past the first page. Now the side key closes the tip for this
open and turns the page. The page identity comes from the firmware's own `Progress saved:` line.

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
BOOK = '/sach/goi-y.epub'
TEXT = 'Mặt trời lên trên đồi cao, gió thổi qua rặng tre xanh và tiếng chim hót vang khắp xóm làng nhỏ bé. ' * 4


def write_book(path):
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Goi y</dc:title><dc:identifier id="id">goi-y-n5</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata><manifest>'
                      '<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/></manifest>'
                      '<spine><itemref idref="c1"/></spine></package>')
        epub.writestr('c1.xhtml',
                      '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>c1</title></head>'
                      '<body>' + ''.join(f'<p>{TEXT}</p>' for _ in range(60)) + '</body></html>')


def run(script, tip):
    with tempfile.TemporaryDirectory(prefix='x4-goi-y-') as tmp:
        sd = Path(tmp) / 'sd'
        (sd / '.crosspoint').mkdir(parents=True)
        (sd / 'sach').mkdir()
        write_book(sd / 'sach/goi-y.epub')
        (sd / '.crosspoint/settings.json').write_text(json.dumps({
            'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1,
            'uiShellSleepMemo': 0, 'language': 'VI', 'uiTextSize': 0, 'wakeIntoBook': 1, 'fontSize': 18,
            'fontFamily': 0, 'readerMenuStyle': 1, 'uiShell': 0, 'sleepTimeout': 10, 'readerTapTip': tip,
            'longPressButtonBehavior': 0, 'uiShellClockOnce': 1, 'clockShowInHeader': 0}))
        (sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': BOOK,
                                                               'lastSleepFromReader': True}))
        (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Goi y'}]}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_INPUT_SCRIPT=script)
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        return result.stdout + result.stderr


def pages(log):
    return [int(m.group(1)) for m in re.finditer(r'Progress saved: spine=\d+ offset=\d+ page=(\d+)', log)]


class TapTipSideKeyTest(unittest.TestCase):
    def test_side_key_closes_the_tip_and_turns_the_page(self):
        log = run('4000:DOWN;6000:DOWN;8000:QUIT', tip=1)
        self.assertIn(1, pages(log), f'the first side key must turn the page past the tip: {pages(log)}')
        self.assertIn(2, pages(log), f'and the next one turns again: {pages(log)}')

    def test_without_the_tip_the_side_key_turns_as_before(self):
        log = run('4000:DOWN;6000:DOWN;8000:QUIT', tip=0)
        self.assertEqual(pages(log)[-1], 2, pages(log))


if __name__ == '__main__':
    unittest.main()
