"""X4 Pro Home, Cover and stats: a book with no reading numbers shows "Chua ghi nhan" in the stats column, and the
cover keeps the size it has once the book has numbers (text sizes 0, 1 and 2). The placeholder is the only row drawn,
inside a frame sized for the 6 rows, so its 2nd line cannot reach the title: the stats ink stays clear of it.

A record whose values are the frame's samples (15%, 10h 8m, 2h 1m/day, 5 days, 24 turns) stands for a book with
numbers. Runs the X4 Pro simulator; X4PRO_PROGRAM picks the build.
"""
import sys
import tempfile
import os
from pathlib import Path

from test_home_stats_wrap import PATH, TITLE, rows, shoot

# 608 minutes over 5 days is 10h 8m in all and 2h 1m a day: none of the values is wider than its sample.
SAMPLE = dict(bookEpoch=0, path=PATH, minutes=608, ms=0, turns=24, first=20261006, last=20261010, days=5,
              progress=15, startProgress=0)


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='home-stats-none-', dir=os.environ.get('TMPDIR')) as tmp:
        for tier in (0, 1, 2):
            _, with_numbers, _ = shoot(tmp, f'numbers-{tier}', tier, 'A', SAMPLE)
            short_ink, none, stats_x = shoot(tmp, f'none-short-{tier}', tier, 'A', None)
            long_ink, none_long, _ = shoot(tmp, f'none-long-{tier}', tier, TITLE, None)
            same = with_numbers == none == none_long
            stats = short_ink.copy()
            stats[:, :stats_x] = False
            stats[:36] = False
            stats[712:] = False
            title = long_ink & ~short_ink
            title[:, :stats_x] = False
            s, t = rows(stats), rows(title)
            clear = bool(len(s) and len(t)) and (s.max() < t.min() if tier == 0 else not [r for r in t if s.min() <= r <= s.max()])
            print(('GREEN' if same and clear else 'RED') +
                  f' size {tier}: cover with numbers {with_numbers}, with none {none}; stats ink clear of the title: {clear}',
                  flush=True)
            if not (same and clear):
                failures.append(tier)
    assert not failures, f'the cover changes size or the stats meet the title at sizes {failures}'
    print('GREEN: a book with no numbers keeps its cover size and the stats clear of the title at sizes 0, 1 and 2')


if __name__ == '__main__':
    main()
