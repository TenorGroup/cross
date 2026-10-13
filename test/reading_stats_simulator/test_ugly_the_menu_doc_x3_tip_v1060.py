"""tenor/ugly on the X3: the list reader menu keeps its tip on the Favorites tab, over the bar of the names.

Founder, v1.0.60: the Favorites tab showed no tip line ("On any tab, hold ... to pin"). The tip was drawn with the
tab's content, and the ugly menu then cleared the screen and wrote its own paper, names and rows over a blank page.
One run per tab (Favorites, Place, Read, Tools, Text), the book opened, the menu opened, the side key to the next tab.
 1. Favorites: the tip's ink stands over the bar, under the paper's foot line, and the paper's sides end over the tip.
 2. The other 4 tabs have no tip: they do not change with the Tips switch, and their paper runs down to the bar.
 3. Tips off (the Tips switch): Favorites has no tip and the paper runs down to the bar like the other tabs.
 4. The tip ends over the bar's circle and names (6 rows blank between them).
TEST_PROGRAM picks the simulator (simulator_x3 is the UC8253 panel, simulator_x3_uc8279 the other one).
"""
import json
import unittest

import numpy as np

from ugly_common import Card
from test_ugly_doc_sach import write_epub

TABS = 5
MIDDLE = slice(30, 498)
LEFT, RIGHT = slice(12, 23), slice(503, 516)   # where the paper's side lines run
BAR_TOP = 706                                    # the bar's strip starts here (the circle from 712)


def shots(**settings):
    """The screen after the menu opened, then after each side key: [TABS arrays of ink]."""
    card = Card(shell=1, books=[], stats=False, textAntiAliasing=0, readerMenuStyle=0, **settings)
    try:
        write_epub(card.sd / 'k.epub', 'Kidnapped', 8)
        (card.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/k.epub', 'title': 'Kidnapped', 'author': 'RLS'}]}))
        keys = [(1000, 'CONFIRM'), (3200, 'CONFIRM')] + [(4900 + 1500 * i, 'DOWN') for i in range(TABS - 1)]
        at = [(4600 + 1500 * i, 't%d' % i) for i in range(TABS)]
        _, images = card.run(';'.join('%d:%s' % k for k in keys) + ';%d:QUIT' % (at[-1][0] + 800), at, timeout=150,
                             CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_UGLY_SCRAP='0')
    finally:
        card.close()
    return [np.asarray(images['t%d' % i].convert('L')) < 128 for i in range(TABS)]


def foot_line(page):
    """The row of the paper's foot line: the row with the most ink between the rows 560 and the bar (it is a long line)."""
    rows = page[560:BAR_TOP, MIDDLE].sum(axis=1)
    return 560 + int(np.argmax(rows)), int(rows.max())


def under(page, foot):
    """Ink under the foot line and over the bar: the tip's, and the paper's side lines beside it."""
    rows = slice(foot + 4, BAR_TOP)
    return int(page[rows, MIDDLE].sum()), int(page[rows, LEFT].sum() + page[rows, RIGHT].sum())


class UglyReaderMenuTipTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tips = shots()
        cls.quiet = shots(tipsEnabled=0)

    def test_1_favorites_keeps_its_tip_over_the_bar(self):
        foot, length = foot_line(self.tips[0])
        self.assertGreater(length, 150, 'no foot line of the paper')
        self.assertLess(foot, 660, f'the paper runs down to row {foot}, the tip has no room')
        tip, sides = under(self.tips[0], foot)
        self.assertGreater(tip, 400, 'the tip under the paper on Favorites')
        self.assertLess(sides, 40, 'the paper runs down beside the tip')

    def test_2_the_other_tabs_have_no_tip(self):
        for tab in range(1, TABS):
            with self.subTest(tab=tab):
                self.assertTrue(np.array_equal(self.tips[tab], self.quiet[tab]), 'a tab that has no tip changed with the Tips switch')
                foot, _ = foot_line(self.tips[tab])
                self.assertGreaterEqual(foot, 698, f'the paper ends at row {foot}, short of the bar')

    def test_3_tips_off_leaves_no_tip_and_the_full_paper(self):
        foot, _ = foot_line(self.quiet[0])
        self.assertGreaterEqual(foot, 698, f'the paper ends at row {foot}, short of the bar')
        self.assertEqual(under(self.quiet[0], foot)[0], 0)

    def test_4_the_tip_ends_over_the_bar(self):
        self.assertEqual(int(self.tips[0][BAR_TOP:BAR_TOP + 6, MIDDLE].sum()), 0, 'ink in the 6 rows over the circle and the names')


if __name__ == '__main__':
    unittest.main()
