"""X4 Pro File screens (founder 07/10: "the File screen has no round frame"): the File card and a folder list their
rows with the grey rules between them (rows of one height), the fades and the scroll bar, and no ring round them;
the held row's menu keeps its own frame and rules, and Settings keeps its frames.

Measured on the pixels: a ring is the dotted column at x 16..17 (left side) and x 462..463 (right side).
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_dong_deu import root_files, line_kind
from test_thanh_day import run, TABS_X

SWIPE = '240,600,240,250,200'
FILE = f'3000:TAP:{TABS_X[1]},754'
SCREENS = [
    ('File card', FILE, 4800),
    ('File card end', FILE + f';5000:SWIPE:{SWIPE};7000:SWIPE:{SWIPE};9000:SWIPE:{SWIPE}', 11000),
    ('Folder', FILE + ';5000:TAP:240,68', 7000),
]


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def side_ink(image, x0, x1, y0=36, y1=716):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if dark(image, x, y))


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='x4pro-file-') as tmp:
        for name, script, at in SCREENS:
            folder = Path(tmp) / name.replace(' ', '-')
            (shot,) = run(folder, script, [at], write_books=root_files)
            if side_ink(shot, 14, 20) or side_ink(shot, 460, 466):
                failures.append(f'{name}: a ring round the rows (x 16 / x 462 inked)')
            rules = [y for y in range(36, 716) if line_kind(shot, y) == 'dot']
            if len(rules) < (1 if name == 'Folder' else 5):
                failures.append(f'{name}: {len(rules)} grey rules between the rows')
            elif len({b - a for a, b in zip(rules, rules[1:])}) > 1:
                failures.append(f'{name}: rows of unequal height between the rules {rules}')
            if name != 'Folder' and not side_ink(shot, 469, 475, 40, 700):
                failures.append(f'{name}: no scroll bar')
        held, settings = (run(Path(tmp) / 'held', FILE + ';5000:TAP:240,98;8000:TAP:240,157,900', [10000],
                              write_books=root_files)[0],
                          run(Path(tmp) / 'settings', f'3000:TAP:{TABS_X[4]},754', [4800])[0])
        menu_lines = [y for y in range(36, 716) if line_kind(held, y, 40, 180) == 'dot']
        if not side_ink(held, 14, 20) or len(menu_lines) < 4:
            failures.append(f'the held row menu lost its frame or its rules ({len(menu_lines)} dotted lines)')
        if not side_ink(settings, 14, 20):
            failures.append('Settings lost its frames')
    assert not failures, '\n'.join(failures)
    print(f'GREEN: X4 Pro File screens list their rows without a ring, {len(SCREENS)} screens; menus keep theirs')


if __name__ == '__main__':
    main()
