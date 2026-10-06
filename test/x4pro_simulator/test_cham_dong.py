"""X4 Pro: tapping a row opens that row and never moves the list.

A tap on the last full row of Settings ("Khac" when it had 9 rows, "Ngu" in its 3 titled groups) used to scroll
the list up by a row instead of opening it, and the offset stayed when the screen came back.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-tap-') as tmp:
        folder = Path(tmp)
        # Cai dat tab: tap the last full row (Ngu, y 657). Its group opens: its name is in the bar at the foot.
        settings, group = run(folder, f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,657', [4800, 7500])
        assert ink(group, (152, 724, 464, 784)) > 0.02, 'the group screen has no name in the bar'
        assert list(settings.crop((0, 80, 480, 140)).getdata()) != list(group.crop((0, 80, 480, 140)).getdata()), \
            'tapping the last row did not open it'
        # Back to Settings and tap File Transfer, the first row.
        folder2 = Path(tmp) / 'b'
        folder2.mkdir()
        before, after = run(folder2, f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,657;7500:TAP:240,12;9500:TAP:240,117',
                            [4800, 11500])
        assert ink(after, (16, 724, 76, 784)) > 0.04 and list(after.crop((0, 30, 480, 700)).getdata()) != list(
            settings.crop((0, 30, 480, 700)).getdata()), 'File Transfer did not open after coming back'
    print('GREEN: X4 Pro a tap opens the row and never scrolls the list')


if __name__ == '__main__':
    main()
