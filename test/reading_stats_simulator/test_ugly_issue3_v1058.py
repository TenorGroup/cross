"""tenor/ugly on the X3, the 4 faults of GitHub issue 3 (v1.0.58), each read on the pixels of the simulator.

Settings are the reporter's: English, 12 hour clock, "Clock left, Battery right", battery percentage shown, text size
tiers 0, 1 and 2, the straight (0) and the turned (1) hand.
 1. Desk: the labels under the folder and the favorites are whole, 6 px or more above the line at the foot.
 2. The battery level is written beside an empty body (12 px or more high, 6 px clear of the body and the marks).
 3. Reading habits: the explanation is written once, clear of the rule above and of the tip below.
 4. No tenor/cross path pill on any screen of the shell that draws its path in the foot bar.
 5. The Select key is the pen's tick under tenor/ugly, in the foot bar and in the tips and sentences of the screens;
    tenor/cross keeps its Enter symbol.
"""
import datetime
import json
import unittest
from collections import deque

from ugly_common import Card, entered

NOW = '1790000000'
HAND = dict(language='EN', clockFormat=1, statusBarClock=2, clockUtcOffsetQ=28, clockAutoTimezone=0, clockTimezone=255,
            hideBatteryPercentage=0)


def habits_stats(card):
    """5 days of reading, 2 of them at night: the habit pages have evidence under their explanation."""
    local = datetime.datetime.utcfromtimestamp(int(NOW)) + datetime.timedelta(hours=7)
    today = (local.date() - datetime.date(2000, 1, 1)).days + 1
    days = [[today - k, 3600000, 600000 if k in (1, 3) else 0, 0, 2, 0, 1, 0, k in (1, 3)] for k in range(1, 6)]
    path = card.store / 'reading-stats.json'
    doc = json.loads(path.read_text())
    doc['habits'] = {'first': today - 5, 'days': days}
    path.write_text(json.dumps(doc))


def shot(keys, tier=0, level=1, prepare=None, shell=1, env=None, **settings):
    """The screen after `keys` from the diary (1500 ms apart). Returns (image, activities entered). `env` adds
    simulator variables to the run."""
    card = Card(shell=shell, uiTextSize=tier, uiUglyLevel=level, **dict(HAND, **settings))
    try:
        if prepare:
            prepare(card)
        parts = ['%d:%s' % (1500 * (i + 1), k) for i, k in enumerate(keys)]
        at = 1500 * len(keys) + 2200
        parts.append('%d:QUIT' % (at + 700))
        log, shots = card.run(';'.join(parts), [(at, 'f')], timeout=120, CROSSPOINT_SIM_NOW=NOW, CROSSPOINT_SIM_MARK_SEED='12345',
                               **(env or {}))
    finally:
        card.close()
    return shots['f'].convert('L'), entered(log)


def bands(im, x0, x1, y0, y1):
    """Runs of rows with ink in columns x0..x1 between y0..y1: [(first row, last row)]."""
    px = im.load()
    out, start = [], None
    for y in range(y0, y1 + 1):
        ink = any(px[x, y] < 128 for x in range(x0, x1))
        if ink and start is None:
            start = y
        if not ink and start is not None:
            out.append((start, y - 1))
            start = None
    if start is not None:
        out.append((start, y1))
    return out


def ink_components(im, box, reach=2):
    """Components of the ink in box (x0, y0, x1, y1); ink within `reach` px of each other is one component."""
    x0, y0, x1, y1 = box
    px = im.load()
    ink = {(x, y) for y in range(y0, y1) for x in range(x0, x1) if px[x, y] < 128}
    out = []
    while ink:
        seed = ink.pop()
        comp, queue = [seed], deque([seed])
        while queue:
            cx, cy = queue.popleft()
            for dx in range(-reach, reach + 1):
                for dy in range(-reach, reach + 1):
                    n = (cx + dx, cy + dy)
                    if n in ink:
                        ink.discard(n)
                        comp.append(n)
                        queue.append(n)
        xs, ys = [c[0] for c in comp], [c[1] for c in comp]
        out.append((min(xs), min(ys), max(xs), max(ys)))
    return out


