"""tenor/ugly on the X3, the quotation screen (v1.0.58, GitHub issue 3 follow up), read on the pixels of the simulator.

Settings are the reporter's: English, 12 hour clock, "Clock left, Battery right", battery percentage shown, text size
tiers 0, 1 and 2, the straight (0) and the turned (1) hand.
The facts under the quote (book, chapter, moment, page) are centred under it with air between the lines, the count
stands at the right end of the path in the foot bar (as on the answer sheet) and the top left is empty, and no
tenor/cross pill is drawn.
"""
import unittest

from test_quotes_v1011 import write_quote
from test_ugly_issue3_v1058 import bands, shot
import cross_pills

QUOTES = [('/sach/eros.epub', 'Eros the Bittersweet', 28, 8, 20261011, 17 * 60 + 12,
           'If ice-pleasure consists, to some degree, in novelty, then ice must melt in order to be desirable.'),
          ('/sach/dd.epub', 'A very long title of a book that wraps onto several lines of the page', 2, 120, 20261010, 9 * 60 + 5,
           'Short words.'),
          ('/sach/xx.epub', 'Dế Mèn phiêu lưu ký', 1, 3, 20261009, 600,
           'Một đoạn trích ngắn để thử tiếng Việt có dấu.')]
TO_DETAIL = ['DOWN'] * 4 + ['RIGHT'] * 3 + ['CONFIRM'] * 3  # Stats, Quotations, the first book, its first quotation


def seed_quotes(card):
    for path, title, spine, page, day, minute, text in QUOTES:
        write_quote(card.store / 'quotes', path, title, text, spine, page, day, minute)


class UglyQuoteDetailTest(unittest.TestCase):
    def meta_bands(self, im):
        runs = bands(im, 40, 500, 20, 690)  # clear of the margin line of the page (v1.0.59)
        return runs[-4:]

    def test_2_the_facts_under_the_quotation_are_centred_with_air_and_the_count_is_in_the_path(self):
        for level in (0, 1):
            for tier in (0, 1, 2):
                with self.subTest(level=level, tier=tier):
                    im, acts = shot(TO_DETAIL, tier, level, prepare=seed_quotes, env=dict(CROSSPOINT_SIM_UGLY_SCRAP='0'))
                    self.assertEqual(acts[-1], 'QuoteDetail')
                    px = im.load()
                    meta = self.meta_bands(im)
                    for top, bottom in meta:
                        xs = [x for x in range(40, 500) for y in range(top, bottom + 1) if px[x, y] < 128]
                        self.assertLessEqual(abs((min(xs) + max(xs)) / 2 - 263.5), 1.5, f'line {top}..{bottom} is off the centre: {min(xs)}..{max(xs)}')
                    for (a, b), (c, d) in zip(meta, meta[1:]):
                        self.assertGreaterEqual(c - b - 1, 4, f'lines {a}..{b} and {c}..{d} are crowded')
                    self.assertEqual(bands(im, 0, 264, 0, 40), [], 'the count at the top left')
                    foot = bands(im, 380, 528, 690, 748)
                    self.assertTrue(foot, 'the count at the right end of the path in the foot bar')
                    self.assertEqual(cross_pills.find(im), [], 'a tenor/cross pill')


if __name__ == '__main__':
    unittest.main()
