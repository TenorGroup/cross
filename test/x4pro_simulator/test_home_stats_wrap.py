"""X4 Pro Home, Cover and stats: a real value that wraps where its sample does not (1h 41m/day against the 2h 1m/day
sample, 123h 45m in all) keeps the stats column clear of the book's title, at text sizes 0, 1 and 2.

Each size is drawn twice from the same card: with the long title, and with a 1-letter title. The stats geometry does
not depend on the title (the frame is sized on 2 title lines), so the 1-letter frame gives the stats ink and the long
one adds the title. Size 0 (title under the cover): the stats ink ends above the title's first row. Sizes 1 and 2
(title above): no title row falls inside the stats rows. Runs the X4 Pro simulator; X4PRO_PROGRAM picks the build.
"""
import json
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'test/reading_stats_simulator'))
from test_home_card_v1011 import cover_jpeg  # noqa: E402

PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
PATH = '/books/viec-lon.epub'
TITLE = 'Những việc lớn được hoàn thành như thế nào'
# 123h 45m over 73 days: 1h 41m a day, wider than the 2h 1m sample; 25 page turns (the device photo).
RECORD = dict(bookEpoch=0, path=PATH, minutes=123 * 60 + 45, ms=0, turns=25, first=20260801, last=20261010, days=73,
              progress=34, startProgress=0)


def cache_hash(tmp, path):
    exe = Path(tmp) / 'hash'
    if not exe.exists():
        src = Path(tmp) / 'hash.cpp'
        src.write_text('#include <functional>\n#include <iostream>\n#include <string>\n'
                       'int main(int c,char**v){std::cout<<std::hash<std::string>{}(v[1]);}\n')
        subprocess.run(['c++', '-std=c++17', str(src), '-o', str(exe)], check=True)
    return subprocess.check_output([str(exe), path], text=True).strip()


def shoot(tmp, name, tier, title, record=RECORD):
    sd = Path(tmp) / name
    store = sd / '.crosspoint'
    cache = store / ('epub_' + cache_hash(tmp, PATH))
    cache.mkdir(parents=True)
    (cache / 'cover.ref').write_bytes(b'CREF\x01\x09cover.jpg')
    book = sd / PATH.lstrip('/')
    book.parent.mkdir(parents=True)
    with zipfile.ZipFile(book, 'w') as z:
        z.writestr('cover.jpg', cover_jpeg())
    books = [dict(path=PATH, title=title, author='Tenor', coverBmpPath='/' + str(cache.relative_to(sd)) + '/thumb2_[HEIGHT].bmp')]
    (store / 'recent.json').write_text(json.dumps(dict(books=books), ensure_ascii=False), encoding='utf-8')
    if record:  # no record: a book with no reading numbers yet
        (store / 'reading-stats.json').write_text(json.dumps(dict(schema=3, activeBook=dict(record, title=title)),
                                                             ensure_ascii=False), encoding='utf-8')
    (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
    (store / 'settings.json').write_text(json.dumps(dict(language='VI', uiTheme=4, uiShell=0, uiTextSize=tier, homeStyle=0,
                                                         sleepTimeout=120, wakeIntoBook=0, tipsEnabled=0)))
    shot = sd / 'home.bmp'
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT='6300:QUIT', CROSSPOINT_SIM_SCREENSHOTS=f'6000:{shot}')
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
    log = run.stdout + run.stderr
    assert run.returncode == 0 and shot.exists(), log[-1500:]
    geometry = [line for line in log.splitlines() if 'Card geometry cover=' in line]
    assert geometry, 'no Cover and stats card with a real cover'
    cover = [int(v) for v in geometry[-1].split('cover=')[1].split()[0].split(',')]
    stats_x = int(geometry[-1].split('stats=')[1].split(',')[0])
    out = os.environ.get('X4PRO_TEST_SHOTS')
    if out:
        Path(out).mkdir(parents=True, exist_ok=True)
        Image.open(shot).convert('L').save(Path(out) / f'{name}.png')
    return np.asarray(Image.open(shot).convert('L')) < 128, cover, stats_x


def rows(mask):
    return np.nonzero(mask.any(axis=1))[0]


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='home-stats-wrap-', dir=os.environ.get('TMPDIR')) as tmp:
        for tier in (0, 1, 2):
            long_ink, cover, stats_x = shoot(tmp, f'long-{tier}', tier, TITLE)
            short_ink, cover2, stats_x2 = shoot(tmp, f'short-{tier}', tier, 'A')
            assert (cover, stats_x) == (cover2, stats_x2), (tier, cover, cover2, stats_x, stats_x2)
            x0, x1, band = stats_x, long_ink.shape[1] - 16, slice(36, 712)  # the status strip and the bar stay out
            stats = np.zeros_like(short_ink)
            stats[band, x0:x1] = short_ink[band, x0:x1]
            title = long_ink & ~short_ink
            title[:, :x0] = False
            s, t = rows(stats), rows(title[band]) + band.start
            assert len(s) and len(t), (tier, len(s), len(t))
            if tier == 0:
                ok = s.max() < t.min()
                report = f'stats ink ends at row {s.max()}, the title starts at row {t.min()}'
            else:
                inside = [int(r) for r in t if s.min() <= r <= s.max()]
                ok = not inside
                report = f'stats rows {s.min()}..{s.max()}, title rows in them {inside[:4]}'
            print(('GREEN' if ok else 'RED') + f' size {tier}: {report}', flush=True)
            if not ok:
                failures.append(tier)
    assert not failures, f'the stats column runs into the title at sizes {failures}'
    print('GREEN: a wrapping real value keeps the stats column clear of the title at sizes 0, 1 and 2')


if __name__ == '__main__':
    main()
