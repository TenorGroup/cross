"""X4 Pro: an empty Favorites tab says so, and holding a row pins it.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-fav-') as tmp:
        folder = Path(tmp) / 'a'
        folder.mkdir()
        # Favorites: pins start empty. The middle of the screen holds the empty-list line.
        (fav,) = run(folder, f'3000:TAP:{TABS_X[2]},{BAR_Y}', [4800])
        assert ink(fav, (20, 100, 460, 600)) > 0.004, 'an empty Favorites tab is a blank screen'
        # The sentence wraps instead of being cut with an ellipsis: more than one line of ink, none at the edges.
        assert ink(fav, (0, 330, 480, 470)) > 0.02 and ink(fav, (0, 330, 20, 470)) == 0 and ink(fav, (460, 330, 480, 470)) == 0, \
            'the empty Favorites line is cut or runs to the screen edge'
        lines = sum(1 for y in range(330, 470, 4) if ink(fav, (0, y, 480, y + 4)) > 0 and ink(fav, (0, y - 4, 480, y)) == 0)
        assert lines >= 2, f'the empty Favorites sentence is on {lines} line(s); it should wrap'
        # Holding a folder row opens its menu, anchored under the row; its Pin pins it: it shows up in
        # Favorites, the empty line is gone.
        folder = Path(tmp) / 'b'
        folder.mkdir()
        (pinned,) = run(folder, f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68,1200;7500:TAP:100,130;'
                        f'9500:TAP:{TABS_X[2]},{BAR_Y}', [11500])
        assert ink(pinned, (20, 40, 460, 100)) > 0.01, 'holding a row did not pin it'
        assert ink(pinned, (20, 380, 460, 420)) == 0, 'the empty line stays over a pinned row'
        # Holding the Recent card opens the menu of the book it shows; holding the "other books" line, the
        # menu of the book it names. Pin pins that book (y of the menu's one line in each case: the cover's hangs
        # from the cover).
        names = {}
        for tag, point, pin in (('cover', '240,573', 498), ('other books', '300,686', 622)):
            folder = Path(tmp) / tag.replace(' ', '-')
            folder.mkdir()
            (held,) = run(folder, f'3000:TAP:{point},900;5500:TAP:100,{pin};7500:TAP:{TABS_X[2]},{BAR_Y}', [9500])
            assert ink(held, (20, 40, 460, 100)) > 0.01, f'holding the Recent card at the {tag} did not pin the book'
            names[tag] = list(held.crop((20, 40, 460, 100)).getdata())
        assert names['cover'] != names['other books'], 'the "other books" line pinned the book shown, not the one it names'
    print('GREEN: X4 Pro empty Favorites line, hold to pin')


if __name__ == '__main__':
    main()
