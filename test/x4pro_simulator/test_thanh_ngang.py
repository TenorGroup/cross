"""Dynamic bar follows the logical display: landscape back, status and keyboard."""
import json
import os
from pathlib import Path
import tempfile
from concurrent.futures import ThreadPoolExecutor

from test_thanh_day import run, ink


def capture(folder, names, images):
    output = os.environ.get('X4PRO_TEST_SHOTS')
    if output:
        target = Path(output) / folder.name
        target.mkdir(parents=True, exist_ok=True)
        for name, image in zip(names, images):
            image.save(target / (name + '.png'))


def bookmark(sach, name='Marker'):
    directory = sach.parent / '.crosspoint/bookmarks'
    directory.mkdir()
    (directory / 'sach_test_kerning_ligature.json').write_text(json.dumps({'bookmarks': [
        dict(name=name, summary='Marker', xpath='/body/p', percentage=0, si=0, pc=31, pp=0)
    ]}))


def check_reader(folder, orientation):
    # Normalized taps stay valid when the book rotates the display after boot.
    images = run(folder, '3000:TAP:240,300;7000:TAP:0.5,0.5;9500:TAP:0.05757,0.906054',
                 [6800, 9300, 12000], settings={'orientation': orientation})
    capture(folder, ('page', 'menu', 'after-back'), images)
    page, menu, back = images
    assert menu.size == (800, 480), 'the reader menu lost its landscape orientation'
    assert ink(menu, (16, 404, 76, 464)) > 0.04, 'no landscape back pill'
    assert 'Entering activity: EpubReader' in (folder/'simulator.log').read_text(), 'fixture never entered Reader'
    # Approved round3 sheet preserves the page preview; clock/battery are not placed there.
    page_pixels=list(page.crop((0,0,800,270)).getdata())
    menu_pixels=list(menu.crop((0,0,800,270)).getdata())
    assert all(a == b for a,b in zip(page_pixels,menu_pixels) if a in (0,255)), 'toolbar changed solid page pixels'
    assert set(menu_pixels) <= {0,255}, 'stored BW preview contains gray upload pixels'
    assert ink(menu,(16,276,784,392)) > 0.015, 'toolbar panel is missing'
    for x in (200,433,666):
        assert ink(menu,(x-20,414,x+20,454)) > 0.03, 'reader tool is outside the logical bottom bar'
    box = (0, 24, 800, 390)
    assert list(page.crop(box).getdata()) == list(back.crop(box).getdata()), 'landscape back did not close to the page'


