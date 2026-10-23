"""X4 Pro: a tap in the 16 px band under the dynamic bar (y 784..799) counts as a tap on the bar (v1.0.75).

The founder's taps on the bar fall about 15 px under its centre; 2 of 26 met this band and did nothing. Each journey is
run twice, once tapping the bar's centre (y 754) and once the band (y 792): the frames after must be the same, the
status strip's clock left out. Under the jump bar the band stays the page's (test_nut_quay_ve_x4pro_v1061.py).

Runs the X4 Pro simulator (pio run -e simulator_x4pro); X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import TABS_X, run

CENTRE, BAND = 754, 792
OPEN_BOOK = '3000:TAP:240,300;6000:TAP:240,400;8000:TAP:240,400;'  # the first Recent card, then 2 pages on


def same_after(name, script, shot):
    """The frame at `shot` after `script` with {y} the bar's centre, and with {y} the band under it."""
    frames = []
    for y in (CENTRE, BAND):
        with tempfile.TemporaryDirectory(prefix='x4pro-band-') as tmp:
            frame, = run(Path(tmp), script.format(y=y), [shot])
            frames.append(frame.crop((0, 40, 480, 800)))
    assert list(frames[0].getdata()) == list(frames[1].getdata()), f'{name}: a tap at y={BAND} did not do what y={CENTRE} does'
    print(f'GREEN: {name}')


def main():
    # Home's tab bar: the last tab (Settings).
    same_after('Home tab', f'3000:TAP:{TABS_X[4]},{{y}}', 4800)
    # The reader menu's tools: the text menu from the page's foot, then Contents (the second tool).
    same_after('reader tool', OPEN_BOOK + '10000:TAP:240,775;12000:TAP:228,{y}', 14000)
    # A screen below another (Full bar): Settings, Connections, then its "<".
    same_after('foot "<"', f'3000:TAP:{TABS_X[4]},754;5000:TAP:240,118;7000:TAP:46,{{y}}', 9000)


if __name__ == '__main__':
    main()
