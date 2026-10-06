"""X4 Pro: every round frame of rows has its grey rules (founder 06/10/2026: a round frame always has its rules
between the rows, in any frame).

Measured on the pixels: in each frame taller than 2 rows, the white band between 2 consecutive lines (ring or rule)
is never taller than a row. The 2 panels of figures at the top of Stats are no list of rows and are left out.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_dong_deu import frames, root_files
from test_thanh_day import run, TABS_X

TALLEST_ROW = 100  # a 2-line Settings row is 76 px
SCREENS = [
    ('Settings', f'3000:TAP:{TABS_X[4]},754', 4800),
    ('Display', f'3000:TAP:{TABS_X[4]},754;4500:TAP:240,180', 6500),
    ('Folder', f'3000:TAP:{TABS_X[1]},754', 4800),
    ('Held row menu', f'3000:TAP:{TABS_X[1]},754;5000:TAP:240,98;8000:TAP:240,157,900', 10000),
]


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='x4pro-ke-') as tmp:
        for name, script, at in SCREENS:
            folder = Path(tmp) / name.replace(' ', '-').replace(',', '')
            folder.mkdir()
            (shot,) = run(folder, script, [at], write_books=root_files)
            tall = [f for f in frames(shot) if sum(f['rows']) >= 2 * 56]
            if not tall:
                failures.append(f'{name}: no frame of rows found')
            for f in tall:
                if max(f['rows']) > TALLEST_ROW:
                    failures.append(f'{name}: a frame at y={f["top"]} has {max(f["rows"])} px between its lines '
                                    f'(rows {f["rows"]}): a rule is missing')
    assert not failures, '\n'.join(failures)
    print(f'GREEN: X4 Pro frames have their rules between rows, {len(SCREENS)} screens')


if __name__ == '__main__':
    main()
