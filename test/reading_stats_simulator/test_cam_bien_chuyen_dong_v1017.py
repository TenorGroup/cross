"""v1.0.17 Motion sensor settings tab, driven through the real X3 simulator UI.

Every sensor row now lives in Settings > Motion sensor, right after Controls: page tilt,
tab tilt, row tilt, the two flick strengths, the hard shake and its strength, face down,
face up and double tap. A settings file written before the move keeps every value, and the rows
left behind in Controls are the other ones. The simulated X3 reports an IMU but has no
gyro, so the rows are checked through what a press saves, and the screens by picture.

Set CROSSPOINT_MOTION_EVIDENCE to a directory to keep the screenshots.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

# Home: Up opens Settings on Display. Sleep, Reader, Controls and Motion follow.
HOME_TO_SETTINGS = ['1000:UP']
HOME_TO_CONTROLS = ['1000:UP', '1500:RIGHT', '2000:RIGHT', '2500:RIGHT', '3500:CONFIRM']
HOME_TO_MOTION = ['1000:UP', '1500:RIGHT', '2000:RIGHT', '2500:RIGHT', '2800:RIGHT', '3500:CONFIRM']

# A file from v1.0.16, every sensor row away from its default.
OLD_SENSOR_VALUES = {
    'tiltPageTurn': 2, 'tiltTabNavigation': 1, 'tiltMenuNavigation': 2, 'tiltStrengthH': 0,
    'tiltStrengthV': 2, 'shakeAction': 3, 'shakeStrength': 2,
}


class MotionSensorTabTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-motion-v1017-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()

    def write_settings(self, language='VI', **fields):
        settings = {'language': language, 'sleepTimeout': 10, **OLD_SENSOR_VALUES}
        settings.update(fields)
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor(settings)))

    def saved(self):
        return json.loads((self.store / 'settings.json').read_text())

    def run_sim(self, events, shots=()):
        end = int(events[-1].split(':')[0]) + 2500
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join([*events, f'{end}:QUIT']))
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        evidence = os.environ.get('CROSSPOINT_MOTION_EVIDENCE')
        if evidence:
            Path(evidence).mkdir(parents=True, exist_ok=True)
            for _, name in shots:
                if (self.sd / f'{name}.bmp').exists():
                    shutil.copy2(self.sd / f'{name}.bmp', Path(evidence) / f'{name}.bmp')
        return log

    def ink(self, name):
        with Image.open(self.sd / f'{name}.bmp') as image:
            return image.convert('1').tobytes()

    def rows_ink(self, name):
        with Image.open(self.sd / f'{name}.bmp') as image:
            return image.convert('1').crop((0, 0, image.width, image.height - 60)).tobytes()

    def test_face_down_row_is_eighth_in_motion_sensor_and_old_values_stay(self):
        # Rows: page tilt, tab tilt, row tilt, side strength, up/down strength, shake, shake
        # strength, face down. Seven Right reach it; six choices open the picker on Off, one
        # Right is Refresh.
        self.write_settings()
        log = self.run_sim([*HOME_TO_MOTION, *[f'{4500 + 400 * i}:RIGHT' for i in range(7)], '7800:CONFIRM',
                            '9500:RIGHT', '10200:CONFIRM'])
        self.assertIn('Entering activity: Settings', log)
        saved = self.saved()
        self.assertEqual((saved['faceDownAction'], saved['faceUpAction']), (1, 0), log[-4000:])
        self.assertEqual({key: saved[key] for key in OLD_SENSOR_VALUES}, OLD_SENSOR_VALUES, log[-4000:])

    def test_face_up_row_is_last_in_motion_sensor(self):
        self.write_settings()
        log = self.run_sim([*HOME_TO_MOTION, *[f'{4500 + 400 * i}:RIGHT' for i in range(8)], '8200:CONFIRM',
                            '9900:RIGHT', '10300:RIGHT', '11000:CONFIRM'])
        self.assertIn('Entering activity: Settings', log)
        saved = self.saved()
        self.assertEqual((saved['faceDownAction'], saved['faceUpAction']), (0, 2), log[-4000:])

    def test_double_tap_row_is_last_in_motion_sensor(self):
        # Nine Right reach the tenth row; the picker opens on Off, one Right is Refresh.
        self.write_settings()
        log = self.run_sim([*HOME_TO_MOTION, *[f'{4500 + 400 * i}:RIGHT' for i in range(9)], '8600:CONFIRM',
                            '10300:RIGHT', '11000:CONFIRM'])
        self.assertIn('Entering activity: Settings', log)
        saved = self.saved()
        self.assertEqual((saved['doubleTapAction'], saved['faceDownAction'], saved['faceUpAction']), (1, 0, 0),
                         log[-4000:])
        self.assertEqual({key: saved[key] for key in OLD_SENSOR_VALUES}, OLD_SENSOR_VALUES, log[-4000:])

    def test_three_languages_show_the_double_tap_row(self):
        # The cursor on the double tap row, in each language, as pictures without the status
        # bar (its clock). Nine Right from the first row land on a tenth row, not back on the first.
        pictures = {}
        for language in ('VI', 'EN', 'ZH_HANS'):
            shutil.rmtree(self.store)
            self.store.mkdir()
            self.write_settings(language=language)
            tag = language.lower()
            self.run_sim([*HOME_TO_MOTION, *[f'{4500 + 400 * i}:RIGHT' for i in range(9)]],
                         shots=[(4300, f'first-row-{tag}'), (7500, f'face-up-row-{tag}'),
                                (9000, f'double-tap-row-{tag}')])
            pictures[language] = self.rows_ink(f'double-tap-row-{tag}')
            self.assertTrue(self.rows_ink(f'first-row-{tag}') != pictures[language], f'{language}: not the first row')
            self.assertTrue(self.rows_ink(f'face-up-row-{tag}') != pictures[language], f'{language}: not face up')
        self.assertEqual(len(set(pictures.values())), 3, 'each language draws its own row')

    def test_first_motion_row_is_page_tilt(self):
        # A three-choice row steps in place: Reversed (2) goes round to Off.
        self.write_settings()
        self.run_sim([*HOME_TO_MOTION, '4500:CONFIRM'])
        saved = self.saved()
        self.assertEqual((saved['tiltPageTurn'], saved['tiltTabNavigation']), (0, 1))

    def test_controls_keeps_its_own_rows(self):
        # Controls begins on remap; the next row is now "front buttons follow orientation",
        # where tab tilt used to be.
        self.write_settings()
        self.run_sim([*HOME_TO_CONTROLS, '4500:RIGHT', '5500:CONFIRM'])
        saved = self.saved()
        self.assertEqual(saved['frontButtonFollowOrientation'], 1)
        self.assertEqual(saved['tiltTabNavigation'], OLD_SENSOR_VALUES['tiltTabNavigation'])

    def test_three_languages_show_the_tab(self):
        # Home's settings groups and the Motion sensor tab, in each language, as pictures.
        pictures = {}
        for language in ('VI', 'EN', 'ZH_HANS'):
            shutil.rmtree(self.store)
            self.store.mkdir()
            self.write_settings(language=language)
            tag = language.lower()
            self.run_sim([*HOME_TO_MOTION, '6000:BACK'],
                         shots=[(1400, f'groups-{tag}'), (4800, f'motion-{tag}'), (7500, f'back-{tag}')])
            pictures[language] = (self.ink(f'groups-{tag}'), self.ink(f'motion-{tag}'))
            self.assertNotEqual(pictures[language][0], pictures[language][1], language)
        self.assertEqual(len({p[1] for p in pictures.values()}), 3, 'each language draws its own tab')


if __name__ == '__main__':
    unittest.main()
