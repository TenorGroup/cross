"""X3 reader toolbar contract from v1.0.61.

The toolbar opens directly on Favorites. Front buttons move a row, side buttons
switch tabs, and an empty Favorites sheet keeps its in-frame tip.
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

CONTAINER = """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
CHAPTERS = 3
OPF = ("""<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Toolbar keys</dc:title>
<dc:identifier id="id">toolbar-keys-v1053</dc:identifier><dc:language>en</dc:language></metadata><manifest>"""
       + ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1, CHAPTERS + 1))
       + '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">'
       + ''.join(f'<itemref idref="c{i}"/>' for i in range(1, CHAPTERS + 1)) + '</spine></package>')
NCX = ("""<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<docTitle><text>Toolbar keys</text></docTitle><navMap>"""
       + ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chapter {i}</text></navLabel>'
                 f'<content src="c{i}.xhtml"/></navPoint>' for i in range(1, CHAPTERS + 1)) + '</navMap></ncx>')
CHAPTER = """<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><body>{body}</body></html>"""


def run_keys(keys, settings=None, quit_after=1200):
    tmp = Path(tempfile.mkdtemp(prefix='menu-thanh-x3-'))
    store = tmp / '.crosspoint'
    store.mkdir()
    target = tmp / '.fonts/BeVietnamPro/BeVietnamPro_18.cpfont'
    target.parent.mkdir(parents=True)
    shutil.copy2(FONT, target)
    with zipfile.ZipFile(tmp / 'book.epub', 'w') as book:
        book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        book.writestr('META-INF/container.xml', CONTAINER)
        book.writestr('OEBPS/content.opf', OPF)
        book.writestr('OEBPS/toc.ncx', NCX)
        for chapter in range(1, CHAPTERS + 1):
            body = ''.join(f'<p>Chapter {chapter} paragraph {row:02d} with plain words for a full page.</p>'
                           for row in range(40))
            book.writestr(f'OEBPS/c{chapter}.xhtml', CHAPTER.format(body=body))
    base = {'language': 'EN', 'readerMenuStyle': 1, 'sdFontFamilyName': 'BeVietnamPro',
            'fontSize': 18, 'sleepTimeout': 120}
    base.update(settings or {})
    (store / 'settings.json').write_text(json.dumps(truoc_tenor(base)) + '\n')
    (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                   'showBootScreen': False, 'readerActivityLoadCount': 0}) + '\n')
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Keys'}]}) + '\n')
    actions = [(1500, 'CONFIRM'), (3500, 'CONFIRM')]
    first = 5000
    step = 1500
    actions += [(first + i * step, key) for i, key in enumerate(keys)]
    end = first + len(keys) * step
    script = ';'.join(f'{ms}:{key}' for ms, key in actions) + f';{end + quit_after}:QUIT'
    shot_times = [first - 300 + i * step for i in range(len(keys) + 1)]
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{tmp}/{i}.bmp' for i, ms in enumerate(shot_times)))
    images = []
    try:
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=120)
        assert run.returncode == 0, run.stdout[-3000:]
        images = [Image.open(tmp / f'{i}.bmp').convert('L') for i in range(len(shot_times))]
        return images
    finally:
        if ARTIFACTS:
            out = Path(ARTIFACTS)
            out.mkdir(parents=True, exist_ok=True)
            for i, image in enumerate(images):
                image.save(out / f'menu-{i}.png')
        shutil.rmtree(tmp, ignore_errors=True)


def same(first, second):
    return ImageChops.difference(first.crop((0, 0, 528, 768)), second.crop((0, 0, 528, 768))).getbbox() is None


class MenuToolbarContractTest(unittest.TestCase):
    def test_menu_opens_on_favorites_and_wraps(self):
        shots = run_keys(['DOWN', 'DOWN', 'DOWN', 'DOWN', 'DOWN'])
        self.assertTrue(same(shots[0], shots[5]), 'five side steps return to Favorites')
        self.assertTrue(any(not same(shots[0], image) for image in shots[1:5]), 'Favorites is a distinct tab')

    def test_side_buttons_switch_tabs(self):
        shots = run_keys(['DOWN', 'DOWN'])
        self.assertFalse(same(shots[0], shots[1]), 'side Down opens Position')
        self.assertFalse(same(shots[1], shots[2]), 'side Down again opens Reading')

    def test_front_buttons_move_favorites_rows(self):
        settings = {'readerFavorites': [14, 'text/fontSize'], 'readerFavoriteCount': 2,
                    'readerFavoritesDaDat': 1}
        shots = run_keys(['RIGHT', 'LEFT'], settings)
        self.assertFalse(same(shots[0], shots[1]), 'front Right moves the Favorites row')
        self.assertTrue(same(shots[0], shots[2]), 'front Left returns to the original row')

    def test_empty_favorites_keeps_the_in_frame_tip(self):
        empty = run_keys([])[0]
        self.assertIsNotNone(ImageChops.difference(empty.crop((0, 450, 528, 708)),
                                                   Image.new('L', (528, 258), 255)).getbbox(),
                             'empty Favorites keeps the in-frame tip')

    def test_select_and_back_restore_the_favorites_sheet(self):
        shots = run_keys(['CONFIRM', 'BACK'], {'readerFavorites': [14], 'readerFavoriteCount': 1,
                                                'readerFavoritesDaDat': 1})
        self.assertFalse(same(shots[0], shots[1]), 'Select opens the Favorites row')
        self.assertFalse(same(shots[1], shots[2]), 'Back returns to the toolbar')


if __name__ == '__main__':
    unittest.main()
