"""tenor/ugly on the X3 desk (v1.0.58, GitHub issue 3 follow up), read on the pixels of the simulator.

Settings are the reporter's: English, 12 hour clock, "Clock left, Battery right", battery percentage shown, text size
tiers 0, 1 and 2, the straight (0) and the turned (1) hand.
The pen ring round the folder and the favorites keeps its lower arc under the whole middle of it, 6 px or more above
the line at the foot.
"""
import unittest

from test_ugly_issue3_v1058 import bands, shot


class UglyDeskRingTest(unittest.TestCase):
    def test_1_the_ring_round_the_folder_and_the_favorites_is_whole_and_clear_of_the_line_at_the_foot(self):
        for level in (0, 1):
            for tier in (0, 1, 2):
                for name, keys, columns, middle in (('folder', ['DOWN'], (0, 280), 138), ('favorites', ['DOWN', 'DOWN'], (300, 528), 401)):
                    with self.subTest(level=level, tier=tier, ring=name):
                        im, acts = shot(['RIGHT', 'RIGHT', 'CONFIRM'] + keys, tier, level)
                        self.assertEqual(acts[-1], 'UglyDesk')
                        runs = bands(im, columns[0], columns[1], 672, 760)
                        # the words of the line at the foot stand in the left half, under the folder, not under the favorites
                        tip = runs[1][0] if name == 'folder' else bands(im, 0, 260, 702, 760)[0][0]
                        # the lower arc is there under the whole middle of the ring: a stretch of it wiped by the line
                        # at the foot leaves columns with no ink within 10 rows of the ring's lowest row
                        low = runs[0][1]
                        px = im.load()
                        bare = [x for x in range(middle - 45, middle + 46) if not any(px[x, y] < 128 for y in range(low - 10, low + 1))]
                        self.assertEqual(bare, [], f'the ring is cut at the foot, columns without its arc: {bare[:3]}..{bare[-3:]}')
                        # and it clears the label: a white row between the word and the arc under it, in the middle (at size 2
                        # the line at the foot stands 17 px under the labels, too near for a ring of 3 px with a gap on both sides)
                        if tier < 2:
                            self.assertGreaterEqual(len(bands(im, middle - 25, middle + 25, 676, low)), 2, 'the ring touches the label')
                        self.assertGreaterEqual(tip - runs[0][1] - 1, 6, f'ring to the line at the foot: ring to row {runs[0][1]}, line from row {tip}')


if __name__ == '__main__':
    unittest.main()
