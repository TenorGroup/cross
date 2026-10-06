"""v1.0.53: the X3 toolbar reader menu (Reader Menu Style = Toolbar) on buttons.

Founder 06/10/2026:
- Level 1, the bar: the front buttons move between the tabs, the side buttons turn the chapter, Select
  opens the tab, Back closes the menu.
- Level 2, a tab's sheet: the side buttons go to the tab before or after, the front buttons move the
  cursor, Select opens the row, Back returns to the bar. A second level (the font list) keeps the side
  buttons still.

Every run is one simulator process: a key every STEP ms after the bar is open, a screenshot just before
the next key. Shots of one run are compared with each other; the footer (clock) is masked.
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image, ImageChops

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FONT = Path(__file__).resolve().parent / 'fixtures/BeVietnamPro_18.cpfont'
ARTIFACTS = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')

CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
CHAPTERS = 3
OPF = ("""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>Toolbar keys</dc:title><dc:identifier id="id">toolbar-keys-v1053</dc:identifier>
<dc:language>en</dc:language></metadata><manifest>"""
       + ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1, CHAPTERS + 1))
       + '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">'
       + ''.join(f'<itemref idref="c{i}"/>' for i in range(1, CHAPTERS + 1)) + '</spine></package>')
NCX = ("""<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="toolbar-keys-v1053"/></head><docTitle><text>Toolbar keys</text></docTitle><navMap>"""
       + ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chapter {i}</text></navLabel>'
                 f'<content src="c{i}.xhtml"/></navPoint>' for i in range(1, CHAPTERS + 1)) + '</navMap></ncx>')
CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter {n}</title></head><body>{body}</body></html>"""

# Home opens the recent book (1500), Select opens the bar (3500). Keys after that come every STEP ms.
OPEN_BAR = [(1500, 'CONFIRM'), (3500, 'CONFIRM')]
FIRST = 5000
STEP = 1500
FOOTER = 40  # clock and button hints at the foot: masked


def run_keys(keys, settings=None, quit_after=STEP):
    """keys: the presses after the bar is open, one every STEP ms ('KEY' or 'KEY:holdms').
    Returns dict(shots=[image before key 0, after key 0, ...], settings=dict)."""
    tmp = Path(tempfile.mkdtemp(prefix='menu-thanh-x3-'))
    try:
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
            for n in range(1, CHAPTERS + 1):
                body = ''.join(f'<p>Chapter {n} paragraph {i:02d} with plain words for a full page.</p>'
                               for i in range(40))
                z.writestr(f'OEBPS/c{n}.xhtml', CHAPTER.format(n=n, body=body))
        base = {'language': 'EN', 'readerMenuStyle': 1, 'sdFontFamilyName': 'BeVietnamPro', 'fontSize': 18,
                'sleepTimeout': 120}
        base.update(settings or {})
        (store / 'settings.json').write_text(json.dumps(truoc_tenor(base)) + '\n')
        (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                      'showBootScreen': False, 'readerActivityLoadCount': 0}) + '\n')
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Keys'}]}) + '\n')
        presses = OPEN_BAR + [(FIRST + i * STEP, key) for i, key in enumerate(keys)]
        end = FIRST + len(keys) * STEP
        script = ';'.join(f'{ms}:{key}' for ms, key in presses) + f';{end + quit_after}:QUIT'
        shots = [(FIRST - 300 + i * STEP, i) for i in range(len(keys) + 1)]
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{tmp}/{i}.bmp' for ms, i in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, timeout=120)
        assert run.returncode == 0, run.stdout[-3000:]
        images = [Image.open(tmp / f'{i}.bmp').convert('L') for _, i in shots]
        return dict(shots=images, settings=json.loads((store / 'settings.json').read_text()), log=run.stdout)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def keep(label, img):
    if ARTIFACTS:
        Path(ARTIFACTS).mkdir(parents=True, exist_ok=True)
        img.save(Path(ARTIFACTS) / f'{label}.png')


def body(img):
    """The screen without the footer band (clock, hints)."""
    return img.crop((0, 0, img.width, img.height - FOOTER))


