"""X4 Pro: the way back after a jump is the dynamic bar for 3 seconds ("<", "tr. N", the seconds left in a ring).

Runs the X4 Pro simulator (pio run -e simulator_x4pro); X4PRO_PROGRAM picks another build. The jump is the Contents tool of
the reader bar (tap the foot, the second tool, the second chapter). The bar stands where the dynamic bar always stands
(y 724..784, 16 px in): the "<" is 60 x 60 at its left end, and the bar goes down by itself 3 seconds after the page
that carries it reached the panel. Checked from the real frames and the firmware's own log lines.
"""
import os
import re
import sys
import unittest
from pathlib import Path

from PIL import ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
from test_vien_quay_lai_v1061 import REPO, SAVED, WayBackPill

BAR = (16, 724, 448, 60)           # x, y, width, height of the bar
BACK = (46, 754)                   # the centre of its "<"
FACE = (140, 754)                  # its page pill
OUTSIDE_BAR = (240, 792)           # the foot band under the bar: the text menu's, as before
WINDOW = re.compile(r'\[GFX\] Window logical=(\d+),(\d+) (\d+)x(\d+)')
STEP = re.compile(r'^\[(\d+)\] ')


def same(a, b, box=None):
    """True when two frames (or the same box of them) are the same pixel for pixel."""
    if box:
        a, b = a.crop(box), b.crop(box)
    return ImageChops.difference(a, b).getbbox() is None


def bar_up(image):
    """The bar clears the page's status strip (y 780 on) with the band it stands in: a frame with the bar has no ink
    at rows 788..795, a page has its clock and battery there."""
    return all(image.getpixel((px, py)) >= 128 for py in range(788, 796) for px in range(0, image.width))


