"""X4 Pro reader menu, Text tab: every row's name starts at the frame's text edge (x 32, 16 px in from the
round frame like the Settings rows) and its value ends 16 px in from the frame's right side (founder 06/10:
the names wandered). The Font list's names start at the same edge."""
from pathlib import Path
import tempfile

from test_thanh_day import run

LEFT, RIGHT = 32, 448      # frame x 16..463, 16 px padding
TOP, ROW = 402, 62         # first row below the 40 px title, 62 px rows


def ink_cols(image, y0, y1, x0, x1):
    return [x for x in range(x0, x1) if any(image.getpixel((x, y)) < 128 for y in range(y0, y1))]


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-align-') as tmp:
        text, fonts = run(Path(tmp) / 'a', '3000:TAP:240,300;7000:TAP:240,775;9500:TAP:240,433', [9300, 11800],
                          settings=dict(readerTapTip=0))
        rows = 0
        for i in range(10):
            y0, y1 = TOP + i * ROW + 14, TOP + i * ROW + ROW - 14
            if y1 > 708:
                break
            cols = ink_cols(text, y0, y1, 20, 240)
            if not cols:
                continue
            rows += 1
            assert abs(cols[0] - LEFT) <= 3, f'Text row {i}: name starts at x {cols[0]}, not {LEFT}'
        assert rows >= 5, f'only {rows} Text rows found'
        for i in range(2):
            y0, y1 = TOP + i * ROW + 14, TOP + i * ROW + ROW - 14
            cols = ink_cols(fonts, y0, y1, 20, 400)
            assert cols and abs(cols[0] - LEFT) <= 3, f'Font row {i}: name starts at x {cols[0] if cols else None}'
    print('GREEN: X4 Pro Text tab names and Font list names start at the frame text edge')


if __name__ == '__main__':
    main()
