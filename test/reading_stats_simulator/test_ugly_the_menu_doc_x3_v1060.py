"""tenor/ugly on the X3: the list reader menu (Reader Menu Style = List) writes the names of its 5 tabs in the bar at the foot.

GitHub issue 3 (v1.0.58): the bar stood empty, the 5 names were gone and only the path words "Menu doc sach" were left
there. The tab strip took the height of a strip at the head of the screen, which is negative for a bar at the foot.
One run per tab (Favorites, Place, Read, Tools, Text), the book opened, the menu opened, the side key to the next tab.
 1. 5 names stand in the bar (y 706 to 758), each its own run of ink, none of them the path words.
 2. The gaps between the names are equal (3 px of the hand's own wobble at most).
 3. The circle goes round the chosen name only: its centre is the name's centre, it stays clear of the paper's foot
    line above and of the button hints and battery row below.
TEST_PROGRAM picks the simulator (simulator_x3 is the UC8253 panel, simulator_x3_uc8279 the other one).
"""
import json
import unittest

import numpy as np

from ugly_common import Card
from test_ugly_doc_sach import write_epub

TABS = 5
BAR = (706, 759)  # rows of the bar's strip: under the paper's foot line (704), over the button hints (760)


def shots():
    """The strip of the screen after the menu opened, then after each side key: [TABS arrays of ink]."""
    card = Card(shell=1, books=[], stats=False, textAntiAliasing=0, readerMenuStyle=0)
    try:
        write_epub(card.sd / 'k.epub', 'Kidnapped', 8)
        (card.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/k.epub', 'title': 'Kidnapped', 'author': 'RLS'}]}))
        keys = [(1000, 'CONFIRM'), (3200, 'CONFIRM')] + [(4900 + 1500 * i, 'DOWN') for i in range(TABS - 1)]
        at = [(4600 + 1500 * i, 't%d' % i) for i in range(TABS)]
        _, images = card.run(';'.join('%d:%s' % k for k in keys) + ';%d:QUIT' % (at[-1][0] + 800), at, timeout=120,
                             CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_UGLY_SCRAP='0')
    finally:
        card.close()
    return [np.asarray(images['t%d' % i].convert('L'))[BAR[0]:BAR[1]] < 128 for i in range(TABS)]


def names(strips):
    """The ink every screen has (the names; the circle is in one screen only) as runs of columns, the words' own gaps (under 18 px) closed."""
    common = np.sum(strips, axis=0) >= TABS - 1
    columns = common.any(axis=0)
    runs, start = [], None
    for x, on in enumerate(list(columns) + [False]):
        if on and start is None:
            start = x
        if not on and start is not None:
            if runs and start - runs[-1][1] - 1 < 18:
                runs[-1][1] = x - 1
            else:
                runs.append([start, x - 1])
            start = None
    return common, runs


class UglyReaderMenuTabsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.strips = shots()
        cls.common, cls.runs = names(cls.strips)

    def test_1_five_names_stand_in_the_bar(self):
        self.assertEqual(len(self.runs), TABS, f'the names in the bar: {self.runs}')
        for a, b in self.runs:
            self.assertGreaterEqual(int(self.common[:, a:b + 1].sum()), 60, f'a name of ink at x {a} to {b}')
        # the path words of the old bar stood left, 24 to 170: the first name is not them but all the names go to the right end
        self.assertGreaterEqual(self.runs[-1][1], 440, f'the last name ends at {self.runs[-1][1]}')

    def test_2_the_gaps_between_the_names_are_equal(self):
        gaps = [self.runs[i + 1][0] - self.runs[i][1] - 1 for i in range(len(self.runs) - 1)]
        self.assertEqual(len(gaps), TABS - 1)
        self.assertLessEqual(max(gaps) - min(gaps), 3, f'gaps {gaps}')

    def test_3_the_circle_goes_round_the_chosen_name_and_clears_the_rows_around_it(self):
        for tab, strip in enumerate(self.strips):
            with self.subTest(tab=tab):
                ring = strip & ~self.common
                ys, xs = np.nonzero(ring)
                self.assertGreater(len(xs), 120, 'the circle')
                a, b = self.runs[tab]
                self.assertLessEqual(abs((xs.min() + xs.max()) / 2 - (a + b) / 2), 4, f'circle x {xs.min()} to {xs.max()}, name {a} to {b}')
                self.assertGreaterEqual(ys.min() + BAR[0], 710, 'clear of the paper line above')
                self.assertLessEqual(ys.max() + BAR[0], 757, 'clear of the button hints and the battery below')
                # the other 4 names are left whole: no ink of the circle over them
                for other, (c, d) in enumerate(self.runs):
                    if other != tab:
                        self.assertEqual(int(ring[:, c:d + 1].sum()), 0, f'the circle of tab {tab} over name {other}')


if __name__ == '__main__':
    unittest.main()
