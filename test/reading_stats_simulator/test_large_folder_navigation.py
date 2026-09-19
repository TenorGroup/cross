"""Large folders retain global selection when the row viewport moves."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))


class LargeFolderNavigationTest(unittest.TestCase):
    def check_folder(self, count):
        with tempfile.TemporaryDirectory(prefix='cross-large-folder-') as tmp:
            sd = Path(tmp)
            store = sd / '.crosspoint'
            store.mkdir()
            folder = sd / 'books'
            folder.mkdir()
            for index in range(count):
                (folder / f'book{index:05d}-測試-a\u0301.txt').write_text('Original test text.\n' * 15)
            (store / 'settings.json').write_text(json.dumps({
                'language': 'VI', 'uiTheme': 4, 'sleepTimeoutMinutes': 31,
                'globalStatusBarMode': 1,
            }))
            (store / 'state.json').write_text(json.dumps({
                'openEpubPath': '', 'lastSleepFromReader': False, 'showBootScreen': False,
            }))
            (store / 'menu-customization.json').write_text(json.dumps({
                'version': 1, 'tabs': {'home': [0, 1, 4, 2, 3], 'settings': list(range(7)),
                                     'reader': list(range(4)), 'text': list(range(4))}, 'pins': [],
            }))
            env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                       # Open the folder from Home, wrap first -> last, then
                       # commit the selected absolute file through the reader.
                       CROSSPOINT_SIM_INPUT_SCRIPT='1200:DOWN;2000:CONFIRM;3200:LEFT;4200:CONFIRM;7500:QUIT')
            result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True,
                                    text=True, timeout=40)
            log = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, log[-8000:])
            self.assertIn('Entering activity: FileBrowser', log)
            self.assertIn('Entering activity: TxtReader', log)
            recent = json.loads((store / 'recent.json').read_text())
            self.assertEqual(recent['books'][0]['path'], f'/books/book{count - 1:05d}-測試-a\u0301.txt')
            self.assertEqual(len(list(folder.iterdir())), count)

    def test_100_files(self):
        self.check_folder(100)

    def test_1000_files(self):
        self.check_folder(1000)

    def test_5000_files(self):
        self.check_folder(5000)


if __name__ == '__main__':
    unittest.main()
