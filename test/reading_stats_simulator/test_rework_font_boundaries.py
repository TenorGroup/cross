"""Exercise real font discovery, picker boundaries and no-change reader returns."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'


class ReworkFontBoundariesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if importlib.util.find_spec('freetype') is None:
            raise unittest.SkipTest('Install freetype-py for the cpfont fixture converter')
        cls.font_temp = tempfile.TemporaryDirectory(prefix='cross-audit-font-')
        cls.addClassCleanup(cls.font_temp.cleanup)
        cls.font = Path(cls.font_temp.name) / 'audit_14.cpfont'
        subprocess.run([sys.executable, str(REPO / 'lib/EpdFont/scripts/fontconvert_sdcard.py'),
                        '--regular', str(REPO / 'lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf'),
                        '--size', '14', '--intervals', 'ascii', '-o', str(cls.font)], check=True,
                       capture_output=True, text=True)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cross-font-boundary-')
        self.addCleanup(self.temp.cleanup)
        self.sd = Path(self.temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        shutil.copyfile(REPO / 'test/epubs/test_kerning_ligature.epub', self.sd / 'books/sach.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/sach.epub', 'title': 'Audit'}]}))
        self.settings = dict(language='VI', fontSize=14, sdFontFamilyName='', sleepTimeout=10)

    def fonts(self, total):
        for i in range(total - 2):
            name = f'Audit{i:03}'
            folder = self.sd / '.fonts' / name
            folder.mkdir(parents=True)
            shutil.copyfile(self.font, folder / f'{name}_14.cpfont')

    def run_sim(self, script):
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=35)
        log = run.stdout + run.stderr
        output = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            (Path(output) / (self._testMethodName + '.log')).write_text(log)
        self.assertEqual(run.returncode, 0, log)
        return json.loads((self.store / 'settings.json').read_text()), log

    READING = '1000:CONFIRM;1700:CONFIRM;8000:CONFIRM;9000:DOWN;9700:DOWN;10400:CONFIRM'

    def check_font_boundary(self, total):
        self.fonts(total)
        saved, log = self.run_sim(self.READING + ';11200:RIGHT;11900:CONFIRM;14800:LEFT;15500:LEFT;'
                                 '16400:CONFIRM;18000:BACK;21000:QUIT')
        self.assertIn(f'SD font system ready ({total - 2} families discovered)', log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(saved['sdFontFamilyName'], f'Audit{total - 3:03}', log)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)

    def test_16_families_all_reachable(self):
        self.check_font_boundary(16)

    def test_17_families_all_reachable(self):
        self.check_font_boundary(17)

    def test_130_families_last_can_be_applied(self):
        self.check_font_boundary(130)

    def test_single_size_returns_without_reloading_section(self):
        self.fonts(3)
        self.settings['sdFontFamilyName'] = 'Audit000'
        saved, log = self.run_sim(self.READING + ';11400:CONFIRM;14500:QUIT')
        self.assertEqual(saved['fontSize'], 14)
        self.assertEqual(saved['sdFontFamilyName'], 'Audit000')
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)
        resumed = log.split('Exiting activity: EpubReaderMenu', 1)[1]
        self.assertNotIn('Cache found, skipping build', resumed, log)
        self.assertNotIn('Cache not found, building', resumed, log)

    def test_same_popup_size_then_back_preserves_section(self):
        saved, log = self.run_sim(self.READING + ';11400:CONFIRM;13000:CONFIRM;14000:BACK;16500:QUIT')
        self.assertEqual(saved['fontSize'], 14)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)
        resumed = log.split('Exiting activity: EpubReaderMenu', 1)[1]
        self.assertNotIn('Cache found, skipping build', resumed, log)
        self.assertNotIn('Cache not found, building', resumed, log)


if __name__ == '__main__':
    unittest.main()
