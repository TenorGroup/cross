"""v1.0.61: the X3 reader menu in the tenor/cross shell, both styles (List and Toolbar), with the buttons.

Founder: the menu opens on the Favorites tab, even with nothing pinned; the side buttons step the tabs.
 1. List style, nothing pinned: the menu opens on the Favorites tab (the ring is on the first of the 5 tabs).
 2. Toolbar style, nothing pinned or one row pinned: the bar opens with the ring on the first tab.
 3. In both, side Down moves the ring to the next tab and side Up brings it back.
 4. tenor/ugly keeps what it had: a list menu with nothing pinned opens on the Reading tab.
 5. The front buttons Left and Right on the bar turn the chapter back and forward.
One simulator run per case: Home opens the book (1800), Select opens the menu (4500), then a key every 2000 ms with
a screenshot 1500 ms after it.
TEST_PROGRAM picks the simulator (simulator_x3_uc8279 is the UC8279 panel).
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops

from cai_dat_truoc_tenor import truoc_tenor
from test_menu_thanh_x3_v1053 import CHAPTER, CONTAINER, NCX, OPF

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FONT = Path(__file__).resolve().parent / 'fixtures/BeVietnamPro_18.cpfont'
TAB_CENTRES = [66, 165, 264, 362, 461]  # the 5 tab slots of the bar (y 696 to 762)
LIST, TOOLBAR = 0, 1


def run(style, favorites, keys=(), shell=0):
    """The menu opened, then each key: the screens (L mode, 528 x 792), the menu's own shot first."""
    tmp = Path(tempfile.mkdtemp(prefix='menu-doc-x3-'))
    try:
        store = tmp / '.crosspoint'
        store.mkdir()
        target = tmp / '.fonts/BeVietnamPro/BeVietnamPro_18.cpfont'
        target.parent.mkdir(parents=True)
        shutil.copy2(FONT, target)
        with zipfile.ZipFile(tmp / 'book.epub', 'w') as z:
            z.writestr('mimetype', 'application/epub+zip')
            z.writestr('META-INF/container.xml', CONTAINER)
            z.writestr('OEBPS/content.opf', OPF)
            z.writestr('OEBPS/toc.ncx', NCX)
            for n in range(1, 4):
                body = f'<h1>Chapter {n}</h1>' + ''.join(f'<p>Chapter {n} paragraph {i:02d} with plain words.</p>'
                                                         for i in range(40))
                z.writestr(f'OEBPS/c{n}.xhtml', CHAPTER.format(n=n, body=body))
        settings = truoc_tenor({'language': 'EN', 'readerMenuStyle': style, 'sdFontFamilyName': 'BeVietnamPro',
                                'fontSize': 18, 'sleepTimeout': 120, 'uiShell': shell, 'readerFavorites': favorites,
                                'clockShowInHeader': 0, 'uiShellClockOnce': 1, 'uiTheme': 4, 'uiTextSize': 1,
                                'tipsEnabled': 1, 'globalStatusBarMode': 0, 'tenorButtonSymbols': 1,
                                'tenorSideArrows': 1, 'readerTapTip': 0})
        (store / 'settings.json').write_text(json.dumps(settings))
        (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                      'showBootScreen': False, 'readerActivityLoadCount': 0}))
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Keys'}]}))
        actions = [(1800, 'CONFIRM'), (4500, 'CONFIRM')] + [(6400 + 2000 * i, k) for i, k in enumerate(keys)]
        shots = [(5800, 0)] + [(6400 + 2000 * i + 1500, i + 1) for i in range(len(keys))]
        script = ';'.join(f'{ms}:{key}' for ms, key in actions) + f';{shots[-1][0] + 700}:QUIT'
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_NOW='1791700000',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{tmp}/{i}.bmp' for ms, i in shots))
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=300)
        assert result.returncode == 0, (result.stdout + result.stderr)[-3000:]
        return [Image.open(tmp / f'{i}.bmp').convert('L') for _, i in shots]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def chosen_tab(image):
    """The tab the selection ring is on: the ring draws a thick stroke along y 702 and y 755 over its slot."""
    a = np.asarray(image) < 128
    ink = [int(a[702:704, c - 30:c + 30].sum() + a[754:756, c - 30:c + 30].sum()) for c in TAB_CENTRES]
    best = max(range(len(ink)), key=ink.__getitem__)
    assert ink[best] >= 40 and sorted(ink)[-2] < ink[best] // 2, f'no single ring on the bar: {ink}'
    return best


def page(image):
    """The page above the sheet (the chapter heading and text), the bar and the foot left out."""
    return np.asarray(image)[:520]


class OpensOnFavoritesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.list_empty = run(LIST, [], ['DOWN', 'UP'])
        cls.list_pinned = run(LIST, [14], ['DOWN', 'UP'])
        cls.bar_empty = run(TOOLBAR, [], ['DOWN', 'UP'])
        cls.bar_pinned = run(TOOLBAR, [14], ['DOWN', 'UP'])

    def test_1_the_list_opens_on_favorites_with_nothing_pinned(self):
        self.assertEqual(chosen_tab(self.list_empty[0]), 0)

    def test_2_the_list_opens_on_favorites_with_a_pin(self):
        self.assertEqual(chosen_tab(self.list_pinned[0]), 0)

    def test_2_the_bar_opens_on_favorites(self):
        for shots in (self.bar_empty, self.bar_pinned):
            self.assertEqual(chosen_tab(shots[0]), 0)

    def test_3_side_down_and_up_step_the_tabs(self):
        for shots in (self.list_empty, self.list_pinned, self.bar_empty, self.bar_pinned):
            self.assertEqual([chosen_tab(s) for s in shots], [0, 1, 0])


class FrontKeysTest(unittest.TestCase):
    """The front Left and Right on the bar turn the chapter and leave the ring where it is (A)."""

    @classmethod
    def setUpClass(cls):
        cls.shots = run(TOOLBAR, [14], ['RIGHT', 'LEFT'])

    def test_5_front_right_turns_the_chapter_and_front_left_turns_it_back(self):
        opened, forward, back = self.shots
        self.assertTrue((page(forward) != page(opened)).any(), 'front Right: the next chapter')
        self.assertTrue((page(back) == page(opened)).all(), 'front Left: the first chapter again')
        self.assertEqual([chosen_tab(s) for s in self.shots], [0, 0, 0])


class UglyKeepsItsOpeningTabTest(unittest.TestCase):
    def test_4_a_list_menu_with_nothing_pinned_opens_on_reading_in_ugly(self):
        """The hand-drawn bar: the ring starts on the Reading label (x 230 to 295), not on Favorites (x under 140)."""
        opened, after_down = run(LIST, [], ['DOWN'], shell=1)
        moved = np.asarray(ImageChops.difference(opened, after_down))[706:759] > 0
        columns = np.nonzero(moved.any(axis=0))[0]
        self.assertGreater(len(columns), 0, 'the ring moves on side Down')
        self.assertGreater(int(columns.min()), 150, f'the old ring stood at x {columns.min()}')


if __name__ == '__main__':
    unittest.main()
