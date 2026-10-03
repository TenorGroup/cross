"""v1.0.52: the Home tab icons are the 40 px, stroke 3 set drawn by scripts/icons.

The 4 tabs that are not selected sit on white, so each icon must appear in the screenshot exactly as
scripts/icons draws it (found by exact match in the tab band). The headers on disk must also be what
scripts/icons/sinh.py makes from hinh.py.
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
BAND = (40, 140)  # rows of the tab band in the 528x792 screenshot
UNSELECTED = ('thu_muc_hop', 'yeu_tim', 'thong_ke', 'cai_dat')  # Home opens on Recent, the first tab


def expected(shape):
    sys.path.insert(0, str(REPO / 'scripts/icons'))
    try:
        from net import G
        import hinh
    finally:
        sys.path.pop(0)
    g = G(40, 3)
    hinh.H[shape][1](g)
    return np.array(g.img().convert('L')) < 128


class BoIconTest(unittest.TestCase):
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
                cls.band = np.array(image.convert('L'))[BAND[0]:BAND[1]] < 128

    def test_unselected_tabs_draw_the_new_icons(self):
        for shape in UNSELECTED:
            with self.subTest(shape=shape):
                want = expected(shape)
                windows = sliding_window_view(self.band, want.shape)
                hits = (windows == want).all(axis=(2, 3)).sum()
                self.assertEqual(hits, 1, f'{shape} 40 px stroke 3 not found exactly once in the tab band')

    def test_headers_match_the_generator(self):
        run = subprocess.run([sys.executable, str(REPO / 'scripts/icons/sinh.py'), '--check'],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