def same(a, b):
    return ImageChops.difference(body(a), body(b)).getbbox() is None


def cursor_top(img, top=0):
    """y of the cursor pill's top ring in a sheet: the first row under `top` black over most of the width
    that is not the sheet's own top rule (the rule spans the whole width)."""
    px = img.load()
    for y in range(top, img.height - 120):
        black = sum(1 for x in range(20, img.width - 20) if px[x, y] == 0)
        if black > 0.8 * (img.width - 40) and px[2, y] != 0 and px[img.width - 3, y] != 0:
            return y
    return None


def sheet_top(img):
    px = img.load()
    for y in range(100, img.height - 100):
        if sum(1 for x in range(img.width) if px[x, y] == 0) >= 0.8 * img.width:
            return y
    raise AssertionError('no sheet on screen')


class LevelTwoKeysTest(unittest.TestCase):
    """In a tab's sheet: side buttons change the tab, front buttons move the cursor."""

    @classmethod
    def setUpClass(cls):
        # bar on Contents: RIGHT -> Text, CONFIRM opens the Text sheet, then the keys under test.
        cls.run_a = run_keys(['RIGHT', 'CONFIRM', 'DOWN', 'UP', 'UP', 'BACK', 'CONFIRM', 'BACK',
                              'RIGHT', 'RIGHT', 'CONFIRM'])
        cls.run_b = run_keys(['RIGHT', 'CONFIRM', 'RIGHT', 'LEFT', 'CONFIRM', 'DOWN', 'UP'])
        for name, res in (('a', cls.run_a), ('b', cls.run_b)):
            for i, img in enumerate(res['shots']):
                keep(f'keys-{name}-{i}', img)

    def test_side_down_goes_to_the_next_tab(self):
        s = self.run_a['shots']
        # shot 3: Text sheet + DOWN; shot 11: the More sheet opened from the bar (RIGHT, RIGHT, CONFIRM).
        self.assertTrue(same(s[3], s[11]), 'side Down on the Text sheet must open the More sheet')

    def test_side_up_goes_to_the_tab_before(self):
        s = self.run_a['shots']
        # shot 4: More + UP = Text sheet as first opened (shot 2); shot 5: Text + UP = Contents (shot 7).
        self.assertTrue(same(s[4], s[2]), 'side Up on More must come back to the Text sheet')
        self.assertTrue(same(s[5], s[7]), 'side Up on the Text sheet must open the Contents sheet')

    def test_back_from_a_sheet_is_the_bar(self):
        s = self.run_a['shots']
        # shot 6: Contents + BACK = the bar with Contents in focus, as the bar first opened (shot 0).
        self.assertTrue(same(s[6], s[0]), 'Back from a sheet must show the bar again')

    def test_front_buttons_move_the_cursor_not_the_tab(self):
        s = self.run_b['shots']
        top = sheet_top(s[2]) + 60
        y0, y1, y2 = cursor_top(s[2], top), cursor_top(s[3], top), cursor_top(s[4], top)
        self.assertIsNotNone(y0)
        self.assertLess(y0, y1, 'front Right must move the cursor one row down')
        self.assertEqual(y2, y0, 'front Left moves it back')
        self.assertTrue(same(s[3].crop((0, 0, 528, top)), s[2].crop((0, 0, 528, top))), 'same tab')

    def test_side_buttons_wait_in_the_font_list(self):
        s = self.run_b['shots']
        # shot 5: the font list (Select on the Font row); 6 and 7: after side Down and side Up.
        self.assertFalse(same(s[5], s[4]), 'Select on the Font row opens the font list')
        self.assertTrue(same(s[6], s[5]), 'side Down must not move inside the font list')
        self.assertTrue(same(s[7], s[5]), 'side Up must not move inside the font list')


