import os
from pathlib import Path
import subprocess
import sys

from PIL import Image


program, board = sys.argv[1:]
root = Path(__file__).resolve().parents[2]
reference = Path(os.environ.get('ICON57_REFERENCE_DIR', root / 'scripts/icons/icon57/png'))
centres = [56, 160, 264, 367, 471] if board == 'X3' else [74, 169, 264, 358, 453]
failures = []
checks = 0
for kind, names in [('home', ['gan_day', 'thu_muc_hop', 'yeu_tim', 'thong_ke', 'cai_dat']),
                    ('text', ['yeu_tim', 'font', 'co_chu', 'bo_cuc', 'kieu'])]:
    if board == 'Touch' and kind == 'text':
        continue
    for chosen in range(5):
        actual = subprocess.check_output([program, kind, str(chosen)])
        assert len(actual) == 528 * 60
        image = Image.frombytes('L', (528, 60), actual)
        for index, name in enumerate(names):
            suffix = 'chon' if index == chosen else 'xam'
            expected = Image.open(reference / f'{name}-{suffix}.png').convert('L').tobytes()
            pixels = image.crop((centres[index] - 20, 10, centres[index] + 20, 50)).tobytes()
            checks += 1
            count = sum(actual_pixel != expected_pixel for actual_pixel, expected_pixel in zip(pixels, expected))
            if count:
                failures.append(f'{board} {kind} chosen={chosen} slot={index} {name}-{suffix}: {count}/1600 pixels differ')
for path in sorted(reference.glob('*.png')):
    image = Image.open(path).convert('1')
    assert image.size == (40, 40)
    for chosen in (0, 1):
        actual = subprocess.check_output([program, 'raw', str(chosen)], input=image.tobytes())
        pixels = Image.frombytes('L', (528, 60), actual).crop((0, 0, 40, 40)).tobytes()
        expected = image.convert('L').tobytes()
        checks += 1
        count = sum(actual_pixel != expected_pixel for actual_pixel, expected_pixel in zip(pixels, expected))
        if count:
            failures.append(f'{board} direct {path.name} chosen={chosen}: {count}/1600 pixels differ')
print(f'{"RED" if failures else "GREEN"} {board}: {checks} pixel comparisons, {len(failures)} failures')
print('\n'.join(failures))
raise SystemExit(bool(failures))
