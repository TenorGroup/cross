"""v1.0.52: "Save quotation" joins the Hold Select list (TenorGroup/cross#1).

The list entry runs the call the reader menu's Save quotation runs (the quote selector), and the stored
number is 7, appended after the tilt toggle so every earlier save reads as before. Driven through the real
X3 simulator, which reports an IMU, so the list has all 8 entries here; the board without an IMU is
checked on the host (test/menu_tilt_settings).
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

# Same Home-to-Controls walk test_quick_actions_v1012 uses; the Hold Select row is the fourth one.
HOME_TO_CONTROLS = ['1000:UP', '1500:RIGHT', '2000:RIGHT', '2500:RIGHT', '3000:RIGHT', '3500:CONFIRM']
SAVE_QUOTATION = 7


class GiuTrichDanTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-giu-trich-dan-v1052-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', self.sd / 'audit.epub')
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': '/audit.epub', 'title': 'Synonym Lookup Test'}]}))

    def write_settings(self, **fields):
        settings = {'language': 'VI', 'sleepTimeout': 10}
        settings.update(fields)
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor(settings)))

    def saved(self):
        return json.loads((self.store / 'settings.json').read_text())

    def run_sim(self, events, end):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join([*events, f'{end}:QUIT']))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        return log

    def test_hold_select_opens_the_quote_selector(self):
        self.write_settings(longPressMenuFunction=SAVE_QUOTATION)
        log = self.run_sim(['1000:CONFIRM', '3200:CONFIRM:900'], end=6000)
        self.assertEqual(log.count('Entering activity: QuoteSelect'), 1, log[-4000:])
        self.assertNotIn('Entering activity: EpubReaderMenu', log)
        # The choice is still the one stored.
        self.assertEqual(self.saved()['longPressMenuFunction'], SAVE_QUOTATION)

    def test_hold_select_with_bookmark_does_not_open_the_selector(self):
        self.write_settings(longPressMenuFunction=2)
        log = self.run_sim(['1000:CONFIRM', '3200:CONFIRM:900'], end=6000)
        self.assertNotIn('Entering activity: QuoteSelect', log, log[-4000:])

    def test_hold_list_offers_save_quotation_last(self):
        # The popup opens on Off (index 1); six Right reach index 7, the last entry.
        rows = [f'{4500 + 400 * i}:RIGHT' for i in range(3)]
        steps = [f'{10000 + 400 * i}:RIGHT' for i in range(6)]
        log = self.run_sim([*HOME_TO_CONTROLS, *rows, '8000:CONFIRM', *steps, '13500:CONFIRM'], end=15000)
        self.assertIn('Entering activity: Settings', log)
        self.assertEqual(self.saved()['longPressMenuFunction'], SAVE_QUOTATION, log[-4000:])


if __name__ == '__main__':
    unittest.main(verbosity=2)
