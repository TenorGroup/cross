"""Battery digits stay inside the X3 ugly footer without moving physical key hints."""
import os
from pathlib import Path
import unittest

from PIL import Image, ImageChops

from ugly_common import Card, digest, ink


class UglyBatteryPercentTest(unittest.TestCase):
    def frames(self, mode):
        card = Card(hideBatteryPercentage=mode, statusBarMode=1, readerStatusBarMode=5, statusBarItemsMode=5,
                    statusBarBattery=1, statusBarChapterPageCount=0,
                    statusBarBookProgress=0, statusBarTitle=0)
        self.addCleanup(card.close)
        _, shots = card.run('2800:BACK;7200:QUIT',
                            [(1800, f'home-{mode}'), (6200, f'reader-{mode}')])
        return shots[f'home-{mode}'], shots[f'reader-{mode}']

    def test_hide_modes_keep_footer_hints_and_reader_strip_at_base_pixels(self):
        frames = {mode: self.frames(mode) for mode in (0, 1, 2)}
        baseline = os.environ.get('UGLY_BASELINE_SHOTS')
        hints = (75, 750, 442, 792)
        body = (18, 765, 45, 779)
        for mode, (home, reader) in frames.items():
            with self.subTest(mode=mode):
                reference = Image.open(Path(baseline) / f'home-{mode}.png').convert('1') if baseline else frames[2][0]
                changed = sum(ImageChops.difference(home.crop(hints).convert('L'), reference.crop(hints).convert('L')).histogram()[1:])
                self.assertEqual(changed, 0, f'physical key hints moved: {changed} pixels')
                difference = ImageChops.difference(home.crop((0, 750, 528, 792)).convert('L'),
                                                   reference.crop((0, 750, 528, 792)).convert('L'))
                if mode == 2:
                    self.assertIsNone(difference.getbbox())
                else:
                    bounds = difference.getbbox()
                    self.assertIsNotNone(bounds, 'visible battery digits missing')
                    left, top, right, bottom = bounds
                    self.assertGreaterEqual(left, body[0])
                    self.assertGreaterEqual(top + 750, body[1])
                    self.assertLessEqual(right, body[2])
                    self.assertLessEqual(bottom + 750, body[3])
                if baseline:
                    reference_reader = Image.open(Path(baseline) / f'reader-{mode}.png').convert('1')
                    self.assertEqual(digest(reader.crop((0, 750, 528, 792))),
                                     digest(reference_reader.crop((0, 750, 528, 792))))
                print(f'GREEN mode={mode} hint pixel differences={changed}, footer change bbox={difference.getbbox()}', flush=True)
        self.assertEqual(digest(frames[0][0].crop(body)), digest(frames[1][0].crop(body)))
        self.assertNotEqual(digest(frames[0][0].crop(body)), digest(frames[2][0].crop(body)))


if __name__ == '__main__':
    unittest.main()