def check_keyboard(folder, size, orientation):
    images = run(folder, '3000:TAP:240,300;7000:TAP:0.5,0.5;8500:TAP:0.83,0.906054;9500:TAP:0.5,0.236;'
                        '12500:TAP:0.5,0.138,900;15500:TAP:0.5,0.55;18500:TAP:0.05757,0.906054',
                 [12000, 18000, 21000], settings={'orientation': orientation, 'uiTextSize': size, 'readerFavorites': [9]},
                 write_books=bookmark)
    log = (folder/'simulator.log').read_text()
    assert 'Entering activity: EpubReaderBookmarks' in log, 'fixture never entered Bookmarks'
    assert 'Entering activity: KeyboardEntry' in log, 'fixture never entered rename keyboard'
    capture(folder, ('bookmarks', 'keyboard', 'after-back'), images)
    before, keyboard, back = images
    for normal in (before,keyboard):
        assert ink(normal,(14,0,110,24)) > 0.02, 'normal sub-screen clock is outside the top strip'
        assert ink(normal,(740,0,796,24)) > 0.02, 'normal sub-screen battery is outside the top strip'
    assert keyboard.size == (800, 480), 'keyboard lost its landscape orientation'
    assert ink(keyboard, (16, 404, 76, 464)) > 0.04, 'no keyboard back pill'
    # The field underline crosses most of the screen; keys start below it with a clear gap.
    rules = [y for y in range(40, 150) if ink(keyboard, (100, y, 700, y + 1)) > 0.9]
    assert rules, 'keyboard keys covered the input field underline'
    bottom = max(rules)
    assert ink(keyboard, (100, bottom + 3, 700, bottom + 8)) == 0, 'keys overlap the input field'
    selected = [y for y in range(bottom + 1, 180)
                if any(keyboard.getpixel((x, y)) < 128 for x in range(24, 97))]
    # Compact alternates trim 2 px from each side of the highlight only.
    # The row hit rect remains [100,160), checked at its boundary below.
    trim = 2 if size == 0 else 0
    assert selected and min(selected) == 100 + trim and max(selected) == 159 - trim, 'wrong first-row geometry'
    assert bottom < 100 and 100 + 5 * 60 == 400 < 404, 'input/keys/bar overlap'
    # Digits 2..0 are unselected: a complete compact glyph is at least 14 px tall.
    # The selected 1 uses the same primary font and row geometry.
    for x in range(132, 741, 76):
        runs = []
        for y in range(100, 165):
            if any(keyboard.getpixel((xx, y)) < 128 for xx in range(x - 12, x + 13)):
                if runs and y == runs[-1][-1] + 1:
                    runs[-1].append(y)
                else:
                    runs.append([y])
        assert max((len(run) for run in runs), default=0) >= 14, f'clipped digit at x={x}, size={size}'
    box = (0, 24, 800, 390)
    assert list(before.crop(box).getdata()) == list(back.crop(box).getdata()), 'keyboard back did not return to bookmarks'

    # Adjacent 60 px rows share a boundary, with disjoint half-open hit rects.
    hits = folder / 'hits'
    hits.mkdir()
    run(hits, '3000:TAP:240,300;7000:TAP:0.5,0.5;8500:TAP:0.83,0.906054;9500:TAP:0.5,0.236;'
              '12500:TAP:0.5,0.138,900;15500:TAP:0.5,0.55;'
              '19000:TAP:0.075094,0.331942;21000:TAP:0.075094,0.33403;'
              '23500:TAP:0.05757,0.906054', [22000, 26500],
        settings={'orientation': orientation, 'uiTextSize': size, 'readerFavorites': [9]}, write_books=bookmark)
    assert 'Entering activity: KeyboardEntry' in (hits/'simulator.log').read_text(), 'boundary fixture never entered keyboard'
    saved = json.loads((hits / 'sd/.crosspoint/bookmarks/sach_test_kerning_ligature.json').read_text())
    assert saved['bookmarks'][0]['name'] == 'Marker1q', 'adjacent row hits overlap or leave a dead boundary'

    long = folder / (folder.name + '-long')
    long.mkdir()
    (field, _) = run(long, '3000:TAP:240,300;7000:TAP:0.5,0.5;8500:TAP:0.83,0.906054;9500:TAP:0.5,0.236;'
                          '12500:TAP:0.5,0.138,900;15500:TAP:0.5,0.55;19000:TAP:0.05757,0.906054',
                    [18000, 21500], settings={'orientation': orientation, 'uiTextSize': size, 'readerFavorites': [9]},
                    write_books=lambda sach: bookmark(sach, 'W' * 128))
    assert 'Entering activity: KeyboardEntry' in (long/'simulator.log').read_text(), 'long-name fixture never entered keyboard'
    capture(long, ('keyboard',), (field,))
    rules = [y for y in range(40, 280) if ink(field, (100, y, 700, y + 1)) > 0.9]
    assert rules and max(rules) < 100, '128-character input covered the keyboard'
    saved = json.loads((long / 'sd/.crosspoint/bookmarks/sach_test_kerning_ligature.json').read_text())
    assert saved['bookmarks'][0]['name'] == 'W' * 128, 'input window changed the stored text'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-landscape-') as tmp:
        for orientation in (1, 3):
            folder = Path(tmp) / ('reader-' + str(orientation))
            folder.mkdir()
            check_reader(folder, orientation)
        with ThreadPoolExecutor(max_workers=3) as pool:
            tasks = []
            for orientation in (1, 3):
                for size in (0, 1, 2):
                    folder = Path(tmp) / f'keyboard-{orientation}-{size}'
                    folder.mkdir()
                    tasks.append(pool.submit(check_keyboard, folder, size, orientation))
            for task in tasks:
                task.result()
    print('GREEN: 2 landscape directions, 3 text sizes, complete glyphs and disjoint 60 px row hits')


if __name__ == '__main__':
    main()
