"""X4 Pro: a screen laid over a book's page has the dynamic bar too, on a clean band.

Reader menu, More, "Go to %": the slider over the page gets "<" at the foot (x 16-76), and the bar's band
(y 712 to the bottom) holds nothing else: the page's last line and status footer are cleared under it.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, TABS_X
import test_thanh_dong as td

# Folder, the book folder, the contents book, a tap at the page's foot (the menu), More, a short scroll, "Go to %".
PERCENT = (f'3000:TAP:{TABS_X[1]},{td.BAR_Y};5000:TAP:240,68;7000:TAP:240,68;9500:TAP:240,775;11000:TAP:321,754;'
           '12500:SWIPE:400,620,400,400,600;15000:TAP:240,620')


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-over-page-') as tmp:
        (shot,) = run(Path(tmp), PERCENT, [17500], write_books=td.toc_book)
        log = (Path(tmp) / 'simulator.log').read_text()
        assert 'Entering activity: EpubReaderPercentSelection' in log, 'the journey never reached "Go to %"'
        assert ink(shot, (16, 724, 76, 784)) > 0.04, 'no "<" at the foot of "Go to %"'
        assert ink(shot, (84, 712, 480, 800)) == 0, 'the page shows through the band of the bar'
    print('GREEN: X4 Pro "Go to %" over the page: "<" on a clean band')


if __name__ == '__main__':
    main()
