"""X4 Pro: a value list (OptionPopup) hangs from the row tapped to open it, in the panel frame, no black block.

Dynamic bar rule 7: the frame of the lists (grey dots 2 px, radius 20, 16 px in from both sides), the value in use
bold with the tick at its end, never a black cursor row; it stops over the bar at the foot and a list longer than
that scrolls with its scroll bar (the Home key's Tap list used to run 11 rows over the bar, the sleep screen list
paged with "Previous / Next page" rows).
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30
SETTINGS = f'3000:TAP:{TABS_X[4]},{BAR_Y}'
# Rows of a settings group: 56 px from y 64 (row 2 of the Reader group is Orientation, 176..232).
ORIENTATION_BOTTOM = 232
PANEL_FOOT = BAR_TOP - 8


def black_rows(image, top, bottom):
    """Rows of the popup's width that are mostly ink: a black cursor block."""
    return [y for y in range(top, bottom) if ink(image, (40, y, 440, y + 1)) > 0.6]


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-pick-') as tmp:
        folder = Path(tmp)
        # Settings > Reader > Orientation.
        _, picked = run(folder, f'{SETTINGS};4500:TAP:240,242;6500:TAP:400,203', [6300, 8500])
        edge = ink(picked, (60, ORIENTATION_BOTTOM + 4, 420, ORIENTATION_BOTTOM + 6))
        assert 0.35 < edge < 0.65, f'the list does not hang from the Orientation row (edge ink {edge:.2f})'
        assert not black_rows(picked, ORIENTATION_BOTTOM, PANEL_FOOT), 'a black cursor row in the value list'
        # The value in use (Portrait, the first row) carries the tick at the row's end.
        assert ink(picked, (424, 258, 448, 282)) > 0.05, 'no tick at the value in use'

        # Settings > Controls > Home key gestures > Tap: 11 actions, all over the bar.
        f2 = Path(tmp) / 'home'
        f2.mkdir()
        before, gestures = run(f2, f'{SETTINGS};4500:TAP:240,303;6500:TAP:240,80;8500:TAP:240,88', [8300, 10500])
        assert list(before.crop((0, BAR_TOP, 480, 800)).getdata()) == list(gestures.crop((0, BAR_TOP, 480, 800)).getdata()), \
            'the list covers the bar at the foot'
        assert ink(gestures, (30, PANEL_FOOT + 1, 450, BAR_TOP - 1)) == 0, 'the list runs into the gap over the bar'
        assert not black_rows(gestures, 40, PANEL_FOOT), 'a black cursor row in the Tap list'

        # Settings > Sleep > Sleep screen: more values than fit, a scroll bar, no page rows.
        f3 = Path(tmp) / 'sleep'
        f3.mkdir()
        before, sleep, swiped = run(f3, f'{SETTINGS};4500:TAP:240,657;6500:TAP:240,80;9000:SWIPE:240,200,240,500,150',
                                    [6300, 8500, 11000])
        assert list(before.crop((0, BAR_TOP, 480, 800)).getdata()) == list(sleep.crop((0, BAR_TOP, 480, 800)).getdata()), \
            'the sleep screen list covers the bar'
        assert not black_rows(sleep, 40, PANEL_FOOT), 'a black cursor row in the sleep screen list'
        assert ink(sleep, (452, 200, 458, 600)) > 0.2, 'no scroll bar on a list longer than its frame'
        # A flick down shows the values above, the list still open over the same rows.
        assert list(sleep.crop((30, 100, 440, 700)).getdata()) != list(swiped.crop((30, 100, 440, 700)).getdata()), \
            'a flick does not scroll the list'
        assert ink(swiped, (452, 200, 458, 600)) > 0.2, 'the list closed on the flick'
    print('GREEN: X4 Pro value lists hang from their row, framed, ticked, over the bar')


if __name__ == '__main__':
    main()