class WayBackBar(WayBackPill, unittest.TestCase):
    PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
    ORIGIN = '3000:TAP:240,300;6000:TAP:240,400;8000:TAP:240,400;'
    JUMP = '10000:TAP:240,775;12000:TAP:228,754;14000:TAP:240,500;'
    # The page with the bar reaches the panel at about 14.1 s; its seconds go 3, 2, 1 and the bar is down at about 17.1 s.
    WHEN = {'origin': 9000, 'bar3': 14800, 'bar2': 15900, 'bar1': 16900, 'down': 18500, 'later': 22500, 'quit': 24000}
    # The inherited pill checks do not apply: the X4 Pro has no pill in the strip.
    test_jump_shows_the_page_and_back_removes_it = None
    test_empty_stack_draws_no_pill = None
    test_hidden_title_draws_no_pill = None

    @staticmethod
    def lines(log, after=0):
        """(ms, text) of the firmware's log lines from `after` on."""
        found = []
        for line in log.splitlines():
            m = STEP.match(line)
            if m and int(m.group(1)) >= after:
                found.append((int(m.group(1)), line))
        return found

    def saved(self, log, after=0):
        """The pages painted, as (spine, page), from `after` ms on."""
        return [(int(m.group(1)), int(m.group(2))) for _, line in self.lines(log, after)
                if (m := SAVED.search(line))]

    def windows(self, log):
        return [(ms, *map(int, m.groups())) for ms, line in self.lines(log) if (m := WINDOW.search(line))]

    def menu_opened(self, log, after):
        return any('BARFRAME] screen=EpubReader' in line for _, line in self.lines(log, after))

    def test_the_bar_counts_down_and_goes_down_by_itself(self):
        self.WHEN = {**self.WHEN, 'later': 22500}
        # Next page then the back column: the same page painted again the ordinary way, for the gray text to compare with.
        log, image = self.run_card(('JUMP', 'TURN'), 'origin', 'bar3', 'bar2', 'bar1', 'down', 'later')
        for name in ('bar3', 'bar2', 'bar1'):
            self.assertTrue(bar_up(image[name]), f'no bar at {name}')
        self.assertFalse(bar_up(image['down']), 'the bar is still up after the 3 seconds')
        # The ring holds a different number each second: its box differs frame to frame and nothing else of the bar moves.
        x, y, w, h = 416, 730, 48, 48
        ring = (x, y, x + w, y + h)
        self.assertFalse(same(image['bar3'], image['bar2'], ring) or same(image['bar2'], image['bar1'], ring))
        self.assertTrue(same(image['bar3'], image['bar1'], (BAR[0], BAR[1], x - 4, BAR[1] + BAR[3])))
        # Each second is one refresh of the ring alone, inside the bar and clear of the page's last line above it.
        windows = self.windows(log)
        self.assertEqual(len(windows), 2, windows)
        for _, wx, wy, ww, wh in windows:
            self.assertEqual((wx, ww), (x, w))
            self.assertGreaterEqual(wy, BAR[1])
            self.assertLessEqual(wy + wh, BAR[1] + BAR[3])
        # The 3 seconds run from the page that carries the bar, not from the tap that asked for it.
        shown = next(ms for ms, line in self.lines(log, 14000) if SAVED.search(line) and 'spine=1' in line)
        self.assertAlmostEqual(windows[0][0] - shown, 1000, delta=250)
        self.assertAlmostEqual(windows[1][0] - shown, 2000, delta=250)
        # The page comes back as the page always paints: identical, grays included, to the same page reached by turning.
        self.assertTrue(same(image['down'], image['later']),
                        'the page painted after the bar differs from the same page painted the ordinary way')

    def test_tap_on_the_back_goes_to_the_page_before_the_jump(self):
        self.WHEN = {**self.WHEN, 'back': 17000}
        log, image = self.run_card(('JUMP', 'TAP_BACK'), 'origin', 'bar3', 'back')
        self.assertTrue(bar_up(image['bar3']))
        pages = self.saved(log)
        self.assertEqual(pages[-1], pages[2], f'the tap did not bring back the page before the jump: {pages}')
        # The strip's page counts on the right are an estimate that moves on its own; the page and the title are the origin's.
        self.assertTrue(same(image['back'], image['origin'], (0, 0, 320, 800)), 'not the origin page')
        self.assertEqual(self.windows(log), [], 'no count refresh after the tap')

    TAP_BACK = f'15000:TAP:{BACK[0]},{BACK[1]};'

    def test_tap_on_the_back_is_not_the_text_menu(self):
        # The "<" stands in the text menu's band: the menu must not open.
        log, _ = self.run_card(('JUMP', 'TAP_BACK'), 'origin')
        self.assertFalse(self.menu_opened(log, 15000))

    TAP_FACE = f'15000:TAP:{FACE[0]},{FACE[1]};'

    def test_tap_on_the_rest_of_the_bar_does_nothing_and_the_count_goes_on(self):
        self.WHEN = {**self.WHEN, 'mid': 15500, 'down': 18500}
        log, image = self.run_card(('JUMP', 'TAP_FACE'), 'origin', 'bar3', 'mid', 'down')
        self.assertTrue(bar_up(image['mid']), 'the tap on the bar took it down')
        self.assertFalse(bar_up(image['down']))
        spines = {s for s, _ in self.saved(log, 14000)}
        self.assertEqual(spines, {1}, 'the tap moved the reader')
        self.assertFalse(self.menu_opened(log, 15000))

    TAP_NEXT = '15000:TAP:300,300;'

    def test_tap_off_the_bar_does_what_its_zone_does_and_the_bar_is_gone(self):
        self.WHEN = {**self.WHEN, 'turned': 16500}
        log, image = self.run_card(('JUMP', 'TAP_NEXT'), 'origin', 'bar3', 'turned')
        self.assertFalse(bar_up(image['turned']), 'the bar stayed after a page turn')
        pages = self.saved(log, 14000)
        self.assertEqual(pages[-1], (1, 1), f'the tap did not turn the page: {pages}')
        self.assertEqual(self.windows(log)[1:], [], 'a count refresh after the bar went')

    TAP_MENU_AFTER = '18000:TAP:46,754;'

    def test_the_same_place_after_the_3_seconds_is_the_text_menu_as_before(self):
        self.WHEN = {**self.WHEN, 'hidden': 17800, 'menu': 19500}
        log, image = self.run_card(('JUMP', 'TAP_MENU_AFTER'), 'origin', 'hidden', 'menu')
        self.assertFalse(bar_up(image['hidden']))
        self.assertTrue(self.menu_opened(log, 18000), 'the text menu did not open')
        self.assertEqual({s for s, _ in self.saved(log, 14000)}, {1}, 'the tap went back instead of opening the menu')

    SWIPE_UP = '15000:SWIPE:240,797,240,500,150;'

    def test_a_swipe_up_from_the_bottom_edge_still_leaves_the_book(self):
        log, _ = self.run_card(('JUMP', 'SWIPE_UP'), 'origin')
        leaving = [ms for ms, line in self.lines(log) if 'Exiting activity: EpubReader' in line]
        self.assertTrue(leaving and leaving[-1] > 15000, f'the book was not left: {leaving}')

    # A second jump re-arms the count: Contents again, the first chapter's row this time, opened from the text menu's band
    # under the bar.
    JUMP_AGAIN = '16000:TAP:240,792;18000:TAP:228,754;20000:TAP:240,437;'

    def test_a_new_jump_starts_the_count_again(self):
        self.WHEN = {**self.WHEN, 'again3': 21000, 'again2': 22000, 'gone': 24500, 'quit': 26500}
        log, image = self.run_card(('JUMP', 'JUMP_AGAIN'), 'origin', 'again3', 'again2', 'gone')
        self.assertTrue(bar_up(image['again3']) and bar_up(image['again2']), 'no bar after the second jump')
        self.assertFalse(bar_up(image['gone']))
        self.assertEqual(len(self.windows(log)), 2 + 1, self.windows(log))  # the second jump showed 3 and ticked once before the shot at 22 s

    TURN = '19000:TAP:300,300;21000:TAP:60,300;'

    def test_each_number_stands_a_second_on_the_panel_whatever_the_refresh_takes(self):
        # A slow panel: every paint takes 1 s and every ring refresh 0.7 s (the simulator sleeps that long, as the panel does).
        # Each number counts from the moment the refresh that showed it has finished, so no refresh eats into it.
        self.SIM_ENV = {'CROSSPOINT_SIM_REFRESH_MS': '1000', 'CROSSPOINT_SIM_WINDOW_MS': '700'}
        self.WHEN = {**self.WHEN, 'quit': 26000}
        log, _ = self.run_card(('JUMP',))
        lines = self.lines(log)
        # The jump's page is on the panel when its paint has returned.
        shown = next(ms for ms, line in lines if ms > 14000 and 'Rendered page' in line)
        spans = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r'WINDOW begin ms=(\d+) .*\nWINDOW end ms=(\d+)', log)]
        self.assertEqual(len(spans), 2, spans)
        hide = next(ms for ms, line in lines if ms > spans[1][1] and 'Prewarm' in line)
        stood = [spans[0][0] - shown, spans[1][0] - spans[0][1], hide - spans[1][1]]
        self.assertTrue(all(ms >= 1000 for ms in stood), f'3, 2 and 1 stood {stood} ms on the panel')
        self.assertGreaterEqual(hide - shown, 3000)


if __name__ == '__main__':
    unittest.main(verbosity=2)
