"""X4 Pro icon bars (dynamic bar rule): an icon not chosen is drawn grey the way the Home bar draws it,
every other ink pixel of the icon (a checker), and the chosen one solid. Checked on the Home bar and on the
reader menu's bar (founder 06/10)."""
from pathlib import Path
import tempfile

from test_thanh_day import run

HOME_ICONS = [(44, 734), (134, 734), (220, 734), (306, 734), (396, 734)]  # 40 x 40, Recent chosen
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


def pill(image, x0, x1):
    """The chosen tab's ring between x0 and x1: (left, top, right, bottom), from its solid 3 px strokes on the
    bar's middle row and the ring's middle column."""
    black = lambda x, y: image.getpixel((x, y)) < 128
    solid = [x for x in range(x0, x1) if black(x, 754) and black(x + 1, 754) and black(x + 2, 754)]
    left, right = solid[0], solid[-1] + 2
    cx = (left + right) // 2
    ys = [y for y in range(722, 788) if black(cx, y) and black(cx, y + 1) and black(cx, y + 2)]
    return left, ys[0], right, ys[-1] + 2


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-icons-') as tmp:
        root = Path(tmp)
        (home,) = run(root / 'home', '', [2800])
        check(home, HOME_ICONS, 0, 'Home bar')
        (menu,) = run(root / 'menu', '3000:TAP:240,300;7000:TAP:240,775', [9500], settings=dict(readerTapTip=0))
        check(menu, READER_ICONS, 1, 'reader menu bar')
        # The chosen tab's ring has the Home bar's size and place around its icon (founder 06/10: the reader
        # bar's icons crowded their ring).
        home_pill = pill(home, 18, 120)
        reader_pill = pill(menu, 212, 336)
        hw, hh = home_pill[2] - home_pill[0], home_pill[3] - home_pill[1]
        rw, rh = reader_pill[2] - reader_pill[0], reader_pill[3] - reader_pill[1]
        assert (rw, rh) == (hw, hh), f'reader chosen ring {rw}x{rh}, Home {hw}x{hh}'
        assert reader_pill[1] == home_pill[1], f'ring top at y {reader_pill[1]}, Home {home_pill[1]}'
    print('GREEN: X4 Pro icon bars draw icons not chosen grey, the chosen one solid, Home and reader menu alike')


if __name__ == '__main__':
    main()
