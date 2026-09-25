"""v1.0.12 controls, driven through the real X3 simulator UI.

Covers the two flick strength rows in Settings > Controls, the Confirm-hold function list
(Reader menu now on every board, then File transfer and the tilt toggle), the two quick
actions at the end of the reader menu's Tools tab, and what a Confirm hold in a book does
for each new function. The simulated X3 reports an IMU but has no gyro, so tilt settings
are checked through what gets saved, never through a gesture.

Set CROSSPOINT_QUICK_ACTIONS_EVIDENCE to a directory to keep the screenshots.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

# Home: Up opens the Settings tab; four Right from "File transfer" reach Controls; Select.
HOME_TO_CONTROLS = ['1000:UP', '1500:RIGHT', '2000:RIGHT', '2500:RIGHT', '3000:RIGHT', '3500:CONFIRM']
# Home: Select opens the recent book; Select again opens the reader menu; three Down to Tools.
BOOK_TO_TOOLS = ['1000:CONFIRM', '3200:CONFIRM', '4400:DOWN', '5000:DOWN', '5600:DOWN']


def filled_rows(image, x0, x1, y0, y1, share=0.9):
    """Rows whose strip [x0, x1) is almost all ink: a filled selection band."""
    pixels = image.load()
    return [y for y in range(y0, y1) if sum(1 for x in range(x0, x1) if pixels[x, y] < 128) >= (x1 - x0) * share]


class QuickActionsV1012Test(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-quick-actions-v1012-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', self.sd / 'audit.epub')
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': '/audit.epub', 'title': 'Synonym Lookup Test'}]}))
        self.write_settings()

    def write_settings(self, **fields):
        settings = {'language': 'VI', 'sleepTimeout': 10}
        settings.update(fields)
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor(settings)))

    def saved(self):
        return json.loads((self.store / 'settings.json').read_text())

    def run_sim(self, events, shots=(), end=None):
        """`events` are 'ms:KEY' or 'ms:KEY:holdms'; `shots` are (ms, name)."""
        end = end or int(events[-1].split(':')[0]) + 2500
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join([*events, f'{end}:QUIT']))
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        self.keep(name for _, name in shots)
        return log

    def keep(self, names):
        evidence = os.environ.get('CROSSPOINT_QUICK_ACTIONS_EVIDENCE')
        if not evidence:
            return
        target = Path(evidence)
        target.mkdir(parents=True, exist_ok=True)
        for name in names:
            shot = self.sd / f'{name}.bmp'
            if shot.exists():
                shutil.copy2(shot, target / f'{name}.bmp')

    def shot(self, name):
        with Image.open(self.sd / f'{name}.bmp') as image:
            return image.convert('L')

    # ---- Settings > Controls ----

    def test_side_flick_strength_row_saves_strong(self):
        # Rows: remap, tab tilt, row tilt, side strength. Three Right reach it. A three-choice
        # row steps in place, so one Select moves Medium to Strong and leaves up/down alone.
        log = self.run_sim([*HOME_TO_CONTROLS, '4500:RIGHT', '5000:RIGHT', '5500:RIGHT', '6500:CONFIRM'],
                           shots=[(4300, 'controls-rows'), (7700, 'controls-side-strong')])
        self.assertIn('Entering activity: Settings', log)
        saved = self.saved()
        self.assertEqual((saved['tiltStrengthH'], saved['tiltStrengthV']), (2, 1), log[-4000:])

    def test_confirm_hold_list_offers_file_transfer(self):
        # Eighth row of Controls is "Hold Select while reading". Its popup opens on Off
        # (index 1); four Right move to File transfer (index 5).
        rows = [f'{4500 + 400 * i}:RIGHT' for i in range(7)]
        log = self.run_sim([*HOME_TO_CONTROLS, *rows, '8000:CONFIRM', '10000:RIGHT', '10400:RIGHT',
                            '10800:RIGHT', '11200:RIGHT', '12000:CONFIRM'],
                           shots=[(9800, 'confirm-hold-values')])
        self.assertIn('Entering activity: Settings', log)
        self.assertEqual(self.saved()['longPressMenuFunction'], 5, log[-4000:])

    # ---- reader menu, Tools tab ----

    def test_tools_tab_ends_with_file_transfer_and_tilt_toggle(self):
        # Left from the tab band wraps to the last row, the tilt toggle; Select flips it in place.
        self.write_settings(tiltPageTurn=2)
        log = self.run_sim([*BOOK_TO_TOOLS, '7200:LEFT', '9000:CONFIRM', '11000:CONFIRM'],
                           shots=[(6800, 'tools-tab'), (8700, 'tools-tab-end'), (10700, 'tools-tilt-off')])
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log[-4000:])
        # Both Selects toggled in place: no screen opened over the menu.
        self.assertNotIn('Entering activity', log.split('Entering activity: EpubReaderMenu', 1)[1])
        saved = self.saved()
        # Off, then back on to the Inverted mode it had, not to Normal.
        self.assertEqual(saved['tiltPageTurn'], 2, log[-4000:])
        end = self.shot('tools-tab-end')
        self.assertGreaterEqual(len(filled_rows(end, 40, end.width - 60, 600, 700)), 30,
                                'the last Tools row is not selected')

    def test_tools_tab_tilt_toggle_remembers_inverted_across_restart(self):
        self.write_settings(tiltPageTurn=2)
        self.run_sim([*BOOK_TO_TOOLS, '7200:LEFT', '9000:CONFIRM'])
        saved = self.saved()
        self.assertEqual((saved['tiltPageTurn'], saved.get('tiltPageTurnLastOn')), (0, 2))
        # A fresh start reads it back; one more toggle turns Inverted on again.
        self.run_sim([*BOOK_TO_TOOLS, '7200:LEFT', '9000:CONFIRM'])
        self.assertEqual(self.saved()['tiltPageTurn'], 2)

    # ---- Confirm hold in a book ----

    def test_confirm_hold_opens_file_transfer(self):
        self.write_settings(longPressMenuFunction=5)
        log = self.run_sim(['1000:CONFIRM', '3200:CONFIRM:900'], shots=[(6000, 'hold-file-transfer')], end=7000)
        self.assertIn('Entering activity: CrossPointWebServer', log, log[-4000:])
        self.assertNotIn('Entering activity: EpubReaderMenu', log)
        after = log.split('Entering activity: CrossPointWebServer', 1)[1]
        self.assertNotIn('Entering activity: EpubReader\n', after)

    def test_confirm_hold_opens_the_reader_menu_once(self):
        self.write_settings(longPressMenuFunction=4)
        log = self.run_sim(['1000:CONFIRM', '3200:CONFIRM:900'], shots=[(5200, 'hold-reader-menu')], end=6000)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log[-4000:])
        # Offered on the X3 too, so the stored choice survives loading.
        self.assertEqual(self.saved()['longPressMenuFunction'], 4)
        # The menu opens while Select is still held (pressed at 3200, released at 4100).
        opened = int(re.search(r'\[(\d+)\] \[DBG\] \[ACT\] Entering activity: EpubReaderMenu', log)[1])
        self.assertLess(opened, 4000, log[-4000:])
        # The swallowed release must not pin the row under the cursor.
        self.assertNotIn('readerFavorites', self.saved())

    def test_confirm_hold_toggles_tilt_with_a_popup(self):
        self.write_settings(longPressMenuFunction=6)
        log = self.run_sim(['1000:CONFIRM', '3200:CONFIRM:900'], shots=[(4400, 'hold-tilt-popup')], end=5500)
        self.assertNotIn('Entering activity: EpubReaderMenu', log, log[-4000:])
        self.assertEqual(self.saved()['tiltPageTurn'], 1, log[-4000:])


if __name__ == '__main__':
    unittest.main(verbosity=2)
