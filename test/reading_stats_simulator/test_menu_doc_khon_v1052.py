"""v1.0.52: a text change in the reader menu shows the page at once and lays the chapter out behind it.

Deep in a long chapter, the Text panel's line spacing is changed three times in quick succession.
1. Every change draws the page from the nearest resume point (PREVIEW_PAGE ok=1); none lays the
   chapter out from its first page in the paint, as every change did before.
2. Only the last request is laid out to the end: one CATCH_UP landing, after the presses stop.
3. The panel closed before the chapter is laid out leaves a readable page, and a turn from it lands on
   the chapter laid out under the new spacing.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

from cai_dat_truoc_tenor import truoc_tenor
from test_menu_bang_v1052 import CHAPTER, CONTAINER, NCX, OPF, PROGRAM, FONT, keep

STEP = 1500
QUICK = 250  # under the catch-up's 600 ms pause


def body():
    words = 'plain words that fill the page with a long running chapter of text for layout'.split()
    return ''.join(f'<p>Paragraph {i:03d} ' + ' '.join(words[(i + k) % len(words)] for k in range(30)) + '.</p>'
                   for i in range(400))


def run_sim(presses, shots=(), quit_after=4000):
    tmp = Path(tempfile.mkdtemp(prefix='menu-khon-'))
    store = tmp / '.crosspoint'
    store.mkdir()
    target = tmp / '.fonts/BeVietnamPro/BeVietnamPro_18.cpfont'
    target.parent.mkdir(parents=True)
    shutil.copy2(FONT, target)
    with zipfile.ZipFile(tmp / 'book.epub', 'w') as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', CONTAINER)
        z.writestr('OEBPS/content.opf', OPF)
        z.writestr('OEBPS/toc.ncx', NCX)
        z.writestr('OEBPS/c1.xhtml', CHAPTER.format(body=body()))
    settings = truoc_tenor({'language': 'EN', 'readerMenuStyle': 1, 'sdFontFamilyName': 'BeVietnamPro',
                            'fontSize': 18, 'textAntiAliasing': 1, 'sleepTimeout': 120})
    (store / 'settings.json').write_text(json.dumps(settings) + '\n')
    (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                  'showBootScreen': False, 'readerActivityLoadCount': 0}) + '\n')
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Khon'}]}) + '\n')
    script = ';'.join(f'{ms}:{key}' for ms, key in presses) + f';{presses[-1][0] + quit_after}:QUIT'
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp), CROSSPOINT_SIM_INPUT_SCRIPT=script)
    if shots:
        env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{tmp}/{label}.bmp' for ms, label in shots)
    run = subprocess.run([str(PROGRAM)], cwd=Path(__file__).resolve().parents[2], env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, timeout=120)
    assert run.returncode == 0, run.stdout[-3000:]
    images = {label: Image.open(tmp / f'{label}.bmp').convert('L') for _, label in shots}
    settings = json.loads((store / 'settings.json').read_text())
    shutil.rmtree(tmp, True)
    return dict(log=run.stdout, shots=images, settings=settings)


def plan():
    """Open the book, turn 12 pages, open the Text panel. Returns (presses, time of the last press)."""
    presses = [(1500, 'CONFIRM')] + [(4000 + i * 700, 'RIGHT') for i in range(12)]
    t = 4000 + 12 * 700 + 1500
    presses += [(t, 'CONFIRM'), (t + STEP, 'RIGHT'), (t + 2 * STEP, 'CONFIRM')]  # toolbar, Text, its panel
    return presses, t + 2 * STEP


def three_quick_changes(t):
    """Line spacing changed 3 times, an immediate preview per choice. v1.0.53: the front buttons move the
    rows, and a choice is the row's list (Select), one step down, Select: the keeps QUICK ms apart."""
    presses = [(t + STEP, 'RIGHT'), (t + 2 * STEP, 'RIGHT')]
    t += 3 * STEP
    third = QUICK // 3
    presses += [(t + k * QUICK + d * third, key) for k in range(3)
                for d, key in enumerate(('CONFIRM', 'RIGHT', 'CONFIRM'))]
    return presses, t + 2 * QUICK + 2 * third


class QuickChangesTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        presses, t = plan()
        changes, t = three_quick_changes(t)
        cls.res = run_sim(presses + changes, quit_after=3000)
        log = cls.res['log']
        cls.tail = log[log.find('PREVIEW_PAGE'):] if 'PREVIEW_PAGE' in log else log

    def test_every_change_draws_a_preview(self):
        ok = re.findall(r'PREVIEW_PAGE ok=1 from=(\d+) to=(\d+) ms=(\d+)', self.res['log'])
        print('previews (from, to, sim ms):', ok)
        self.assertEqual(len(ok), 3, self.res['log'][-4000:])
        self.assertTrue(all(int(f) > 0 for f, _, _ in ok), 'laid out from a resume point, not the chapter top')
        self.assertEqual(len({to for _, to, _ in ok}), 1, 'the same place for every change')

    def test_no_change_lays_the_chapter_out_in_the_paint(self):
        self.assertNotIn('Cache not found, building', self.tail)
        self.assertNotIn('resuming build', self.tail)

    def test_only_the_last_request_lands(self):
        self.assertEqual(self.res['log'].count('CATCH_UP landed'), 1, self.res['log'][-3000:])


class CloseBeforeLandingTest(unittest.TestCase):
    """Back twice right after the last change (the panel, the toolbar), then a turn within the pause."""

    @classmethod
    def setUpClass(cls):
        presses, t = plan()
        changes, t = three_quick_changes(t)
        close = [(t + QUICK, 'BACK'), (t + 2 * QUICK, 'BACK')]
        turn = [(t + 3 * QUICK, 'RIGHT')]
        shots = [(t + 2 * QUICK + 200, 'closed'), (t + 3 * QUICK + 1500, 'turned')]
        cls.res = run_sim(presses + changes + close + turn, shots, quit_after=3000)
        for name, img in cls.res['shots'].items():
            keep(f'khon-{name}', img)

    def test_closed_page_is_readable(self):
        img = self.res['shots']['closed']
        dark = sum(1 for p in img.crop((0, 100, img.width, img.height - 100)).getdata() if p < 128)
        self.assertGreater(dark, 2000, 'text on the page after the panel closed')

    def test_turn_lands_on_the_laid_out_chapter(self):
        log = self.res['log']
        self.assertNotEqual(self.res['shots']['closed'].tobytes(), self.res['shots']['turned'].tobytes())
        self.assertEqual(self.res['settings'].get('lineSpacing') is not None, True)
        self.assertNotIn('Failed during incremental section build', log)
        self.assertNotIn('STR_PAGE_LOAD_ERROR', log)


if __name__ == '__main__':
    unittest.main()
