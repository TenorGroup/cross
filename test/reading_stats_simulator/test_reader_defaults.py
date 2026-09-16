"""Reader defaults, legacy migration and direct three-choice cycling on the native firmware."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))


class ReaderDefaultsTest(unittest.TestCase):
    def run_settings(self, settings, key=None, clicks=0):
        with tempfile.TemporaryDirectory(prefix='reader-defaults-') as folder:
            sd = Path(folder)
            store = sd / '.crosspoint'
            store.mkdir()
            settings = dict(settings, language='VI', uiTheme=4)
            (store / 'settings.json').write_text(json.dumps(settings))
            script = '1500:QUIT'
            if key:
                (store / 'menu-customization.json').write_text(json.dumps(dict(
                    version=1, tabs=dict(home=[0,1,4,2,3], settings=list(range(7)),
                                         reader=list(range(4)), text=list(range(4))), pins=['text/' + key])))
                script = '400:DOWN;800:DOWN;1200:CONFIRM;'
                script += ''.join(f'{1800+i*600}:CONFIRM;' for i in range(clicks))
                script += f'{2300+clicks*600}:QUIT'
            env = {k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
            result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn('page buffer slots full', result.stdout + result.stderr)
            saved = json.loads((store / 'settings.json').read_text())
            if key:
                self.assertIn('Entering activity: TextSettings', result.stdout + result.stderr)
            return saved

    def test_defaults(self):
        actual = self.run_settings({}, 'lineSpacing', 2)
        for key, expected in dict(fontSize=16, lineSpacing=1, extraParagraphSpacing=0, paragraphAlignment=0,
                                  paragraphIndent=1, letterSpacing=1, focusReadingEnabled=1,
                                  hyphenationEnabled=0, embeddedStyle=1, textAntiAliasing=1, readerInkWeight=0).items():
            self.assertEqual(actual[key], expected, key)

    def test_existing_preferences_survive(self):
        original = dict(fontSize=22, sdFontFamilyName='', lineSpacing=0, extraParagraphSpacing=2,
                        textSpacingVersion=2, paragraphIndentVersion=1, paragraphIndent=2,
                        letterSpacing=0, focusReadingEnabled=0, hyphenationEnabled=1,
                        embeddedStyle=0, textAntiAliasing=0, readerInkWeight=2)
        actual = self.run_settings(original, 'lineSpacing', 2)
        for key, value in original.items():
            self.assertEqual(actual[key], value, key)

    def test_legacy_paragraph_spacing(self):
        for version, old, expected in [(None,False,0),(None,True,1),(1,0,0),(1,1,0),(1,2,1),(2,0,0),(2,1,1),(2,2,2)]:
            with self.subTest(version=version, old=old):
                cfg=dict(extraParagraphSpacing=old)
                if version is not None: cfg['textSpacingVersion']=version
                actual=self.run_settings(cfg)
                self.assertEqual(actual['extraParagraphSpacing'], expected)
                self.assertEqual(actual['textSpacingVersion'], 2)

    def test_legacy_extra_wide_line_becomes_wide(self):
        self.assertEqual(self.run_settings(dict(lineSpacing=3))['lineSpacing'], 2)

    def test_line_cycles_directly_and_wraps(self):
        for clicks, expected in [(0,2),(1,0),(2,1)]:
            self.assertEqual(self.run_settings(dict(lineSpacing=1), 'lineSpacing', clicks)['lineSpacing'], expected)

    def test_paragraph_cycles_directly_and_wraps(self):
        for clicks, expected in [(0,1),(1,2),(2,0)]:
            cfg=dict(extraParagraphSpacing=0, textSpacingVersion=2)
            self.assertEqual(self.run_settings(cfg, 'extraParagraphSpacing', clicks)['extraParagraphSpacing'], expected)


if __name__ == '__main__':
    unittest.main()
