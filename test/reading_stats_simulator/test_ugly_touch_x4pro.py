"""tenor/ugly on the X4 Pro touch screen, in the simulator: whole journeys by finger and Home key.

Founder rules under test (04/10/2026): the three tiers of the X3 (diary, desk, notebook) touched straight;
the Home key and "to the desk" go up one tier; a swipe across turns the notebook page, from the edge too;
a setting of two or three values changes on the tap, four and more open a paper on the row touched; the
Interface row asks once before leaving the shell; an X over a file asks before it bins it, a ring pins.

Needs an X4 Pro simulator build (env simulator_x4pro, TEST_PROGRAM pointing at it) whose script input
can move a finger (PATH and stepped SWIPE); required when this suite runs.
"""
import json
import os
from pathlib import Path
import re
import unittest

import ugly_common
from ugly_common import Card, entered, notebook_pages

PROGRAM = Path(os.environ.get('TEST_PROGRAM', ugly_common.REPO / '.pio/build/simulator_x4pro/program'))
HAVE = PROGRAM.exists() and 'x4pro' in str(PROGRAM)

# The grid of UglyTouch.h: rows of 64 px from y 144, the bottom band 720 to 784 in three cells.
ROW = lambda i: 144 + 64 * i + 32
PREV, BACK_CELL, NEXT = (60, 750), (240, 750), (420, 750)


def frames(log):
    return re.findall(r'Notebook frame page=(\d+) group=(-?\d+) top=(\d+) rows=(\d+) pop=(\d+)', log)


