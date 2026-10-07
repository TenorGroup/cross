"""X3 lists (founder 07/10): the heart of a pinned row, in the left margin, stands on the middle of the row's
capitals, at every interface text size; it sat low, its foot 2 px over the baseline.

A folder pinned on the File card (Home, Down, Select held): the heart against the folder name's capital "T".
Run with TEST_PROGRAM (the X3 simulator) and MENU_TEST_OUTPUT as in test_menu_customization.py.
"""
import shutil
import time
import unittest

from PIL import Image

import test_menu_customization as t

t.o = t.o / ('tim-le-' + str(time.time_ns()))
t.o.mkdir(parents=True)


def heart_and_capital(path):
    img = Image.open(path).convert('L')
    px = img.load()
    dark = lambda x, y: px[x, y] < 128
    heart = [y for y in range(60, img.height - 60) if any(dark(x, y) for x in range(12, 23))]
    assert heart, 'no heart in the margin'
    middle = (heart[0] + heart[-1]) // 2
    band = range(middle - 22, middle + 22)
    first = next(x for x in range(26, 200) if any(dark(x, y) for y in band))
    capital = [y for y in band if any(dark(x, y) for x in range(first, first + 6))]
    return (heart[0] + heart[-1]) / 2, (capital[0] + capital[-1]) / 2


class MarginHeartTest(unittest.TestCase):
    def test_the_margin_heart_is_on_the_middle_of_the_capital(self):
        self.assertEqual(check(), [])


def check():
    failures = []
    for size in (0, 2):
        label = f'tim-{size}'
        sd = t.o / ('sd-' + label)
        (sd / 'Thu muc').mkdir(parents=True)
        shutil.copy(t.r / 'test/epubs/test_kerning_ligature.epub', sd / 'Thu muc/sach.epub')
        t.run(label, '1000:DOWN;1700:CONFIRM:900;3400:QUIT', [(2900, 'pin')], settings={'uiTextSize': size})
        heart, capital = heart_and_capital(t.o / f'{label}-pin.png')
        if abs(heart - capital) > 1:
            failures.append(f'size {size}: the heart is {heart - capital:+.1f} px off the middle of the capital')
    return failures


if __name__ == '__main__':
    unittest.main()
