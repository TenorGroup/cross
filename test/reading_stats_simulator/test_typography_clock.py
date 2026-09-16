"""Check paragraph layout and clock refresh using the actual X3 simulator."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'


def write_epub(path):
    paragraph = 'MMMM Reading keeps the first line distinct while the following lines use the full width. ' * 5
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Paragraph fixture</dc:title><dc:identifier id="id">indent-test</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        epub.writestr('body.xhtml', '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Paragraph fixture</title></head><body>' + ''.join('<p style="text-indent:0">' + paragraph + '</p>' for _ in range(8)) + '</body></html>')


class TypographyClockTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cross-type-clock-')
        self.addCleanup(self.temp.cleanup)
        self.sd = Path(self.temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        self.settings = {'language': 'VI', 'sleepTimeout': 10, 'textAntiAliasing': 0,
                         'paragraphAlignment': 1, 'extraParagraphSpacing': True, 'statusBarClock': 0}
        write_epub(self.sd / 'audit.epub')

    def run_sim(self, events, captures, timeout=25):
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in captures))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering activity: EpubReader', log)
        images = [Image.open(self.sd / (name + '.bmp')).convert('RGB') for _, name in captures]
        artifact_dir = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        if artifact_dir:
            output = Path(artifact_dir)
            output.mkdir(parents=True, exist_ok=True)
            for (_, name), image in zip(captures, images):
                image.save(output / (name + '.png'))
        return images, log

    def test_indent_changes_layout_with_spacing_and_invalidates_warm_cache(self):
        images = []
        for mode in (0, 1, 2):
            self.settings['paragraphIndent'] = mode
            captured, log = self.run_sim(
                '1000:DOWN;1800:CONFIRM;2600:CONFIRM;5800:BACK;6800:QUIT', [(5000, f'indent-{mode}')])
            images.append(captured[0])
        self.assertIsNotNone(ImageChops.difference(images[0], images[1]).getbbox(),
                             'Enabling indentation must change the actual EPUB layout with spacing enabled')
        self.assertIsNone(ImageChops.difference(images[0], images[2]).getbbox(),
                         'Disabling indentation must rebuild the warm cache and restore zero-indent layout')

    def test_indent_menu_updates_preview_and_survives_restart(self):
        self.settings['readerFavorites'] = [2]  # Pin the existing Text Settings action.
        captured, log = self.run_sim(
            # Hop dong 14/09/2026 dem: cua Cai dat van ban tu menu mo THANG the Bo cuc, dong 1; LEFT hai
            # nhip quay vong toi dong 5 Thut dau dong; Quay lai mot nhip la ve sach.
            '1000:DOWN;1800:CONFIRM;2600:CONFIRM;5000:CONFIRM;6000:CONFIRM;7000:CONFIRM;'
            '8200:LEFT;9000:LEFT;11000:CONFIRM;13000:BACK;14000:BACK;15500:BACK;16500:QUIT',
            [(10600, 'menu-auto'), (12100, 'menu-on')])
        self.assertIn('Entering activity: TextSettings', log)
        self.assertIn('Exiting activity: TextSettings', log)
        self.settings = json.loads((self.store / 'settings.json').read_text())
        self.assertEqual(self.settings['paragraphIndent'], 1, log)
        self.assertTrue(self.settings['extraParagraphSpacing'])
        portrait = [im.rotate(90, expand=True) if im.width > im.height else im for im in captured]
        self.assertIsNotNone(ImageChops.difference(portrait[0].crop((0, 70, 528, 300)),
                                                  portrait[1].crop((0, 70, 528, 300))).getbbox(),
                             'The text preview must immediately relayout after the choice changes')
        self.run_sim('1000:DOWN;1800:CONFIRM;2600:CONFIRM;5000:BACK;6000:QUIT', [(4500, 'menu-restart')])
        self.assertEqual(json.loads((self.store / 'settings.json').read_text())['paragraphIndent'], 1)

    def test_clock_waits_for_page_refresh_across_a_minute(self):
        self.settings.update(statusBarClock=1, clockFormat=0, clockUtcOffsetQ=76)
        captured, log = self.run_sim(
            '1000:DOWN;1800:CONFIRM;2600:CONFIRM;71500:RIGHT;73500:LEFT;77500:QUIT',
            [(5500, 'before'), (70500, 'idle'), (76500, 'after')], timeout=90)
        self.assertIsNone(ImageChops.difference(captured[0], captured[1]).getbbox(),
                         'A minute change while idle must leave the displayed frame unchanged')
        # Inspect the actual display calls, not only identical pixels.
        refresh_times = [int(ms) for ms in re.findall(r'\[(\d+)\].*display(?:Buffer|GrayscaleBase), mode=', log)]
        self.assertTrue(refresh_times, log)
        self.assertFalse([ms for ms in refresh_times if 5500 < ms < 70500], log)
        self.assertIsNotNone(ImageChops.difference(captured[1], captured[2]).getbbox(),
                             'Returning to the same page must display the new clock minute')
        # The panel buffer is landscape; portrait content is stored rotated.
        portrait = [im.rotate(90, expand=True) if im.width > im.height else im for im in captured]
        self.assertIsNone(ImageChops.difference(portrait[0].crop((0, 0, 528, 710)),
                                               portrait[2].crop((0, 0, 528, 710))).getbbox(),
                         'The page text must remain the same when only the clock changes')


if __name__ == '__main__':
    unittest.main()
