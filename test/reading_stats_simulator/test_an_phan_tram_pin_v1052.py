"""v1.0.52: Hide Battery % works in the tenor/cross status bar (TenorGroup/cross#1).

"Always" drops the percentage beside the battery on Home; "In reader" leaves Home as "Never" draws it.
Only the bottom-left corner is compared: the battery sits there, the clock on the right changes with
the minute between runs.
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


class AnPhanTramPinTest(unittest.TestCase):
    def corner(self, hide):
        with tempfile.TemporaryDirectory(prefix='an-pin-v1052-') as tmp:
            sd = Path(tmp)
            (sd / '.crosspoint').mkdir()
            (sd / '.crosspoint/settings.json').write_text(
                json.dumps({'language': 'VI', 'hideBatteryPercentage': hide, 'sleepTimeout': 120}))
            shot = sd / 'home.bmp'
            env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT='2400:QUIT',
                       CROSSPOINT_SIM_SCREENSHOTS=f'2000:{shot}')
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log[-3000:])
            self.assertIn('Entering activity: Home', log)
            with Image.open(shot) as image:
                return image.convert('L').crop((0, image.height - 40, 160, image.height))

    def test_hide_battery_percent_on_home(self):
        never = self.corner(0)
        self.assertIsNotNone(ImageChops.invert(never).getbbox(), 'no battery in the corner')
        self.assertIsNotNone(ImageChops.difference(never, self.corner(2)).getbbox(), '"Always" still draws the %')
        self.assertIsNone(ImageChops.difference(never, self.corner(1)).getbbox(), '"In reader" changed Home')


if __name__ == '__main__':
    unittest.main()
