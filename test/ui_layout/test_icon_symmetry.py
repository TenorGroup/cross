"""The open-book icon is the same left and right of its spine, pixel for pixel (founder, 04/10).

At 40 px with a selected tab's stroke 4 the two pages came out unequal. Checked on what the generator draws
at every size and stroke the set uses (40 px stroke 3 and 4, 32 and 24 px stroke 2), and on the bitmaps
written to the headers the firmware includes.
"""
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'scripts/icons'))
from net import G      # noqa: E402
import hinh            # noqa: E402

SIZES = [(40, 3), (40, 4), (32, 2), (24, 2)]


def drawn(n, w):
    g = G(n, w)
    hinh.H['doc'][1](g)
    im = g.img().convert('L')
    return [[im.getpixel((x, y)) < 128 for x in range(n)] for y in range(n)]


def unequal(rows):
    n = len(rows)
    return [(x, y) for y in range(n) for x in range(n // 2) if rows[y][x] != rows[y][n - 1 - x]]


def from_header(name, n):
    text = (REPO / 'src/components/icons/tenorReaderTabIcons.h').read_text()
    body = re.search(r'icon_%s_%d_bits\[\] = \{(.*?)\};' % (name, n), text, re.S).group(1)
    data = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', body)]
    stride = (n + 7) // 8
    return [[not (data[y * stride + x // 8] >> (7 - x % 8)) & 1 for x in range(n)] for y in range(n)]


class DocSymmetry(unittest.TestCase):
    def test_generator_mirrors_every_pixel_at_every_size(self):
        for n, w in SIZES:
            with self.subTest(n=n, stroke=w):
                rows = drawn(n, w)
                self.assertTrue(any(any(r) for r in rows), 'blank icon')
                self.assertEqual(unequal(rows), [], f'{n} px stroke {w}: pages differ')

    def test_headers_on_disk_mirror_every_pixel(self):
        for name in ('tenor_reader_reading', 'tenor_reader_reading_bold'):
            with self.subTest(name=name):
                self.assertEqual(unequal(from_header(name, 40)), [], f'{name}: pages differ')


if __name__ == '__main__':
    unittest.main()
