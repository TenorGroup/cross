"""tenor/ugly on the X4 Pro, the quotation page under the header of the touch reader (v1.0.59).

Runs the X4 Pro simulator: X4PRO_PROGRAM (or TEST_PROGRAM) picks the build. The page is the one of the button readers
(test/reading_stats_simulator/test_ugly_quote_page_v1059.py) laid out between the header and the foot of the content:
 1. a long quote turns pages without losing a word or a fact, every row on the pen's rows;
 2. the scribbles stay clear of the words, come back the same, and a page full of words has none.
"""
import os
import sys
import unicodedata
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
os.environ.setdefault('TEST_PROGRAM', os.environ.get('X4PRO_PROGRAM', str(REPO / '.pio/build/simulator_x4pro/program')))
sys.path.insert(0, str(REPO / 'test/reading_stats_simulator'))
import test_ugly_quote_page_v1059 as q  # noqa: E402
from PIL import ImageChops  # noqa: E402

TO_DETAIL = q.TO_DETAIL
SHORT = 'If ice-pleasure consists, to some degree, in novelty, then ice must melt in order to be desirable.'


class UglyQuotePageX4ProTest(unittest.TestCase):
    def test_1_a_long_quote_turns_pages_and_keeps_every_word_and_every_fact(self):
        text = q.long_text(1010)
        log, _ = q.run([dict(text=text, title=q.TITLE)], TO_DETAIL + ['DOWN'] * 7)
        pages = q.last_of_each_page(q.renders(log))
        total = max(p for _, p in pages)
        self.assertGreaterEqual(total, 3, log[-1500:])
        body = unicodedata.normalize('NFC', text) + '”'
        read = []
        top = None
        for page in range(1, total + 1):
            r = pages[(0, page)]
            rows = sorted({w[0] for w in r['words']})
            self.assertEqual(rows, list(range(len(rows))))
            if page < r['bodyPages']:
                self.assertEqual(len(rows), r['rows'], f'page {page} is not full')
            for row in rows:
                parts = sorted((w for w in r['words'] if w[0] == row), key=lambda w: w[1])
                top = top if top is not None else parts[0][2] - 40 * row
                self.assertTrue(all(w[2] == top + 40 * row for w in parts), f'row {row} is off its baseline')
                self.assertTrue(all(48 <= w[1] < 480 - 40 for w in parts), 'a word outside the column')
                read.append(' '.join(w[3] for w in parts))
        self.assertEqual(' '.join(read).split(), body.split())
        facts = [m for page in range(1, total + 1) for m in sorted(pages[(0, page)]['meta'])]
        self.assertEqual(q.letters(''.join(m[2] for m in facts)), q.letters(q.TITLE + 'Chapter 29' + '11/10/2026 17:12' + 'Page 9'))

    def test_2_the_scribbles_stay_clear_of_the_words_and_come_back_the_same(self):
        quotes = [dict(text=SHORT)]
        keys = TO_DETAIL + ['BACK', 'CONFIRM']
        shots = [(len(TO_DETAIL) - 1, 'first'), (len(keys) - 1, 'again')]
        log, with_ = q.run(quotes, keys, shots)
        _, without = q.run(quotes, TO_DETAIL, [(len(TO_DETAIL) - 1, 'f')], env=dict(CROSSPOINT_SIM_UGLY_SCRAP='0'))
        r = q.renders(log)[-1]
        self.assertGreaterEqual(r['scrapCount'], 2)
        self.assertGreaterEqual(r['band'], 112)
        self.assertEqual(len(q.kus(r)), 1, 'a page with room has one ku')
        self.assertGreaterEqual(len(q.worms(r)), 3, 'a page with room has 3 worms at least')
        self.assertEqual(ImageChops.difference(with_['first'], with_['again']).getbbox(), None)
        added = q.added_ink(with_['first'], without['f'])
        self.assertGreater(len(added), 200)
        near = q.dilate(without['f'], 3).load()
        boxes = [s[2] for s in r['scraps']]
        for x, y in added:
            self.assertGreater(near[x, y], 128, f'a scribble pixel at {x},{y} is within 3 px of the page')
            self.assertTrue(any(b[0] - 1 <= x < b[2] + 1 and b[1] - 1 <= y < b[3] + 1 for b in boxes), f'{x},{y} is outside every box')
        for a in boxes:
            for k in r['keep']:
                self.assertFalse(q.meets(a, k, 14))
        # a page full of words
        text = q.long_text(1010)
        log, _ = q.run([dict(text=text, title='Eros')], TO_DETAIL + ['DOWN'] * 7)
        pages = q.last_of_each_page(q.renders(log))
        for page in range(1, max(p for _, p in pages)):
            self.assertEqual(pages[(0, page)]['scrapCount'], 0, f'page {page} is full of words')
        for (_, page), r in pages.items():
            if r['band'] >= 112:
                self.assertEqual(len(q.kus(r)), 1, f'page {page}')
                self.assertGreaterEqual(len(q.worms(r)), 3, f'page {page}')
            else:
                self.assertEqual(len(q.kus(r)), 0, f'page {page}')


if __name__ == '__main__':
    unittest.main()
