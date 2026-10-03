"""A second press of the power key while the device is still waking (v1.0.52).

The wake's own release is swallowed in the first loop pass. A short press that follows it, before
the wake's first frame is on the glass, used to run the short-press action (a forced refresh) on
top of the frame being drawn: Home flashed once more. A release that comes before the first frame
is now dropped; one that comes after still acts, and the long hold that sleeps is unchanged.
"""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FORCE_REFRESH = 3
REFRESH = 'Manual screen refresh triggered'


class WakeSecondPressTest(unittest.TestCase):
    def wake(self, after_wake):
        """Sleep, wake by the power key, then run `after_wake` on the woken process. Returns its log."""
        with tempfile.TemporaryDirectory(prefix='cross-wake-second-press-') as directory:
            sd = Path(directory)
            store = sd / '.crosspoint'
            store.mkdir()
            (store / 'settings.json').write_text(json.dumps(truoc_tenor(
                {'language': 'EN', 'sleepScreen': 8, 'shortPwrBtn': FORCE_REFRESH})))
            (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
            env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_REFRESH_MS='390',
                       CROSSPOINT_SIM_INPUT_SCRIPT='2500:SLEEP;4500:POWER;12000:QUIT',
                       CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=after_wake)
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log[-4000:])
            self.assertIn('Entering deep sleep', log)
            return log.split('Entering deep sleep', 1)[1]

    def test_press_before_the_first_frame_does_not_refresh_again(self):
        wake = self.wake('100:POWER:100;3500:QUIT')
        self.assertIn('Entering activity: Home', wake)
        self.assertNotIn(REFRESH, wake)

    def test_press_after_the_first_frame_still_refreshes(self):
        wake = self.wake('1200:POWER:150;3500:QUIT')
        self.assertEqual(wake.count(REFRESH), 1, wake)

    def test_hold_before_the_sleep_guard_does_not_sleep(self):
        wake = self.wake('1000:POWER:600;3500:QUIT')
        self.assertNotIn('Entering deep sleep', wake)

    def test_hold_after_the_sleep_guard_sleeps(self):
        # allowSleepAt is 2 s after setup; the hold runs past it.
        wake = self.wake('2500:POWER:600;6000:QUIT')
        self.assertIn('Entering deep sleep', wake)


if __name__ == '__main__':
    unittest.main()
