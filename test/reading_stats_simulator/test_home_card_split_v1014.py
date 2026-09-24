"""v1.0.14 Recent card file: the cover holds when only the text changed.

Leaving a book on a new page changes the card's excerpt. The card file used to be keyed on the
cover and the text together, so the whole card was drawn again, cover decode included (about 210 of
the 333 ms on the X3). The file now keys the cover apart: a card whose text changed puts its saved
cover back and lays out only the text. The result must match a card drawn from nothing pixel for
pixel. Screenshots land in CROSSPOINT_TEST_ARTIFACTS when it is set.
"""
import re
import unittest

import test_home_card_file_v1013 as v1013
from test_home_card_file_v1013 import BOOKS, CARD, COVER, TEXT
from test_home_recent_v1011 import cover

KEPT = re.compile(r'Recent card build=\d+ms cache=\d+ cover=(\d+) kept=(\d)')


class HomeCardSplitTest(v1013.HomeCardFileTest):
    def test_new_excerpt_keeps_the_saved_cover(self):
        _, first = self.launch('first-', self.WALK, self.SHOTS)
        self.books[0]['excerpt'] = 'Buổi sáng hôm ấy cả làng ra bãi, thuyền lớn thuyền nhỏ nằm san sát.'
        self.write_recent()
        log, after = self.launch('after-', self.WALK, self.SHOTS)
        builds = KEPT.findall(log)
        # The first book: text laid out again over its saved 356 px cover. The others read whole.
        self.assertEqual(builds, [('356', '1')], log[-4000:])
        self.assertFalse(self.same(first['a'], after['a'], TEXT), 'new excerpt not drawn')
        self.assertTrue(self.same(first['a'], after['a'], COVER))
        # The same card drawn from nothing, with no card files on the card.
        for path in self.store.glob('home-covers/*.card*'):
            path.unlink()
        log, fresh = self.launch('fresh-', self.WALK, self.SHOTS)
        self.assertEqual([kept for _, kept in KEPT.findall(log)], ['0', '0', '0'], log[-4000:])
        for shot in ('a', 'b', 'c'):
            self.assertTrue(self.same(after[shot], fresh[shot], CARD), f'card {shot} differs from a fresh build')

    def test_cover_change_still_draws_the_whole_card(self):
        self.launch('first-', self.WALK, self.SHOTS)
        # The third book got its thumbnail since: its saved cover is stale, text or not.
        cover(self.thumb(self.books[2]['coverBmpPath']), BOOKS[2][1], 356)
        self.books[2]['excerpt'] = 'Chiều xuống, sương tan dần trên đồi chè.'
        self.write_recent()
        log, _ = self.launch('after-', self.WALK, self.SHOTS)
        self.assertEqual(KEPT.findall(log), [('356', '0')], log[-4000:])


# The v1.0.13 cases run from their own module.
for name in [n for n in vars(v1013.HomeCardFileTest) if n.startswith('test_')]:
    setattr(HomeCardSplitTest, name, None)


if __name__ == '__main__':
    unittest.main()