class UglyIssue3Test(unittest.TestCase):
    def test_1_the_desk_labels_are_whole_and_clear_of_the_line_at_the_foot(self):
        for tier in (0, 1, 2):
            with self.subTest(tier=tier):
                im, acts = shot(['RIGHT', 'RIGHT', 'CONFIRM'], tier)
                self.assertEqual(acts[-1], 'UglyDesk')
                # the labels stand at y 676 to 698 under the stack of folders (x 36 to 240) and the favorites (322 to 480)
                folder = bands(im, 36, 240, 674, 756)
                favorites = bands(im, 322, 480, 674, 756)
                self.assertGreaterEqual(folder[0][1] - folder[0][0] + 1, 18, f'Folder cut: {folder}')
                self.assertGreaterEqual(favorites[0][1] - favorites[0][0] + 1, 18, f'Favorites cut: {favorites}')
                self.assertGreaterEqual(folder[1][0] - folder[0][1] - 1, 6, f'Folder touches the line at the foot: {folder}')
                # with the ring round the folder (Down from the book) its ink and the line stay 6 px apart too
                ringed = bands(shot(['RIGHT', 'RIGHT', 'CONFIRM', 'DOWN'], tier)[0], 0, 260, 672, 760)
                self.assertGreaterEqual(ringed[1][0] - ringed[0][1] - 1, 6, f'the ring touches the line at the foot: {ringed}')

    def foot(self, clock_left, mode):
        """The comps of ink (letters, marks, the battery) of the foot bar, left to right, and the battery body among them."""
        im, _ = shot([], 0, 1, statusBarClock=2 if clock_left else 1, hideBatteryPercentage=mode)
        parts = sorted(ink_components(im, (0, 752, 528, 792), reach=1))
        bodies = [p for p in parts if 28 <= p[2] - p[0] + 1 <= 44 and 11 <= p[3] - p[1] + 1 <= 24 and (p[0] > 440 if clock_left else p[2] < 90)]
        self.assertEqual(len(bodies), 1, f'the battery body among {parts}')
        return im, parts, bodies[0]

    def test_2_the_battery_level_is_written_beside_an_empty_body(self):
        for clock_left in (True, False):
            for mode in (0, 1):  # Hide Battery %: never, in the reader: the foot bar shows the number in both
                with self.subTest(clock_left=clock_left, mode=mode):
                    im, parts, body = self.foot(clock_left, mode)
                    inside = [p for p in parts if p != body and p[0] >= body[0] and p[2] <= body[2] and p[1] >= body[1] and p[3] <= body[3]]
                    self.assertEqual(inside, [], f'ink inside the body {body}')
                    # the letters beside it: from the body outwards while the gap is a letter's
                    outward = sorted((p for p in parts if p != body and (p[2] < body[0] if clock_left else p[0] > body[2])),
                                     key=lambda p: -p[2] if clock_left else p[0])
                    gap = lambda a, b: (a[0] - b[2] if clock_left else b[0] - a[2]) - 1
                    block, last = [], body
                    for p in outward:
                        if block and gap(last, p) > 5:
                            break
                        if not block and gap(last, p) > 20:
                            break
                        block.append(p)
                        last = p
                    self.assertGreaterEqual(len(block), 3, f'the number beside the body {body}: {block}')
                    top, bottom = min(p[1] for p in block), max(p[3] for p in block)
                    self.assertGreaterEqual(bottom - top + 1, 12, f'number {top}..{bottom}')
                    self.assertGreaterEqual(gap(body, block[0]), 6, 'number to body')
                    beyond = [p for p in outward if p not in block]
                    if beyond:
                        self.assertGreaterEqual(gap(block[-1], beyond[0]), 6, f'number to the next mark {beyond[0]}')
            with self.subTest(clock_left=clock_left, mode='hidden'):
                shown, _, body = self.foot(clock_left, 0)
                hidden, _, same = self.foot(clock_left, 2)
                box = (same[0], same[1], same[2] + 1, same[3] + 1)
                count = lambda im: sum(1 for p in im.crop(box).getdata() if p < 128)
                self.assertGreater(count(hidden), count(shown) + 20, 'the level as strokes in the body')

    def test_3_the_habit_explanation_is_written_once_clear_of_the_rule_and_the_tip(self):
        for level in (0, 1):
            for tier in (0, 1, 2):
                with self.subTest(level=level, tier=tier):
                    im, acts = shot(['DOWN'] * 4 + ['CONFIRM'], tier, level, prepare=habits_stats)
                    self.assertEqual(acts[-1], 'ReadingHabits')
                    px = im.load()
                    rows = [y for y in range(330, 700) if sum(1 for x in range(24, 504) if px[x, y] < 128) >= 200]
                    self.assertTrue(rows, 'the rule above the explanation')
                    rule = rows[-1]
                    self.assertEqual(bands(im, 24, 504, rule + 1, rule + 5), [], 'the text touches the rule')
                    first = bands(im, 24, 504, rule + 1, 700)[0][0]
                    self.assertGreaterEqual(first - rule - 1, 6, 'text under the rule')
                    # one layer starts at x 24; the pages of the X3 layout started at 0
                    self.assertEqual(bands(im, 0, 18, rule + 1, 650), [], 'a second layer at the left edge')
                    if level == 0:
                        lines = bands(im, 24, 504, rule + 1, 650)
                        self.assertGreaterEqual(len(lines), 5, lines)

    def test_4_no_cross_path_pill_on_the_screens_of_the_foot_bar(self):
        import cross_pills
        routes = {
            'habits': ['DOWN'] * 4 + ['CONFIRM'],
            'days': ['DOWN'] * 4 + ['RIGHT', 'RIGHT', 'CONFIRM'],
            'keyboard': ['UP'] + ['RIGHT'] * 7 + ['CONFIRM', 'RIGHT', 'CONFIRM'],
            'network': ['UP', 'CONFIRM'] + ['RIGHT'] * 2 + ['CONFIRM'],
            'remap': ['UP'] + ['RIGHT'] * 4 + ['CONFIRM', 'CONFIRM'],
            'clock': ['UP'] + ['RIGHT'] * 6 + ['CONFIRM', 'CONFIRM'],
            'language': ['UP'] + ['RIGHT'] * 7 + ['CONFIRM', 'CONFIRM'],
            'about': ['UP'] + ['RIGHT'] * 7 + ['CONFIRM'] + ['RIGHT'] * 4 + ['CONFIRM'],
            'koreader': ['UP'] + ['RIGHT'] * 9 + ['CONFIRM', 'CONFIRM'],
            'opds': ['UP'] + ['RIGHT'] * 9 + ['CONFIRM', 'RIGHT', 'CONFIRM'],
            'clear cache': ['UP'] + ['RIGHT'] * 9 + ['CONFIRM'] + ['RIGHT'] * 3 + ['CONFIRM'],
        }
        for name, keys in routes.items():
            with self.subTest(screen=name):
                im, acts = shot(keys)
                self.assertEqual(cross_pills.find(im), [], f'a tenor/cross pill on {acts[-1]}')

    def test_5_the_select_key_is_the_tick_in_the_pen_and_stays_the_enter_in_tenor_cross(self):
        import enter_symbol
        routes = {
            'diary': [],
            'notebook, hold to pin': ['DOWN'] * 4,
            'habits, the tip': ['DOWN'] * 4 + ['CONFIRM'],
            'keyboard, the tips': ['UP'] + ['RIGHT'] * 7 + ['CONFIRM', 'RIGHT', 'CONFIRM'],
            'answer sheet': ['UP', 'CONFIRM'],
        }
        for name, keys in routes.items():
            with self.subTest(screen=name):
                im, acts = shot(keys)
                self.assertEqual(enter_symbol.find(im), [], f'an Enter symbol of tenor/cross on {acts[-1]}')
        im, _ = shot([], shell=0)
        self.assertTrue(enter_symbol.find(im), 'tenor/cross keeps the Enter symbol in its foot bar')
        # the tick of the foot bar is centred on its key: the middle of the ink on the foot's centre line, 197 px across
        im, _ = shot([], statusBarClock=1)
        ticks = [p for p in ink_components(im, (160, 752, 240, 792), reach=1) if 9 <= p[2] - p[0] + 1 <= 16 and 8 <= p[3] - p[1] + 1 <= 14]
        self.assertEqual(len(ticks), 1, ticks)
        self.assertLessEqual(abs((ticks[0][1] + ticks[0][3]) / 2 - 772), 0.5, f'tick {ticks[0]}')


if __name__ == '__main__':
    unittest.main()
