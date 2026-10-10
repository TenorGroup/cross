from pathlib import Path
import os
import subprocess
import sys

from PIL import Image, ImageChops


root = Path(__file__).resolve().parents[2]
reference = Path(os.environ.get('ICON57_REFERENCE_DIR', root / 'scripts/icons/icon57/png'))
failures = []
checks = 0


def centre(image):
    bounds = ImageChops.invert(image.convert('L')).getbbox()
    assert bounds, 'empty icon'
    return ((bounds[0] + bounds[2] - 1) / 2, (bounds[1] + bounds[3] - 1) / 2)


def aligned(label, actual, expected):
    global checks
    checks += 1
    delta = tuple(left - right for left, right in zip(actual, expected))
    if any(abs(value) > 0.5 for value in delta):
        failures.append(f'{label}: delta={delta}, centre={actual}, target={expected}')


if len(sys.argv) == 1:
    paths = sorted(reference.glob('*.png'))
    assert len(paths) == 42
    for path in paths:
        image = Image.open(path)
        assert image.mode == '1' and image.size == (40, 40)
        actual = centre(image)
        aligned(path.name, actual, (19.5, 19.5))
        checks += 1
        if any(value not in (19, 19.5) for value in actual):
            failures.append(f'{path.name}: odd extent must leave the extra white pixel at right/bottom')
        if path.stem.endswith('-chon'):
            grey = Image.open(path.with_name(path.name.replace('-chon', '-xam')))
            aligned(f'{path.stem} pair', actual, centre(grey))
            checks += 1
            if image.tobytes() == image.transpose(Image.Transpose.FLIP_LEFT_RIGHT).tobytes():
                if grey.tobytes() != grey.transpose(Image.Transpose.FLIP_LEFT_RIGHT).tobytes():
                    failures.append(f'{path.stem}: grey lost mirror symmetry')
else:
    program, board = sys.argv[1:]

    def render(kind, chosen, bits=None):
        pixels = subprocess.check_output([program, kind, str(chosen)], input=bits)
        height = 80 if kind == 'toolbar' else 60
        assert len(pixels) == 528 * height
        return Image.frombytes('L', (528, height), pixels)

    for kind, count in [('home', 5), ('text', 5), ('reader', 5 if board == 'X3' else 4), ('toolbar', 4)]:
        if board == 'Touch' and kind == 'text':
            continue
        first, last = (66, 461) if board == 'X3' else (74, 453)
        centres = [first + (2 * (last - first) * slot + count - 1) // (2 * (count - 1))
                   for slot in range(count)]
        if kind == 'toolbar':
            centres = [66, 198, 330, 462]
        icon_top = 20 if kind == 'toolbar' else 10
        for chosen in range(count):
            image = render(kind, chosen)
            for slot, xpos in enumerate(centres):
                crop = image.crop((xpos - 20, icon_top, xpos + 20, icon_top + 40))
                actual = centre(crop)
                ring = image.crop((centres[chosen] - 42, 6, centres[chosen] + 42, image.height - 6))
                ring_centre = centre(ring)
                target = (xpos - 0.5, ring_centre[1] + 6)
                if slot == chosen:
                    target = (ring_centre[0] + xpos - 42, ring_centre[1] + 6)
                aligned(f'{board} {kind} chosen={chosen} slot={slot}',
                        (actual[0] + xpos - 20, actual[1] + icon_top), target)
    for path in sorted(reference.glob('*.png')):
        for chosen in (0, 1):
            image = render('tab', chosen, Image.open(path).tobytes())
            actual = centre(image.crop((140, 10, 180, 50)))
            aligned(f'{board} tab {path.name} chosen={chosen}',
                    (actual[0] + 140, actual[1] + 10), (159.5, 29.5))
    for width, height in [(6, 8), (7, 9), (1, 1), (40, 40)]:
        for left, top in [(0, 0), (40 - width, 40 - height)]:
            icon = Image.new('1', (40, 40), 1)
            icon.paste(0, (left, top, left + width, top + height))
            for chosen in (0, 1):
                image = render('tab', chosen, icon.tobytes())
                actual = centre(image.crop((140, 10, 180, 50)))
                aligned(f'{board} translated {width}x{height} at {left},{top} chosen={chosen}',
                        (actual[0] + 140, actual[1] + 10), (159.5, 29.5))
    for chosen in (0, 1):
        image = render('tab', chosen, Image.new('1', (40, 40), 1).tobytes())
        checks += 1
        if ImageChops.invert(image.crop((140, 10, 180, 50))).getbbox():
            failures.append(f'{board} empty icon emitted ink')

print(f'{"RED" if failures else "GREEN"}: {checks} alignment checks, {len(failures)} failures')
print('\n'.join(failures))
raise SystemExit(bool(failures))
