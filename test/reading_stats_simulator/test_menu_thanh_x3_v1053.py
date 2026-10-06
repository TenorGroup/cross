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
        if sum(1 for x in range(img.width) if px[x, y] == 0) >= 0.9 * img.width:
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


if __name__ == '__main__':
    unittest.main()
