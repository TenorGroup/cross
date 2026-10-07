"""X4 Pro reader menu (founder 07/10): the bar's tools stand Favorites, Contents, Text, More from the left. A tap
on the foot of the page still opens Text, its pill on the 3rd tool.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink
from test_menu_chu_14 import TEXT_MENU, ROW_Y, MINUS
from test_thanh_dong import toc_book

TOOLS_X = [131, 226, 321, 416]  # the 4 cells of tenorchrome::readerToolRect, x 84..464
BAR = (726, 782)
TICK = (410, ROW_Y[0] - 20, 452, ROW_Y[0] + 20)  # the chosen mark at the end of the chapter being read


def pill_tool(image):
    """The tool whose cell holds the most ink: the chosen pill's solid ring and bold icon."""
    cells = [ink(image, (x - 40, BAR[0], x + 40, BAR[1])) for x in TOOLS_X]
    return cells.index(max(cells))


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-tools-') as tmp:
        root = Path(tmp)
        taps = ';'.join(f'{12000 + 3000 * k}:TAP:{x},754' for k, x in enumerate(TOOLS_X))
        shots = [11800] + [14800 + 3000 * k for k in range(4)]
        text, favorites, contents, text_again, more = run(root / 'a', TEXT_MENU + ';' + taps, shots,
                                                         write_books=toc_book,
                                                         settings=dict(readerFavorites=[14], readerFavoriteCount=1,
                                                                       readerFavoritesDaDat=1))
        assert pill_tool(text) == 2, f'the foot tap opened Text, its pill is on tool {pill_tool(text)}, not the 3rd'
        # Favorites: the one pin (sync) and nothing under it.
        assert ink(favorites, (40, ROW_Y[0] - 20, 300, ROW_Y[0] + 20)) > 0.01, '1st tool: no Favorites row'
        assert ink(favorites, (40, ROW_Y[1] - 25, 440, ROW_Y[4] + 25)) == 0, '1st tool is no Favorites list'
        assert pill_tool(favorites) == 0, 'the pill is not on the 1st tool'
        # Contents: the chapter being read ends in the chosen mark.
        assert ink(contents, TICK) > 0.02 and ink(contents, MINUS) == 0, '2nd tool is no Contents list'
        assert pill_tool(contents) == 1, 'the pill is not on the 2nd tool'
        # Text: the size row's stepper.
        assert ink(text_again, MINUS) > 0, '3rd tool is no Text panel'
        # More: 5 rows, no stepper, no chosen mark.
        assert ink(more, MINUS) == 0 and ink(more, TICK) == 0 and ink(more, (40, ROW_Y[4] - 20, 300, ROW_Y[4] + 20)) > 0.01, \
            '4th tool is no More list'
        assert pill_tool(more) == 3, 'the pill is not on the 4th tool'
    print('GREEN: X4 Pro reader bar: Favorites, Contents, Text, More; the foot tap opens Text')


if __name__ == '__main__':
    main()
