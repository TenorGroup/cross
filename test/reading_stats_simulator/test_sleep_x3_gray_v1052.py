"""v1.0.52: the X3 UC8279 sleeps on a real 4-level gray picture by default.

1. The cover is made with the even thresholds 43/128/213 (cache file `_original`) on every panel
   that has absolute gray, the X3 UC8279 included. It was SSD1677 (X4) only.
2. Folding to black and white before sleep is off for everyone. A card that was saved by an
   earlier release holds `"sleepBwRefresh": 1` (the old default): that key is no longer read, the
   switch is `sleepBwFold`, default off, and turning it on still folds.

The simulator offers absolute gray on every panel, so the UC8253 X3 (supported() false, thresholds
and fold unchanged) cannot be walked here; it is covered by the code path, not by this test.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sleep_cover_v1014 import BOOK, SLEEP_AT, tone_cover, write_epub
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))


def sleep_log(program, settings):
    with tempfile.TemporaryDirectory(prefix='cross-sleep-gray-v1052-') as tmp:
        sd = Path(tmp)
        store = sd / '.crosspoint'
        store.mkdir()
        (store / 'settings.json').write_text(json.dumps(truoc_tenor(dict({'language': 'VI', 'sleepScreen': 3}, **settings))))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': BOOK}))
        write_epub(sd / BOOK.lstrip('/'), tone_cover())
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=f'{SLEEP_AT}:SLEEP;{SLEEP_AT + 5000}:QUIT')
        run = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        assert run.returncode == 0, log[-4000:]
        assert 'Entering deep sleep' in log, log[-4000:]
        sleep = log.split('Entering activity: Sleep', 1)[1].split('Entering deep sleep', 1)[0]
        return sleep, sorted(p.name for p in sd.rglob('cover*.bmp'))


class SleepX3GrayTest(unittest.TestCase):
    def test_old_file_with_the_old_switch_on_sleeps_in_4_gray_levels(self):
        sleep, covers = sleep_log(PROGRAM, {'sleepBwRefresh': 1})
        self.assertIn('absolute=1', sleep)
        self.assertIn('original thresholds', sleep)
        self.assertTrue(any('_original' in c for c in covers), covers)
        self.assertNotIn('[BMP] Timing bpp=1 ', sleep, 'the cover was folded to black and white')

    def test_new_switch_on_folds_to_black_and_white(self):
        sleep, covers = sleep_log(PROGRAM, {'sleepBwFold': 1})
        self.assertIn('[BMP] Timing bpp=1 ', sleep)
        self.assertFalse(any('_original' in c for c in covers), covers)


if __name__ == '__main__':
    unittest.main()
