"""X4 Pro icon bars (dynamic bar rule): an icon not chosen is drawn grey the way the Home bar draws it,
every other ink pixel of the icon (a checker), and the chosen one solid. Checked on the Home bar and on the
reader menu's bar (founder 06/10)."""
from pathlib import Path
import tempfile

from test_thanh_day import run

HOME_ICONS = [(36, 734), (128, 734), (220, 734), (311, 734), (403, 734)]  # 40 x 40, Recent chosen
READER_ICONS = [(127, 734), (253, 734), (380, 734)]  # Contents, Text (chosen), More


def pixels(image, x, y):
    return [[image.getpixel((x + i, y + j)) < 128 for i in range(40)] for j in range(40)]


def solid_pairs(grid):
    """Horizontally touching ink pixels: none in a checker, many in a solid stroke."""
    return sum(1 for row in grid for a, b in zip(row, row[1:]) if a and b)


def ink(grid):
    return sum(1 for row in grid for p in row if p)


def check(image, icons, chosen, where):
    for k, (x, y) in enumerate(icons):
        grid = pixels(image, x, y)
        assert ink(grid) > 20, f'{where}: icon {k} has no ink'
        if k == chosen:
            assert solid_pairs(grid) > 20, f'{where}: chosen icon {k} is not solid'
        else:
            assert solid_pairs(grid) == 0, f'{where}: icon {k} not chosen is not grey ({solid_pairs(grid)} solid pairs)'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-icons-') as tmp:
        root = Path(tmp)
        (home,) = run(root / 'home', '', [2800])
        check(home, HOME_ICONS, 0, 'Home bar')
        (menu,) = run(root / 'menu', '3000:TAP:240,300;7000:TAP:240,775', [9500], settings=dict(readerTapTip=0))
        check(menu, READER_ICONS, 1, 'reader menu bar')
    print('GREEN: X4 Pro icon bars draw icons not chosen grey, the chosen one solid, Home and reader menu alike')


if __name__ == '__main__':
    main()
