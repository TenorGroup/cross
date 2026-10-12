"""tenor/ugly on the X3, the quotation page written by hand (v1.0.59), read on the pixels and the log of the simulator.

 1. The quote is set in the pen (size 30, a row every 40 px) and a long one turns pages without losing a word: the rows of all
    pages read back as the quote, and the facts under it (book, chapter, moment, page) all stand on some page.
 2. Each quote has its line of abuse at the head of the page (8 lines, the same quote always the same one, the lines spread over
    the quotes), in the language of the screen.
 3. The Select key is the pen's tick in the foot bar, not the cut word "Options".
 4. The scribbles on the empty part of the page never touch a word: what they add to the page lies in the boxes the page kept
    clear of the words, none of it within 3 px of the page's own ink, the same quote on the same page scribbles the same pixels
    when it is drawn again, and a page full of words has none.
"""
import json
import re
import unicodedata
import unittest
from pathlib import Path

from PIL import ImageChops, ImageFilter

import cross_pills
from test_quotes_v1011 import write_quote
from test_ugly_issue3_v1058 import HAND, NOW, bands
from ugly_common import Card, REPO

TO_DETAIL = ['DOWN'] * 4 + ['RIGHT'] * 3 + ['CONFIRM'] * 3  # Stats, Quotations, the first book, its first quotation
WORDS = ('đi cho biết đó biết đây ở nhà với mẹ biết ngày nào khôn pleasure ice novelty melt order desirable quiet river '
         'lamp window thought morning bread silence garden road letter evening stone water').split()
TITLE = 'A very long title of a book that wraps onto several lines of the page and then a few more words to be sure'