class UglyTouchX4ProTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not HAVE:
            raise AssertionError(f"X4 Pro simulator is required: {PROGRAM}")

    def card(self, **kw):
        card = Card(**kw)
        self.addCleanup(card.close)
        return card

    def run_card(self, card, script, shots=()):
        ugly_common.PROGRAM = PROGRAM
        return card.run(script, shots, timeout=120)

    def test_read_on_and_the_home_key_comes_back_to_the_diary(self):
        log, _ = self.run_card(self.card(), '2000:TAP:120,480;4500:HOME;6500:QUIT')
        self.assertEqual(entered(log), ['Boot', 'UglyDiary', 'TxtReader', 'UglyDiary'], log[-1500:])

    def test_desk_objects_and_one_tier_back_at_a_time(self):
        # the diary's "look at the whole desk" (its last line, under the title the diary has had since rc.2), the clock
        # (Recent), then the Home key twice
        log, _ = self.run_card(self.card(), '2000:TAP:240,670;3500:TAP:240,150;5000:HOME;6500:TAP:240,750;8000:QUIT')
        self.assertEqual(entered(log), ['Boot', 'UglyDiary', 'UglyDesk', 'UglyNotebook', 'UglyDesk', 'UglyDiary'], log[-1500:])
        self.assertEqual(notebook_pages(log), [0])

    def test_a_swipe_across_turns_the_page_even_from_the_edge(self):
        log, _ = self.run_card(self.card(), '2000:TAP:420,750;3500:SWIPE:400,400,80,400,200;5000:SWIPE:6,400,400,400,200;6500:QUIT')
        self.assertEqual(entered(log)[-1], 'UglyNotebook', 'an edge swipe is no Back here')
        self.assertEqual([int(p) for p, *_ in frames(log)], [0, 1, 0])

    def test_settings_tick_on_the_tap_and_open_a_paper_from_four_values(self):
        card = self.card()
        before = card.settings()
        # Settings, the first group, the toggle on row 2, then the six-value row 5 and its fourth value
        log, shots = self.run_card(card, '2000:TAP:60,750;3500:TAP:200,%d;5000:TAP:200,%d;6500:TAP:200,%d;8000:TAP:200,%d;9500:QUIT'
                                   % (ROW(1), ROW(2), ROW(5), ROW(5) + 64), [(7800, 'paper')])
        f = frames(log)
        self.assertEqual(f[1][1], '0', 'the group opens as a page')
        self.assertEqual(f[-2][4], '1', 'a paper of values')
        self.assertEqual(f[-1][4], '0', 'a value picked closes it')
        after = card.settings()
        changed = sorted(k for k in after if after.get(k) != before.get(k))
        self.assertGreaterEqual(len(changed), 2, changed)

    def test_an_edge_button_does_not_turn_the_page_under_an_open_group(self):
        # Settings, the first group, the upper edge button (sideways on the X4 Pro), then a tap on row 0: the tap must
        # land on the group's own row 0, never on a row of a page the key turned to under it.
        log, _ = self.run_card(self.card(), '2000:TAP:60,750;3500:TAP:200,%d;5000:UP;6500:TAP:200,%d;8500:QUIT' % (ROW(1), ROW(0)))
        f = frames(log)
        self.assertEqual({p for p, *_ in f}, {'3'}, 'the Settings page stays while its group is open: %s' % f)
        self.assertTrue(len(f) > 1 and all(g == '0' for _, g, *_ in f[1:]), 'the group stays open: %s' % f)

    def test_the_interface_row_asks_before_it_leaves_the_shell(self):
        card = self.card()
        # Interface is row 6 of the first group; its paper opens over the row: "leave it" next to it, "switch" above
        log, _ = self.run_card(card, '2000:TAP:60,750;3500:TAP:200,%d;5000:TAP:200,%d;6500:TAP:200,%d;8000:QUIT'
                               % (ROW(1), ROW(6), ROW(6) - 64))
        self.assertEqual(frames(log)[-1][4], '0', 'the paper closed')
        self.assertEqual(card.settings()['uiShell'], 1, '"keep it ugly" keeps the shell')
        card2 = self.card()
        log, _ = self.run_card(card2, '2000:TAP:60,750;3500:TAP:200,%d;5000:TAP:200,%d;6500:TAP:200,%d;8500:QUIT'
                               % (ROW(1), ROW(6), ROW(6) - 64 - 88))
        self.assertEqual(card2.settings()['uiShell'], 0, '"switch" leaves for tenor/cross')
        self.assertIn('Entering activity: Home', log)

    def test_an_x_over_a_file_asks_then_bins_it(self):
        card = self.card()
        # desk, the stack of books (the card's root), an X over row 1 (b1.txt), then "bin it"
        script = ('2000:TAP:240,670;3500:TAP:120,620;5000:PATH:60,214,110,240,170,266,200;'
                  '5400:PATH:170,214,120,240,60,268,200;7000:TAP:120,500;9000:QUIT')
        log, _ = self.run_card(card, script)
        self.assertEqual(frames(log)[1][4], '2', 'the question')
        self.assertFalse((card.sd / 'b1.txt').exists(), 'binned')
        self.assertTrue((card.sd / 'b0.txt').exists())

    def test_a_tap_beside_the_question_keeps_the_file(self):
        card = self.card()
        script = ('2000:TAP:240,670;3500:TAP:120,620;5000:PATH:60,214,110,240,170,266,200;'
                  '5400:PATH:170,214,120,240,60,268,200;7000:TAP:120,240;9000:QUIT')
        self.run_card(card, script)
        self.assertTrue((card.sd / 'b1.txt').exists())

    def test_a_ring_pins_and_a_hold_opens_the_tasks(self):
        card = self.card()
        ring = []
        import math
        for k, a in enumerate(range(200, 600, 25)):
            ring += [round(110 + 70 * math.cos(math.radians(a)) * (1 + 0.04 * k)), round(304 + 26 * math.sin(math.radians(a)))]
        script = ('2000:TAP:240,670;3500:TAP:120,620;5000:PATH:%s,500;7000:TAP:100,240,900;9000:QUIT'
                  % ','.join(map(str, ring)))
        log, _ = self.run_card(card, script)
        pins = (card.store / 'favorite-files')
        self.assertTrue(pins.exists() and any(pins.iterdir()), 'b2.txt pinned')
        self.assertEqual(frames(log)[-1][4], '4', 'the tasks of the row held')


if __name__ == '__main__':
    unittest.main()
