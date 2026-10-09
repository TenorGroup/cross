"""X4 Pro rule 14: one external scrollbar, clear of frame corners, hidden after 2000 ms."""
import tempfile
from pathlib import Path
from PIL import ImageChops

from test_thanh_day import run, TABS_X
from test_menu_chu_14 import TEXT_MENU, OPEN_BOOK
from test_thanh_dong import toc_book
from test_shared_scroll_v1055 import check_geometry, difference_outside, thumb

SWIPE = '240,600,240,250,200'
SETTINGS = f'3000:TAP:{TABS_X[4]},754'
CONTENTS = OPEN_BOOK + ';9500:TAP:240,775;12000:TAP:226,754'
SCREENS = [
    ('Settings', SETTINGS, 3800, None),
    ('Settings end', SETTINGS + f';4000:SWIPE:{SWIPE};6000:SWIPE:{SWIPE};8000:SWIPE:{SWIPE}', 9000, None),
    ('Contents', CONTENTS, 13000, toc_book),
    ('Contents end', CONTENTS + ';' + ';'.join(f'{14000 + 1500 * i}:SWIPE:240,650,240,420,200' for i in range(10)),
     28700, toc_book),
    ('Text panel', TEXT_MENU, 10800, None),
    ('More panel', TEXT_MENU + ';12000:TAP:416,754', 13000, None),
    ('Value list', SETTINGS + ';4500:TAP:240,303;6500:TAP:240,80;8500:TAP:240,88', 9600, None),
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


def check(image, hidden):
    problems = []
    top, bottom = (402, 712) if sides(image) and min(s[0] for s in sides(image)) > 350 else (32, 716)
    try:
        check_geometry(image, top, bottom, background=hidden)
    except AssertionError as error:
        problems.append(str(error))
    overlay = ImageChops.invert(ImageChops.difference(image, hidden))
    for s0, s1 in sides(image):
        rows = [y for y in range(max(top, s0 - 20), min(bottom, s1 + 21))
                if sum(dark(overlay, x, y) for x in range(469, 475)) >= 2]
        crowd = [(x, y) for y in rows for x in range(464, 469) if dark(overlay, x, y)]
        if crowd:
            problems.append(f'no air between bar and ring, frame y {s0}..{s1}: {crowd[:4]}')
    return problems


def main():
    failures = []
    found = 0
    with tempfile.TemporaryDirectory(prefix='x4pro-cuon-khung-') as tmp:
        for name, script, at, books in SCREENS:
            folder = Path(tmp) / name.replace(' ', '-')
            shot, hidden = run(folder, script, [at, at + 2400], write_books=books, settings=dict(readerTapTip=0))
            frames = sides(shot)
            if not frames:
                failures.append(f'{name}: no frame found beside the external scrollbar')
            found += len(frames)
            failures += [f'{name}: {p}' for p in check(shot, hidden)]
            run_length = longest = 0
            for y in range(32, 716):
                run_length = run_length + 1 if sum(dark(hidden, x, y) for x in range(469, 475)) >= 4 else 0
                longest = max(longest, run_length)
            if longest >= 20:
                failures.append(f'{name}: scrollbar remained after 2000 ms')
            if difference_outside(shot.crop((0, 32, 480, 800)), hidden.crop((0, 32, 480, 800)), (469, 0, 475, 684)):
                failures.append(f'{name}: idle hide changed content outside the scrollbar')
    assert not failures, '\n'.join(failures)
    print(f'GREEN: X4 Pro external scrollbars clear of frames, idle-hidden, {len(SCREENS)} screens')


if __name__ == '__main__':
    main()