def long_text(size, seed=7):
    """About `size` bytes of UTF-8 (the store takes 1024 at most), whole words, ending in a full stop."""
    out, i = [], seed
    while len(' '.join(out).encode()) < size:
        i = (i * 1103515245 + 12345) & 0x7fffffff
        out.append(WORDS[i // 7 % len(WORDS)])
    out.pop()
    return ' '.join(out) + '.'


def quote_file(card, text, title='Eros the Bittersweet', slot=0, spine=28, page=8, minute=17 * 60 + 12, path='/sach/eros.epub'):
    write_quote(card.store / 'quotes', path, title, text, spine, page, 20261011, minute, slot)


def yaml_quips(language):
    name = {'EN': 'english', 'VI': 'vietnamese'}[language]
    text = (REPO / 'lib/I18n/translations' / (name + '.yaml')).read_text(encoding='utf-8')
    return [re.search(r'^STR_UGLY_QUOTE_QUIP_%d: "(.*)"$' % n, text, re.M).group(1) for n in range(1, 9)]


def run(quotes, keys, shots=(), tier=0, level=1, language='EN', env=None, timeout=240):
    """Plays `keys` (1500 ms apart) on a card holding `quotes` ([kwargs of quote_file]). shots: [(index of the key it follows,
    name)]. Returns (log, {name: image})."""
    card = Card(shell=1, uiTextSize=tier, uiUglyLevel=level, **dict(HAND, language=language))
    try:
        for q in quotes:
            quote_file(card, **q)
        parts = ['%d:%s' % (1500 * (i + 1), k) for i, k in enumerate(keys)]
        end = 1500 * len(keys) + 2200
        parts.append('%d:QUIT' % (end + 700))
        timed = [(1500 * (i + 1) + 1200, name) for i, name in shots]
        log, images = card.run(';'.join(parts), timed, timeout=timeout, CROSSPOINT_SIM_NOW=NOW,
                               CROSSPOINT_SIM_MARK_SEED='12345', **(env or {}))
        return log, {name: im.convert('L') for name, im in images.items()}
    finally:
        card.close()


WORD = re.compile(r'QuoteWord page=(\d+) row=(\d+) x=(\d+) y=(\d+) (.*)')
META = re.compile(r'QuoteMeta page=(\d+) row=(\d+) y=(\d+) (.*)')
PAGE = re.compile(r'QuotePage quote=(\d+) page=(\d+)/(\d+) bodyPages=(\d+) rows=(\d+) lines=(\d+)(?: quip=(\d+) quipRows=(\d+))? scraps=(\d+) band=(-?\d+) ms=\d+ free=\d+ largest=\d+ (.*)')
SCRAP = re.compile(r'QuoteScrap page=(\d+) kind=(\d+) pic=(\d+) box=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
KEEP = re.compile(r'QuoteWords page=(\d+) box=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')


def renders(log):
    """Every page drawn, in order: {'page', 'pages', 'quote', 'quip', 'quipText', 'words', 'meta', 'scraps', 'keep', ...}."""
    out, cur = [], None
    for line in log.splitlines():
        for regex, kind in ((WORD, 'w'), (META, 'm'), (PAGE, 'p'), (SCRAP, 's'), (KEEP, 'k')):
            m = regex.search(line)
            if not m:
                continue
            if kind in 'wm' and (cur is None or 'page' in cur):
                cur = {'words': [], 'meta': [], 'scraps': [], 'keep': []}
                out.append(cur)
            if cur is None:
                break
            if kind == 'w':
                cur['words'].append((int(m[2]), int(m[3]), int(m[4]), m[5]))
            elif kind == 'm':
                cur['meta'].append((int(m[2]), int(m[3]), m[4]))
            elif kind == 'p':
                cur.update(quote=int(m[1]), page=int(m[2]), pages=int(m[3]), bodyPages=int(m[4]), rows=int(m[5]), lines=int(m[6]),
                           quip=int(m[7] or -1), quipRows=int(m[8] or 0), scrapCount=int(m[9]), band=int(m[10]), quipText=m[11])
            elif kind == 's':
                cur['scraps'].append((int(m[2]), int(m[3]), tuple(int(m[i]) for i in range(4, 8))))
            else:
                cur['keep'].append(tuple(int(m[i]) for i in range(2, 6)))
            break
    return [r for r in out if 'page' in r]


def last_of_each_page(rs):
    by = {}
    for r in rs:
        by[(r['quote'], r['page'])] = r
    return by


def kus(r):
    """The ku among the scribbles of a render (kind 0 is a doodle of the set, picture 7 the ku)."""
    return [s for s in r['scraps'] if s[0] == 0 and s[1] == 7]


def worms(r):
    return [s for s in r['scraps'] if s[0] == 5]


def letters(text):
    return re.sub(r'\s+', '', unicodedata.normalize('NFC', text))


def meets(a, b, grow=0):
    return a[0] < b[2] + grow and b[0] - grow < a[2] and a[1] < b[3] + grow and b[1] - grow < a[3]


def dilate(im, radius):
    return im.filter(ImageFilter.MinFilter(2 * radius + 1))  # black is 0: the minimum grows the ink


def added_ink(with_scraps, without):
    """Pixels black in `with_scraps` and white in `without`, as a list of (x, y)."""
    diff = ImageChops.subtract(without, with_scraps)  # white where the first is white and the second black
    px = diff.load()
    w, h = diff.size
    return [(x, y) for y in range(h) for x in range(w) if px[x, y] > 128]


class UglyQuotePageTest(unittest.TestCase):
    def test_1_a_long_quote_turns_pages_and_keeps_every_word_and_every_fact(self):
        text = long_text(1010)
        keys = TO_DETAIL + ['DOWN'] * 7
        log, _ = run([dict(text=text, title=TITLE)], keys)
        pages = last_of_each_page(renders(log))
        total = max(p for _, p in pages)
        self.assertGreaterEqual(total, 3, 'a quote of 1010 bytes in the pen takes 3 pages or more: ' + log[-1500:])
        body = unicodedata.normalize('NFC', text) + '”'
        read = []
        for page in range(1, total + 1):
            r = pages[(0, page)]
            rows = sorted({w[0] for w in r['words']})
            self.assertEqual(rows, list(range(len(rows))), f'page {page} rows are not one under another')
            if page < r['bodyPages']:
                self.assertEqual(len(rows), r['rows'], f'page {page} is not full')
            for row in rows:
                parts = sorted((w for w in r['words'] if w[0] == row), key=lambda w: w[1])
                self.assertTrue(all(w[2] == 16 + 188 + 40 * row for w in parts), f'row {row} is off its baseline')
                self.assertTrue(all(w[1] >= 48 for w in parts))
                read.append(' '.join(w[3] for w in parts))
        self.assertEqual(' '.join(read).split(), body.split(), 'a word of the quote was lost, added or moved')
        facts = [m for page in range(1, total + 1) for m in sorted(pages[(0, page)]['meta'])]
        self.assertEqual(letters(''.join(m[2] for m in facts)),
                         letters(TITLE + 'Chapter 29' + '11/10/2026 17:12' + 'Page 9'), 'a fact under the quote is missing')
        self.assertEqual(pages[(0, total)]['pages'], total)

    def test_2_every_quote_has_its_line_of_abuse_and_it_stays_when_the_quote_is_met_again(self):
        quotes = [dict(text='Quote number %d about a quiet morning by the river.' % n, slot=n % 16, minute=600 + 3 * n)
                  for n in range(24)]
        for language in ('EN', 'VI'):
            with self.subTest(language=language):
                log, _ = run(quotes, TO_DETAIL + ['RIGHT'] * 23 + ['LEFT'] * 3 + ['RIGHT'] * 3, language=language)
                rs = renders(log)
                first = {}
                for r in rs:
                    first.setdefault(r['quote'], r['quip'])
                    self.assertEqual(first[r['quote']], r['quip'], 'the same quote changed its line')
                    self.assertEqual(r['quipText'], yaml_quips(language)[r['quip']])
                    self.assertLessEqual(r['quipRows'], 2, 'the line of abuse runs into the rule under it')
                self.assertEqual(len(first), 24)
                self.assertGreaterEqual(len(set(first.values())), 6, 'the 8 lines are spread over the quotes')

    def test_3_the_select_key_is_the_pens_tick_not_a_cut_word(self):
        for language in ('EN', 'VI'):
            with self.subTest(language=language):
                _, shots = run([dict(text='Short words.')], TO_DETAIL, [(len(TO_DETAIL) - 1, 'f')], language=language)
                im = shots['f']
                px = im.load()
                ink = [(x, y) for y in range(745, 792) for x in range(150, 300) if px[x, y] < 128]
                self.assertTrue(ink, 'no mark over the Select key')
                width = max(x for x, _ in ink) - min(x for x, _ in ink) + 1
                height = max(y for _, y in ink) - min(y for _, y in ink) + 1
                self.assertLessEqual(width, 34, f'over the Select key: {width} px wide, a word, not the tick')
                self.assertLessEqual(height, 34)

    def test_4_the_facts_stand_centred_in_the_pen_at_every_size_and_hand(self):
        for level in (0, 1):
            for tier in (0, 1, 2):
                with self.subTest(level=level, tier=tier):
                    _, shots = run([dict(text='If ice-pleasure consists, to some degree, in novelty, then ice must melt in order '
                                              'to be desirable.')], TO_DETAIL, [(len(TO_DETAIL) - 1, 'f')], tier=tier, level=level,
                                   env=dict(CROSSPOINT_SIM_UGLY_SCRAP='0'))
                    im = shots['f']
                    px = im.load()
                    meta = bands(im, 40, 500, 20, 690)[-4:]
                    self.assertEqual(len(meta), 4)
                    for top, bottom in meta:
                        xs = [x for x in range(40, 500) for y in range(top, bottom + 1) if px[x, y] < 128]
                        self.assertLessEqual(abs((min(xs) + max(xs)) / 2 - 263.5), 0.51, f'{top}..{bottom}: {min(xs)}..{max(xs)}')
                    # the quote stays in its column: nothing left of the margin of the page, nothing past the right edge
                    body = [(x, y) for y in range(160, meta[0][0] - 6) for x in range(0, 528) if px[x, y] < 128]
                    self.assertTrue(body)
                    self.assertGreaterEqual(min(x for x, _ in body if x > 30), 40)
                    self.assertLessEqual(max(x for x, _ in body), 500)
                    self.assertEqual(cross_pills.find(im), [], 'a tenor/cross pill')

    def test_5_the_scribbles_stay_clear_of_the_words_and_come_back_the_same(self):
        short = {'EN': 'If ice-pleasure consists, to some degree, in novelty, then ice must melt in order to be desirable.',
                 'VI': 'Đi cho biết đó biết đây, ở nhà với mẹ biết ngày nào khôn.'}
        cases = [('EN', level, tier) for level in (0, 1) for tier in (0, 1, 2)] + [('VI', 1, 0), ('VI', 0, 2)]
        for language, level, tier in cases:
            with self.subTest(language=language, level=level, tier=tier):
                quotes = [dict(text=short[language])]
                keys = TO_DETAIL + ['BACK', 'CONFIRM']
                shots = [(len(TO_DETAIL) - 1, 'first'), (len(keys) - 1, 'again')]
                log, with_ = run(quotes, keys, shots, tier=tier, level=level, language=language)
                _, without = run(quotes, TO_DETAIL, [(len(TO_DETAIL) - 1, 'f')], tier=tier, level=level, language=language,
                                 env=dict(CROSSPOINT_SIM_UGLY_SCRAP='0'))
                r = renders(log)[-1]
                self.assertGreaterEqual(r['scrapCount'], 3, 'a short page has scribbles on it')
                self.assertGreaterEqual(r['band'], 112)
                self.assertEqual(len(kus(r)), 1, 'a page with room has one ku')
                self.assertGreaterEqual(len(worms(r)), 3, 'a page with room has 3 worms at least')
                self.assertEqual(ImageChops.difference(with_['first'], with_['again']).getbbox(), None,
                                 'the page drew other scribbles when it was drawn again')
                added = added_ink(with_['first'], without['f'])
                self.assertGreater(len(added), 300)
                near = dilate(without['f'], 3).load()
                boxes = [s[2] for s in r['scraps']]
                for x, y in added:
                    self.assertGreater(near[x, y], 128, f'a scribble pixel at {x},{y} is within 3 px of the page')
                    self.assertTrue(any(b[0] - 1 <= x < b[2] + 1 and b[1] - 1 <= y < b[3] + 1 for b in boxes),
                                    f'a scribble pixel at {x},{y} is outside every box the page kept for them')
                for i, a in enumerate(boxes):
                    for k in r['keep']:
                        self.assertFalse(meets(a, k, 14), f'scribble box {a} comes within 14 px of the words {k}')
                    for b in boxes[i + 1:]:
                        self.assertFalse(meets(a, b))

    def test_6_a_page_full_of_words_has_no_scribble_and_a_short_last_page_has_some(self):
        text = long_text(1010)
        keys = TO_DETAIL + ['DOWN'] * 7
        log, _ = run([dict(text=text, title='Eros')], keys)
        pages = last_of_each_page(renders(log))
        total = max(p for _, p in pages)
        for page in range(1, total):
            self.assertEqual(pages[(0, page)]['scrapCount'], 0, f'page {page} is full of words')
        for (_, page), r in pages.items():  # in every page of the quote: a ku and 3 worms from 112 px of room, else no ku
            if r['band'] >= 112:
                self.assertEqual(len(kus(r)), 1, f'page {page} has room and no ku, or two')
                self.assertGreaterEqual(len(worms(r)), 3, f'page {page} has room and fewer than 3 worms')
            else:
                self.assertEqual(len(kus(r)), 0, f'page {page} has no room for a ku')
        _, shots = run([dict(text=text, title='Eros')], TO_DETAIL, [(len(TO_DETAIL) - 1, 'f')])
        _, bare = run([dict(text=text, title='Eros')], TO_DETAIL, [(len(TO_DETAIL) - 1, 'f')], env=dict(CROSSPOINT_SIM_UGLY_SCRAP='0'))
        self.assertEqual(ImageChops.difference(shots['f'], bare['f']).getbbox(), None, 'a full page was scribbled on')


if __name__ == '__main__':
    unittest.main()
