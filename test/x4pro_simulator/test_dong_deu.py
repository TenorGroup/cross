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
        elif ring and len(current) == 1 and first - current[0][1] < 20:
            # Two rings 20 px apart with nothing between: the first was the foot of a frame scrolled in from
            # above (a page that scrolls, Stats), the second opens the next frame.
            current = [(first, last)]
        elif ring:
            current.append((first, last))
            found.append(dict(rows=[b[0] - a[1] - 1 for a, b in zip(current, current[1:])], rules=len(current) - 2,
                              top=current[0][0], bottom=current[-1][1]))
            current = None
        elif current is not None:
            current.append((first, last))
    if current is not None and len(current) >= 3:
        # A frame whose foot fades out with the next row: its full rows end at its last rule.
        rows = [b[0] - a[1] - 1 for a, b in zip(current, current[1:])]
        found.append(dict(rows=rows, rules=len(rows) - 1, top=current[0][0], bottom=current[-1][1], open=True))
    return found


def full_rows(frame, image, lowest):
    """The lowest frame of a list with more rows below (its scroll bar shows, 6 px, 4 px inside the ring) goes on
    under its last full row, around the faded next one: only the rows above that fade count."""
    bar = sum(1 for y in range(frame['top'], frame['bottom']) if image.getpixel((457, y)) < 128)
    if bar == 0 or not lowest or frame.get('open'):
        return frame
    rows = list(frame['rows'])
    tallest = max(rows)
    if rows[-1] >= tallest / 2:
        rows.pop()  # the faded part as tall as a row
    while rows and rows[-1] < tallest / 2:
        rows.pop()  # the faded part, split by the next row's own lines
    return dict(rows=rows, rules=max(0, len(rows) - 1))


def check_rows(folder, name, script, size, at=4800):
    (shot,) = run(folder, script, [at], settings=dict(uiTextSize=size), write_books=root_files)
    found = frames(shot)
    lists = [full_rows(f, shot, f is found[-1]) for f in found if f['rules'] >= 2]
    assert lists, f'{name}, text size {size}: no framed list with rules between its rows ({found})'
    for f in lists:
        assert len(set(f['rows'])) == 1, f'{name}, text size {size}: rows of unequal height {f["rows"]}'
        assert f['rules'] == len(f['rows']) - 1, f'{name}, text size {size}: a rule is missing {f}'


def main():
    failures = []
    # Stats scrolls as one page under its panels: its rows are measured at the page's end.
    flicks = ';'.join(f'{4500 + i * 1200}:SWIPE:240,640,240,300,150' for i in range(3))
    # File lists its rows without a frame (founder 07/10): test_file_khong_khung measures its rules.
    screens = [('Stats', f'3000:TAP:{TABS_X[3]},754;{flicks}', 9000), ('Settings', f'3000:TAP:{TABS_X[4]},754', 4800)]
    with tempfile.TemporaryDirectory(prefix='x4pro-rows-') as tmp:
        for name, script, at in screens:
            for size in (0, 1, 2):
                folder = Path(tmp) / f'{name}-{size}'
                folder.mkdir()
                try:
                    check_rows(folder, name, script, size, at)
                except AssertionError as error:
                    failures.append(str(error))
    assert not failures, '\n'.join(failures)
    print('GREEN: X4 Pro framed rows of one height with their rules, Folder, Stats and Settings x 3 text sizes')


if __name__ == '__main__':
    main()
