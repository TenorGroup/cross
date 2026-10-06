"""X4 Pro: every row of a framed list has the same height between its lines, and the lines are there.

A framed list draws a 2 px grey ring around its rows and a 1 px grey dotted rule between 2 rows; Home's Settings
groups draw a solid ring. The first and the last row used to be 2 and 7 px lower than the others, and the Settings
groups had no rules at all. Measured on the pixels: the white rows between 2 consecutive lines of a frame.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, TABS_X, H


def root_files(sach):
    for i in range(30):
        (sach.parent / f'tep-{i:02d}.txt').write_text('Tep thu.\n' * 20)


def line_kind(image, y, x0=100, x1=420):
    dark = [image.getpixel((x, y)) < 128 for x in range(x0, x1)]
    share = sum(dark) / len(dark)
    flips = sum(a != b for a, b in zip(dark, dark[1:])) / (len(dark) - 1)
    if 0.4 <= share <= 0.6 and flips > 0.9:
        return 'dot'
    return 'solid' if share > 0.9 else None


def frames(image):
    """Each frame as the heights of its rows (between ring and rule lines) and its count of rules."""
    marks = []
    for y in range(40, H - 80):
        kind = line_kind(image, y)
        if not kind:
            continue
        if marks and marks[-1][1] == y - 1 and marks[-1][2] == kind:
            marks[-1][1] = y
        else:
            marks.append([y, y, kind])
    found, current = [], None
    for first, last, kind in marks:
        ring = last > first or kind == 'solid'
        if ring and current is None:
            current = [(first, last)]
        elif ring:
            current.append((first, last))
            found.append(dict(rows=[b[0] - a[1] - 1 for a, b in zip(current, current[1:])], rules=len(current) - 2))
            current = None
        elif current is not None:
            current.append((first, last))
    return found


def full_rows(frame):
    """A frame with more rows below goes on under its last full row, around the faded next one: only the rows
    above that fade (none shorter than half the tallest) count."""
    rows = frame['rows']
    keep = next((i for i, row in enumerate(rows) if row < max(rows) / 2), len(rows))
    return dict(rows=rows[:keep], rules=max(0, keep - 1)) if keep < len(rows) else frame


def check_rows(folder, name, script, size):
    (shot,) = run(folder, script, [4800], settings=dict(uiTextSize=size), write_books=root_files)
    lists = [full_rows(f) for f in frames(shot) if f['rules'] >= 2]
    assert lists, f'{name}, text size {size}: no framed list with rules between its rows ({frames(shot)})'
    for f in lists:
        assert len(set(f['rows'])) == 1, f'{name}, text size {size}: rows of unequal height {f["rows"]}'
        assert f['rules'] == len(f['rows']) - 1, f'{name}, text size {size}: a rule is missing {f}'


def main():
    failures = []
    screens = [('Folder', f'3000:TAP:{TABS_X[1]},754'), ('Stats', f'3000:TAP:{TABS_X[3]},754'),
               ('Settings', f'3000:TAP:{TABS_X[4]},754')]
    with tempfile.TemporaryDirectory(prefix='x4pro-rows-') as tmp:
        for name, script in screens:
            for size in (0, 1, 2):
                folder = Path(tmp) / f'{name}-{size}'
                folder.mkdir()
                try:
                    check_rows(folder, name, script, size)
                except AssertionError as error:
                    failures.append(str(error))
    assert not failures, '\n'.join(failures)
    print('GREEN: X4 Pro framed rows of one height with their rules, Folder, Stats and Settings x 3 text sizes')


if __name__ == '__main__':
    main()
