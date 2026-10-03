"""The Tenor sleep art on an X3 without absolute gray planes (the UC8253 X3), v1.0.14.

That panel cannot take the two gray planes the Tenor screen normally uses, so the screen draws
fallback art: one black and white frame, kept in flash compressed, the smallest form of it; flash
on the BLE build is nearly full. Since v1.0.14 that frame is the tenor/cross picture itself: the
gray sleep planes folded to black and white exactly as the UC8279 X3 folds them at sleep
(SleepGrayPlanes::show), so both X3 panels show the same picture. It is compared with the frame
the UC8279 path leaves on the glass, fixtures/sleep_golden/bw-8-0.png (test_sleep_ends_bw.py).

The art it replaced had a line of text on a white field along its foot, under a hairline. The
check for that line looks at the same two places of the frame, so it needs no copy of the old art.

The simulator offers absolute gray planes on every panel; CROSSPOINT_SIM_NO_ABSOLUTE_GRAY makes
the Tenor screen take its fallback, as the UC8253 X3 does. Set SLEEP_BW_SHOTS to a directory to
keep the screenshots as PNG.
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
GOLDEN = Path(__file__).resolve().parent / 'fixtures/sleep_golden/bw-8-0.png'
SHOTS = os.environ.get('SLEEP_BW_SHOTS')
ART = REPO / 'src/components/ManNguTenor.h'
# Where the replaced art carried its line of text along the foot: left and right of the line.
FOOT_TEXT = ((50, 700, 170, 732), (300, 700, 480, 732))


def foot_text_line(image):
    """True when either place is a light field with marks on it: a line of text on white."""
    for box in FOOT_TEXT:
        pixels = list(image.crop(box).getdata())
        white = sum(1 for p in pixels if p == 255) / len(pixels)
        if 0.6 < white < 0.98:
            return True
    return False


class TenorFallbackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='cross-tenor-fallback-')
        cls.root = Path(cls.temp.name)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def sleep(self, name, settings, from_book):
        sd = self.root / name
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (store / 'settings.json').write_text(json.dumps(truoc_tenor(dict({'language': 'EN', 'sleepScreen': 8}, **settings))))
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
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
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
        part, image = self.sleep('mot-khung', {}, False)
        self.assertEqual(image.size, (528, 792))
        self.assertEqual(sum(1 for p in image.getdata() if 0 < p < 255), 0)
        self.assertGreater(sum(1 for p in image.getdata() if p == 0), 10000)
        self.assertEqual(re.findall(r'displayBuffer, mode=\d', part)[-1], 'displayBuffer, mode=0')

    def test_same_picture_as_the_gray_panel(self):
        # Home and a book held sideways (the reader turns the screen back upright on the way out),
        # switch on and off: always the tenor/cross picture the UC8279 X3 folds at sleep, with no
        # line of text along its foot.
        golden = Image.open(GOLDEN).convert('L')
        self.assertFalse(foot_text_line(golden))
        for settings in ({'sleepBwFold': 1}, {'sleepBwFold': 0}):
            for from_book in (False, True):
                with self.subTest(settings=settings, from_book=from_book):
                    extra = dict(settings, orientation=1) if from_book else settings
                    label = f'{"sach-ngang" if from_book else "home"}-{settings["sleepBwFold"]}'
                    image = self.sleep(label, extra, from_book)[1]
                    self.assertFalse(foot_text_line(image), f'{label}: a line of text along the foot')
                    self.assertEqual(golden.size, image.size)
                    diff = ImageChops.difference(golden, image)
                    self.assertIsNone(diff.getbbox(), f'{label}: pixels differ in {diff.getbbox()}')


if __name__ == '__main__':
    unittest.main()
