"""The wake notice over a gray sleep screen, for the build with -DTENOR_WAKE_LABEL_GRAY.

Not a test_*.py file: the build that ships has no such notice, so run_all.py leaves this alone.
Run it against a simulator built with that flag (env simulator_x3_uc8279_wakegray of the local
platformio.local.ini):

  TEST_PROGRAM=.pio/build/simulator_x3_uc8279_wakegray/program python3 check_wake_label_gray.py

A gray sleep screen leaves no kept frame, so the controller's RAM is written to agree everywhere
but under the label: one fast refresh drives only the label's box, the gray picture stays around
it, and the first screen after it still drives every pixel (nothing of the picture or the label
is left behind). The glass model (glass_model.py) says what the glass holds after each call.
"""

import json
from pathlib import Path
import tempfile
import unittest

import glass_model
from cai_dat_truoc_tenor import truoc_tenor

BOX_LIMIT = 40000  # pixels: a notice box and its ring are about 300 by 60


class WakeLabelGrayTest(unittest.TestCase):
    def test_label_drives_only_its_box_and_the_next_screen_cleans_everything(self):
        with tempfile.TemporaryDirectory(prefix='cross-wake-label-gray-') as directory:
            tool = glass_model.build(Path(directory) / 'tool')
            sd = Path(directory) / 'sd'
            store = sd / '.crosspoint'
            store.mkdir(parents=True)
            (store / 'settings.json').write_text(json.dumps(truoc_tenor(
                {'language': 'EN', 'sleepScreen': 8, 'sleepBwFold': 0, 'wakeNotice': 1})))
            (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
            (store / 'sleep_frame.bin').write_bytes(b'\xff' * glass_model.PANEL_BYTES)
            trace = sd / 'panel.trace'
            code, log = glass_model.run_simulator(sd, trace, '3000:SLEEP;6000:POWER;12000:QUIT', '2500:QUIT', wake='power')
            self.assertEqual(code, 0, log)
            self.assertIn('Wake notice shown over an unknown frame', log)
            records = glass_model.replay(tool, trace)
            self.assertEqual([r for r in records if 'anomaly' in r], [])
            start = max(r['i'] for r in records if r['op'] == 'begin')
            before = [r for r in records if r['i'] < start][-1]  # the glass as the device went to sleep
            woke = [r for r in records if r['i'] > start and r['op'] == 'display']
            self.assertEqual([r['a'] for r in woke], [2, 0])  # the notice (fast), then the clean refresh
            label, home = woke
            self.assertGreater(before['gray'], 100000)  # the sleep screen really is gray
            self.assertEqual(label['bank'], 'du')
            self.assertTrue(1000 < label['driven'] < BOX_LIMIT, label)
            self.assertGreaterEqual(label['gray'], before['gray'] - BOX_LIMIT)  # the picture stays around it
            self.assertEqual(home['glass_vs_frame'], 0)
            self.assertEqual(home['gray'], 0)  # and the next screen leaves none of it


if __name__ == '__main__':
    unittest.main()
