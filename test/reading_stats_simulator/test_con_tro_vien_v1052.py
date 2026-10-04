"""v1.0.52: on a button device the selected row is a white pill ringed in black, the shape of the
selected tab, in place of the black block.

Real X3 simulator input. Measured on the framebuffer: the ring is 3 px, the inside is white (the
label is the only ink), the ends are half circles, the pill keeps clear of the edge arrows and of the
pin mark hung in the left margin, and a row with an icon and two lines of text keeps off the ring.
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from pill_row import RING, dark, ink_inside, pill_band, pill_extent

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
POPUP_BOUNDS = (76, 452)  # inside the popup's own frame
HEART_LEFT_MARGIN_GAP = 1  # white columns between the pin mark and the ring (the same gap the black block left)


class ConTroVienTest(unittest.TestCase):
    def run_sim(self, script, shots, settings=None, files=8, epub=False):
        tmp = tempfile.mkdtemp(prefix='cross-con-tro-vien-')
        self.addCleanup(shutil.rmtree, tmp, True)
        sd = Path(tmp)
        store = sd / '.crosspoint'
        store.mkdir()
        for i in range(files):
            (sd / f'tep{i:02d}.txt').write_text('Original test text.\n' * 15)
        recent = []
        if epub:
            import test_chapter_hold as hold
            (sd / 'books').mkdir()
            hold.write_epub(sd / 'books/sach.epub', chuong=8)
            recent = [{'path': '/books/sach.epub', 'title': 'Sach', 'author': 'A', 'coverBmpPath': '', 'excerpt': 'x'}]
        (store / 'recent.json').write_text(json.dumps({'books': recent}))
        (store / 'settings.json').write_text(json.dumps(dict(
            {'language': 'VI', 'uiTheme': 4, 'sleepTimeoutMinutes': 31, 'globalStatusBarMode': 1}, **(settings or {}))))
        (store / 'state.json').write_text(json.dumps({
            'openEpubPath': '', 'lastSleepFromReader': False, 'showBootScreen': False}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{sd}/{label}.bmp' for ms, label in shots))
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, log[-4000:])
        return {label: Image.open(sd / f'{label}.bmp').convert('L') for _, label in shots}

    def check_pill(self, image, name, label_free_x=None, mirrored=False, bounds=(14, None)):
        """The ring, the white inside and the round ends of the selected row."""
        band = pill_band(image)
        self.assertIsNotNone(band, f'{name}: no pill ring on screen')
        h = band[1] - band[0]
        self.assertTrue(44 <= h <= 90, f'{name}: pill height {h}')
        left, right = pill_extent(image, band, mirrored, bounds)
        px = image.load()
        # Ring depth 3: in a column past the label the pill is exactly two rings of RING rows.
        probe = right - 40 if label_free_x is None else label_free_x
        dark_rows = [y for y in range(band[0], band[1]) if dark(px, probe, y)]
        self.assertEqual(len(dark_rows), 2 * RING, f'{name}: column {probe} holds {len(dark_rows)} dark rows')
        # Round ends: the first dark pixel of the top ring line sits well inside the pill's left end.
        top_first = next(x for x in range(left, right) if dark(px, x, band[0]))
        self.assertGreaterEqual(top_first - left, min(h // 2, 40) - 12, f'{name}: ends are not half circles')
        # Ring 3 px thick at the middle row, white right inside it.
        mid = (band[0] + band[1]) // 2
        run = 0
        while dark(px, left + run, mid):
            run += 1
        self.assertEqual(run, RING, f'{name}: ring is {run} px at the end')
        self.assertGreater(px[left + RING + 2, mid], 250, f'{name}: inside the ring is not white')
        # The text keeps 2 px off the ring (a descender of a subtitle's last line is the closest ink).
        ink = ink_inside(image, band, left, right)
        self.assertTrue(all(band[0] + RING + 2 <= y < band[1] - RING - 2 for _, y in ink),
                        f'{name}: ink touches the ring')
        return band, left, right

    def test_file_list_rows_at_every_text_size(self):
        for tier in (0, 1, 2):
            with self.subTest(tier=tier):
                im = self.run_sim('1000:DOWN;1700:RIGHT;2500:QUIT',
                                  [(1500, 'a'), (2300, 'b')], settings={'uiTextSize': tier})
                a, left, right = self.check_pill(im['a'], f'tier{tier}-a')
                b, left_b, right_b = self.check_pill(im['b'], f'tier{tier}-b')
                self.assertGreater(b[0], a[0], 'the pill follows the selection down')
                self.assertEqual(b[1] - b[0], a[1] - a[0])
                self.assertEqual((left, right), (left_b, right_b))
                # The edge arrows sit outside the pill.
                self.assertGreaterEqual(left, 16)
                self.assertLessEqual(right, im['a'].width - 16)

    def test_option_popup_buttons(self):
        im = self.run_sim('800:DOWN;1600:DOWN;2400:DOWN;3200:DOWN;3800:RIGHT;4400:RIGHT;5000:CONFIRM;'
                          '6300:CONFIRM;7600:DOWN;8700:QUIT', [(7300, 'a'), (8300, 'b')])
        # The popup is a narrower, centred column: its buttons are pills too.
        a, left, right = self.check_pill(im['a'], 'popup-a', bounds=POPUP_BOUNDS)
        b, _, _ = self.check_pill(im['b'], 'popup-b', bounds=POPUP_BOUNDS)
        self.assertGreater(left, 60)
        self.assertGreater(b[0], a[0])

    def test_pin_mark_stays_clear_of_the_ring(self):
        # Reader menu, Favourites tab: its row carries the pin mark in the left margin.
        im = self.run_sim('1000:CONFIRM;4000:CONFIRM;5300:RIGHT;6000:QUIT', [(5000, 'a')], epub=True)['a']
        band, left, right = self.check_pill(im, 'menu', label_free_x=480, mirrored=True)
        px = im.load()
        mid = (band[0] + band[1]) // 2
        heart = [x for x in range(0, left) for y in range(mid - 12, mid + 13) if y >= band[0] + 8 and dark(px, x, y)]
        self.assertTrue(heart, 'pin mark is missing from the margin')
        self.assertLess(max(heart) + HEART_LEFT_MARGIN_GAP, left, 'pin mark touches the ring')

    def test_icon_row_with_subtitle_keeps_off_the_ring(self):
        # Settings, Send file: rows with an icon and a second line of text.
        im = self.run_sim('800:DOWN;1600:DOWN;2400:DOWN;3200:DOWN;3800:CONFIRM;5000:RIGHT;5800:QUIT',
                          [(5500, 'a')])['a']
        band, left, right = self.check_pill(im, 'icon-row', label_free_x=right_probe(im))
        px = im.load()
        mid = (band[0] + band[1]) // 2
        # The icon starts past the ring's curved end: nothing dark between the ring (inner edge at most 6 px in)
        # and the icon, which sits at the row's side padding.
        inner = [x for x in range(left + 8, left + 40) for y in range(mid - 10, mid + 11) if dark(px, x, y)]
        self.assertTrue(inner, 'the icon is missing')
        self.assertGreaterEqual(min(inner) - left, 10, 'the icon touches the ring')


def right_probe(image):
    """A column near the right end that holds no value text, found from the ring itself."""
    band = pill_band(image)
    return pill_extent(image, band)[1] - 40


if __name__ == '__main__':
    unittest.main()
