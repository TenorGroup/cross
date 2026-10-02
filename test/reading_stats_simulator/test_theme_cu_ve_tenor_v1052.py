"""v1.0.52: the old UI themes are gone, so a settings file that still stores one draws tenor/cross.

A settings.json written by an earlier release can hold "uiTheme" 0 to 3 or 5 (Classic, Lyra, Lyra
Extended, RoundedRaff, Cover Grid). Every one of them must boot into the same Home screen as the
value 4 that tenor/cross wrote, pixel for pixel.
"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))


class ThemeCuVeTenorTest(unittest.TestCase):
    def home(self, theme):
        with tempfile.TemporaryDirectory(prefix='theme-cu-v1052-') as tmp:
            sd = Path(tmp)
            (sd / '.crosspoint').mkdir()
            (sd / '.crosspoint/settings.json').write_text(
                json.dumps({'language': 'VI', 'uiTheme': theme, 'sleepTimeout': 120}))
            shot = sd / 'home.bmp'
            env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT='2400:QUIT',
                       CROSSPOINT_SIM_SCREENSHOTS=f'2000:{shot}')
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log[-3000:])
            self.assertIn('Entering activity: Home', log)
            with Image.open(shot) as image:
                # The bottom 40 px hold the clock, which changes with the minute between runs.
                return image.convert('L').crop((0, 0, image.width, image.height - 40))

    def test_every_stored_theme_draws_tenor_home(self):
        tenor = self.home(4)
        self.assertIsNotNone(ImageChops.invert(tenor).getbbox(), 'the reference Home is blank')
        for theme in (0, 1, 2, 3, 5):
            with self.subTest(uiTheme=theme):
                self.assertIsNone(ImageChops.difference(tenor, self.home(theme)).getbbox())


if __name__ == '__main__':
    unittest.main()
