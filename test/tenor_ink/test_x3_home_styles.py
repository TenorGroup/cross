"""X3 Home draws the 3 Home styles (Settings, Display, Home screen): Stats card, Large cover and Shelf.
On the X3 simulator, 7 books, the 6th one without a cover:
- Large cover: the main cover starts at the body top (y = 16) and is centred on the 528 px screen (half a pixel), the
  neighbours' covers stand left and right, bottoms level with the main cover, the same width of each showing, 16 px clear
  of the main cover on the left;
- Shelf: 3 cells of 128 px at x = 24, 200, 376, each cover centred in its cell, the 3 books after the shown one;
- both keep the tenor/ink 1 bit caches (ink2_1_ only, no ink2_4_) and the placeholder of a book with no cover is drawn
  (native=0), the covers of the others are drawn from their files (native=1) once written;
- the Stats card (style 0) draws none of this.
X3_PROGRAM picks the simulator_x3_uc8279 build.
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

PROGRAM = Path(os.environ.get('X3_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BOOKS = [f'/books/b{i}.epub' for i in range(7)]
NO_COVER = 1  # the second book has none
W = 528


def cache_hash(tmp, path):
    exe = Path(tmp) / 'hash'
    if not exe.exists():
        src = Path(tmp) / 'hash.cpp'
        src.write_text('#include <functional>\n#include <iostream>\n#include <string>\n'
                       'int main(int c,char**v){std::cout<<std::hash<std::string>{}(v[1]);}\n')
        subprocess.run(['c++', '-std=c++17', str(src), '-o', str(exe)], check=True)
    return subprocess.check_output([str(exe), path], text=True).strip()


def shoot(tmp, style):
    sd = Path(tmp) / f'x3-style-{style}'
    store = sd / '.crosspoint'
    books = []
    for i, path in enumerate(BOOKS):
        cache = store / ('epub_' + cache_hash(tmp, path))
        cache.mkdir(parents=True)
        (cache / 'cover.ref').write_bytes(b'CREF\x01\x00' if i == NO_COVER else b'CREF\x01\x09cover.jpg')
        book = sd / path.lstrip('/')
        book.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(book, 'w') as z:
            z.writestr('cover.jpg', cover_jpeg())
        books.append(dict(path=path, title=f'Sach {i}', author='Tenor',
                          coverBmpPath='' if i == NO_COVER else '/' + str(cache.relative_to(sd)) + '/thumb2_[HEIGHT].bmp'))
    (store / 'recent.json').write_text(json.dumps(dict(books=books)))
    (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
    (store / 'settings.json').write_text(json.dumps(dict(language='VI', homeStyle=style, uiTextSize=0, uiShell=0, uiTheme=4,
                                                         tenorPresetVersion=1, sleepTimeout=120, uiShellClockOnce=1)))
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT='9000:QUIT')
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = run.stdout + run.stderr
    assert run.returncode == 0, log[-1500:]
    return log, sorted(p.name for p in sd.rglob('ink2_*.bmp'))


def last_frame(log):
    """Cover rectangles of the last Home build: {book: (x, y, w, h, native)}."""
    frame = log[log.rindex('Home build start'):] if 'Home build start' in log else ''
    return {int(m[0]): tuple(map(int, m[1:])) for m in re.findall(
        r'Home cover book=(\d+) rect=(-?\d+),(-?\d+),(\d+),(\d+) native=(\d)', frame)}


def large(log):
    f, fails = last_frame(log), []
    if 'Home layout style=1' not in log or not f:
        return ['Large cover not drawn on the X3']
    if 0 not in f or f[0][1] != 16:
        return [f'want the main cover (book 0) at y = 16, got {f}']
    x, y, w, h, native = f[0]
    if abs(2 * x + w - W) > 1 or w > 261:
        fails.append(f'main cover {f[0]}: not centred on {W} px, or wider than 261 px')
    left, right = f.get(len(BOOKS) - 1), f.get(1)
    if not left or not right:
        return fails + [f'neighbours missing: {f}']
    if left[0] + left[2] != x - 16:
        fails.append(f'left neighbour ends at {left[0] + left[2]}, want 16 px clear of {x}')
    if left[0] + left[2] != W - right[0]:
        fails.append(f'visible widths differ: {left[0] + left[2]} against {W - right[0]}')
    if not (left[1] + left[3] == right[1] + right[3] == y + h):
        fails.append(f'bottoms differ: {left[1] + left[3]}, {right[1] + right[3]}, {y + h}')
    if native != 1 or left[4] != 1:
        fails.append('the covers with a file were not drawn from it')
    if right[4] != 0:
        fails.append('the book with no cover drew no placeholder')
    return fails


def shelf(log):
    f = last_frame(log)
    if 'Home layout style=2' not in log or not f:
        return ['Shelf not drawn on the X3']
    fails = []
    for slot, book in enumerate((1, 2, 3)):
        r = f.get(book)
        if not r:
            fails.append(f'book {book} missing from the shelf: {f}')
            continue
        centre = r[0] + (r[2] - 1) / 2
        want = 24 + 176 * slot + 63.5
        if abs(centre - want) > 0.5 or r[1] + r[3] > 708 or r[2] > 128:
            fails.append(f'shelf cell {slot}: cover {r}, centre {centre}, want {want}')
    if f.get(0, (0,) * 5)[0] != 24:
        fails.append('the shown book is not the main cover at x = 24')
    if f.get(NO_COVER, (0,) * 5)[4] != 0:
        fails.append('the book with no cover drew no placeholder')
    if any(f.get(b, (0,) * 5)[4] != 1 for b in (2, 3)):
        fails.append('a cover with a file was not drawn from it')
    return fails


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='x3-home-styles-', dir=os.environ.get('TMPDIR')) as tmp:
        stats, _ = shoot(tmp, 0)
        if 'Home layout style=' in stats:
            failures.append('style 0 drew a Large cover or Shelf layout')
        for style, check in ((1, large), (2, shelf)):
            log, caches = shoot(tmp, style)
            fails = check(log)
            if not caches or any(not c.startswith('ink2_1_') for c in caches):
                fails.append(f'caches {caches}: want ink2_1_ only')
            print(('RED' if fails else 'GREEN') + f' X3 Home style {style}: ' + '; '.join(fails), flush=True)
            failures += [f'style {style}: {f}' for f in fails]
    assert not failures, '\n'.join(failures)
    print('GREEN: X3 Home draws the Large cover and the Shelf on the 528 px screen, ink2_1_ caches only')


if __name__ == '__main__':
    main()
