"""X4 Pro pinned rows (founder 07/10): the heart sits on the middle of the row's capitals, a size up (18 px), clear
of the row's name; the chosen tool at either end of the reader bar keeps more air from the bar's round end than
the top gap of every tool.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run
from test_menu_chu_14 import TEXT_MENU, ROW_Y

SIZE_ROW = ROW_Y[1]  # "Co chu trinh doc", pinned on every new reader
DIGITS = (324, 396)  # its value between "-" and "+"
HEART = (120, 262)   # before the "-"


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def box(image, x0, x1, y0, y1):
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            if dark(image, x, y):
                xs.append(x)
                ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


def last_group(image, x0, x1, y0, y1):
    """The rightmost run of inked columns in [x0, x1) (runs part at 3 white columns): its box."""
    cols = [x for x in range(x0, x1) if any(dark(image, x, y) for y in range(y0, y1))]
    if not cols:
        return None
    start = cols[-1]
    for a, b in zip(reversed(cols[:-1]), reversed(cols[1:])):
        if b - a > 3:
            break
        start = a
    return box(image, start, cols[-1] + 1, y0, y1)


def end_gap(image, xs):
    """White px on the bar's middle row between the bar's dotted ring and the chosen tool's solid ring."""
    row = [dark(image, x, 754) for x in xs]
    first = row.index(True)
    k = first + 1
    while k < len(row) and not (row[k] and row[k + 1] and row[k + 2]):
        k += 1
    return k - first - 1


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-tim-') as tmp:
        root = Path(tmp)
        failures = []
        for size in (0, 2):
            (text,) = run(root / f'text{size}', TEXT_MENU, [11800], settings=dict(uiTextSize=size))
            band = (SIZE_ROW - 30, SIZE_ROW + 30)
            heart = last_group(text, *HEART, *band)
            digits = box(text, *DIGITS, *band)
            assert heart and digits, f'size {size}: no heart or no value on the size row'
            off = (heart[1] + heart[3]) / 2 - (digits[1] + digits[3]) / 2
            if abs(off) > 1:
                failures.append(f'size {size}: the heart is {off:+.1f} px off the middle of the capitals')
            if heart[3] - heart[1] + 1 < 13:
                failures.append(f'size {size}: the heart is {heart[3] - heart[1] + 1} px tall')
            # Clear of the name: white columns between the name's last ink and the heart.
            name = box(text, 30, heart[0], *band)
            if name and heart[0] - name[2] - 1 < 4:
                failures.append(f'size {size}: the heart touches the row name ({heart[0] - name[2] - 1} px)')
        # The chosen Favorites (first) and More (last) tools: air at the bar's round ends.
        fav, more = run(root / 'ends', TEXT_MENU + ';12000:TAP:136,754;15000:TAP:412,754', [14800, 17800])
        left = end_gap(fav, range(80, 200))
        right = end_gap(more, range(470, 340, -1))
        if left < 6 or right < 6:
            failures.append(f'the chosen end tool crowds the bar ring: {left} px left, {right} px right')
        assert not failures, '\n'.join(failures)
    print('GREEN: X4 Pro hearts centred on the capitals, 18 px, clear of the name; end tools clear of the ring')


if __name__ == '__main__':
    main()
