"""v1.0.17: the reader menu and save quotation as quick actions, on the real X3 simulator.

Both actions sit in the shared quick action catalog (src/QuickAction.h), the list a Bluetooth
remote button, the short power press and the motion gestures pick from. The simulator has no
Bluetooth, so the catalog path is driven by the short power press: in a book it opens the
reader menu or the quote selector through the reader's own branches; on Home it does nothing.

Set CROSSPOINT_READER_SHORTCUTS_EVIDENCE to a directory to keep the screenshots.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

READER_MENU = 7  # CrossPointSettings::READER_MENU, stored by value
SAVE_QUOTE = 8   # CrossPointSettings::SAVE_QUOTE


class ReaderShortcutsV1017Test(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-reader-shortcuts-v1017-')
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

    def run_sim(self, events, shots=(), end=None):
        end = end or int(events[-1].split(':')[0]) + 2500
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join([*events, f'{end}:QUIT']))
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        evidence = os.environ.get('CROSSPOINT_READER_SHORTCUTS_EVIDENCE')
        if evidence:
            Path(evidence).mkdir(parents=True, exist_ok=True)
            for _, name in shots:
                if (self.sd / f'{name}.bmp').exists():
                    shutil.copy2(self.sd / f'{name}.bmp', Path(evidence) / f'{name}.bmp')
        return log

    def test_power_press_opens_the_reader_menu_once(self):
        self.write_settings(shortPwrBtn=READER_MENU)
        log = self.run_sim(['1000:CONFIRM', '3200:POWER'], shots=[(5200, 'power-reader-menu')], end=6000)
        self.assertIn('Entering activity: EpubReader', log, log[-4000:])
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log[-4000:])
        self.assertNotIn('Entering activity: QuoteSelect', log)

    def test_power_press_opens_the_quote_selector(self):
        self.write_settings(shortPwrBtn=SAVE_QUOTE)
        log = self.run_sim(['1000:CONFIRM', '3200:POWER'], shots=[(5200, 'power-save-quote')], end=6000)
        self.assertEqual(log.count('Entering activity: QuoteSelect'), 1, log[-4000:])
        self.assertNotIn('Entering activity: EpubReaderMenu', log)

    def test_power_press_on_home_does_nothing(self):
        for action in (READER_MENU, SAVE_QUOTE):
            with self.subTest(action=action):
                self.write_settings(shortPwrBtn=action)
                log = self.run_sim(['1500:POWER'], end=4000)
                self.assertNotIn('Entering activity: EpubReader', log, log[-4000:])
                self.assertNotIn('Entering activity: QuoteSelect', log)
                self.assertEqual(json.loads((self.store / 'settings.json').read_text())['shortPwrBtn'], action,
                                 'the stored action was not kept')


if __name__ == '__main__':
    unittest.main()
