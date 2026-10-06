"""X4 Pro: Clock and the Home key gestures write their rows in the text of every other list.

Both lists used the small text, so their rows were shorter than the rows of Language beside them. Measured
on the pixels: the height of the capital that starts the first row's label (English, Múi giờ, Chạm).
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, TABS_X
import test_thanh_dong as td

SETTINGS = f'3000:TAP:{TABS_X[4]},{td.BAR_Y}'
# Activity name: Settings card, the group's row, the list's first row.
SCREENS = {
    'LanguageSelect': f'{SETTINGS};5000:TAP:240,{td.ROW[6]};7000:TAP:240,68',
    'ClockSettings': f'{SETTINGS};5000:TAP:240,{td.ROW[td.SYSTEM]};7000:TAP:240,68',
    'HomeButtonSettings': f'{SETTINGS};5000:TAP:240,{td.ROW[3]};7000:TAP:240,68',
}


def capital_height(tmp, name):
    folder = Path(tmp) / name
    folder.mkdir()
    (shot,) = run(folder, SCREENS[name], [9500])
    assert f'Entering activity: {name}' in (folder / 'simulator.log').read_text(), f'never reached {name}'
    dark = lambda x, y: shot.getpixel((x, y)) < 128
    # The frame's top ring, then the first row under it; the label starts 16 px into the frame, clear of its
    # round corner from x 30, and its first letter runs to the first blank column.
    top = next(y for y in range(36, 200) if sum(dark(x, y) for x in range(100, 400)) > 100)
    band = range(top + 4, top + 50)
    x = next(x for x in range(30, 120) if any(dark(x, y) for y in band))
    columns = []
    while any(dark(x, y) for y in band):
        columns.append(x)
        x += 1
    ys = [y for y in band for c in columns if dark(c, y)]
    return max(ys) - min(ys) + 1


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-row-text-') as tmp:
        heights = {name: capital_height(tmp, name) for name in SCREENS}
    standard = heights['LanguageSelect']
    short = {name: h for name, h in heights.items() if h < standard}
    assert not short, f'row text smaller than a standard list (capital {standard} px): {short}'
    print(f'GREEN: X4 Pro Clock and Home key rows in the text of Language ({heights})')


if __name__ == '__main__':
    main()
