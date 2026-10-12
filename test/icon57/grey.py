"""Grey (unselected) 40 px tab icons: same ink box as the selected icon, never bigger, drawn with clean runs.

Run `python3 test/icon57/grey.py`. For each of the 25 tab shapes it reads the selected and the grey bitmap the
way scripts/icons/sinh.py hands them to the headers, and fails when
- the grey ink box is not the selected ink box (size or centre),
- a grey pixel lies more than 1 px from the selected ink (the circles and the keyboard are drawn true, so they may differ by a pixel),
- the selected icon is mirrored and the grey one is not,
- a grey pixel stands alone (no ink above, below, left or right of it): the checkerboard,
- the grey ink is not lighter than the selected ink, or is under a third of it.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts/icons'))
import sinh
import xam

SHAPES = list(sinh.ICON57_NAMES) + ['vi_tri', 'cong_cu', 'muc_luc', 'chu']
failures = []
checks = 0


def fail(name, text):
    failures.append(f'{name}: {text}')


for name in SHAPES:
    selected = xam.ink_of(sinh.draw(name, 40, 4, True))
    grey = xam.ink_of(sinh.draw(name, 40, 3, False))
    checks += 5
    if xam.box(selected) != xam.box(grey):
        fail(name, f'grey ink box {xam.box(grey)} differs from selected {xam.box(selected)}')
    outside = sorted(p for p in grey if not any((p[0] + dx, p[1] + dy) in selected
                                                 for dx in (-1, 0, 1) for dy in (-1, 0, 1)))
    if outside:
        fail(name, f'{len(outside)} grey pixels more than 1 px from the selected ink, first {outside[0]}')
    if xam.mirrored(selected) and not xam.mirrored(grey):
        fail(name, 'selected icon is mirrored, grey is not')
    lonely = sorted(p for p in grey if not any((p[0] + dx, p[1] + dy) in grey
                                                 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))))
    if lonely:
        fail(name, f'{len(lonely)} grey pixels with no ink beside them (checkerboard), first {lonely[0]}')
    share = len(grey) / len(selected)
    if not 1 / 3 <= share < 1:
        fail(name, f'grey ink is {share:.2f} of the selected ink, expected from 0.33 up to 1')

print(f'{"RED" if failures else "GREEN"}: {checks} grey checks over {len(SHAPES)} shapes, {len(failures)} failures')
print('\n'.join(failures))
raise SystemExit(bool(failures))
