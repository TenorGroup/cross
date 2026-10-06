"""tenor/ugly on the X4 Pro touch screen, in the simulator: whole journeys by finger and Home key.

Founder rules under test (04/10/2026): the three tiers of the X3 (diary, desk, notebook) touched straight;
the Home key and "to the desk" go up one tier; a swipe across turns the notebook page, from the edge too;
the approved answer sheet commits an option on tap, long lists open an anchored paper; the
Interface row asks once before leaving the shell; an X over a file asks before it bins it; a held row
opens tasks and Pin persists its target.

Needs an X4 Pro simulator build (env simulator_x4pro, TEST_PROGRAM pointing at it) whose script input
can move a finger (SWIPE); required when this suite runs.
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


def form_frames(log):
    return re.findall(r'Settings form total=\d+ms tab=(\d+) rows=(\d+) sheet=(\d+)/(\d+) question=(\d+) candidate=(\d+) paper=(\d+)', log)


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
        log, _ = self.run_card(self.card(), '2000:TAP:120,450;4500:HOME;6500:QUIT')
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

    def test_settings_tick_choices_and_long_list_paper_persist(self):
        card = self.card()
        # Display form in 4 sheets since no sheet keeps a question alone (2584a7ea): Night on sheet 4; Refresh is a
        # six-option choice on sheet 3.
        log, _ = self.run_card(card, '2000:TAP:60,750;3500:TAP:200,240;5000:TAP:420,770;'
                              '6500:TAP:420,770;8000:TAP:420,770;9500:TAP:200,460;'
                              '11000:SWIPE:150,600,400,600,200;12500:TAP:200,470;14000:QUIT')
        f = form_frames(log)
        self.assertIn('Settings', entered(log))
        self.assertEqual(f[0], ('0', '8', '1', '4', '0', '0', '0'))
        self.assertEqual(f[-1], ('0', '8', '3', '4', '3', '4', '0'))
        after = card.settings()
        self.assertEqual(after['screenInverted'], 1, 'Night toggle persisted')
        self.assertEqual(after['refreshFrequency'], 4, 'Refresh selection persisted')
        # Sleep screen has 12 values: paper anchored at the current cover, then cover plus text.
        paper = self.card(sleepScreen=3)
        log, _ = self.run_card(paper, '2000:TAP:60,750;3500:TAP:200,304;5000:TAP:200,250;'
                              '6500:TAP:200,300;8000:QUIT')
        f = form_frames(log)
        self.assertEqual([x[6] for x in f], ['0', '1', '0'], 'paper opens then closes after selection')
        self.assertEqual(f[-1][4:6], ('0', '4'))
        self.assertEqual(paper.settings()['sleepScreen'], 4, 'paper selection persisted')

    def test_an_edge_button_does_not_turn_the_page_under_an_open_group(self):
        # Edge Up previews an option in the active form; the following tap commits its first answer.
        card = self.card()
        log, _ = self.run_card(card, '2000:TAP:60,750;3500:TAP:200,240;5000:UP;'
                              '6500:TAP:60,276;8500:QUIT')
        f = form_frames(log)
        self.assertEqual(entered(log), ['Boot', 'UglyDiary', 'UglyNotebook', 'Settings'])
        self.assertGreaterEqual(len(f), 3, f)
        self.assertTrue(all(x[:5] == ('0', '8', '1', '4', '0') for x in f), f)
        self.assertEqual([x[5] for x in f], ['0', '2', '0'], 'preview then tap on the same question')
        self.assertEqual(card.settings()['uiTextSize'], 0)

    def test_the_interface_row_asks_before_it_leaves_the_shell(self):
        # Display sheet 4, Cross option A, then the shared Switch activity's measured answer bands.
        to_box = ('2000:TAP:60,750;3500:TAP:200,240;5000:TAP:420,770;'
                  '6500:TAP:420,770;8000:TAP:420,770;9500:TAP:200,170;')
        for answer_y, shell in ((490, 1), (420, 0)):
            card = self.card()
            log, _ = self.run_card(card, to_box + '11000:TAP:200,%d;13000:QUIT' % answer_y)
            self.assertIn('UglySwitch', entered(log), 'Interface asks first')
            self.assertEqual(re.findall(r'Switch frame total=\d+ms sel=(\d)', log), ['1'], 'default No')
            self.assertEqual(card.settings()['uiShell'], shell)
            if shell == 1:
                self.assertEqual(form_frames(log)[-1][4:7], ('4', '1', '0'), 'cancel returns to committed Ugly')
            else:
                self.assertIn('Entering activity: Home', log)

    def test_an_x_over_a_file_asks_then_bins_it(self):
        card = self.card()
        # desk, the stack of books (the card's root), an X over row 1 (b1.txt), then "bin it"
        script = ('2000:TAP:240,670;3500:TAP:120,620;5000:SWIPE:60,214,170,266,200;'
                  '5400:SWIPE:170,214,60,268,200;7000:TAP:120,500;9000:QUIT')
        log, _ = self.run_card(card, script)
        self.assertEqual(frames(log)[1][4], '2', 'the question')
        self.assertFalse((card.sd / 'b1.txt').exists(), 'binned')
        self.assertTrue((card.sd / 'b0.txt').exists())

    def test_a_tap_beside_the_question_keeps_the_file(self):
        card = self.card()
        script = ('2000:TAP:240,670;3500:TAP:120,620;5000:SWIPE:60,214,170,266,200;'
                  '5400:SWIPE:170,214,60,268,200;7000:TAP:120,240;9000:QUIT')
        log, _ = self.run_card(card, script)
        self.assertIn('2', [f[4] for f in frames(log)], 'the question opened')
        self.assertEqual(frames(log)[-1][4], '0', 'outside tap dismisses it')
        self.assertTrue((card.sd / 'b1.txt').exists())

    def test_a_hold_pins_and_a_hold_opens_the_tasks(self):
        card = self.card()
        # Hold b2.txt, choose anchored Pin, then hold b1.txt for its tasks.
        script = ('2000:TAP:240,670;3500:TAP:120,620;5000:TAP:100,304,900;'
                  '6500:TAP:100,304;8000:TAP:100,240,900;10000:QUIT')
        log, _ = self.run_card(card, script)
        pins = card.store / 'favorite-files'
        self.assertTrue(pins.exists() and any(pins.iterdir()), 'b2.txt pinned')
        self.assertIn('/b2.txt', ''.join(p.read_text() for p in pins.iterdir() if p.is_file()))
        self.assertEqual([f[4] for f in frames(log)], ['0', '4', '0', '4'])


if __name__ == '__main__':
    unittest.main()
