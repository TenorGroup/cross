"""X4 Pro reader menu panels (Text, Contents) wear the one frame: grey dots 2 px, 16 px in, radius 20, the
same as the Settings lists and the bar below (frame audit 06/10: they came out solid black)."""
from pathlib import Path
import tempfile

from test_thanh_day import run, TABS_X
import test_thanh_dong as td


def edge_ratio(image, x, y0, y1):
    col = [image.getpixel((x, y)) < 128 for y in range(y0, y1)]
    return sum(col) / len(col)


def check(image, where):
    # Left and right straight edges of the panel (frame x 16..463, y 362..711), its 2 px ring.
    for x in (16, 17, 462, 463):
        r = edge_ratio(image, x, 400, 680)
        assert 0.35 < r < 0.65, f'{where}: frame column {x} ink {r:.2f}, not grey dots'
    assert edge_ratio(image, 18, 400, 680) == 0 and edge_ratio(image, 461, 400, 680) == 0, f'{where}: frame thicker than 2 px'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-panel-') as tmp:
        root = Path(tmp)
        (text,) = run(root / 't', '3000:TAP:240,300;7000:TAP:240,775', [9500], settings=dict(readerTapTip=0, readerFavorites=[], readerFavoriteCount=0, readerFavoritesDaDat=1))
        check(text, 'Text')
        ob = f'3000:TAP:{TABS_X[1]},{td.BAR_Y};5000:TAP:240,68;7000:TAP:240,130;9500:TAP:240,775;12000:TAP:226,754'
        (toc,) = run(root / 'c', ob, [14000], write_books=td.toc_book, settings=dict(readerTapTip=0))
        check(toc, 'Contents')
    print('GREEN: X4 Pro reader menu panels wear the grey 2 px frame 16 px in')


if __name__ == '__main__':
    main()
