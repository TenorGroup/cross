"""v1.0.14 corners and arrows, measured on the simulator's frames.

1. The Home tab cursor is a 2 px black ring around its gray block: the corner comes from solid
   ink, so its four corners match and every tab gets the same shape. A gray dither alone samples the
   corner on even pixels only and changes phase with the tab's position.
2. The one-line notice popup is a leaf of its own height (tenorradius::leaf), not the 28 px sheet
   radius that turned a 57 px block into a pill.
3. The "another book" row on the Recent card says "more this way" with an open V, the same mark as
   the list's down chevron; filled triangles are left to the button hints.
4. The Recent card shows the expected finish date as its second stats row, and the book stats
   screen lists it after the current position.
Screenshots land in CROSSPOINT_TEST_ARTIFACTS when it is set.
"""
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
CARD_STATS = re.compile(r'Card stats rows=([0-9a-f]{2}) bar=(-?\d+)')


def fnv32(text):
    value = 2166136261
    for byte in text.encode('utf-8'):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def leaf(short_side):
    return max(3, min(14, (3 * short_side + 10) // 20))


class CornersArrowsTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-v1014-corners-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps({'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120}))
        (self.store / 'state.json').write_text(json.dumps(
            {'openEpubPath': '', 'lastSleepFromReader': False, 'showBootScreen': False}))
        books = []
        for path, title, epub in (('/lang.epub', 'Làng ven biển', 'test_dictionary_synonyms.epub'),
                                  ('/ben-song.epub', 'Bến sông ngày gió', 'test_kerning_ligature.epub')):
            shutil.copy(REPO / 'test/epubs' / epub, self.sd / path.lstrip('/'))
            books.append({'path': path, 'title': title, 'author': 'Người Viết Thử', 'coverBmpPath': '',
                          'excerpt': 'Nước lên từ sáng, bến vắng người.'})
        (self.store / 'recent.json').write_text(json.dumps({'books': books}, ensure_ascii=False), encoding='utf-8')
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None

    def launch(self, events, shots, refresh_ms=0):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
        if refresh_ms:
            env['CROSSPOINT_SIM_REFRESH_MS'] = str(refresh_ms)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        images = {}
        for _, name in shots:
            with Image.open(self.sd / (name + '.bmp')) as shot:
                images[name] = shot.convert('L')
            if self.artifacts:
                self.artifacts.mkdir(parents=True, exist_ok=True)
                images[name].save(self.artifacts / f'{self._testMethodName}-{name}.png')
        return log, images

    @staticmethod
    def dark(image, x, y):
        return image.getpixel((x, y)) < 128

    def cursor_profile(self, image):
        """Ink margins of the tab cursor's top and bottom four rows, from its own edges.

        The tab band is rows 53..112 at the smallest text size. The cursor's top rows hold no icon,
        so the first inked row of the band is the cursor's top edge; its columns bound the block.
        """
        band = range(53, 113)
        top = next(y for y in band if any(self.dark(image, x, y) for x in range(image.width)))
        xs = [x for x in range(image.width) if self.dark(image, x, top)]
        lo, hi = max(0, min(xs) - 12), min(image.width, max(xs) + 12)
        rows = [y for y in band if any(self.dark(image, x, y) for x in range(lo, hi))]
        bottom = max(rows)
        cols = [x for x in range(lo, hi) if any(self.dark(image, x, y) for y in range(top, bottom + 1))]
        left, right = min(cols), max(cols)

        def margins(y):
            ink = [x for x in range(left, right + 1) if self.dark(image, x, y)]
            return (ink[0] - left, right - ink[-1]) if ink else None
        return ([margins(top + i) for i in range(4)], [margins(bottom - i) for i in range(4)],
                (left, top, right, bottom))

    def test_home_tab_cursor_corners_even(self):
        _, images = self.launch('2500:DOWN;4000:DOWN;5500:DOWN;7000:DOWN;9000:QUIT',
                                [(2300, 'tab0'), (3800, 'tab1'), (5300, 'tab2'), (6800, 'tab3'), (8500, 'tab4')])
        profiles = {name: self.cursor_profile(image) for name, image in images.items()}
        first_top, first_bottom, box = profiles['tab0']
        # Four matching corners: left and right margins equal, bottom rows the top rows turned over.
        self.assertTrue(all(m and m[0] == m[1] for m in first_top), f'tab0 left and right differ: {profiles["tab0"]}')
        self.assertEqual(first_top, first_bottom, f'tab0 top and bottom differ: {profiles["tab0"]}')
        # A rounded corner: the first row is set in further than the fourth.
        self.assertTrue(first_top[0] and first_top[3] and first_top[0][0] > first_top[3][0], profiles['tab0'])
        for name, (top, bottom, _) in profiles.items():
            self.assertEqual((top, bottom), (first_top, first_bottom), f'{name} cursor differs from tab0: {profiles}')

    def popup_block(self, image):
        """Rows of the black notice block: flood fill from its top middle, text glyphs are holes."""
        y0 = int(image.height * 0.165)
        seed = (image.width // 2, y0 + 1)
        self.assertTrue(self.dark(image, *seed), 'no popup at the notice position')
        seen, stack = set(), [seed]
        while stack:
            x, y = stack.pop()
            if (x, y) in seen or not (0 <= x < image.width and 0 <= y < image.height) or not self.dark(image, x, y):
                continue
            seen.add((x, y))
            stack.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
        return seen

    def test_notice_popup_is_a_leaf(self):
        # Opening a book the first time shows the indexing notice; a slow refresh keeps it on screen.
        _, images = self.launch('2000:CONFIRM;6000:QUIT', [(2600, 'notice')], refresh_ms=1500)
        block = self.popup_block(images['notice'])
        left = min(x for x, _ in block); right = max(x for x, _ in block)
        top = min(y for _, y in block); bottom = max(y for _, y in block)
        height = bottom - top + 1
        cuts = []
        for y in range(top, bottom + 1):
            row = [x for x, yy in block if yy == y]
            cuts.append(min(row) - left)
        rounded = [c for c in cuts[:height // 2] if c > 0]
        # leaf(57) = 9: the continuous corner sets in 1 to 6 px over at most 9 rows. The 28 px
        # radius set the first row in 25 px and curved half the block's height.
        self.assertLessEqual(len(rounded), leaf(height), f'{height} px block, cuts {cuts}')
        self.assertLessEqual(cuts[0], leaf(height), f'{height} px block, cuts {cuts}')
        self.assertEqual(cuts, cuts[::-1], f'top and bottom corners differ: {cuts}')
        self.assertGreater(right - left, 100)

    def test_other_book_arrows_are_outlined(self):
        _, images = self.launch('2000:RIGHT;5000:QUIT', [(3800, 'second')])
        card = images['second']
        rule = next(y for y in range(700, 760) if sum(self.dark(card, x, y) for x in range(card.width)) > 400)
        for name, (x0, x1) in (('left', (8, 34)), ('right', (494, 520))):
            ink = [(x, y) for x in range(x0, x1) for y in range(rule + 2, 756) if self.dark(card, x, y)]
            self.assertTrue(ink, f'no {name} arrow')
            top = min(y for _, y in ink); bottom = max(y for _, y in ink)
            middle = (top + bottom) // 2
            across = sum(1 for x, y in ink if y == middle)
            # The open V meets its tip on the middle row with the 3 px stroke; a filled triangle is
            # as wide as the whole arrow there.
            self.assertLessEqual(across, 3, f'{name} arrow is filled: {across} px on its middle row')
            widths = [sum(1 for x, y in ink if y == row) for row in range(top, bottom + 1)]
            self.assertTrue(all(w <= 4 for w in widths), f'{name} arrow rows {widths}')

    def seed_stats(self, first_days_ago, days, progress):
        """The first book's record, dated from the simulator's clock (UTC, the default offset)."""
        today = datetime.datetime.now(datetime.timezone.utc).date()
        code = lambda d: int(d.strftime('%Y%m%d'))
        record = {'bookEpoch': 0, 'path': '/lang.epub', 'title': 'Làng ven biển', 'minutes': 5 * 60 + 10, 'ms': 0,
                  'turns': 300, 'first': code(today - datetime.timedelta(days=first_days_ago)),
                  'last': code(today - datetime.timedelta(days=1)), 'days': days, 'progress': progress,
                  'startProgress': 0}
        (self.store / 'reading-stats.json').write_text(json.dumps({'schema': 3, 'activeBook': record}))

    def test_recent_card_shows_the_expected_finish_date(self):
        # 40 % over 15 calendar days: a date 23 days out, so the card's second row is the estimate.
        self.seed_stats(14, 8, 40)
        log, _ = self.launch('3000:QUIT', [(2500, 'finish')])
        # Rows, one bit each: read, finish, total, average, days, span.
        self.assertEqual(CARD_STATS.findall(log)[0][0], '3f', log[-3000:])
        # One reading day is too little to estimate: the row is left out and the column closes up.
        self.seed_stats(0, 1, 40)
        log, _ = self.launch('3000:QUIT', [(2500, 'too-little')])
        self.assertEqual(CARD_STATS.findall(log)[0][0], '3d', log[-3000:])

    def text_rows(self, image, x0, x1, top, bottom):
        rows, run = [], None
        for y in range(top, bottom):
            dark = any(self.dark(image, x, y) for x in range(x0, x1))
            if dark and run is None:
                run = y
            elif not dark and run is not None:
                # A dot under a letter (ọ, ự) sits a row or two below the line: same line.
                if rows and run - rows[-1][1] <= 3:
                    run = rows.pop()[0]
                rows.append((run, y - 1)); run = None
        return rows

    def test_book_stats_lists_the_finish_date(self):
        self.seed_stats(14, 8, 40)
        # Stats tab (three steps with the default tab order), row 2 "by book", then the first book.
        _, images = self.launch('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4600:CONFIRM;6600:CONFIRM;9500:QUIT',
                                [(9000, 'book-stats')])
        image = images['book-stats']
        # Row labels sit at the left of the list, under the header: one text line per row.
        labels = self.text_rows(image, 20, 200, 110, 700)
        self.assertEqual(len(labels), 9, labels)


if __name__ == '__main__':
    unittest.main()
