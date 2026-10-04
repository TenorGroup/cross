"""v1.0.52: the Recent card, plan F (founder, 04/10).

The stats column is a third of the 480 px text width (160 px, x 344 to 503); the cover is 298 x 450 and keeps
that size whatever the title; the excerpt is one line ending in an ellipsis, and a two-line title drops it.
Stat labels are one size up (Geist 10), the progress bar is 10 px high with round ends and a round black part
read inside its edge, and the gap under the bar equals the gap between the groups below it. No stat value is
cut (VI and EN); the card file is the new format and an old one is not read.
"""
import datetime
import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_home_card_v1011 import epub_with_cover  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
STATS_X, STATS_RIGHT = 344, 504          # 160 px: a third of the 480 px text width
COVER = (24, 124, 322, 574)              # 298 x 450
BUILD = re.compile(r'Recent card build=(\d+)ms cache=(\d+) cover=(\d+)')
LONG_TITLE = 'Một cuốn sách có tên rất dài để cần tới hai dòng trên thẻ Gần đây của máy'


def runs(dark, x0, x1, y0=124, y1=590, gap=4):
    """Row runs of ink in a column band: (top, bottom) pairs, split where `gap` rows are empty."""
    rows = dark[y0:y1, x0:x1].any(axis=1)
    out, start, last = [], None, None
    for i, v in enumerate(rows):
        if not v:
            continue
        if start is None:
            start = i
        elif i - last > gap:
            out.append((start + y0, last + y0))
            start = i
        last = i
    if start is not None:
        out.append((start + y0, last + y0))
    return out


def retitle(path, title):
    """The book's own title is what the card shows: write it into the OPF."""
    with zipfile.ZipFile(path) as source:
        items = [(i, source.read(i.filename)) for i in source.infolist()]
    with zipfile.ZipFile(path, 'w') as book:
        for item, data in items:
            if item.filename == 'book.opf':
                data = data.decode('utf-8').replace('Synonym Lookup Test', title).encode('utf-8')
            book.writestr(item, data, compress_type=zipfile.ZIP_STORED if item.filename == 'mimetype' else
                          zipfile.ZIP_DEFLATED)


class Shot:
    """One launch of the simulator with a recent book that has a cover, stats and a quote-free page excerpt."""

    def __init__(self, lang, title, progress=20):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-card-f-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps({'language': lang, 'uiTheme': 4, 'sleepTimeout': 120}))
        epub_with_cover(self.sd / 'a.epub')
        if title:
            retitle(self.sd / 'a.epub', title)
        (self.store / 'recent.json').write_text(json.dumps({'books': [
            {'path': '/a.epub', 'title': title, 'author': 'Tenor', 'coverBmpPath': '', 'excerpt': 'x'}]}))
        # The reader writes the thumbnails and the stats record of the book: open it once and come back.
        self.first_log = self.run('1500:CONFIRM;9000:BACK;12500:QUIT', [])
        today = datetime.date.today()
        first = int((today - datetime.timedelta(days=20)).strftime('%Y%m%d'))
        record = {'bookEpoch': 0, 'path': '/a.epub', 'title': title, 'minutes': 23 * 60 + 14, 'ms': 0, 'turns': 412,
                  'first': first, 'last': int(today.strftime('%Y%m%d')), 'days': 7, 'progress': progress,
                  'startProgress': 0}
        (self.store / 'reading-stats.json').write_text(json.dumps({'schema': 3, 'activeBook': record}))
        self.log = self.run('2500:QUIT', [(2000, 'home')])
        self.warm_log = self.run('2500:QUIT', [(2000, 'warm')])
        self.dark = self.image('home') < 128
        self.warm = self.image('warm') < 128

    def run(self, events, shots):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        assert run.returncode == 0, (run.stdout + run.stderr)[-3000:]
        return run.stdout + run.stderr

    def image(self, name):
        with Image.open(self.sd / (name + '.bmp')) as image:
            return np.array(image.convert('L'))


class CardFTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.en = Shot('EN', '')
        cls.vi = Shot('VI', '')
        cls.long = Shot('VI', LONG_TITLE)

    def stat_runs(self, shot):
        return runs(shot.dark, STATS_X, STATS_RIGHT, 124, 575)

    def test_stats_column_is_a_third_of_the_text_width(self):
        for shot in (self.en, self.vi):
            band = shot.dark[124:575, 330:512]  # the arrow at x 519 is the tab's
            xs = np.where(band.any(axis=0))[0] + 330
            # The bar edge starts at the column's left and ends at its right, 160 px wide.
            bar = runs(shot.dark, STATS_X, STATS_RIGHT, 124, 575)[2]
            row = shot.dark[(bar[0] + bar[1]) // 2, 330:512]
            cols = np.where(row)[0] + 330
            self.assertEqual((int(cols.min()), int(cols.max())), (STATS_X, STATS_RIGHT - 1))
            # Nothing of the column is left of it (the cover ends at 321) or right of the text margin.
            self.assertGreaterEqual(int(xs.min()), 322)
            inside = shot.dark[124:575, 323:343]
            self.assertFalse(inside.any(), 'ink between the cover and the column')
            self.assertFalse(shot.dark[124:575, STATS_RIGHT:512].any(), 'stats past the right margin')

    def test_cover_is_298_by_450_and_the_card_draws_the_450_thumbnail(self):
        for shot in (self.en, self.vi, self.long):
            ys, xs = np.where(shot.dark[120:590, 16:340])
            xs = xs + 16  # the tab arrow at x 4 to 10 is not the cover
            self.assertTrue(24 <= xs.min() <= 26 and 319 <= xs.max() <= 321, (xs.min(), xs.max()))
            self.assertTrue(124 <= ys.min() + 120 <= 127 and 570 <= ys.max() + 120 <= 575, (ys.min(), ys.max()))
            built = BUILD.findall(shot.first_log)
            self.assertTrue(built, shot.first_log[-2000:])
            self.assertEqual(int(built[-1][2]), 450, 'the card did not draw the 450 px thumbnail')

    def test_no_stat_value_is_cut(self):
        for name, shot in (('EN', self.en), ('VI', self.vi), ('long', self.long)):
            self.assertNotIn('Card stat cut', shot.log, name)

    def test_labels_are_one_size_up(self):
        # "Read" / "Đã đọc" in Geist 8 was 12 rows of ink in the EN screenshot; Geist 10 is 14.
        top, bottom = self.stat_runs(self.en)[0]
        self.assertGreaterEqual(bottom - top + 1, 14)

    def test_bar_is_10_px_with_round_ends_and_a_round_black_part(self):
        for shot in (self.en, self.vi):
            top, bottom = runs(shot.dark, STATS_X, STATS_RIGHT, 124, 575)[2]
            self.assertEqual(bottom - top + 1, 10)
            d = shot.dark
            # Round track: the corners are white, the middle of the end is edge.
            for x, y in ((STATS_X, top), (STATS_X, bottom), (STATS_RIGHT - 1, top), (STATS_RIGHT - 1, bottom)):
                self.assertFalse(d[y, x], ('square corner', x, y))
            self.assertTrue(d[top + 5, STATS_X] or d[top + 4, STATS_X])
            self.assertTrue(d[top + 5, STATS_RIGHT - 1] or d[top + 4, STATS_RIGHT - 1])
            # 20 %: black from the edge to a round end at 346 + 156 * 20 / 100 = 377, white after it.
            mid = top + 5
            self.assertTrue(d[mid, STATS_X + 4] and d[mid, STATS_X + 20] and d[mid, 375])
            self.assertFalse(d[mid, 380] or d[mid, 400])
            end_x = 346 + 156 * 20 // 100                       # first column after the black part
            self.assertFalse(d[top + 2, end_x - 1] and d[top + 7, end_x - 1], 'read part ends square')
            # Black part inside the edge: the white gap between track and fill at 380 is empty, and the edge
            # above the fill is still drawn.
            self.assertTrue(d[top, STATS_X + 40] and d[bottom, STATS_X + 40])

    def test_gap_under_the_bar_equals_the_gap_between_groups(self):
        r = self.stat_runs(self.en)
        self.assertEqual(len(r), 11, r)
        bar, finish_label = r[2], r[3]
        total_value, average_label = r[6], r[7]                  # "23 h 14 min": no descender
        under_bar = finish_label[0] - bar[1] - 1
        between = average_label[0] - total_value[1] - 1
        # Cap top against ascender top differ by a row at most.
        self.assertLessEqual(abs(under_bar - between), 1, (under_bar, between))

    def test_one_line_excerpt_with_an_ellipsis_and_none_under_a_two_line_title(self):
        shot = self.en
        quote = runs(shot.dark, 24, 504, 600, 720)
        # Title, author, excerpt: three runs; the excerpt is one line.
        self.assertEqual(len(quote), 3, quote)
        e_top, e_bottom = quote[2]
        self.assertLess(e_bottom - e_top, 40)
        # It was cut to the width and ends in the ellipsis: the last 10 columns hold three dots, not a letter.
        line = shot.dark[e_top:e_bottom + 1, 24:504]
        right = int(np.where(line.any(axis=0))[0].max())
        self.assertGreaterEqual(right, 440)
        tail = np.where(line[:, right - 9:right + 1].any(axis=1))[0]
        self.assertLessEqual(int(tail.max() - tail.min()) + 1, 6, 'the line does not end in an ellipsis')
        # The long title is two lines, then the author; nothing after the author.
        long_runs = runs(self.long.dark, 24, 504, 576, 740)
        self.assertEqual(len(long_runs), 3, long_runs)
        self.assertGreater(long_runs[1][0] - long_runs[0][0], 30, 'the title did not wrap')
        self.assertLess(long_runs[2][1], 700, 'an excerpt under a two-line title')

    def test_card_file_has_the_new_magic_and_a_cold_card_equals_a_warm_one(self):
        for shot in (self.en, self.vi):
            cards = sorted(shot.store.glob('epub_*/thumb2_450.bmp.card*'))
            self.assertTrue(cards, 'no card file written')
            self.assertEqual(cards[0].read_bytes()[:4], b'CRD4')
            self.assertIn('Recent card file=', shot.warm_log)
            self.assertFalse(BUILD.findall(shot.warm_log), 'the warm visit rebuilt the card')
            self.assertTrue((shot.dark[:740] == shot.warm[:740]).all(), 'cold card and cached card differ')

    def test_an_old_format_card_file_is_not_read(self):
        shot = self.en
        card = sorted(shot.store.glob('epub_*/thumb2_450.bmp.card*'))[0]
        data = bytearray(card.read_bytes())
        data[:4] = b'CRD3'
        card.write_bytes(bytes(data))
        log = shot.run('2500:QUIT', [(2000, 'old')])
        self.assertTrue(BUILD.findall(log), 'an old card file was used')
        self.assertEqual(card.read_bytes()[:4], b'CRD4', 'the card was not written again in the new format')


if __name__ == '__main__':
    unittest.main()