class LevelOneKeysTest(unittest.TestCase):
    """On the bar: front buttons move between the tabs, side buttons turn the chapter."""

    @classmethod
    def setUpClass(cls):
        cls.res = run_keys(['RIGHT', 'LEFT', 'DOWN', 'UP'])
        for i, img in enumerate(cls.res['shots']):
            keep(f'bar-{i}', img)

    def test_front_buttons_move_the_tab_focus(self):
        s = self.res['shots']
        self.assertFalse(same(s[0], s[1]), 'front Right moves the focus')
        self.assertTrue(same(s[0], s[2]), 'front Left moves it back')

    def test_side_buttons_turn_the_chapter(self):
        s = self.res['shots']
        top = sheet_top(s[0])
        page = lambda img: img.crop((0, 0, img.width, top))
        self.assertIsNotNone(ImageChops.difference(page(s[2]), page(s[3])).getbbox(), 'side Down: next chapter')
        self.assertIsNone(ImageChops.difference(page(s[2]), page(s[4])).getbbox(), 'side Up: back again')

    def test_tab_icons_are_tab_size(self):
        # The Text tab's "Aa", out of focus, between the Contents tab's ring and the More tab: the 40 px art
        # inks about 34 x 20 px (the 24 px art inked under 24 x 15).
        img = self.res['shots'][0]
        band = img.crop((150, img.height - 140, 290, img.height - 40))
        box = ImageChops.invert(band).getbbox()
        self.assertIsNotNone(box)
        w, h = box[2] - box[0], box[3] - box[1]
        self.assertTrue(28 <= w <= 48 and 17 <= h <= 40, f'Aa ink {w}x{h}')


HOLD = 'CONFIRM:1100'  # past readermenu::GIU_GHIM_MS (1000), released before the next shot
SYNC = 14  # readermenu::Action::SYNC, the pin a reader who never pinned starts with


class FavoritesTabTest(unittest.TestCase):
    """4 tabs as on the X4 Pro: Contents, Text, More, Favorites. A held Select on a Text or More row pins it."""

    @classmethod
    def setUpClass(cls):
        to_favorites = ['RIGHT', 'CONFIRM', 'RIGHT', HOLD, 'BACK', 'RIGHT', 'RIGHT', 'CONFIRM']
        cls.pin = run_keys(to_favorites + ['UP', 'DOWN', 'DOWN', 'BACK', 'LEFT', 'CONFIRM'])
        cls.unpin = run_keys(to_favorites + ['RIGHT', HOLD])
        for name, res in (('pin', cls.pin), ('unpin', cls.unpin)):
            for i, img in enumerate(res['shots']):
                keep(f'fav-{name}-{i}', img)

    def test_a_held_select_pins_the_text_row(self):
        self.assertEqual(self.pin['settings'].get('readerFavorites'), [SYNC, 'text/fontSize'])
        self.assertEqual(self.pin['settings'].get('fontSize', 18), 18, 'the hold must not also open the row')

    def test_favorites_is_the_fourth_tab(self):
        s = self.pin['shots']
        # shot 8: Favorites from the bar (Right x3 from Contents). 9: side Up = More. 10: side Down = Favorites.
        # 11: side Down = Contents (4 tabs round). 14: Left from Contents on the bar, Select = Favorites.
        self.assertFalse(same(s[8], s[9]), 'side Up from Favorites is More')
        self.assertTrue(same(s[10], s[8]), 'side Down from More is Favorites')
        self.assertFalse(same(s[11], s[8]), 'side Down from Favorites goes round to Contents')
        self.assertTrue(same(s[14], s[8]), 'Left from Contents on the bar is Favorites')

    def test_a_held_select_in_favorites_takes_the_pin_off(self):
        self.assertEqual(self.unpin['settings'].get('readerFavorites'), [SYNC])
        s = self.unpin['shots']
        self.assertFalse(same(s[10], s[9]), 'the unpinned row leaves the list at once')


# Line spacing as the Text tab shows it, tightest first: the stored readerSpacing level at each place.
SPACING_BY_PLACE = [1, 2, 0, 3, 4]
TO_TEXT_ROW = lambda row: ['RIGHT', 'CONFIRM'] + ['RIGHT'] * row  # bar on Contents -> Text sheet, cursor on `row`
CLOSE = ['BACK', 'BACK']  # the sheet, then the bar: settings reach the card once the page is back


