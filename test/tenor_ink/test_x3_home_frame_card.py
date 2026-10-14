"""X3 Home, Large cover and Shelf: the built frame is kept in a file on the card, not in RAM (as the Stats card is).
With the radio's heap (61.204 B free, 55.284 B largest block, the X3 right after the first page of a book):
- once a frame is built and on the panel it is written to /.crosspoint/home-frame-<style>-<place>.card and the RAM copy is
  given back: the Frame line shows held=0 (it held 43.296 B on the Large cover and 39.360 B on the Shelf);
- the next paint of that frame reads the file (frame_file_us) instead of building it again;
- a file is read only for the frame it was written for: another title, or a cover file that is gone, builds again;
- a cover still to be made stands as the ring of a placeholder, not a block of gray; the sliver of a neighbour's placeholder at
  the Large cover's edge is the ring alone, no name cut through.
7 books, the 2nd one without a cover. X3_PROGRAM picks the simulator_x3_uc8279 build.
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
from test_x3_home_styles import BOOKS, NO_COVER, PROGRAM, W, cache_hash  # noqa: E402

RADIO = {'CROSSPOINT_SIM_FREE_HEAP': '61204', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '55284'}
# The X3 on 14/10/2026 after a Bluetooth session: free heap enough, largest block 28.660 B (the frame is 39.840 to 43.296 B).
AFTER_BLE = {'CROSSPOINT_SIM_FREE_HEAP': '93716', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '28660'}
FREE_FLOOR, LARGEST_FLOOR = 10240, 8180  # heapfloor::kFreeFloor, and the largest block as the heap API reports it
FRAME = re.compile(r'Frame row=\d+ top=\d+ total=\d+ms heap=(\d+) held=(\d+) largest=(\d+)')
COVER = re.compile(r'Home cover book=(\d+) rect=(-?\d+),(-?\d+),(\d+),(\d+) native=(\d)')


def make_sd(tmp, name, style, title0='Sach 0'):
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
        books.append(dict(path=path, title=title0 if i == 0 else 'Chuyện cũ Hà Nội - Tập 1' if i == NO_COVER else f'Sach {i}', author='Tenor',
                          coverBmpPath='' if i == NO_COVER else '/' + str(cache.relative_to(sd)) + '/thumb2_[HEIGHT].bmp'))
    (store / 'recent.json').write_text(json.dumps(dict(books=books)))
    (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
    (store / 'settings.json').write_text(json.dumps(dict(language='VI', homeStyle=style, uiTextSize=0, uiShell=0, uiTheme=4,
                                                         tenorPresetVersion=1, sleepTimeout=120, uiShellClockOnce=1)))
    return sd


def retitle(sd, title0):
    recent = sd / '.crosspoint/recent.json'
    data = json.loads(recent.read_text())
    data['books'][0]['title'] = title0
    recent.write_text(json.dumps(data))


def launch(sd, quit_ms, extra=None, shot=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT=f'{quit_ms}:QUIT')
    if shot:
        env['CROSSPOINT_SIM_SCREENSHOTS'] = f'{shot[0]}:{shot[1]}'
    env.update(extra or {})
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = run.stdout + run.stderr
    assert run.returncode == 0, log[-1500:]
    return log


def frame_files(sd):
    return sorted(p.name for p in (sd / '.crosspoint').glob('home-frame-*.card'))


def held(log):
    frames = FRAME.findall(log)
    return tuple(map(int, frames[-1])) if frames else None


def last_covers(log):
    chunk = log[log.rindex('Home build start'):] if 'Home build start' in log else ''
    return {int(m[0]): tuple(map(int, m[1:])) for m in COVER.findall(chunk)}


def margin(heap_held):
    heap, kept, largest = heap_held
    return heap - kept - FREE_FLOOR, largest - kept - LARGEST_FLOOR


def luminance(bmp, box):
    from PIL import Image
    pixels = list(Image.open(bmp).convert('L').crop(box).getdata())
    return min(pixels), sum(pixels) / len(pixels)


def main():
    failures = []

    def check(ok, what):
        print(('GREEN ' if ok else 'RED ') + what, flush=True)
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory(prefix='x3-home-frame-', dir=os.environ.get('TMPDIR')) as tmp:
        for style, name in ((1, 'Large cover'), (2, 'Shelf')):
            sd = make_sd(tmp, f'sd{style}', style)
            shot = Path(tmp) / f'covers{style}.bmp'
            log = launch(sd, 9000, shot=(8500, shot))
            rest = held(log)
            check(rest is not None and rest[1] == 0, f'{name}: the frame is on the card and held=0 at rest, got {rest}')
            check(frame_files(sd) == [f'home-frame-{style}-0.card'], f'{name}: one frame file, got {frame_files(sd)}')
            if style == 1:
                x, y, w, h, native = last_covers(log).get(1, (0,) * 5)
                low, _ = luminance(shot, (x + 12, y + 12, W - 30, y + h - 12)) if x + 12 < W - 30 else (0, 0)
                check(native == 0 and x + w > W and low >= 200,
                      f'Large cover: the right neighbour (no cover) shows its ring only, darkest pixel {low} in {(x, y, w, h)}')

            radio = launch(sd, 3000, RADIO)
            heap = held(radio)
            check('frame_file_us' in radio and 'Home build start' not in radio,
                  f'{name}: the next paint reads the frame file instead of building it')
            check(heap is not None and heap[1] == 0, f'{name}: held=0 at the radio heap, got {heap}')
            if heap:
                free_margin, largest_margin = margin(heap)
                print(f'  {name} at rest, radio heap: {free_margin} B free and {largest_margin} B largest block over the floor')

            retitle(sd, 'Sach doi ten')
            changed = launch(sd, 3000, RADIO)
            check('Home build start' in changed and 'frame_file_us' not in changed,
                  f'{name}: another title builds the frame again, the old file is not read')

            for cover in sorted(sd.rglob('ink2_1_*.bmp'))[:1]:
                cover.unlink()
            lost = launch(sd, 3000, RADIO)
            check('Home build start' in lost and 'frame_file_us' not in lost,
                  f'{name}: a cover file that is gone builds the frame again')

            fresh = make_sd(tmp, f'fresh{style}', style)
            shot = Path(tmp) / f'ring{style}.bmp'
            log = launch(fresh, 2000, RADIO, shot=(1500, shot))
            cover = last_covers(log).get(0)
            if cover:
                x, y, w, h, native = cover
                low, mean = luminance(shot, (x + 12, y + 12, x + w - 12, y + h - 12))
                check(native == 0 and mean >= 250, f'{name}: a cover not made yet is a ring, white inside: mean {mean:.0f}')
            else:
                check(False, f'{name}: no cover rectangle in the log')

        for style, name in ((1, 'Large cover'), (2, 'Shelf')):
            sd = make_sd(tmp, f'ble{style}', style)
            launch(sd, 9000, AFTER_BLE)
            check(frame_files(sd) == [f'home-frame-{style}-0.card'], f'{name}: after Bluetooth the frame still reaches the card, got {frame_files(sd)}')
            again = launch(sd, 3000, AFTER_BLE)
            heap = held(again)
            check('frame_file_us' in again and 'Home build start' not in again and heap is not None and heap[1] == 0,
                  f'{name}: after Bluetooth the next paint reads the frame from the card, held=0')

        for style in (0,):
            sd = make_sd(tmp, 'stats', style)
            launch(sd, 9000)  # the covers and the card file are made first
            heap = held(launch(sd, 3000, RADIO))
            print(f'  Stats card at rest, radio heap: held={heap[1] if heap else None}; margins {margin(heap) if heap else None}')
    assert not failures, '\n'.join(failures)
    print('GREEN: Large cover and Shelf frames live on the card; RAM holds none at rest')


if __name__ == '__main__':
    main()
