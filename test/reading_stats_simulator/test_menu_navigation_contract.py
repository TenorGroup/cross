"""Settings routes and saved pins survive the v1.0.3 menu reordering."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))


class MenuNavigationContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-menu-contract-')
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        self.settings = {
            'language': 'VI', 'uiTheme': 4, 'sleepTimeoutMinutes': 10,
            'statusBarClock': 1, 'textSpacingVersion': 3, 'paragraphIndentVersion': 1,
            'letterSpacing': 0, 'wordSpacing': 0, 'keyboardAxisSwapped': 0, 'keyboardAligned': 0,
        }
        self.write_settings()
        (self.store / 'state.json').write_text(json.dumps({
            'openEpubPath': '', 'lastSleepFromReader': False, 'showBootScreen': False,
        }))
        self.write_pins([])

    def write_settings(self):
        (self.store / 'settings.json').write_text(json.dumps(self.settings))

    def write_pins(self, pins):
        (self.store / 'menu-customization.json').write_text(json.dumps({
            'version': 1, 'tabs': {
                'home': [0, 1, 4, 2, 3], 'settings': list(range(7)),
                'reader': list(range(4)), 'text': list(range(4)),
            }, 'pins': pins,
        }))

    def run_keys(self, keys, screenshots=()):
        events = [f'{1000 + 500 * index}:{key}' for index, key in enumerate(keys)]
        events.append(f'{2000 + 500 * len(keys)}:QUIT')
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(events))
        if screenshots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(
                f'{at}:{self.sd / (name + ".bmp")}' for at, name in screenshots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=35)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        return log, json.loads((self.store / 'settings.json').read_text())

    @staticmethod
    def group(index):
        # Home Settings begins on Transfer, followed by Display through Other.
        return ['UP'] + ['RIGHT'] * (index + 1) + ['CONFIRM']

    def test_clock_cursor_preserves_legacy_hidden_value(self):
        self.settings['statusBarClock'] = 0
        self.write_settings()
        log, saved = self.run_keys(self.group(0) + ['RIGHT'] * 4 + ['BACK'])
        self.assertEqual(saved['statusBarClock'], 0, log)
        _, rebooted = self.run_keys([])
        self.assertEqual(rebooted['statusBarClock'], 0)

    def test_legacy_action_pin_redirects_to_display(self):
        self.write_pins(['action/2'])
        log, saved = self.run_keys(['DOWN', 'DOWN', 'CONFIRM', 'BACK'])
        self.assertIn('Entering activity: Settings', log)
        self.assertNotIn('Entering activity: StatusBarSettings', log)
        self.assertEqual(saved['statusBarClock'], 2, log)
        self.assertEqual(json.loads((self.store / 'menu-customization.json').read_text())['pins'], ['action/2'])

    def test_legacy_status_pin_still_opens_clock_corners(self):
        self.write_pins(['status/statusBarClock'])
        log, saved = self.run_keys(['DOWN', 'DOWN', 'CONFIRM', 'BACK'])
        self.assertIn('Entering activity: StatusBarSettings', log)
        self.assertEqual(saved['statusBarClock'], 2, log)

    def test_keyboard_axis_keeps_numeric_toggle_persistence(self):
        log, saved = self.run_keys(self.group(5) + ['RIGHT', 'RIGHT', 'CONFIRM', 'BACK'])
        self.assertEqual(saved['keyboardAxisSwapped'], 1, log)
        self.assertIs(type(saved['keyboardAxisSwapped']), int)
        self.assertEqual(saved['keyboardAligned'], 0, log)
        _, rebooted = self.run_keys([])
        self.assertEqual(rebooted['keyboardAxisSwapped'], 1)

    def test_reader_side_buttons_keep_four_original_values(self):
        self.settings['sideButtonLayout'] = 0
        self.write_settings()
        log, saved = self.run_keys(self.group(1) + ['RIGHT'] * 5 + ['CONFIRM'] + ['RIGHT'] * 3 + ['CONFIRM', 'BACK'])
        self.assertEqual(saved['sideButtonLayout'], 3, log)
        self.assertEqual(saved['readerStatusBarMode'], 2, log)

    def test_renamed_legacy_pins_open_current_settings_without_rewriting_pin_keys(self):
        cases = [
            ('text/focusReadingEnabled', 'dropCapMode', 0, 1, 'TextSettings', []),
            ('settings/hideGlobalStatusBar', 'globalStatusBarMode', 0, 1, 'Settings', []),
            ('settings/hideReaderStatusBar', 'readerStatusBarMode', 2, 3, 'Settings', ['RIGHT', 'CONFIRM']),
        ]
        for pin, field, initial, expected, activity, choice in cases:
            with self.subTest(pin=pin):
                self.settings[field] = initial
                self.write_settings()
                self.write_pins([pin])
                log, saved = self.run_keys(['DOWN', 'DOWN', 'CONFIRM'] + choice + ['BACK'])
                self.assertIn('Entering activity: ' + activity, log)
                self.assertEqual(saved[field], expected, log)
                self.assertIs(type(saved[field]), int)
                persisted = json.loads((self.store / 'menu-customization.json').read_text())
                self.assertEqual(persisted['version'], 1)
                self.assertEqual(persisted['pins'], [pin])

    def test_text_spacing_pins_retain_their_setting_keys(self):
        # Opening a Layout favorite advances its setting once. Moving right
        # after launch would select a different setting.
        for field in ('letterSpacing', 'wordSpacing'):
            with self.subTest(field=field):
                self.write_settings()
                self.write_pins(['text/' + field])
                log, saved = self.run_keys(['DOWN', 'DOWN', 'CONFIRM', 'BACK'])
                self.assertIn('Entering activity: TextSettings', log)
                self.assertEqual(saved[field], 1, log)
                other = 'wordSpacing' if field == 'letterSpacing' else 'letterSpacing'
                self.assertEqual(saved[other], 0, log)
                self.assertEqual(saved['lineSpacing'], 0, log)
                self.assertEqual(saved['extraParagraphSpacing'], 0, log)
                persisted = json.loads((self.store / 'menu-customization.json').read_text())
                self.assertEqual(persisted['version'], 1)
                self.assertEqual(persisted['pins'], ['text/' + field])
                _, rebooted = self.run_keys([])
                self.assertEqual(rebooted[field], 1)
                self.assertEqual(rebooted[other], 0)

    def test_hiding_global_status_bar_reclaims_space_on_the_same_screen(self):
        self.settings['globalStatusBarMode'] = 0
        self.write_settings()
        log, saved = self.run_keys(
            self.group(0) + ['RIGHT', 'CONFIRM', 'BACK', 'CONFIRM'],
            [(2800, 'small'), (3350, 'off-live'), (4600, 'off-reopened')])
        self.assertEqual(saved['globalStatusBarMode'], 1, log)

        def row_ink(name):
            with Image.open(self.sd / (name + '.bmp')) as image:
                gray = image.convert('L')
                pixels = gray.tobytes()
                dark_is_ink = sum(value < 128 for value in pixels) < len(pixels) / 2
                row = gray.crop((0, 700, gray.width, 721)).tobytes()
                return sum((value < 128) == dark_is_ink for value in row)

        # Display's eleventh row fits in the recovered footer area. The same
        # row must appear immediately and after reopening the saved setting.
        self.assertLess(row_ink('small'), 120, log)
        self.assertGreater(row_ink('off-live'), 400, log)
        self.assertEqual(row_ink('off-live'), row_ink('off-reopened'), log)

    def test_file_browser_reclaims_hidden_footer_tip_space(self):
        books = self.sd / 'books'
        books.mkdir()
        for index in range(18):
            (books / f'{index:02d}.txt').write_text('Test page.\n' * 10)
        self.settings['globalStatusBarMode'] = 1
        self.write_settings()
        log, _ = self.run_keys(['DOWN', 'CONFIRM'], [(2400, 'files-off')])
        self.assertIn('Entering activity: FileBrowser', log)
        with Image.open(self.sd / 'files-off.bmp') as image:
            gray = image.convert('L')
            pixels = gray.tobytes()
            dark_is_ink = sum(value < 128 for value in pixels) < len(pixels) / 2
            separator_rows = [
                y for y in range(gray.height // 2, gray.height)
                if sum((value < 128) == dark_is_ink for value in
                       gray.crop((24, y, gray.width - 24, y + 1)).tobytes()) >= gray.width - 50
            ]
            self.assertTrue(separator_rows, log)
            # The directory path stays at the bottom once hidden action tips
            # release their former band. A long list fills the space above it.
            self.assertGreater(min(separator_rows), gray.height - 60, log)


if __name__ == '__main__':
    unittest.main()
