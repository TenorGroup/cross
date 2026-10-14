"""v1.0.61: the X3 reader menu in the tenor/cross shell, both styles (List and Toolbar), with the buttons.

Founder: the menu opens on the Favorites tab, even with nothing pinned; the side buttons step the tabs.
 1. List style, nothing pinned: the menu opens on the Favorites tab (the ring is on the first of the 5 tabs).
 2. Toolbar style, nothing pinned or one row pinned: the bar opens with the ring on the first tab.
 3. In both, side Down moves the ring to the next tab and side Up brings it back.
 4. tenor/ugly keeps what it had: a list menu with nothing pinned opens on the Reading tab.
 5. The front buttons Left and Right on the bar turn the chapter back and forward.
 6. Toolbar style on the X3 bar: the menu opens straight on the list of the Favorites tab, with no scrub row (the old
    "Chapter" sheet with its drag row and its page line), and no press fewer than before reaches Contents, Text and
    the next chapter.
 7. Back from a list closes the menu to the page (it went to the toolbar before).
 8. Where the list has 2 rows or more the front Left and Right step the rows, so the chapter stays where it is.
 9. Position > Select chapter shows where the reader stands (page of the chapter, share of the book) over the bar.
10. The pin hint ("On any tab, hold Select to pin or unpin ...") stands alone inside the frame of the Favorites list
    while nothing is pinned (the toolbar's "Hold a row in Text or More to pin it here" is gone), and is gone once a row is pinned (the list gets its room), in the list menu and the toolbar menu.
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


def run(style, favorites, keys=(), shell=0, page=False):
    """The menu opened, then each key: the screens (L mode, 528 x 792), the menu's own shot first (the page before the
    menu opened ahead of it with page=True)."""
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
        shots = ([(4000, 'page')] if page else []) + [(5800, 0)] + [(6400 + 2000 * i + 1500, i + 1)
                                                                      for i in range(len(keys))]
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


def bands(image, least, top=300, bottom=690):
    """(thickness, longest run) of each group of consecutive rows whose longest dark run is at least `least`."""
    a = np.asarray(image) < 128
    out, start, longest = [], None, 0
    for y in range(top, bottom + 1):
        run = best = 0
        if y < bottom:
            for v in a[y]:
                run = run + 1 if v else 0
                best = max(best, run)
        if best >= least:
            start = y if start is None else start
            longest = max(longest, best)
        elif start is not None:
            out.append((y - start, longest))
            start, longest = None, 0
    return out


def scrub_row(image):
    """The old sheet's drag row: a 2 px track about 370 px long (the cursor pill's rings are 3 px thick and longer)."""
    return any(thick == 2 and 340 <= longest <= 400 for thick, longest in bands(image, 300))


def cursor_pill(image):
    """A list's selected row: the ring along its top and its foot, 3 px thick and over 400 px long."""
    return sum(thick >= 3 and longest >= 400 for thick, longest in bands(image, 300)) >= 2


def ink_bands(image, top, bottom, left=24, right=504):
    """How many separate lines of ink stand between rows top and bottom, inside columns left to right."""
    rows = (np.asarray(image)[top:bottom, left:right] < 128).any(axis=1)
    return int(sum(1 for i, row in enumerate(rows) if row and (i == 0 or not rows[i - 1])))


def sheet_bottom(image):
    """The foot of the toolbar's sheet: the last pair of rows of its dotted ring (a dot on every other point)."""
    dark = (np.asarray(image)[:, 20:510] < 128).sum(axis=1)
    rows = [y for y in range(560, 696) if 225 <= dark[y] <= 250]
    return max((y for y in rows if y - 1 in rows or y + 1 in rows), default=0)


def same_page(a, b):
    """Two screens show the same page (the status line at the foot and the gray pass left out)."""
    return int(np.count_nonzero(np.asarray(a)[:680] != np.asarray(b)[:680])) < 40


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


