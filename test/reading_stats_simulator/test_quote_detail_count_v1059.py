"""X3, the quotation screen (v1.0.59), read on the pixels of the simulator.

 1. tenor/cross: the count at the head of the screen gives the page only when the quotation runs over more than one
    ("1/3" for a short one, "2/3, 1/3" for a long one), centred on its ink within 0.5 px.
 2. tenor/cross: the two tiers of the Quotations screen under the quotation (all books, then one book) are one pill:
    the bar reads [Stats][...](Quotations), 3 pills.
 3. tenor/ugly: the count at the right end of the path in the foot bar also leaves out the page of a short quotation.
"""
import unittest

from test_quotes_v1011 import write_quote
from ugly_common import Card
import cross_pills

BOOK = '/sach/doi-che.epub'
LONG = ' '.join(['Người hái chè đi chậm qua từng luống, tay nhặt búp non còn ướt sương, mắt nhìn xuống thung lũng mờ khói.'] * 7)
# Newest first in the list: the short one, the long one, another short one.
QUOTES = [('Gió lên rồi.', 1292), (LONG, 1291), ('Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi.', 1290)]
CROSS_TO_DETAIL = ['DOWN'] * 3 + ['RIGHT'] * 3 + ['CONFIRM'] * 3  # Home: Stats, Quotations, the book, its first quotation
UGLY_TO_DETAIL = ['DOWN'] * 4 + ['RIGHT'] * 3 + ['CONFIRM'] * 3   # the diary: the same way
STEP = 1500


def run(shell, keys, shots_after):
    """The screens `shots_after` ms after the last of `keys` and then RIGHT (the next quotation)."""
    card = Card(shell=shell, uiTextSize=0)
    try:
        for i, (text, minute) in enumerate(QUOTES):
            write_quote(card.store / 'quotes', BOOK, 'Mùa hái chè trên đồi', text, 0, i + 1, 20260919, minute)
        script = ['%d:%s' % (STEP * (i + 1), k) for i, k in enumerate(keys)]
        first = STEP * len(keys) + shots_after
        script += ['%d:RIGHT' % (first + 500), '%d:QUIT' % (first + 500 + shots_after + 700)]
        names = ('quote-count-%d-short' % shell, 'quote-count-%d-long' % shell)
        log, shots = card.run(';'.join(script), [(first, names[0]), (first + 500 + shots_after, names[1])], timeout=120,
                              CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_UGLY_SCRAP='0')
    finally:
        card.close()
    return shots[names[0]].convert('L'), shots[names[1]].convert('L'), log


def ink_x(im, x0, x1, y0, y1):
    px = im.load()
    xs = [x for x in range(x0, x1) for y in range(y0, y1) if px[x, y] < 128]
    return (min(xs), max(xs)) if xs else None


class QuoteDetailCountTest(unittest.TestCase):
    def test_1_cross_count_and_bar(self):
        short, long, log = run(0, CROSS_TO_DETAIL, 1600)
        self.assertIn('screen=QuoteDetail', log)
        for name, im, wide in (('short', short, False), ('long', long, True)):
            left, right = ink_x(im, 0, 528, 12, 44)
            width, centre = right - left + 1, (left + right) / 2
            with self.subTest(name, check='page'):
                if wide:
                    self.assertGreater(width, 40, f'the page of a long quotation is missing: {left}..{right}')
                else:
                    self.assertLess(width, 30, f'the page of a one page quotation is written: {left}..{right}')
            with self.subTest(name, check='centre'):
                self.assertLessEqual(abs(centre - 263.5), 0.5, f'the count is off the centre: {left}..{right}')
            with self.subTest(name, check='bar'):
                pills = cross_pills.find(im)
                self.assertEqual(len(pills), 3, f'the bar should be [Stats][...](Quotations): {pills}')

    def test_2_ugly_count_in_the_path(self):
        short, long, log = run(1, UGLY_TO_DETAIL, 2200)
        self.assertIn('QuoteDetail', log)
        a, b = ink_x(short, 380, 528, 720, 750), ink_x(long, 380, 528, 720, 750)
        self.assertTrue(a and b, 'the count at the right end of the path')
        # "1/3" against "2/3, 1/3": the page part makes the long one wider by about the width of the short one.
        self.assertGreater((b[1] - b[0]) - (a[1] - a[0]), 30, f'the page of a one page quotation is written: {a} {b}')


if __name__ == '__main__':
    unittest.main()
