"""v1.0.52: the reader toolbar's Text panel is set in the tenor/cross UI faces.

Reader Menu Style = Toolbar, open the toolbar over an EPUB page, step to the Text tool and open its
panel. The panel title and the row labels must use the same Geist faces the list menus use, with the
book's reader font (an SD .cpfont here) left to the page above the sheet.
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FONT = Path(__file__).resolve().parent / 'fixtures/BeVietnamPro_18.cpfont'
ARTIFACTS = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')

CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>Toolbar font fixture</dc:title><dc:identifier id="id">toolbar-font</dc:identifier>
<dc:language>en</dc:language></metadata>
<manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>
<spine toc="ncx"><itemref idref="c1"/></spine></package>"""
NCX = """<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="toolbar-font"/></head>
<docTitle><text>Toolbar font fixture</text></docTitle>
<navMap><navPoint id="n1" playOrder="1"><navLabel><text>Chapter One</text></navLabel>
<content src="c1.xhtml"/></navPoint></navMap></ncx>"""
CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter One</title></head><body>{body}</body></html>"""

# Home opens the recent book, Confirm opens the toolbar on Contents, Right steps to Text, Confirm opens it.
SCRIPT = '1500:CONFIRM;3500:CONFIRM;5000:RIGHT;6500:CONFIRM;9500:QUIT'
SHOT_AT = 8500


def run_panel(tmp: Path, family):
    store = tmp / '.crosspoint'
    store.mkdir()
    target = tmp / '.fonts/BeVietnamPro/BeVietnamPro_18.cpfont'
    target.parent.mkdir(parents=True)
    shutil.copy2(FONT, target)
    body = ''.join(f'<p>Body paragraph {i:02d} with plain words for a full page.</p>' for i in range(60))
    with zipfile.ZipFile(tmp / 'book.epub', 'w') as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', CONTAINER)
        z.writestr('OEBPS/content.opf', OPF)
        z.writestr('OEBPS/toc.ncx', NCX)
        z.writestr('OEBPS/c1.xhtml', CHAPTER.format(body=body))
    settings = truoc_tenor({'language': 'EN', 'readerMenuStyle': 1, 'sdFontFamilyName': family, 'fontFamily': 1,
                            'fontSize': 18, 'textAntiAliasing': 0, 'sleepTimeout': 120})
    (store / 'settings.json').write_text(json.dumps(settings) + '\n')
    (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                  'showBootScreen': False, 'readerActivityLoadCount': 0}) + '\n')
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Toolbar'}]}) + '\n')
    shot = tmp / 'panel.bmp'
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp), CROSSPOINT_SIM_INPUT_SCRIPT=SCRIPT,
               CROSSPOINT_SIM_SCREENSHOTS=f'{SHOT_AT}:{shot}')
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
    if ARTIFACTS and shot.exists():
        Path(ARTIFACTS).mkdir(parents=True, exist_ok=True)
        shutil.copy2(shot, Path(ARTIFACTS) / f'thanh-doc-chu-{family or "builtin"}.bmp')
        (Path(ARTIFACTS) / f'thanh-doc-chu-{family or "builtin"}.log').write_text(run.stdout + run.stderr)
    assert run.returncode == 0, run.stdout + run.stderr
    return Image.open(shot).convert('L')


class ToolbarTextPanelFontTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.panels = {}
        for family in ('BeVietnamPro', ''):
            with tempfile.TemporaryDirectory(prefix='thanh-doc-font-') as tmp:
                cls.panels[family] = run_panel(Path(tmp), family)

    def test_ui_title_and_row_labels_keep_their_raster_when_the_reader_font_changes(self):
        sd, builtin = self.panels['BeVietnamPro'], self.panels['']
        # Real book glyphs above the panel prove that the 2 reader fonts differ.
        self.assertNotEqual(sd.crop((0, 0, sd.width, 120)).tobytes(),
                            builtin.crop((0, 0, builtin.width, 120)).tobytes())
        # Label-only strips exclude the selected family value, cursor, row icons and toolbar.
        # UI Geist stays stable while the book uses BeVietnamPro or built-in Noto Sans.
        for label, box in (
            ('Text', (20, 385, 100, 416)),
            ('Font', (26, 431, 100, 468)),
            ('Reader Font Size', (26, 487, 239, 522)),
            ('Line Spacing', (26, 543, 220, 579)),
            ('Paragraph Alignment', (26, 599, 260, 633)),
            ('Paragraph Spacing, the faded 6th row', (26, 625, 245, 650)),
        ):
            actual = sd.crop(box)
            self.assertLess(min(actual.getdata()), 128, f'{label}: label is drawn')
            self.assertEqual(actual.tobytes(), builtin.crop(box).tobytes(), label)


if __name__ == '__main__':
    unittest.main()
