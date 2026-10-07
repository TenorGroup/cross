"""X4 Pro rule 13 (founder 07/10): on every screen with a round frame and a scroll bar, the bar stays on the frame's
straight side, clear of its round corners, with 4 px of air from its ring and no text under it. Settings (top and
end), the reader menu's Contents (top and end), Text and More panels, a long value list.

Measured on the pixels: the frame's right side is its ring at x 461..463 (y s0..s1, the corners curving in above
and below); the bar's column is x 450..460. In the corner bands (s0 - 4 .. s0 + 6, s1 - 6 .. s1 + 4) that column
holds the ring alone (its curve keeps right of x 459 there); x 458..460 between the bar and the ring stays white.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, TABS_X
from test_menu_chu_14 import TEXT_MENU, OPEN_BOOK
from test_thanh_dong import toc_book

SWIPE = '240,600,240,250,200'
SETTINGS = f'3000:TAP:{TABS_X[4]},754'
CONTENTS = OPEN_BOOK + ';9500:TAP:240,775;12000:TAP:226,754'
SCREENS = [
    ('Settings', SETTINGS, 4800, None),
    ('Settings end', SETTINGS + f';4000:SWIPE:{SWIPE};6000:SWIPE:{SWIPE};8000:SWIPE:{SWIPE}', 10000, None),
    ('Contents', CONTENTS, 14500, toc_book),
    ('Contents end', CONTENTS + ';' + ';'.join(f'{14000 + 1500 * i}:SWIPE:240,650,240,420,200' for i in range(10)),
     30000, toc_book),
    ('Text panel', TEXT_MENU, 11800, None),
    ('More panel', TEXT_MENU + ';12000:TAP:416,754', 14500, None),
    ('Value list', SETTINGS + ';4500:TAP:240,303;6500:TAP:240,80;8500:TAP:240,88', 10500, None),
]


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def sides(image):
    """The frames' right sides: unbroken runs of ring ink at x 461..463 (a dotted ring inks every row), above the
    foot bar. A frame laid over another (a value list) breaks the run where its corner curves in."""
    ys = [y for y in range(30, 716) if any(dark(image, x, y) for x in range(461, 464))]
    runs = []
    for y in ys:
        if runs and y - runs[-1][1] <= 1:
            runs[-1][1] = y
        else:
            runs.append([y, y])
    return [r for r in runs if r[1] - r[0] > 30]


def check(image):
    problems = []
    for s0, s1 in sides(image):
        bar = [y for y in range(s0 + 6, s1 - 5) if any(dark(image, x, y) for x in range(450, 458))]
        if not bar:
            continue  # a frame without a bar
        for lo, hi in ((s0 - 3, s0 + 6), (s1 - 5, s1 + 4)):
            # The ring's curve keeps right of x 459 in these bands: ink at x 450..457 there is the bar's.
            hit = [(x, y) for y in range(lo, hi) for x in range(450, 458) if dark(image, x, y)]
            if hit:
                problems.append(f'bar in the corner of the frame y {s0}..{s1}: {hit[:4]}')
        # Rows the bar inks (2 px or more of its column: a ring passing through inks 1).
        rows = [y for y in range(s0 + 6, s1 - 5) if sum(dark(image, x, y) for x in range(450, 458)) >= 2]
        crowd = [(x, y) for y in rows for x in range(458, 461) if dark(image, x, y)]
        if crowd:
            problems.append(f'no air between bar and ring, frame y {s0}..{s1}: {crowd[:4]}')
    return problems


def main():
    failures = []
    found = 0
    with tempfile.TemporaryDirectory(prefix='x4pro-cuon-khung-') as tmp:
        for name, script, at, books in SCREENS:
            folder = Path(tmp) / name.replace(' ', '-')
            (shot,) = run(folder, script, [at], write_books=books, settings=dict(readerTapTip=0))
            frames = [s for s in sides(shot) if any(dark(shot, x, y) for y in range(s[0] + 6, s[1] - 5)
                                                   for x in range(450, 458))]
            if not frames:
                failures.append(f'{name}: no framed scroll bar found')
            found += len(frames)
            failures += [f'{name}: {p}' for p in check(shot)]
    assert not failures, '\n'.join(failures)
    print(f'GREEN: X4 Pro scroll bars on the straight side of their frames, {len(SCREENS)} screens')


if __name__ == '__main__':
    main()
