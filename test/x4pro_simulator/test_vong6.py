"""X4 Pro round 6: the other-books line, the file list, the Display group, slow drags, the pin popup, back by header.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30


def fresh(tmp, name):
    folder = Path(tmp) / name
    folder.mkdir()
    return folder


def bands(image, x0, x1, y0, y1, step=2, tall=0):
    """Number of separate ink bands (text lines) down a column range; with `tall`, only bands that high."""
    count, run = 0, 0
    for y in range(y0, y1 + step, step):
        has = y < y1 and ink(image, (x0, y, x1, y + step)) > 0
        if has:
            run += step
            continue
        if run and run >= tall:
            count += 1
        run = 0
    return count


def check_other_books(tmp):
    # Home, Recent: a tap on the "other books" line shows the next book (the card changes) and does not open one.
    home, after = run(fresh(tmp, 'ob'), '3000:TAP:300,686', [2800, 6000])
    assert list(home.crop((0, 430, 480, 540)).getdata()) != list(after.crop((0, 430, 480, 540)).getdata()), \
        'a tap on the other books line did not step to the next book'
    edge = ink(after, (60, BAR_TOP, 420, BAR_TOP + 2))
    assert 0.35 < edge < 0.65, 'a tap on the other books line opened a book'


def check_files(tmp):
    # File -> sach/: rows start under the status strip, no extension column, no path band at the foot.
    (files,) = run(fresh(tmp, 'fl'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68', [7000], extra_books=4)
    # Only the grey dotted rules between rows cross this column.
    assert ink(files, (380, 60, 455, 700)) < 0.012, 'the file extension column is still drawn'
    assert ink(files, (20, 50, 460, 86)) > 0.01, 'the first file row does not start under the status strip'
    assert ink(files, (0, 700, 480, 710)) == 0, 'the path rule is still drawn at the foot'


def check_display_group(tmp):
    # Settings -> Display: the side arrow and button label rows are gone (9 rows became 7).
    (group,) = run(fresh(tmp, 'dg'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,128', [7500])
    assert ink(group, (30, 545, 200, 585)) == 0 and ink(group, (30, 485, 200, 520)) == 0, \
        'the Display group still has 8 or 9 rows'


def check_pin_popup(tmp):
    # File -> hold a book: its menu, anchored under the row, offers Pin, Rename, Delete (a tap already opens).
    (popup,) = run(fresh(tmp, 'pp'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;8000:TAP:240,130,900', [10000],
                   extra_books=2)
    lines = bands(popup, 30, 150, 162, 340, tall=6)
    assert lines == 3, f'the file menu has {lines} choices under the row, not 3'


def check_foot_back(tmp):
    # Settings -> Gui file (first row): a round "<" at the foot (x 16-76, y 724-784); a tap on it goes back to Settings.
    before, mode, back = run(fresh(tmp, 'bk'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,66;8000:TAP:46,754',
                             [4800, 7000, 10000])
    assert ink(mode, (16, 724, 76, 784)) > 0.04, 'Gui file has no round back button at the foot'
    assert ink(before, (16, 724, 76, 784)) > 0.04 and list(back.crop((0, 60, 480, 700)).getdata()) == list(
        before.crop((0, 60, 480, 700)).getdata()), 'a tap on the foot back button did not go back to Settings'
    # A settings group and a file list have it too, and their rows stop above it.
    (group,) = run(fresh(tmp, 'bg'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,128', [7500])
    assert ink(group, (16, 724, 76, 784)) > 0.04, 'the Display group has no round back button'
    (files,) = run(fresh(tmp, 'bf'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68', [7000], extra_books=14)
    assert ink(files, (16, 724, 76, 784)) > 0.04, 'the file list has no round back button'
    assert ink(files, (80, 717, 480, 723)) == 0, 'a file row runs into the bar at the foot'


def check_home_key(tmp):
    # The Home key from a settings group, and from the Settings card of Home, lands on the Recent card.
    home, group_home, card_home = run(fresh(tmp, 'hk'),
                                      f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,128;8000:HOME;11000:TAP:{TABS_X[4]},{BAR_Y};14000:HOME',
                                      [2800, 10000, 16500])
    box = (0, 30, 480, 720)
    assert list(home.crop(box).getdata()) == list(group_home.crop(box).getdata()), \
        'the Home key from a settings group did not land on the Recent card'
    assert list(home.crop(box).getdata()) == list(card_home.crop(box).getdata()), \
        'the Home key from the Settings card did not land on the Recent card'


def check_folder_cap(tmp):
    # 700 books with 55 character names list; the same folder is refused with one line when the heap cannot hold
    # it (CROSSPOINT_SIM_FREE_HEAP stands for the heap left).
    names = [('book-%03d-' % i) + 'x' * 46 for i in range(700)]
    (listed,) = run(fresh(tmp, 'fc0'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68', [7000], extra_books=names)
    assert ink(listed, (60, 100, 440, 200)) > 0.02, 'a folder of 700 files is not listed'
    (refused,) = run(fresh(tmp, 'fc1'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68', [7000], extra_books=names,
                     sim_env={'CROSSPOINT_SIM_FREE_HEAP': '40000', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP': '20000'})
    assert ink(refused, (20, 330, 460, 470)) > 0.01 and ink(refused, (20, 100, 460, 300)) == 0, \
        'a folder past the heap ceiling is not refused with one line'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-r6-') as tmp:
        for check in (check_other_books, check_files, check_display_group, check_pin_popup,
                      check_foot_back, check_folder_cap, check_home_key):
            try:
                check(tmp)
                print('ok   ', check.__name__)
            except AssertionError as error:
                print('RED  ', check.__name__, '-', error)
    print('done')


if __name__ == '__main__':
    main()
