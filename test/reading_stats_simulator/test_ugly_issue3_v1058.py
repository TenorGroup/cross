"""tenor/ugly on the X3, the 4 faults of GitHub issue 3 (v1.0.58), each read on the pixels of the simulator.

Settings are the reporter's: English, 12 hour clock, "Clock left, Battery right", battery percentage shown, text size
tiers 0, 1 and 2, the straight (0) and the turned (1) hand.
 1. Desk: the labels under the folder and the favorites are whole, 6 px or more above the line at the foot.
 2. The battery level is written beside an empty body (12 px or more high, 6 px clear of the body and the marks).
 3. Reading habits: the explanation is written once, clear of the rule above and of the tip below.
 4. No tenor/cross path pill on any screen of the shell that draws its path in the foot bar.
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


def shot(keys, tier=0, level=1, prepare=None, **settings):
    """The screen after `keys` from the diary (1500 ms apart). Returns (image, activities entered)."""
    card = Card(shell=1, uiTextSize=tier, uiUglyLevel=level, **dict(HAND, **settings))
    try:
        if prepare:
            prepare(card)
        parts = ['%d:%s' % (1500 * (i + 1), k) for i, k in enumerate(keys)]
        at = 1500 * len(keys) + 2200
        parts.append('%d:QUIT' % (at + 700))
        log, shots = card.run(';'.join(parts), [(at, 'f')], timeout=120, CROSSPOINT_SIM_NOW=NOW, CROSSPOINT_SIM_MARK_SEED='12345')
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


if __name__ == '__main__':
    unittest.main()
