import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/icons'))
import sinh

from PIL import Image, ImageChops


program, board = sys.argv[1:]
root = Path(__file__).resolve().parents[2]
reference = Path(os.environ.get('ICON57_REFERENCE_DIR', root / 'scripts/icons/icon57/png'))
centres = [66, 165, 264, 362, 461] if board in ('X3', 'Ugly') else [74, 169, 264, 358, 453]
failures = []
checks = 0
for kind, names in [('home', ['gan_day', 'thu_muc_hop', 'yeu_tim', 'thong_ke', 'cai_dat']),
                    ('text', ['yeu_tim', 'font', 'co_chu', 'bo_cuc', 'kieu']),
                    ('reader', ['yeu_tim', 'vi_tri', 'doc', 'cong_cu', 'font'])]:
    if kind == 'reader' and board != 'X3':
        continue
    if board == 'Touch' and kind == 'text':
        continue
    for chosen in range(5):
        actual = subprocess.check_output([program, ('ugly-' if board == 'Ugly' else '') + kind, str(chosen)])
        assert len(actual) == 528 * 60
        image = Image.frombytes('L', (528, 60), actual)
        for index, name in enumerate(names):
            suffix = 'chon' if index == chosen else 'xam'
            path = reference / f'{name}-{suffix}.png'
            approved = (Image.open(path) if path.exists() else sinh.draw(name, 40, 4 if suffix == 'chon' else 3,
                                                                        suffix == 'chon')).convert('L')
            if board == 'X3':
                bounds = ImageChops.invert(approved).getbbox()
                centered = Image.new('L', (40, 40), 255)
                centered.paste(approved, ((40 - (bounds[2] - bounds[0])) // 2 - bounds[0],
                                          (40 - (bounds[3] - bounds[1])) // 2 - bounds[1]))
                approved = centered
            expected = approved.tobytes()
            pixels = image.crop((centres[index] - 20, 10, centres[index] + 20, 50)).tobytes()
            checks += 1
            count = sum(actual_pixel != expected_pixel for actual_pixel, expected_pixel in zip(pixels, expected))
            if count:
                failures.append(f'{board} {kind} chosen={chosen} slot={index} {name}-{suffix}: {count}/1600 pixels differ')
        if kind == 'reader':
            icons = [image.crop((center - 20, 10, center + 20, 50)).tobytes() for center in centres]
            if len(set(icons)) != 5:
                failures.append(f'{board} reader chosen={chosen}: duplicate tab icons')
            for index in range(1, 5):
                for home_name in ('gan_day', 'thu_muc_hop', 'yeu_tim', 'thong_ke', 'cai_dat'):
                    home = Image.open(reference / f'{home_name}-{"chon" if index == chosen else "xam"}.png').convert('L')
                    bounds = ImageChops.invert(home).getbbox()
                    centered = Image.new('L', (40, 40), 255)
                    centered.paste(home, ((40 - (bounds[2] - bounds[0])) // 2 - bounds[0], (40 - (bounds[3] - bounds[1])) // 2 - bounds[1]))
                    if icons[index] == centered.tobytes():
                        failures.append(f'{board} reader slot={index}: Home icon {home_name} has another meaning')
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
if board in ('Touch', 'Ugly'):
    subprocess.run([program, ('ugly-' if board == 'Ugly' else '') + 'reader-legacy', '0'],
                   check=True, stdout=subprocess.DEVNULL)
print(f'{"RED" if failures else "GREEN"} {board}: {checks} pixel comparisons, {len(failures)} failures')
print('\n'.join(failures))
raise SystemExit(bool(failures))