def parallel(jobs):
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(4) as pool:
        return list(pool.map(lambda keys: run_keys(keys), jobs))


class ValueListTest(unittest.TestCase):
    """A Text row with several values opens them over the sheet, as the Font row opens the fonts: the front
    buttons move, Select keeps, Back drops. Line spacing runs in the order the values are shown."""

    @classmethod
    def setUpClass(cls):
        steps = range(1, 5)
        jobs = [TO_TEXT_ROW(2) + ['CONFIRM'] + ['RIGHT'] * k + ['CONFIRM'] + CLOSE for k in steps]
        jobs += [TO_TEXT_ROW(2) + ['CONFIRM'] + ['LEFT'] * k + ['CONFIRM'] + CLOSE for k in steps]
        jobs.append(TO_TEXT_ROW(2) + ['CONFIRM', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'BACK'] + CLOSE)
        jobs.append(TO_TEXT_ROW(3) + ['CONFIRM', 'RIGHT', 'CONFIRM'] + CLOSE)
        jobs.append(TO_TEXT_ROW(4) + ['CONFIRM', 'RIGHT', 'CONFIRM'] + CLOSE)
        cls.runs = parallel(jobs)
        for i, img in enumerate(cls.runs[8]['shots']):
            keep(f'pick-{i}', img)

    def test_spacing_runs_down_in_the_order_shown(self):
        got = [r['settings'].get('lineSpacing') for r in self.runs[0:4]]
        self.assertEqual(got, [SPACING_BY_PLACE[(2 + k) % 5] for k in range(1, 5)])

    def test_spacing_runs_up_in_the_order_shown(self):
        got = [r['settings'].get('lineSpacing') for r in self.runs[4:8]]
        self.assertEqual(got, [SPACING_BY_PLACE[(2 - k) % 5] for k in range(1, 5)])

    def test_the_cursor_walks_the_values_one_row_a_press(self):
        s = self.runs[8]['shots']
        # shot 5: the list opened on the value in use (place 2); 6..9 after each Right (3, 4, then round to 0, 1).
        top = sheet_top(s[5]) + 60
        ys = [cursor_top(s[i], top) for i in range(5, 10)]
        self.assertTrue(all(y is not None for y in ys), ys)
        self.assertTrue(ys[0] < ys[1] < ys[2], ys)
        self.assertTrue(ys[3] < ys[4] < ys[0], ys)

    def test_back_drops_the_value(self):
        self.assertEqual(self.runs[8]['settings'].get('lineSpacing'), 0)
        s = self.runs[8]['shots']
        self.assertTrue(same(s[10], s[4]), 'Back returns to the Text rows, cursor on the row it opened')

    def test_alignment_and_drop_cap_keep_the_value_chosen(self):
        self.assertEqual(self.runs[9]['settings'].get('paragraphAlignment'), 1)
        self.assertEqual(self.runs[10]['settings'].get('dropCapMode'), 2)


class StatusBarRowTest(unittest.TestCase):
    """More > Reader status bar opened nothing and the menu vanished (no case for it on the toolbar path)."""

    @classmethod
    def setUpClass(cls):
        to_row = ['RIGHT', 'RIGHT', 'CONFIRM', 'RIGHT', 'RIGHT']  # More sheet, cursor on the 3rd row
        cls.res = run_keys(to_row + ['CONFIRM', 'LEFT', 'LEFT', 'CONFIRM'] + CLOSE)
        for i, img in enumerate(cls.res['shots']):
            keep(f'status-{i}', img)

    def test_the_menu_stays_and_lists_the_modes(self):
        s = self.res['shots']
        self.assertIsNotNone(cursor_top(s[6], sheet_top(s[6]) + 60), 'the sheet stays with the modes listed')
        self.assertFalse(same(s[6], s[5]))

    def test_the_mode_chosen_is_kept(self):
        self.assertEqual(self.res['settings'].get('readerStatusBarMode'), 0)
        s = self.res['shots']
        self.assertIsNotNone(cursor_top(s[9], sheet_top(s[9]) + 60), 'back on the More sheet')


if __name__ == '__main__':
    unittest.main()
