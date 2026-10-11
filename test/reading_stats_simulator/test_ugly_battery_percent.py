"""The battery level of the X3 ugly footer is written beside the battery, where the marks over the keys make room for it."""
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

    def test_hide_modes_show_the_level_beside_the_battery_and_keep_the_reader_strip(self):
        frames = {mode: self.frames(mode) for mode in (0, 1, 2)}
        baseline = os.environ.get('UGLY_BASELINE_SHOTS')
        for mode, (home, reader) in frames.items():
            with self.subTest(mode=mode):
                reference = Image.open(Path(baseline) / f'home-{mode}.png').convert('1') if baseline else frames[2][0]
                difference = ImageChops.difference(home.crop((0, 750, 528, 792)).convert('L'),
                                                   reference.crop((0, 750, 528, 792)).convert('L'))
                if mode == 2:
                    self.assertIsNone(difference.getbbox())
                else:
                    self.assertIsNotNone(difference.getbbox(), 'the battery level is missing')
                if baseline:
                    reference_reader = Image.open(Path(baseline) / f'reader-{mode}.png').convert('1')
                    self.assertEqual(digest(reader.crop((0, 750, 528, 792))),
                                     digest(reference_reader.crop((0, 750, 528, 792))))
                print(f'GREEN mode={mode} footer change bbox={difference.getbbox()}', flush=True)
        footer = (0, 750, 528, 792)
        self.assertEqual(digest(frames[0][0].crop(footer)), digest(frames[1][0].crop(footer)))
        self.assertNotEqual(digest(frames[0][0].crop(footer)), digest(frames[2][0].crop(footer)))


if __name__ == '__main__':
    unittest.main()
