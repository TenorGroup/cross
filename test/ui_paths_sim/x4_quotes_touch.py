#!/usr/bin/env python3
"""X4 Pro Quotations by touch: the list, a quote, its options and the trim, all reached without keys.

x4_quotes_touch.py <simulator_x4pro program> <output dir> [--steps name=KEY;...]
Five quotes of one book. Run 1: Stats -> Quotations (a tap), the book (a tap), the bar's view pill -> "Oldest
first"; a tap opens the second quote; a sideways swipe goes to the third; the bar's pill opens the options;
Edit opens the trim; a tap on the fifth word makes it the first kept; the bar's pill saves (13 of 17 words).
Run 2: a hold on the list's first quote offers Edit and Delete; Delete asks to delete that quote.
With --steps, only run 1 up to the book, then the given steps (for finding places on screen).
RED on v1.0.58: the Quotations screens took no touch, so none of this happened.
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import sim
from test_quotes_v1011 import write_quote

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30851)
ap.add_argument('--steps', default='')
a = ap.parse_args()
out = a.output.resolve()


def seeded(folder):
    sd = sim.card(folder)
    quotes = sd / '.crosspoint' / 'quotes'
    quotes.mkdir(parents=True, exist_ok=True)
    (quotes / '.ten-v2').write_text('2')
    for i in range(5):
        write_quote(quotes, '/sach/ben-song.epub', 'Bến sông ngày gió',
                    f'Câu trích số {i + 1} kể chuyện bến sông lúc gió về chiều, nước lên ngang bậc đá.', 1, i + 1,
                    20260901 + i, i * 30)
    return sd


STATS_TAB, SCROLL, QUOTES_ROW, BOOK_ROW = 'TAP:330,754', 'SWIPE:240,650,240,400,900', 'TAP:240,560', 'TAP:240,150'
PILL = 'TAP:420,754'  # the screen's own pill, at the bar's right end
reach = [('00-home', None), ('01-stats', STATS_TAB), ('02-scroll', SCROLL), ('03-quotes', QUOTES_ROW), ('04-book', BOOK_ROW)]
if a.steps:
    log, _ = sim.run(a.program, out, seeded(out), reach + [tuple(s.split('=', 1)) for s in a.steps.split(';')], a.port, gap=3000)
    print(re.findall(r'(enter \w+|Quote (?:view|detail|trim|list|delete)[^\n]*)', log))
    sys.exit(0)
log, shots = sim.run(a.program, out / 'chinh', seeded(out / 'chinh'), reach + [
    ('05-view', PILL), ('06-oldest', 'TAP:230,618'), ('07-open', 'TAP:240,370'), ('08-swipe', 'SWIPE:400,400,80,400,200'),
    ('09-options', PILL), ('10-edit', 'TAP:240,562'), ('11-word', 'TAP:290,118'), ('12-save', PILL)], a.port, gap=3000)
checks = {
    'view pill picks Oldest first': 'Quote view book oldest' in log,
    'a tap opens the second quote': 'Quote detail 2/5' in log,
    'a sideways swipe shows the third': 'Quote detail 3/5' in log,
    'the options pill leads to the trim': 'enter QuoteTrim' in log,
    'a tapped word and the save pill trim it': bool(re.search(r'Quote trimmed [0-9a-f]+: kept 13 of 17 words', log)),
}
log2, _ = sim.run(a.program, out / 'xoa', seeded(out / 'xoa'), reach + [('05-hold', 'TAP:240,150,900'), ('06-delete', 'TAP:105,412')],
                  a.port, gap=3000)
checks['a hold on a quote offers Delete'] = 'Quote delete asks' in log2
for name, ok in checks.items():
    print(('ok   ' if ok else 'FAIL ') + name)
assert all(checks.values()), 'RED: Quotations do not answer touch'
print('GREEN')
