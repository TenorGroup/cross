"""v1.0.52: Home tab bar of tenor/cross on X3 and X4.

One round bar 60 px high (x 8 to 519 of 528) with a grey 2 px ring, white inside. The selected tab is a
white 84x48 pill with a black 3 px ring and a stroke-4 black icon; the other icons are stroke 3, grey:
ink kept where x + y is even in the icon's own 40x40 box, the same dots on every tab. The tab centres
run from the bar's first end cap to its last (first = 8 + 6 + 42, last = 519 - 6 - 42). The headers on
disk must be what scripts/icons/sinh.py makes from hinh.py.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BAND = (40, 140)  # rows searched for the bar in the 528x792 screenshot
# Home opens on Recent; the tabs after it, in screen order.
ORDER = ('gan_day', 'thu_muc_hop', 'yeu_tim', 'thong_ke', 'cai_dat')


def icon(shape, stroke, grey):
    sys.path.insert(0, str(REPO / 'scripts/icons'))
    try:
        from net import G, xam
        import hinh
    finally:
        sys.path.pop(0)
    g = G(40, stroke)
    hinh.H[shape][1](g)
    im = g.img()
    return np.array((xam(im) if grey else im).convert('L')) < 128


def hits(dark, want):
    return int((sliding_window_view(dark, want.shape) == want).all(axis=(2, 3)).sum())


class BarTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory(prefix='bo-icon-v1052-') as tmp:
            sd = Path(tmp)
            (sd / '.crosspoint').mkdir()
            (sd / '.crosspoint/settings.json').write_text('{"language": "VI", "sleepTimeout": 120}')
            shot = sd / 'home.bmp'
            env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT='2400:QUIT',
                       CROSSPOINT_SIM_SCREENSHOTS=f'2000:{shot}')
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
            assert run.returncode == 0, (run.stdout + run.stderr)[-3000:]
            with Image.open(shot) as image:
                cls.dark = np.array(image.convert('L')) < 128
        # Top of the bar: first dark row in a column the header text does not reach.
        col = cls.dark[BAND[0]:BAND[1], 264:266].any(axis=1)
        cls.y0 = BAND[0] + int(np.argmax(col)) if col.any() else None

    def px(self, x, y):
        return bool(self.dark[y, x])

    def test_unselected_icons_are_grey_stroke_3(self):
        band = self.dark[BAND[0]:BAND[1]]
        for shape in ORDER[1:]:
            with self.subTest(shape=shape):
                self.assertEqual(hits(band, icon(shape, 3, True)), 1)

    def test_selected_icon_is_black_stroke_4(self):
        band = self.dark[BAND[0]:BAND[1]]
        self.assertEqual(hits(band, icon(ORDER[0], 4, False)), 1)

    def test_bar_is_60_high_with_a_grey_ring_and_white_inside(self):
        y0 = self.y0
        self.assertIsNotNone(y0, 'no bar in the tab band')
        for x in range(120, 420):
            for dy in (0, 1, 58, 59):  # 2 px ring top and bottom, every other pixel
                self.assertEqual(self.px(x, y0 + dy), (x + y0 + dy) % 2 == 0, (x, dy))
            for dy in (2, 3, 4, 5, 54, 55, 56, 57):  # white between the ring and the icons
                self.assertFalse(self.px(x, y0 + dy), (x, dy))
        # Ends at x 8 and 519 on the middle row.
        mid = y0 + 30
        self.assertTrue(any(self.px(x, mid) for x in (8, 9)))
        self.assertFalse(any(self.px(x, mid) for x in (6, 7)))
        self.assertTrue(any(self.px(x, mid) for x in (518, 519)))
        self.assertFalse(any(self.px(x, mid) for x in (520, 521)))

    def test_selected_pill_is_white_inside_a_black_3px_ring(self):
        y0 = self.y0
        self.assertIsNotNone(y0, 'no bar in the tab band')
        cx = 8 + 6 + 42
        left, right, top, bottom = cx - 42, cx + 41, y0 + 6, y0 + 53  # 84 x 48
        for x in range(left + 24, right - 24):  # the straight part of the ring
            for dy in (0, 1, 2):
                self.assertTrue(self.px(x, top + dy), (x, dy))
                self.assertTrue(self.px(x, bottom - dy), (x, dy))
            self.assertFalse(self.px(x, top - 1))
            self.assertFalse(self.px(x, top + 3))  # the row under the ring is white
        for dx in (0, 1, 2):
            self.assertTrue(self.px(left + dx, y0 + 30))
            self.assertTrue(self.px(right - dx, y0 + 30))
        # No tone between ring and icon: the strips above, below and beside the 40x40 icon box are white.
        box_l, box_t = cx - 20, y0 + 10
        strips = [(x, y) for y in range(top + 3, box_t) for x in range(left + 24, right - 23)]
        strips += [(x, y) for y in range(box_t + 40, bottom - 2) for x in range(left + 24, right - 23)]
        strips += [(x, y) for y in range(y0 + 27, y0 + 34) for x in range(left + 4, box_l)]
        strips += [(x, y) for y in range(y0 + 27, y0 + 34) for x in range(box_l + 40, right - 3)]
        for x, y in strips:
            self.assertFalse(self.px(x, y), (x, y))

    def test_tab_centres_run_between_the_end_caps(self):
        y0 = self.y0
        self.assertIsNotNone(y0, 'no bar in the tab band')
        first, last = 8 + 6 + 42, 519 - 6 - 42
        want = [first + (2 * (last - first) * i + 4) // 8 for i in range(5)]
        band = self.dark[BAND[0]:BAND[1]]
        for i, shape in enumerate(ORDER[1:], start=1):
            w = icon(shape, 3, True)
            ys, xs = np.where((sliding_window_view(band, w.shape) == w).all(axis=(2, 3)))
            self.assertEqual(len(xs), 1)
            self.assertEqual(int(xs[0]) + 20, want[i], shape)
            self.assertEqual(int(ys[0]) + BAND[0], y0 + 10, shape)

    def test_headers_match_the_generator(self):
        run = subprocess.run([sys.executable, str(REPO / 'scripts/icons/sinh.py'), '--check'],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