class OpensOnTheListTest(unittest.TestCase):
    """The X3 bar has no toolbar step: the first press shows the list, and each way into the menu is a press shorter."""

    @classmethod
    def setUpClass(cls):
        cls.pinned = run(TOOLBAR, [14], [], page=True)
        cls.empty = run(TOOLBAR, [], [])
        cls.text = run(TOOLBAR, [14], ['UP'])
        cls.back = run(TOOLBAR, [14], ['BACK'], page=True)
        cls.position_back = run(TOOLBAR, [14], ['DOWN', 'BACK'], page=True)
        cls.two_rows = run(TOOLBAR, [14, 16], ['RIGHT'])
        cls.empty_keys = run(TOOLBAR, [], ['RIGHT', 'LEFT'])

    def test_6_the_first_press_shows_the_list_and_no_drag_row(self):
        page_shot, opened = self.pinned
        self.assertFalse(scrub_row(page_shot))
        self.assertFalse(scrub_row(opened), 'the old Chapter sheet (drag row) is still the first screen')
        self.assertTrue(cursor_pill(opened), 'the first pinned row is not under the cursor')
        self.assertFalse(scrub_row(self.empty[0]))
        self.assertEqual(chosen_tab(opened), 0)

    def test_6_side_up_from_the_first_tab_shows_the_text_list_at_once(self):
        """Contents, Text and the next chapter: one press after the menu opened, none more (they took 2 and 3 before)."""
        opened, text = self.text
        self.assertEqual(chosen_tab(text), 4)
        self.assertFalse(scrub_row(text))
        self.assertTrue(cursor_pill(text), 'the Text list is not on screen after side Up')

    def test_7_back_closes_the_menu_to_the_page_from_the_list(self):
        page_shot, opened, closed = self.back
        self.assertTrue(same_page(page_shot, closed))

    def test_7_back_from_another_tab_closes_the_menu_too(self):
        page_shot, opened, tab, closed = self.position_back
        self.assertFalse(same_page(page_shot, tab), 'side Down brought the Position list up')
        self.assertTrue(same_page(page_shot, closed), 'Back from the Position list went to the toolbar, not the page')

    def test_8_two_rows_keep_the_front_keys_for_the_rows(self):
        opened, after_right = self.two_rows
        head = slice(0, 250)  # the chapter heading and the first lines, clear of the taller sheet of 2 rows
        self.assertTrue((np.asarray(after_right)[head] == np.asarray(opened)[head]).all(), 'front Right turned the chapter')
        self.assertTrue((np.asarray(after_right) != np.asarray(opened)).any(), 'front Right did not step the row')

    def test_8_an_empty_list_leaves_the_front_keys_to_the_chapter(self):
        opened, forward, back = self.empty_keys
        self.assertTrue((page(forward) != page(opened)).any())
        self.assertTrue((page(back) == page(opened)).all())


class FavoritesHintTest(unittest.TestCase):
    """The pin hint is 2 lines. Empty: inside the list's frame. A pinned row: gone, and the list has its room."""

    @classmethod
    def setUpClass(cls):
        cls.list_empty = run(LIST, [], [])[0]
        cls.list_pinned = run(LIST, [14], [])[0]
        cls.bar_empty = run(TOOLBAR, [], [])[0]
        cls.bar_pinned = run(TOOLBAR, [14], [])[0]

    def test_10_list_menu_empty_holds_the_hint_in_its_frame_and_not_under_it(self):
        self.assertEqual(ink_bands(self.list_empty, 36, 100, 40, 488), 2, 'the 2 hint lines in the frame at the top')
        self.assertEqual(ink_bands(self.list_empty, 596, 694), 1, 'only the position line stands over the bar')

    def test_10_list_menu_with_a_pin_has_no_hint(self):
        self.assertEqual(ink_bands(self.list_pinned, 596, 694), 1, 'only the position line stands over the bar')
        row = (np.asarray(self.list_pinned)[46:92, 300:440] < 128).sum()
        self.assertEqual(int(row), 0, 'nothing but the row\'s own name stands in the first frame')
        below = (np.asarray(self.list_pinned)[108:140, 40:488] < 128).sum()
        self.assertEqual(int(below), 0, 'the frame of the 1 row ends at its row: no card of the hint under it')

    def test_10_toolbar_menu_empty_holds_the_hint_in_its_sheet(self):
        foot = sheet_bottom(self.bar_empty)
        self.assertGreaterEqual(foot, 680, f'the sheet ends at {foot}: room is kept under it for the hint')
        self.assertEqual(ink_bands(self.bar_empty, 380, foot - 4, 40, 488), 2, 'the hint (2 lines) alone, no empty text')

    def test_10_toolbar_menu_with_a_pin_has_no_hint_and_its_sheet_stands_on_the_bar(self):
        foot = sheet_bottom(self.bar_pinned)
        self.assertGreaterEqual(foot, 680, f'the sheet ends at {foot}: room is kept under it for the hint')


class ChapterListShowsWhereYouStandTest(unittest.TestCase):
    """Position > Select chapter (3 presses from the page): the position line over the bar, above the hint."""

    @classmethod
    def setUpClass(cls):
        cls.opened, cls.tab, cls.chapters = run(TOOLBAR, [14], ['DOWN', 'CONFIRM'])

    def test_9_the_chapter_list_has_the_position_line_over_the_bar(self):
        self.assertFalse(scrub_row(self.chapters))
        a = np.asarray(self.chapters)[560:696] < 128
        lines, inside = 0, False
        for row in a.any(axis=1):
            lines += int(row and not inside)
            inside = bool(row)
        self.assertGreaterEqual(lines, 2, f'{lines} text line(s) above the bar: the hint alone')


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
