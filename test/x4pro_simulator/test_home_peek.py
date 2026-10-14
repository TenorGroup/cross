"""X4 Pro Home, Large cover: the books before and after the shown one stand at its edges.
- the sliver of a neighbour that has no cover is the ring of a placeholder alone, no name cut through it;
- a tap on that sliver makes the book the shown one (as one swipe does) and opens nothing;
- a tap on the left sliver goes back to the book before.
7 books, the 2nd one without a cover, so the right neighbour of book 0 is a placeholder. X4PRO_PROGRAM picks the build.
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_home_card_v1011 import cover_jpeg  # noqa: E402
from test_home_shelf_swipe import BOOKS, PROGRAM, cache_hash  # noqa: E402

COVER = re.compile(r'Home cover book=(\d+) rect=(-?\d+),(-?\d+),(\d+),(\d+) native=(\d)')
W = 480
NO_COVER = 1


def make_sd(tmp, name):
    sd = Path(tmp) / name
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
        books.append(dict(path=path, title='Chuyện cũ Hà Nội - Tập 1' if i == NO_COVER else f'Sach {i}', author='Tenor',
                          coverBmpPath='' if i == NO_COVER else '/' + str(cache.relative_to(sd)) + '/thumb2_[HEIGHT].bmp'))
    (store / 'recent.json').write_text(json.dumps(dict(books=books)))
    (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
    (store / 'settings.json').write_text(json.dumps(dict(language='VI', homeStyle=1, uiTextSize=0, uiShell=0, uiTheme=4,
                                                         sleepTimeout=120, tipsEnabled=0)))
    return sd


def launch(sd, script, quit_ms, shot=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(script + [f'{quit_ms}:QUIT']))
    if shot:
        env['CROSSPOINT_SIM_SCREENSHOTS'] = f'{shot[0]}:{shot[1]}'
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = run.stdout + run.stderr
    assert run.returncode == 0, log[-1500:]
    return log


def frames(log):
    """Each Home build: {book: (x, y, w, h, native)}."""
    out = []
    for chunk in log.split('Home build start')[1:]:
        out.append({int(m[0]): tuple(map(int, m[1:])) for m in COVER.findall(chunk.split('Home build end')[0])})
    return out


def main_book(frame):
    """The book whose cover stands whole on the screen."""
    return [b for b, (x, y, w, h, n) in frame.items() if x >= 0 and x + w <= W][0]


def main():
    from PIL import Image
    failures = []

    def check(ok, what):
        print(('GREEN ' if ok else 'RED ') + what, flush=True)
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory(prefix='x4-home-peek-', dir=os.environ.get('TMPDIR')) as tmp:
        sd = make_sd(tmp, 'sd')
        shot = Path(tmp) / 'peek.bmp'
        log = launch(sd, [], 8000, shot=(7500, shot))
        last = frames(log)[-1]
        right = last.get(NO_COVER)
        check(right is not None and right[0] + right[2] > W, f'book 1 stands at the right edge: {right}')
        x, y, w, h, native = right
        pixels = list(Image.open(shot).convert('L').crop((x + 12, y + 12, W, y + h - 12)).getdata())
        check(native == 0 and min(pixels) >= 200, f'the right neighbour with no cover shows its ring only, darkest pixel {min(pixels)}')

        tap_right = f'8500:TAP:{(x + W) // 2},{y + h // 2},80'
        tapped = launch(sd, [tap_right], 12000)
        shown = [main_book(f) for f in frames(tapped)]
        check(shown[-1] == 1 and '[ERS]' not in tapped, f'a tap on the right sliver makes book 1 the shown one, opens none: {shown}')

        mx = last[main_book(last)][0]
        left = next(v for b, v in last.items() if v[0] < 0)
        tap_left = f'10500:TAP:{(mx - 16) // 2},{left[1] + left[3] // 2},80'
        back = [main_book(f) for f in frames(launch(sd, [tap_right, tap_left], 14000))]
        check(1 in back and back[-1] == 0, f'a tap on the left sliver goes back to book 0 after book 1: {back}')
    assert not failures, '\n'.join(failures)
    print('GREEN: the Large cover neighbours are rings with no name, and a tap on one makes it the shown book')


if __name__ == '__main__':
    main()
