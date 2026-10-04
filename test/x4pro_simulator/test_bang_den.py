"""X4 Pro: the light panel (a tap on the header of Home) is drawn in tenor/cross.

Round controls (circle step buttons, stadium tiles), and the clock and battery on the header row
where every other screen has them, once.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink


def longest_run(image, y0, y1, columns=(20, 21)):
    """Longest stretch of ink down the left edge of the controls (x 20, 21) between rows y0 and y1."""
    best = 0
    for x in columns:
        cur = 0
        for y in range(y0, y1):
            if image.getpixel((x, y)) < 128:
                cur += 1
                best = max(best, cur)
            else:
                cur = 0
    return best


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-panel-') as tmp:
        # The status strip (y 0-23) opens it; the first row of a list under it does not (below).
        home, panel = run(Path(tmp), '3000:TAP:240,12', [2800, 5000])
        # The panel is up: the Recent card under it is covered at the top.
        assert list(home.crop((0, 60, 480, 400)).getdata()) != list(panel.crop((0, 60, 480, 400)).getdata()), \
            'the header tap did not open the panel'
        # Battery on the header row, the place and the look every screen gives it.
        box = (400, 0, 480, 24)
        assert list(home.crop(box).getdata()) == list(panel.crop(box).getdata()), \
            'the panel draws its own battery instead of the header row one'
        # Step buttons are circles: down the left edge of the first "-" only the middle rows touch it.
        step = longest_run(panel, 60, 260)
        assert step < 24, f'the "-" button is still a rounded square (edge run {step})'
        # Tiles are stadiums.
        tile = longest_run(panel, 300, 600)
        assert tile < 24, f'the tiles are still rounded rectangles (edge run {tile})'
        # A tap on the top of the first row of the Settings card (y 38) opens the row, not the panel.
        folder = Path(tmp) / 'row'
        folder.mkdir()
        card, row = run(folder, '3000:TAP:423,754;5000:TAP:240,38', [4800, 7500])
        assert ink(row, (16, 724, 76, 784)) > 0.04, 'a tap on the first row under the strip did not open it'
    print('GREEN: X4 Pro light panel in tenor/cross')


if __name__ == '__main__':
    main()
