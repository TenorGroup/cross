"""X4 Pro, single screens brought to the dynamic bar's rules (founder audit 06/10). Each check names its screen:

- reader menu: the status strip covers whole lines of the page, no line cut in half under it;
- Settings card scrolled: the scroll bar stays inside one frame, never across the gaps between frames (rule 13);
- a held cover: its menu hangs from the cover, the "other book" line under the card stays as it was (rule 7);
- keyboard: no black key (a finger picks the key, there is no key cursor);
- Bluetooth: a tap on "Assign buttons" opens the bindings (it reported the row's code, not its index);
- Wi-Fi: no black row, the rows in a round frame, "Retry" in it;
- hotspot: each code in a round frame;
- shell switch question: no button signs at the foot of a touch screen.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, TABS_X
from test_dong_deu import line_kind
import test_thanh_dong as td

SETTINGS = f'3000:TAP:{TABS_X[4]},{td.BAR_Y}'
DEVICE = f'{SETTINGS};5000:TAP:240,{td.ROW[6]}'
STRIP = 32  # the status strip's ink ends above this row; contentTop() is 38


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def shoot(tmp, name, script, shots, **kw):
    folder = Path(tmp) / name
    folder.mkdir()
    images = run(folder, script, shots, **kw)
    return images, (folder / 'simulator.log').read_text()


def check_reader_strip(tmp):
    (shot,), log = shoot(tmp, 'strip', f'3000:TAP:{TABS_X[1]},{td.BAR_Y};5000:TAP:240,68;7000:TAP:240,68;'
                         '9500:TAP:240,775', [11500], write_books=td.toc_book)
    assert 'Entering activity: EpubReader' in log, 'never opened the book'
    first = next(y for y in range(STRIP, 200) if any(dark(shot, x, y) for x in range(480)))
    assert first > 38, f'a line of the page is cut at the foot of the status strip (ink from y {first})'


def check_settings_scroll_bar(tmp):
    (shot,), _ = shoot(tmp, 'scroll', f'{SETTINGS};5000:SWIPE:240,600,240,300,400', [7500])
    # A frame spans its straight left ring (x 16-17, dotted) and a corner radius (20) above and below it; the
    # rows outside every frame are the gaps between frames.
    side = [y for y in range(40, 700) if dark(shot, 16, y) or dark(shot, 17, y)]
    inside = {y + d for y in side for d in range(-22, 23)}
    gaps = [y for y in range(40, 700) if y not in inside]
    poke = [y for y in gaps if any(dark(shot, x, y) for x in range(446, 462))]
    assert gaps, 'no gap between frames on the scrolled Settings card'
    assert not poke, f'the scroll bar runs outside the frames, rows {poke[:4]}..'


def check_held_cover(tmp):
    (before, held), _ = shoot(tmp, 'cover', '3000:TAP:150,330,1200', [2800, 6500])
    box = (0, 640, 480, 715)
    assert list(before.crop(box).getdata()) == list(held.crop(box).getdata()), \
        'the held cover\'s menu landed on the "other book" line, far from the cover'
    assert list(before.crop((0, 40, 480, 640)).getdata()) != list(held.crop((0, 40, 480, 640)).getdata()), \
        'no menu opened on the held cover'


def check_keyboard(tmp):
    (shot,), log = shoot(tmp, 'keys', f'{DEVICE};7000:TAP:240,142', [9500])
    assert 'Entering activity: KeyboardEntry' in log, 'never opened the keyboard'
    blocks = [(x, y) for y in range(300, 700, 4) for x in range(0, 468, 4)
              if all(dark(shot, x + i, y + j) for i in range(12) for j in range(12))]
    assert not blocks, f'a black key on the touch keyboard at {blocks[:2]}'


def dotted_rules(image):
    """Grey dotted rules one row thick (a grey tap flash, many dotted rows deep, is no rule)."""
    dots = {y for y in range(36, 704) if line_kind(image, y) == 'dot'}
    return sum(1 for y in range(40, 700) if y in dots and not any(y + d in dots for d in (-3, -2, -1, 1, 2, 3)))


def check_bluetooth_bind(tmp):
    (main, bind), log = shoot(tmp, 'ble', f'{DEVICE};7000:TAP:240,265;9500:TAP:240,265', [9300, 12000])
    assert 'Entering activity: BlePageTurner' in log, 'never opened Bluetooth'
    assert dotted_rules(bind) >= 5, f'"Assign buttons" opened nothing ({dotted_rules(main)} -> {dotted_rules(bind)} rules)'


def check_wifi(tmp):
    (shot,), log = shoot(tmp, 'wifi', f'{DEVICE};7000:TAP:240,203', [11000])
    assert 'Entering activity: WifiSelection' in log, 'never opened Wi-Fi'
    full = [y for y in range(40, 700) if sum(dark(shot, x, y) for x in range(40, 440)) > 0.9 * 400]
    black = [y for y in full if all(z in full for z in range(y, y + 10))]  # a line under the header is no row
    assert not black, f'a black row in the Wi-Fi list at y {black[0] if black else 0}'
    assert ink(shot, (16, 60, 18, 150)) > 0.2, 'the Wi-Fi rows have no frame'


def check_hotspot(tmp):
    (shot,), log = shoot(tmp, 'ap', f'{SETTINGS};5000:TAP:240,{td.ROW[4]};7000:TAP:240,243', [11000])
    assert 'Entering activity: CrossPointWebServer' in log, 'never opened the hotspot'
    assert ink(shot, (16, 120, 18, 260)) > 0.2, 'the hotspot codes have no frame'


def check_shell_switch(tmp):
    (shot,), log = shoot(tmp, 'switch', f'{SETTINGS};5000:TAP:240,{td.ROW[0]};7000:TAP:240,325', [9500])
    assert 'Entering activity: UglySwitch' in log, 'never opened the shell switch question'
    assert ink(shot, (90, 760, 480, 800)) == 0, 'button signs at the foot of the touch shell switch'
    assert ink(shot, (16, 724, 76, 784)) > 0.02, 'no "<" at the foot of the shell switch'


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='x4pro-single-') as tmp:
        for check in (check_reader_strip, check_settings_scroll_bar, check_held_cover, check_keyboard,
                      check_bluetooth_bind, check_wifi, check_hotspot, check_shell_switch):
            try:
                check(tmp)
            except AssertionError as error:
                failures.append(f'{check.__name__}: {error}')
    assert not failures, '\n'.join(failures)
    print('GREEN: X4 Pro reader strip, Settings scroll bar, held cover, keyboard, Bluetooth, Wi-Fi, hotspot, shell switch')


if __name__ == '__main__':
    main()
