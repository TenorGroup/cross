"""X4 Pro Home, Shelf: a sideways swipe anywhere on the card slides the row of other books by 3 and the shown book stays
(a swipe on the Large cover and the Stats card still steps to the next book). 7 books: others 1-6, 2 pages.
Swipe left (next page): 1 2 3 -> 4 5 6; swipe left again returns round the ring to 1 2 3; swipe right goes back.
The cover at the top left stays book 0. X4PRO_PROGRAM picks the simulator_x4pro build.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'test/reading_stats_simulator'))
from test_home_card_v1011 import cover_jpeg  # noqa: E402

PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
BOOKS = [f'/books/b{i}.epub' for i in range(7)]


def cache_hash(tmp, path):
    exe = Path(tmp) / 'hash'
    if not exe.exists():
        src = Path(tmp) / 'hash.cpp'
        src.write_text('#include <functional>\n#include <iostream>\n#include <string>\n'
                       'int main(int c,char**v){std::cout<<std::hash<std::string>{}(v[1]);}\n')
        subprocess.run(['c++', '-std=c++17', str(src), '-o', str(exe)], check=True)
    return subprocess.check_output([str(exe), path], text=True).strip()


def shoot(tmp, style, swipes):
    sd = Path(tmp) / f'x4-{style}-{len(swipes)}'
    store = sd / '.crosspoint'
    books = []
    for i, path in enumerate(BOOKS):
        cache = store / ('epub_' + cache_hash(tmp, path))
        cache.mkdir(parents=True)
        (cache / 'cover.ref').write_bytes(b'CREF\x01\x09cover.jpg')
        book = sd / path.lstrip('/')
        book.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(book, 'w') as z:
            z.writestr('cover.jpg', cover_jpeg())
        books.append(dict(path=path, title=f'Sach {i}', author='Tenor',
                          coverBmpPath='/' + str(cache.relative_to(sd)) + '/thumb2_[HEIGHT].bmp'))
    (store / 'recent.json').write_text(json.dumps(dict(books=books)))
    (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
    (store / 'settings.json').write_text(json.dumps(dict(language='VI', homeStyle=style, uiTextSize=0, uiShell=0, uiTheme=4,
                                                         sleepTimeout=120, tipsEnabled=0)))
    script = [f'{5000 + 1500 * i}:SWIPE:{a},{y},{b},{y},150' for i, (a, b, y) in enumerate(swipes)]
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(script + [f'{5000 + 1500 * len(swipes) + 800}:QUIT']))
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = run.stdout + run.stderr
    assert run.returncode == 0, log[-1500:]
    return log


def frames(log):
    """Each Home build: the books of its cells, main cover first: [(main book, [shelf books])]."""
    out = []
    for chunk in log.split('Home build start')[1:]:
        rects = [(int(m[0]), int(m[1]), int(m[2])) for m in re.findall(
            r'Home cover book=(\d+) rect=(-?\d+),(-?\d+),\d+,\d+ native=\d', chunk.split('Home build end')[0])]
        if not rects:
            continue
        main = [b for b, x, y in rects if x == 24 and y == 44]
        out.append((main[0] if main else None, [b for b, x, y in rects if y > 300]))
    return out


def main():
    LEFT, RIGHT = (400, 80, 300), (80, 400, 300)  # from x, to x, row (the shelf row is y 450-640)
    SHELF_ROW_LEFT = (400, 80, 540)
    failures = []
    with tempfile.TemporaryDirectory(prefix='x4-shelf-swipe-', dir=os.environ.get('TMPDIR')) as tmp:
        log = shoot(tmp, 2, [LEFT, SHELF_ROW_LEFT, RIGHT])
        seen = [f for f in frames(log)]
        # the settled frame after each swipe is the last one before the next swipe: take each distinct run
        runs = []
        for f in seen:
            if not runs or runs[-1] != f:
                runs.append(f)
        want = [(0, [1, 2, 3]), (0, [4, 5, 6]), (0, [1, 2, 3]), (0, [4, 5, 6])]
        # 1st swipe left: page 2; 2nd swipe left on the shelf row: round to page 1; 3rd swipe right: back to page 2
        ok = runs == want
        print(('GREEN' if ok else 'RED') + f' X4 Pro Shelf swipe: pages {runs}, want {want}', flush=True)
        if not ok:
            failures.append(f'Shelf pages {runs}, want {want}')
        log = shoot(tmp, 1, [LEFT])
        runs = []
        for chunk in log.split('Home build start')[1:]:
            m = re.findall(r'Home cover book=(\d+) rect=(\d+),44,', chunk.split('Home build end')[0])
            if m and (not runs or runs[-1] != m[-1]):
                runs.append(m[-1])
        ok = runs[-1:] != runs[:1] if len(runs) > 1 else False
        print(('GREEN' if ok else 'RED') + f' X4 Pro Large cover: a swipe steps to another book: {runs}', flush=True)
        if not ok:
            failures.append(f'Large cover swipe {runs}')
    assert not failures, '\n'.join(failures)
    print('GREEN: Shelf swipe slides the shelf by 3 round the ring, the main cover stays; the Large cover still steps')


if __name__ == '__main__':
    main()
