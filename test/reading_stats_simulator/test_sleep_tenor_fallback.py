"""The Tenor sleep art on an X3 without absolute gray planes (the UC8253 X3), v1.0.13.

That panel cannot take the two gray planes the Tenor screen normally uses, so the screen draws
fallback art: the same picture as one black and white frame with its gray levels already dotted.
It is kept in flash as one compressed frame, the smallest form of it; flash on the BLE build is
nearly full. The frame must stay what it was, pixel for pixel.

The simulator offers absolute gray planes on every panel; CROSSPOINT_SIM_NO_ABSOLUTE_GRAY makes
the Tenor screen take its fallback, as the UC8253 X3 does. Set SLEEP_BASE_PROGRAM to an earlier
build of the same simulator (with that switch) to compare the frames; TEST_PROGRAM is this one.
Set SLEEP_BW_SHOTS to a directory to keep the screenshots as PNG.
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

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BASE = os.environ.get('SLEEP_BASE_PROGRAM')
SHOTS = os.environ.get('SLEEP_BW_SHOTS')
ART = REPO / 'src/components/ManNguTenor.h'


class TenorFallbackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='cross-tenor-fallback-')
        cls.root = Path(cls.temp.name)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def sleep(self, program, name, settings, from_book):
        sd = self.root / name
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (store / 'settings.json').write_text(json.dumps(dict({'language': 'EN', 'sleepScreen': 8}, **settings)))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
        script = '4200:SLEEP;9000:QUIT'
        if from_book:
            shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', sd / 'book.epub')
            (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Book'}]}))
            script = '1000:CONFIRM;' + script
        times = [4200 + 50 * i for i in range(1, 80)]
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_NO_ABSOLUTE_GRAY='1',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{sd / f"shot-{t}.bmp"}' for t in times))
        run = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering deep sleep', log)
        part = log.split('Entering activity: Sleep', 1)[1].split('Entering deep sleep', 1)[0]
        if from_book:
            self.assertIn('Entering activity: EpubReader', log)
        self.assertIn('Sleep screen mode=8,', part)
        self.assertNotIn('[BRAND] sleep ready=', part)  # the fallback, not the gray planes
        taken = [sd / f'shot-{t}.bmp' for t in times if (sd / f'shot-{t}.bmp').exists()]
        self.assertTrue(taken, 'no screenshot taken')
        image = Image.open(taken[-1]).convert('L')
        if SHOTS:
            Path(SHOTS).mkdir(parents=True, exist_ok=True)
            image.save(Path(SHOTS) / f'{name}.png')
        return part, image

    def test_fallback_art_is_one_packed_frame(self):
        # The run-length art took 21008 bytes of flash; the dotted frame compresses to under 5 KB.
        text = ART.read_text()
        self.assertNotIn('DU_LIEU', text)
        match = re.search(r'KHUNG\[\] = \{(.*?)\};', text, re.S)
        self.assertIsNotNone(match)
        self.assertLess(len(re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))), 5000)

    def test_fallback_frame_is_a_dotted_bw_picture(self):
        part, image = self.sleep(PROGRAM, 'mot-khung', {}, False)
        self.assertEqual(image.size, (528, 792))
        self.assertEqual(sum(1 for p in image.getdata() if 0 < p < 255), 0)
        self.assertGreater(sum(1 for p in image.getdata() if p == 0), 10000)
        self.assertEqual(re.findall(r'displayBuffer, mode=\d', part)[-1], 'displayBuffer, mode=0')

    @unittest.skipUnless(BASE, 'set SLEEP_BASE_PROGRAM to an earlier build')
    def test_same_pixels_as_base(self):
        # Home and a book held sideways (the reader turns the screen back upright on the way out),
        # switch on and off.
        for settings in ({}, {'sleepBwRefresh': 0}):
            for from_book in (False, True):
                case = dict(settings, from_book=from_book, orientation=1 if from_book else 0)
                with self.subTest(**case):
                    extra = dict(settings, orientation=1) if from_book else settings
                    label = f'{"sach-ngang" if from_book else "home"}-{len(settings)}'
                    images = [self.sleep(program, f'{label}-{side}', extra, from_book)[1]
                              for side, program in (('truoc', Path(BASE)), ('sau', PROGRAM))]
                    self.assertEqual(images[0].tobytes(), images[1].tobytes())


if __name__ == '__main__':
    unittest.main()
